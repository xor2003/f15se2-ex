# Multiplayer networking plan

Goal: dedicated authoritative server owns **all** game logic. Clients (human
pilots or AI agents) connect, send **commands** — never positions — and render
from server snapshots. The server is the single source of truth. An AI client
uses the exact same protocol, which doubles as the training interface for a
better AI.

Transport: Valve **GameNetworkingSockets** (GNS) for native clients. Browser
(wasm) clients get a WebSocket leg, because GNS does not compile to Emscripten
and browsers cannot do UDP.

Single-player is untouched: the whole thing hides behind an input-source
abstraction, and `F15_NET` defaults OFF.

---

## 1. Why this shape (and not deterministic lockstep)

The sim is deterministic (fixed-point Q14 math, LUT trig, LCG `randomRange`/
`seedRng` in `egmath.c`), so lockstep *would* work — but the requirement is an
authoritative server plus arbitrary external AI clients, which forces
command-up / state-down anyway. Determinism still pays off later: reproducible
training episodes, replay validation, and optional client-side prediction.

## 2. Current seams we build on

### Sim loop — `egsys.c:gameMainLoop()`
- Fixed timestep: `g_frameRateScaling` pinned at 15 → **15 sim steps/s**; each
  step is `stepFlightModel()` (per-player: input + physics + cockpit commands)
  then `updateFrame()` (world: threats, objects, projectiles, mission clock,
  then `keyDispatch(keyScancode)`).
- Rendering is already decoupled: prev/next `CamSnapshot`/`SimObjSnap`/
  `ProjSnap` + interpolation (`camApplyInterp`/`objApplyInterp`). This is
  exactly the machinery a network client needs for snapshot interpolation —
  it transfers almost verbatim.

### Command surface — already tiny
Per tick the sim consumes exactly three inputs:
- `keyScancode` — one BIOS key word (AH=scan, AL=ascii). `stepFlightModel`
  handles thrust/brake/AB/quit; `keyDispatch()` in `egkeys.c` handles weapons,
  views, countermeasures, gear, autopilot, eject, radar, waypoint. Edge-triggered.
- `joyAxes[0]` (roll) / `joyAxes[1]` (pitch) — bytes, 0x80 centre, from SDL
  gamepad (`readCalibratedJoystick`) or the keyboard virtual stick
  (`g_joyRawX/Y`), scaled into `g_rollInput`/`g_pitchInput`.
- fire buttons — `readAxisInput(0)` = guns, `readAxisInput(1)` = missile, via
  `misc_readJoystick()` or `g_axisInputAccum[]` (Backspace/Enter).

So the wire command is ~8–12 bytes:

```c
struct NetInput {          /* client -> server, one per client tick */
    uint32 clientSeq;      /* packet seq / dedup */
    uint32 lastSnapAck;    /* newest snapshot seq seen (delta baseline) */
    uint8  joyX, joyY;     /* stick, 0x80 centre — latest-wins */
    uint8  nKeys;          /* 0..4 edge-triggered BIOS key words this tick */
    uint8  buttons;        /* bit0 guns, bit1 missile — level-triggered */
    uint16 keys[4];
};
```

Keys are **queued** on the server (a dropped "weapon select" is user-visible),
axes/buttons are latest-wins (a stale stick value is worse than none).

### Authoritative state — what the snapshot carries
- Per-player block: `g_ViewX/Y` (int32 fine pos), `g_viewZ`, `g_ourHead`,
  `g_ourPitch`, `g_ourRoll`, `g_knots`/`g_velocity`, `g_setThrust`,
  `g_altitude`, `g_fuelRemaining`, `g_gunAmmo`, `missleSpec[].ammo`,
  `g_playerPlaneFlags`, `g_ejectState`, `g_airTargetLock`/`g_groundTargetLock`,
  `g_autopilotEngaged`, waypoint/mission scalars — ~60–80 B per player.
- World objects: `g_simObjects[]` (36 B × ~20; `SimObject` is already a packed
  wire-friendly record), `g_projectiles[12]`, `bulletTracks[20]` (tracers;
  v1 can send spawn events only), `g_particles[8]`, `mapEvents[4]`.
- Mostly-static tables sent once at mission start then delta'd:
  `g_planeTable` (74 `MapTarget`s; only `alertLevel`/`threatTimer`/`active`
  mutate), `waypoints[]`, `g_targetSlots[]`, `g_stringPool` (target names).
- Event stream (reliable): `hudMessage()`/`setTimedMessage()` text,
  `makeSound()` id, `playVoiceCue()` id, `appendMapEvent()` entries, kills,
  mission-end.

