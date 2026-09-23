# Netcode review findings

Found 9 issues, including server hangs, unsafe bounds, and broken combat
behavior. Reviewed through e74d6ac against live-verified upstream 1a4061e,
including the changes committed during review. Build succeeds; 33/33 tests
pass. Those tests do not establish multiplayer combat or packet-loss
correctness. Bounds, coordinate conversion, and the pause path were
additionally checked with debugger probes.

## Definition of Done / Definition of Failure

| # | Issue | Definition of Done | Definition of Failure |
|---|-------|--------------------|-----------------------|
| 1 | Remote pause freezes the server | Pause and screenshot commands cannot enter a blocking server-side UI path. Send both during a two-client session; simulation and network polling continue normally. | Either command blocks ticks, stops snapshots, or requires keyboard input on the server. |
| 2 | Remote-player registration exceeds array bounds | Maximum supported world counts plus eight players remain within allocated storage. Snapshot application and subsequent rendering pass ASan/UBSan; insufficient capacity is handled explicitly. | Counts exceed capacity, rejected slots still extend iteration bounds, entities are overwritten, or sanitizers report out-of-bounds access. |
| 3 | Authoritative gun-hit detection never runs | A deterministic headless scenario registers expected aircraft and ground hits, applies damage/destruction, and replicates the result. A corresponding miss causes no damage. | Ammunition is consumed without expected hit processing, misses cause damage, or results depend on client rendering. |
| 4 | Server-side target acquisition is missing | Headless simulation acquires and cycles eligible air/ground targets. Fired weapons use the authoritative selected target, and clients display that selection. | Locks remain unresolved despite eligible targets, designation does nothing, or the displayed target differs from the weapon's authoritative target. |
| 5 | Threat processing affects only the first player | Each player receives position-appropriate threat, landing, and damage calculations while world entities advance once per tick. Swapping player slots preserves equivalent outcomes. | Later players miss updates, threats use another player's position, slot order changes results, or world updates multiply with player count. |
| 6 | Incorrect remote-player map Y | Explicit coordinate conversions match authoritative map positions across representative coordinates and boundaries. The reproduced 0x4000 case publishes as 0x4000; radar and 3D placement agree. | The case still becomes 0xC000, contacts are mirrored/displaced, or map and rendered positions disagree beyond defined rounding tolerance. |
| 7 | Sync-step waits only for initial input | Each tick consumes fresh input from every participating player. Withholding the next input stops advancement; completing the input set permits exactly one tick. | Previously consumed input permits another tick, the server advances at polling speed, or duplicate packets satisfy a new tick. |
| 8 | Packet loss permanently loses commands | Discrete commands remain pending until acknowledged or explicitly failed. Under loss, duplication, and reordering, commands execute exactly once in the defined order. | A command silently disappears, executes twice, executes out of order, or is acknowledged without execution. |
| 9 | Repeated HELLO allocates multiple players | One connection owns at most one player slot. Repeated HELLO is idempotent or rejected without allocation; disconnect releases the slot and reconnect starts cleanly. | Repeated HELLO increases occupied slots, exhausts server capacity, creates ambiguous input ownership, or leaves orphaned players. |

## Architectural changes

