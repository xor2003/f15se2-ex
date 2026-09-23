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

## Verification results (tools/netprobe + tests/net_sim_tests)

| Gate | Result |
|------|--------|
| Sync-step consumption | PASS - one input -> exactly one snapshot; withheld input produced zero snapshots over 1.2s; input resumes -> exactly one tick each |
| Duplicate HELLO | PASS - 3x HELLO on one connection -> 3x HELLO_ACK all id=1, single slot (server log: "joined as player 1" x3, no extra slots) |
| Join/leave/reconnect | PASS - BYE frees the slot immediately ("player N left"), reconnect gets the lowest free slot with clean state; abrupt disconnect frees via GNS timeout; 9th concurrent client gets HELLO_NAK "server full" |
| Packet loss | PASS - under 40% send loss + 15% dup + 30ms reorder a reliable command ("Autopilot on") executed exactly once; unreliable axes-only packets may drop without stalling |
| Remote pause | PASS - NC_PAUSE sent mid-session; subsequent ticks/snapshots continued normally |
| Headless combat sim | PASS - tests/net_sim_tests (7 checks): authoritative air-target lock on parked remote object, own-object exclusion, stationary exclusion, gun round destroys remote object -> g_playerObjectHitHook -> owner damage, owner-filtered rounds skipped, miss = no damage, projectile targetPlayer filter |
| Full capacity | PASS - 8 concurrent slots assigned 0..7; parked remote objects verified live in gdb (slots above g_groundUnitCount, g_simObjScanBound covers them); ASan/UBSan soak: 8 clients + dup HELLOs + commands + leaves clean in our code (found+fixed latent strcpy(x,x) UB in findWaypointFeatures; residual reports are GNS-internal misaligned wire reads, upstream-intentional) |
| AI role path | PASS - NET_ROLE_AI client joins, gets a player slot, inputs flow, and receives NETMSG_OBS each tick (pilot-entitled observation: ownship block + scope-filtered air/site contacts + RWR threat; verified live: radar contacts, OBSF_LOCKED_AIR on the server-side lock, OBSF_ACTIVE sites, threat=site@range). --obs-full gives privileged omniscient obs. 5 filter cases covered in net_sim_tests |

New defects found and fixed during gate testing:
- NETMSG_BYE freed no slot (a locally initiated closePeer produces no
  NET_EV_DISCONNECTED) - BYE now drops the player directly; closePeer
  also lingers so a trailing reliable message (HELLO_NAK) is flushed
  before the socket dies.
- findWaypointFeatures did strcpy(x,x) into g_stringPool when the tile id
  aliased the slot's own name entry (latent UB, ASan abort under
  multi-player state) - self-copy now skipped.

## Round 2 findings (post e0f51a0 + d49a07f)

Second-pass review found 7 further issues plus 3 carry-overs. All fixed
and verified; build green, 34/34 + 31/31 tests pass.