Full snapshot ≈ 1–1.5 KB at 15 Hz ≈ ~20 KB/s/client worst case; deltas shrink
it well under that. Trivially cheap.

## 3. The core refactor: per-player context

Today there is exactly one player: ~150 globals (`g_ViewX`, `g_ourHead`,
`g_knots`, `missleSpec`, `g_ejectState`, locks, autopilot, inputs…). For N
human planes the server needs N contexts.

**Chosen approach — context swap, not signature rewrite.** Define
`struct PlayerSim` holding every player-owned global; keep the game code
reading the globals as-is, and copy a context in/out around each player's
step (`playerSwapIn(i)` / `playerSwapOut(i)`, ~few hundred bytes — free at
15 Hz). Converting every function to take a `ctx*` would touch nearly all of
egame for no behavioural gain; the swap keeps diffs small and testable, and
single-player degenerates to "one context, always swapped in" — a regression
checkpoint: same inputs → identical sim as before.

Scope note: separating "player-owned" from "world-owned" globals is the
delicate part (`g_closestThreatIndex`, `waypoints`, `g_planeTable` are world;
`g_ViewX`, `missleSpec`, `g_fireCooldown` are player). `egdata.c`'s DGROUP
comments plus who-writes-what analysis drives the split; a wrong split shows
up instantly in the determinism regression test.

### Remote planes inside the world model
Each connected pilot gets a reserved `g_simObjects[]` slot (wingman model:
`g_simObjects[1]` today) and a `g_planeTable` entry flagged air/friendly, so
enemy AI, SAM threat logic (`egthreat.c`), radar scope, and the tacmap treat
them exactly like any other aircraft. After each player's physics step the
server mirrors ctx state (pos/head/pitch/bank/speed) into that slot. Weapon
fire (`fireMissile`, `countermeasures`, gun in `updateBulletsAndFire`) runs
under that player's ctx against the shared tables.

### Presentation decoupling (needed for headless)
`updateFrame()` and friends call draw/audio directly (`blitSprite`,
`hudMessage`, `makeSound`, `gfx_restoreFromImage`). The server binary must run
with no gfx init. Introduce a `SimEffects` hook table (sound, hud text,
sprite/event emit) — native game installs real impls, server installs a
broadcast-to-clients impl. Small, contained diff.

## 4. Transport: GameNetworkingSockets

- `ISteamNetworkingSockets`: connection-oriented, message-oriented, reliable
  + unreliable messages, fragmentation/reassembly, per-connection stats and
  ping. Mature (ships in Valve titles), plain C++ API, no Steam dependency.
- Deps: protobuf + crypto (OpenSSL or libsodium). Via `FetchContent` pinned to
  a release tag, gated on `option(F15_NET)` — protobuf build cost stays opt-in.
- **WASM caveat**: GNS does not build under Emscripten (upstream issue #319,
  "not on the roadmap"), and its WebSocket transport carries GNS's own SNP
  framing — a browser's `WebSocket` API can't speak it. So:
  - native client → GNS over UDP;
  - wasm client → `emscripten/websocket.h` to a dedicated **plain-WebSocket**
    endpoint in `f15server` (one WS binary frame = one protocol message; TCP
    head-of-line blocking accepted for v1 — commands are tiny and snapshots
    self-heal, so it's survivable; WebTransport is the future upgrade);
  - both hide behind a `NetTransport` interface:
    `connect/listen/send(msg, lane)/poll(cb)`. Server accepts both front doors
    into the same message handlers.
- Lanes: `unreliable` for inputs/snapshots (sequence-guarded), `reliable` for
  join/mission-setup/events/mission-end.

## 5. Protocol

Versioned little-endian, hand-rolled encode/decode (the codebase is all packed
fixed-point structs already; protobuf would fight the int16-wraparound
semantics — GNS transports opaque bytes, no protobuf needed for *our* payload).

C→S: `Hello{protoVer, name, role=human|ai|spectator}`, `NetInput` (above),
`Bye`.
S→C: `HelloAck{playerId, tickRate, simTick}`, `MissionSetup{theater, difficulty,
seed, world tables (planeTable, waypoints, targetSlots, stringPool, loadout)}`
= essentially a serialized `worldImportToEgame` input, `Snapshot{tick, self
full, entity deltas}`, `Event{...}` (reliable), `MissionEnd{stats}`.

Tick model: server sim tick is authoritative. `NetInput` is stamped with the
client's send seq; server applies queued inputs on the next tick and echoes the
applied tick in the snapshot (lets the client measure command RTT and enables
later prediction/reconciliation).

## 6. `f15server` binary

New target linking `f15se2_core` + headless stubs (precedent: `egstubs.c`,
behavior tests already run `LINK_CORE` headless under SDL dummy drivers):

```
pollNet();                                  // inputs into per-player queues
for each player i: playerSwapIn(i); consume input queue;
                   stepFlightModel();       // uses the swapped-in globals
                   playerSwapOut(i);
updateFrame();                              // world: once per tick
mirror players -> their g_simObjects slots;
build + broadcast snapshots/events;
```

- `updateFrame()` runs **once** per tick (world ownership); `stepFlightModel()`
  runs per player. Discrete cockpit commands (`keyDispatch`) execute inside the
  owning player's ctx.
- Mission bootstrap: reuse START's mission generation (`stgen.c`/`stmissn.c` →
  `worldxfer.c`) driven by a `--scenario` config / lobby vote. v1: server flag
  `--theater N --mission auto`.