| Change | Definition of Done | Definition of Failure |
|--------|--------------------|-----------------------|
| Complete simulation tick | Single-player and server use the same simulation pipeline. Identical seed and tick-indexed inputs produce identical gameplay state with rendering enabled, disabled, or running at different rates. | Rendering changes targeting, damage, movement, timers, or gameplay RNG; headless mode skips gameplay work. |
| World/player separation | World entities advance exactly once per tick. Player-relative calculations run for each applicable player. Projectiles carry explicit owner/target IDs; damage and scoring reach the correct player. | Adding a player accelerates world updates; slot order changes outcomes; threats or damage use another player's context. |
| Presentation commands stay local | Server accepts an explicit gameplay-command allowlist. Presentation actions execute locally; simulation never waits for UI input. | Pause, screenshot, calibration, or another client command blocks server progress or changes another client's presentation. |
| Dedicated remote-player rendering | Remote players use separate storage with stable IDs and explicit coordinate conversions. Maximum supported occupancy fits all storage and renders at authoritative positions. | Player publication exceeds legacy array bounds, overwrites world objects, misplaces contacts, or leaves departed players visible. |
| Explicit input/tick contract | Stale axis updates are rejected. Discrete commands execute once, in defined order, using acknowledgement/retry or reliable delivery. Sync-step consumes one input frame per participating player per tick. | Commands disappear or repeat; stale packets overwrite newer controls; sync-step advances using already-consumed input or stalls outside its documented timeout policy. |
| Atomic snapshot application | Decode and validate the entire snapshot before applying it. Reject stale/incomplete/invalid snapshots without changing live state. Interpolation modifies only render state. | Rejected packets partially mutate state; invalid indices reach consumers; old snapshots roll state backward; rendering alters authoritative state. |

## Verification gates

| Test | Definition of Done | Definition of Failure |
|------|--------------------|-----------------------|
| Headless combat | A deterministic scenario acquires a target, fires guns and missiles, applies expected damage, and records destruction. Gameplay hashes match the rendered run at each tick. | Combat requires rendering, expected damage is absent, or hashes diverge. |
| Two-player damage attribution | Test both players as shooter and victim, then swap slot assignments. Equivalent outcomes, ownership, damage, and scores follow player identity. | Damage or credit goes to the wrong player; one player is unintentionally immune; slot assignment changes results. |
| Full-capacity worlds | Run maximum world entities and eight players, including joins/departures, under ASan/UBSan. Counts stay within capacity; every supported entity remains correctly represented. | Any sanitizer error, overwritten entity, invalid count, or silently missing supported player. |
| Join/leave handling | Repeated join/leave cycles reclaim slots. Duplicate HELLO is idempotent or rejected. Disconnecting the first player preserves simulation; reconnect creates clean state. | Slots leak, one peer owns multiple players, stale controls survive reconnect, or remaining players stop progressing. |
| Packet loss | A seeded fault-injection run exercises loss, duplication, reordering, and bursts. Every acknowledged discrete command executes exactly once; controls converge to the newest delivered state. | An acknowledged command is lost/repeated, stale controls persist, or recovery fails after delivery resumes. |
| Exact sync-step consumption | Withhold one player's next input: no tick advances. Supply the complete next input set: exactly one tick advances. Repeat and exercise the documented timeout/disconnect policy. | Any tick runs early, an input frame drives multiple ticks, or timeout/disconnect handling violates the contract. |

Concrete test parameters (set before implementation, not weakened after):
fault injection 20% loss / 5% duplication / reorder window 4 / bursts of 3
dropped packets; sync-step input timeout 500ms then mark player stale;
scenario duration 600 ticks; ASan/UBSan enabled build for gates 2-4.

## 1. [P1] Remote pause freezes the entire server

NC_PAUSE and NC_SCREENSHOT reach waitForKeyPress(), which waits on the
server's local SDL keyboard instead of network input. The simulation and
network polling stop for everyone. Confirmed the call chain in GDB.

src/server/f15server.cpp:265, src/egflight.c:163.

Status: FIXED — server keyDispatch swallows pause/screenshot commands
before they reach the blocking path.

## 2. [P1] Remote-player registration can extend iteration beyond allocated arrays

Failed slot allocation still sets the parked-player mask; snapshot
application then increases the object/table counts without enforcing
capacity. A debugger probe with a valid maximum-size setup produced 22
objects for a 20-element array and 76 targets for a 74-element array,
exposing subsequent rendering loops to out-of-bounds access.

src/net/snapshot.cpp:437.

Status: FIXED — parked mask set only when a slot was actually allocated;
count extension clamped to array capacity.