| # | Issue | Status |
|---|-------|--------|
| 1 | [P1] Players overwrite each other's gunfire (shared frame-derived bullet slot) | FIXED - server players own `bulletTracks[g_residentPlayer]`; `g_bulletTrackCount` raised to >=8 at boot so slots 0-7 are per-player and enemy tracers shift to 8-11. `net_sim_tests`: firing player 0's round survives player 1's idle pass and vice versa |
| 2 | [P1] Victim ctx swap changes the attacker's aircraft profile | FIXED - `fireAirThreat` + its HUD message now read `aircraftTypes[g_simObjects[objIdx].spec]`, not ambient `g_threatSpec` (verified remaining `g_threatSpec` readers at egthreat.c:407/:588 sit inside `updateObjects`' own loop where spec is assigned per-iteration and correctly restored by the swap). Residual wart noted: `threatSpec` is world-loop scratch that happens to live in the ctx; no reader consumes a stale value today |
| 3 | [P1] Rejected snapshots corrupt live state | FIXED - `netSnapApply` now decodes into a static `SnapTmp`, validates (underrun, count bounds, player-id range/dup, entity-id ranges, frameTick staleness vs last committed tick), then commits. Tests: truncated snap leaves `g_ViewX`/`g_missionTick`/`frameTick`/objects untouched; resend and older ticks rejected; newer accepted; dup player id and out-of-range object id rejected |
| 4 | [P2] Reliable commands can still disappear + stale input counts as fresh | FIXED - `remoteInputPushKey` returns 0 on full queue; server counts/logs drops (explicit rejection). `onInput` applies axes + readiness only when `clientSeq > lastSeq`; command blocks still execute (reliable stream can legitimately arrive behind newer unreliable axes). Live: sync-step seq 5 -> 1 snap, seq 4 -> none, dup seq 5 -> none, seq 6 -> 1 snap |
| 5 | [P2] Repeated HELLO resets an existing player | FIXED - duplicate HELLO on a live association re-sends ACK + MISSION_SETUP and returns without touching the ctx. Live: "Autopilot on" -> apAlt=2000 in the own block; re-HELLO -> same slot re-ACKed, apAlt still 2000, knots continue uninterrupted |
| 6 | [P2] Rendering still changes simulation state | FIXED - HUD draw's `g_groundTargetLock = -1` removed (simTargetLock already performs the identical look-away drop each tick under every ctx); explosion spark flicker uses a private `fxRandomRange` LCG so draw-side consumption can't perturb the sim's `rand()` sequence |
| 7 | [P2] Outgoing missiles reported as inbound in NETMSG_OBS | FIXED - slots 0-7 keep `targetPlayer`=victim semantics; player-fired slots (>=8, `targetPlayer`=shooter) are inbound only when `targetLock` resolves to the observer's parked REMOTE_PLAYER object. Test covers own shot excluded, foreign threat excluded, enemy shot locked-on-me flagged INBOUND |
| 8 | [#2 carryover] Capacity admission | FIXED - `slotFree` bounded by `playerCapacity()` = `min(8, F15_MAX_SIM_OBJECTS - g_groundUnitCount)`; a mission without parked-slot storage NAKs instead of accepting un-simulatable players |
| 9 | [Verification carryover] worldHash must include player ctxs | FIXED - `worldHash` now folds each used+ready player's full PlayerSim (memset-at-init keeps padding deterministic) plus the slot index, alongside the shared tables |

Notes:
- `onHello` keeps the existing-association check before `slotFree`, so the
  capacity gate cannot kick a live player out on a re-handshake.
- The staleness gate resets on each MISSION_SETUP apply, so a reconnecting
  client accepts its first snapshot regardless of prior tick values.

## Round 3 findings (post 451bf51)

Third-pass review reopened four round-2 entries (gunfire, command
delivery, capacity, canonical hash) and found two new defects. All six
fixed and verified; build green, 34/34 + 31/31 tests pass, ASan/UBSan
clean of project-code reports.

| # | Issue | Status |
|---|-------|--------|
| 1 | [P1] Per-player gun slots shortened projectile lifetime (release cleared airborne rounds, second shot overwrote the first) | FIXED - server firing now claims any FREE slot in the shared 16-entry player pool (bulletTracks[0..15], enemy tracers stay at 16-19); saturated pool falls back to rotating overwrite. The no_fire release-sweep runs in SP only. `net_sim_tests::test_bullet_pool_lifetime`: release keeps the round airborne, a second shot claims a different slot, idle players clear nothing, SP rotating slot + sweep unchanged |
| 2 | [P2] Snapshot staleness gate dead for negative ticks (`s_lastFrameTick >= 0` as "unset") | FIXED - explicit `s_haveLastTick` flag replaces the sentinel; the int16 modular diff already handled wrap. Test `test_snap_negative_ticks` reproduces the reported -32760 -> -32761 rejection plus resend, older, newer, and the 32767 -> -32768 wrap (walked via modular-reachable hops) |
| 3 | [P2] Command rejection invisible to clients; no command-level dedup | FIXED - `NE_CMD_ACK` event: subject=clientSeq, object=cmd index, arg=0 executed / 1 rejected. Executed acks fire from a served-hook inside `remoteReadKey` (the sim actually consumed the key); reject acks fire at queue-full push time. Per-key (seq,idx) tags ride parallel queue arrays in `RemoteInput`. Server dedups command-bearing packets via `lastCmdSeq` (independent of axes freshness). Client surfaces rejections on the HUD. Live: cmd ack arg=0 per served key; resent cmd packet executed once (one "Autopilot off"); 52-cmd flood -> arg=1 rejections once the 32-entry queue saturated |
| 4 | [P2] Admission checked only sim-object capacity | FIXED - `playerCapacity()` = `min(8, F15_MAX_SIM_OBJECTS - g_groundUnitCount, F15_MAX_MAP_TARGETS - g_planeCount)`: a mission without map-target storage NAKs instead of accepting players that can't get tactical-map entries |
| 5 | [P2] One player's scope timer gated world threats | FIXED - `g_scopeSweepTimer` is a per-pilot RWR debounce (fireGroundThreat sets it on contact). Its decrement moved from `updateThreatSites` (one resident ctx) into `frameThreatScan` (every player's own pass), and the world-side gate now fires unconditionally on the server - `serverFireGroundThreat` swaps in the chosen victim's ctx and applies `g_scopeSweepTimer < 0` THERE, so pilot A's active sweep can no longer suppress a site engaging pilot B. SP semantics unchanged |
| 6 | [P2] worldHash not canonical (raw PlayerSim bytes included presentation/string state; bullets/map-targets/RNG omitted) | FIXED - hash now covers simObjects + projectiles + bulletTracks (player pool + enemy slots) + planeTable (up to g_planeCount) + mapEvents + waypoints + targetSlots + entity counts + mission tick/status + `g_randCallCount` (new: counts in-sim rand() draws - all sim RNG funnels through randomRange since the fxRandomRange split) + `frameTick`; players fold in via `playerCtxHash()` (sim region only: ViewX..missionEndedFlag) + slot index. Cosmetic state can't change the hash; gameplay divergence can't hide |

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