- Rates: wall-clock 15 Hz for play. For training: `--sim-rate R` (R Hz) and
  `--sync-step` — server waits for every locked AI client's input before
  stepping: deterministic, rate-independent episodes.
- Disconnect → plane becomes AI-controlled or removed (config); reconnect →
  `MissionSetup` + full snapshot resync.

## 7. Client changes

- `f15se2 --connect host[:port]` → net mode: thin join UI in START phase (or a
  CLI/env for v1), then egame runs **renderer-only**: input still flows through
  the existing SDL path but is packed into `NetInput` instead of driving
  physics; incoming snapshots write the sim globals; the existing prev/next
  interpolation (`egsys.c`) smooths them at display rate.
- `InputSource` seam: `LocalSource` (today's ring/joy) vs `NetSource`; server
  side uses per-player `RemoteSource` queues. `stepFlightModel` reads through
  the source pointer — the only change to its head.
- No client prediction in v1 (input→visible latency ≈ RTT/2 + up to one tick).
  Deterministic sim makes prediction+reconciliation a clean later add — the
  client can run `stepFlightModel` locally for its own plane against the same
  fixed-point math.

## 8. AI client / RL training

Same socket, `role=ai`: receives `Snapshot` (or a filtered `Observation` view
per plane slot: own state + entity list + threat flags), sends `NetInput`.
`Event` stream supplies reward terms (gun hits, kills, damage taken, landing,
mission outcome). `--seed` + `--sync-step` give reproducible episodes; a Python
client is trivial since the protocol is a handful of packed structs over one
reliable + one unreliable channel (a C shim lib or plain UDP impl in Python for
the WS/UDP framing).

## 9. Phases

| # | Milestone | Exit check |
|---|-----------|------------|
| P0 | `F15_NET` cmake opt, GNS FetchContent, `NetTransport` iface, loopback echo | `f15server` + native client ping/pong |
| P1 | `PlayerSim` ctx extraction + `InputSource` seam | SP regression: scripted inputs → identical `frameTick`-stamped state hash vs baseline |
| P2 | Headless server: scenario bootstrap, 1-player session | server runs a full mission headless to `finalizeMission` |
| P3 | Native client: connect→fly→render-from-snapshot | 2 clients, LAN dogfight, both render each other via sim-object slots |
| P4 | Events/deltas/robustness | mid-mission join/leave, packet-loss soak (GNS debug %), reconnect resync |
| P5 | wasm WS leg | browser client flies vs native client |
| P6 | AI client + `--sync-step` training loop | scripted AI client completes a sortie |
| later | client-side prediction, lobby/mission vote, coop-vs-dogfight rules, WebTransport |

## 10. Open questions / risks

- **Multi-player mission semantics**: targets/threats/scoring are single-pilot
  (g_targetSlots, debrief via `commData`, `finalizeMission` ends *the* mission).
  MP needs mission-flow redesign: shared objectives + per-pilot outcome; server
  keeps running after a pilot's `finalizeMission` (respawn vs spectator).
- **Hostile AI target selection** (`g_closestThreatIndex`/`g_targetSlots[0]`)
  is player-centric → becomes "nearest hostile to each player" per-ctx.
- **Split risk in P1**: the player/world global split is the whole ballgame —
  mitigate with the state-hash regression test before any net code merges.
- **GNS dep weight**: protobuf+OpenSSL via FetchContent is minutes of build —
  keep `F15_NET` opt-in; evaluate `GNS_FLAT_API`/prebuilt packages.
- **Cheating**: commands are validated server-side (rate-limit, clamp axes,
  legal-state transitions); trivial since clients never send state.
- `bulletTracks`/`g_particles` are cosmetic — send spawn events, let clients
  simulate locally (they're already deterministic-ish; drift acceptable).