## 3. [P1] Authoritative gun-hit detection never runs

The server moves bullets but skips drawWorldEffects(), which contains
aircraft/ground hit detection and damage application. Firing can consume
ammunition without causing authoritative gun damage.

src/egframe.c:79, src/egtarget.c:444.

Status: FIXED — hit/damage detection extracted to simBulletHits()
(egtarget.c), called from the sim step (updateFrame for SP,
updatePlayerFrame per ready ctx on the server); drawWorldEffects() is
now draw-only. BulletTrack carries targetPlayer; player rounds resolve
under the shooter's ctx, enemy tracers stay unowned (first hit consumes).
Parked remote-player objects are lockable/hittable simObjects on the
server; a destroyed parked object applies bombTarget-style damage to the
owning ctx via g_playerObjectHitHook.

## 4. [P1] Server-side target acquisition is missing

updateTargetLock() runs only through rendering. Server player contexts
start with locks at -1, and pressing designate does not resolve them into
targets. Clients can display locally acquired locks that the server never
uses when firing missiles.

src/egplayer.c:355, src/egtarget.c:48.

Status: FIXED — acquisition extracted to simTargetLock() (egtarget.c),
called from the sim step per ready ctx; updateTargetLock() is now
draw-only so locks no longer depend on render rate. Remote players are
published as parked simObjects server-side (g_simObjScanBound extends
the scans over them; own object excluded), so they are lockable; locks
are PlayerSim fields and already ride the snapshot to clients.

## 5. [P1] Threat processing applies only to the first active player

The world pass runs under one player's context and then breaks. Its
functions include player-relative SAM guidance/damage and nearest-airfield
calculations. Other players consequently miss those updates; hostile
missiles use the first player's position.

src/server/f15server.cpp:352.

Status: FIXED — world pass (updateWorldFrame, f15world.c) runs once per
tick under the most-threatened ctx; the player pass (updatePlayerFrame)
runs per ready ctx and now includes the player-relative threat scan
(frameThreatScan), SAM/threat guidance (Projectile.targetPlayer filters
each shot to its owner ctx), gun hits and target acquisition. Threat
fire routines delegate victim selection to the server via
g_threatFireHook/g_airThreatFireHook (fires under the chosen victim's
ctx). Orphaned shots re-home to the first ready player.

## 6. [P1] Remote players receive incorrect map Y coordinates

The conversion uses the rendering-space constant 0x01000000 to derive map
coordinates. Confirmed in GDB: authoritative map Y 0x4000 becomes 0xC000
in both the remote object and radar entry. This misplaces contacts and
affects visibility/range checks.

src/net/snapshot.cpp:374.

Status: FIXED — posX/posY and mapX/mapY use the wire map coords
(s->mapX/s->mapY); only the render-space worldY keeps the flip.

## 7. [P2] Sync-step only waits for the first input packet

allInputsArrived() checks whether lastSeq is nonzero, but completed ticks
never consume/reset that readiness. After every player sends once, the
simulation advances at polling speed even without fresh input.

src/server/f15server.cpp:369.

Status: FIXED — per-player arrived flag consumed by each completed tick.

## 8. [P2] Packet loss permanently loses discrete commands

Keyboard commands are removed from the input queue and sent once using
unreliable delivery, without acknowledgement/retransmission. Losing that
packet loses the gear toggle, missile launch, throttle change, etc.

src/net/netclient.cpp:227, src/net/netclient.cpp:303.

Status: FIXED — discrete command packets sent reliable.

## 9. [P2] Repeated HELLO messages allocate multiple players to one connection

onHello() allocates a new slot without checking whether the peer already
owns one. One connection can exhaust all eight slots, while input dispatch
updates only its first matching slot.

src/server/f15server.cpp:207.

Status: FIXED — HELLO from a peer that already owns a slot re-initializes
that slot instead of allocating a new one.
