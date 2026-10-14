# F-15 Multiplayer + AI Integration Plan

This is a design plan, including unimplemented future work. For current build
instructions, review order and limitations, see [the review guide](netcode-review.md).

## 1. Goals

Add multiplayer and external AI/agent support while preserving the original simulation behavior and minimizing invasive changes to the decompiled code.

The architecture should support:

```text
Native client ── GNS/UDP ──┐
Native client ── GNS/UDP ──┤
                           │
AI agent ───── protocol ───┼── F-15 Server
                           │
WASM client ─ WebSocket ───┘
```

Target capabilities:

* 2–8+ human players in the same mission.
* LAN and Internet multiplayer.
* Authoritative server.
* Dogfights and cooperative missions.
* Human vs human, human vs AI, AI vs AI.
* Headless dedicated server.
* External AI agents using the same conceptual input/state model as humans.
* Deterministic/reproducible AI training where possible.
* Browser/WASM client later.
* Preserve the existing SDL + OpenGL/OpenGL ES client.
* Avoid rewriting the original simulation before multiplayer works.

---

# 2. Existing Simulation Model

The existing game is unusually suitable for a first multiplayer implementation.

The simulation runs at approximately **15 Hz** and consumes a very small player command set:

```text
keyScancode
joyAxes[0]     // roll
joyAxes[1]     // pitch
fire button 1
fire button 2
```

`stepFlightModel()` / `keyDispatch()` ultimately consume these inputs.

Therefore the network does not need to transmit hundreds of cockpit properties. A player's input for one tick can remain very small.

Conceptually:

```cpp
struct NetInput {
    Tick tick;
    Sequence sequence;

    int16_t roll;
    int16_t pitch;

    ButtonBits held;
    ButtonBits pressed;

    Command commands[MAX_COMMANDS];
};
```

Internally, the compatibility layer can convert semantic commands back into the legacy `keyScancode` representation.

---

# 3. Do Not Put BIOS Scancodes in the Network Protocol

The original implementation can continue using BIOS keyboard words, but the wire protocol should describe **game actions**, not PC keyboard implementation details.

For example:

```text
GEAR_TOGGLE
FLAPS_TOGGLE
WEAPON_NEXT
WEAPON_PREVIOUS
RADAR_MODE
TARGET_NEXT
TARGET_PREVIOUS
ECM_TOGGLE
CHAFF
FLARE
AUTOPILOT_TOGGLE
```

The adapter performs:

```text
Network command
      ↓
Legacy command adapter
      ↓
BIOS/keyScancode expected by original code
```

This keeps networking independent of DOS, SDL, Android, WASM, controllers and future frontends.

Axes and continuously held controls use latest-wins semantics.

Discrete actions are queued and processed exactly once.

---

# 4. Tick-Based Protocol

Every simulation-related network message should refer to a simulation tick.

Do not use wall-clock time as the authoritative simulation coordinate.

```text
tick 1000
tick 1001
tick 1002
...
```

Input contains:

```text
playerId
tick
sequence
controls
commands
```

Snapshots contain:

```text
serverTick
world state
player states
entity states
```

This will later make debugging, replay and AI training substantially easier.

---

# 5. Separate Player, World and Presentation State

The largest architectural problem is not networking.

The original game assumes:

```text
THE PLAYER = global variables
```

There are roughly 150 globals describing one player's aircraft.

Before making multiplayer deeply invasive, classify state into three groups.

### Player state

```text
position
orientation
velocity
flight model state
fuel
damage
weapons
radar
selected target
autopilot
aircraft-specific timers
...
```

### World state

```text
g_simObjects
g_projectiles
mapEvents
mission state
AI state
random generator state
...
```

### Presentation state

```text
camera
HUD
cockpit animation
sound state
screen effects
local UI
...
```

Presentation state must **not** be swapped when the server simulates another player.

---

# 6. PlayerSim

Do not immediately rewrite hundreds of functions to accept a `PlayerSim&`.

Introduce:

```cpp
struct PlayerSim {
    // legacy player simulation state
};
```

Then initially use a compatibility mechanism:

```cpp
void simulatePlayer(PlayerSim& player)
{
    loadPlayerGlobals(player);

    stepFlightModel();

    savePlayerGlobals(player);
}
```

The server can therefore execute:

```cpp
for (PlayerSim& player : players)
    simulatePlayer(player);
```

This deliberately minimizes the first diff.

Long term, code can gradually migrate from:

```cpp
stepFlightModel();
```

toward:

```cpp
stepFlightModel(PlayerSim& player, WorldSim& world);
```

The context swap is a **migration mechanism**, not necessarily the final architecture.

---

# 7. Stable Entity IDs

Never expose `g_simObjects` array indexes as network identities.

Introduce:

```cpp
using NetEntityId = uint32_t;
```

Maintain:

```text
NetEntityId → local SimObject slot
```

Network references then remain valid if the local object arrays are reorganized.

Use IDs for:

```text
aircraft
missiles
SAMs
ships
ground targets
killer/victim relationships
radar targets
weapon targets
```

For example:

```cpp
struct NetProjectile {
    NetEntityId id;
    NetEntityId owner;
    NetEntityId target;
    ...
};
```

---

# 8. Remote Players Must Be Real World Aircraft

A remote player should not merely be a visual multiplayer model.

Each remote aircraft also gets an appropriate entry in the existing plane/object system so that existing game logic can see it.

That means existing systems should naturally be able to interact with human aircraft:

```text
radar
AI
SAM
missiles
collision/damage
threat detection
```

Conceptually:

```text
PlayerSim #2
     │
     └── corresponding SimObject
                 │
       ┌─────────┼───────────┐
       ↓         ↓           ↓
     radar      SAM       enemy AI
```

This should reuse as much original combat code as possible.

---

# 9. Authoritative Server

The server owns the actual battle.

Clients must not be authoritative for:

```text
position
damage
weapon hits
kills
mission outcome
entity creation/destruction
```

Architecture:

```text
               F-15 Server
                    │
             authoritative
              simulation
            ╱       │       ╲
           ╱        │        ╲
       Client 1  Client 2  AI agent
```

Clients send **intent/input**.

The server sends **results/state**.

This prevents clients from simply sending:

```text
my position = ...
enemy destroyed = true
```

and makes later Internet multiplayer much easier to reason about.

---

# 10. Simulation and Network Rates

Do not couple networking to rendering FPS.

For example:

```text
Rendering:        60–1000 FPS
Simulation:       15 Hz legacy tick
Input sending:    15–30 Hz
Snapshots:        15 Hz initially
```

The existing game already has `CamSnapshot` / `SimObjSnap` interpolation.

Reuse the same idea for remote network objects:

```text
snapshot N                 snapshot N+1
    ●---------------------------●
               ↑
          rendered state
```

There is no reason to send 600–1000 updates/sec merely because the renderer can run that quickly.

---

# 11. Full Snapshots First

The estimated state is small enough that optimization is unnecessary initially.

A snapshot can contain:

```text
server tick

player states
g_simObjects
g_projectiles
mapEvents
mission-relevant state
```

Approximately 1–1.5 KB at 15 Hz is only around:

```text
15–22.5 KB/s
```

before transport overhead.

That is trivial for modern networks.

Full snapshots make several things dramatically easier:

```text
packet loss recovery
late join
spectators
reconnection
debugging
AI observation
state inspection
```

Do **not** begin with delta compression.

Optimize only if measurements demonstrate a reason.

---

# 12. Gameplay Events vs Presentation Events

The server should send semantic gameplay events.

Good:

```text
MISSILE_LAUNCHED
AIRCRAFT_HIT
AIRCRAFT_DESTROYED
SAM_LAUNCHED
OBJECT_DESTROYED
MISSION_OBJECTIVE_COMPLETED
PLAYER_EJECTED
```

Avoid making these authoritative network events:

```text
makeSound(...)
hudMessage(...)
playVoice(...)
```

Instead:

```text
AIRCRAFT_HIT
     │
     ├── HUD notification
     ├── sound
     ├── explosion
     └── voice response
```

The client presentation layer decides how an event looks and sounds.

Mission-specific scripted voice/HUD events can be explicit presentation events when required.

---

# 13. GameNetworkingSockets

Use **GameNetworkingSockets (GNS)** for native networking.

Native architecture:

```text
Linux/Windows client
        │
     GNS/UDP
        │
     Server
```

Use appropriate reliable/unreliable delivery.

### Unreliable

Suitable for replaceable state:

```text
snapshots
frequent control state
orientation/position updates
```

If snapshot 105 is lost and 106 arrives, there is usually no reason to retransmit 105.

### Reliable

Suitable for things that must arrive:

```text
join/leave
authentication/session setup
mission selection
important discrete events
spawn/despawn
configuration
```

Do not implement reliability, fragmentation and congestion handling from scratch when GNS already provides the transport mechanisms.

---

# 14. Transport Abstraction

Networking code must not directly depend on GNS throughout the simulation.

Define something approximately like:

```cpp
class NetTransport {
public:
    virtual ~NetTransport() = default;

    virtual bool listen(...) = 0;
    virtual bool connect(...) = 0;

    virtual void send(...) = 0;
    virtual bool receive(...) = 0;

    virtual void poll() = 0;
};
```

Then:

```text
NetTransport
    ├── GnsTransport
    └── WebSocketTransport
```

The game protocol sits **above** this abstraction.

---

# 15. WASM / Browser Support

Do not make browser support block native multiplayer.

GNS currently presents problems for direct Emscripten/browser use, so the browser client can eventually use:

```text
WASM F-15
    │
emscripten/websocket.h
    │
WebSocket
    │
f15server
```

while native clients use:

```text
Native F-15
    │
GNS/UDP
    │
f15server
```

Both transports carry the same conceptual F-15 protocol.

Do this **after native multiplayer works**.

---

# 16. Headless Server

The simulation should be able to run without:

```text
SDL window
OpenGL
OpenGL ES
cockpit rendering
audio
physical input devices
```

Example:

```bash
f15server --mission desert1 --port 27015 --seed 12345
```

The game executable itself also carries the server, so a separate binary is
not required:

```bash
f15se2-ex --server --game /path/to/f15 --port 27015   # in-process dedicated server
f15se2-ex --host --port 27015 --name Viper            # spawn local server + join it
f15se2-ex --connect 10.0.0.5 --name Viper             # joins 10.0.0.5:27015
f15se2-ex --host --bind 192.168.1.10 --name Viper     # serve only that interface
```

`--host` forks the same executable with `--server`, waits for its
"listening on" line, then connects to `127.0.0.1`. The child dies with the
client (atexit reaper + `PR_SET_PDEATHSIG`), so no orphan servers remain.
`--connect` without an explicit port defaults to `27015`.

Conceptually:

```text
               Simulation Core
                /           \
               /             \
        F-15 Client        F-15 Server
        SDL/OpenGL          headless
```

Getting this separation right is more important than sophisticated networking.

---

# 17. Multiplayer Mission Model

The existing mission logic apparently assumes one player.

For example:

```text
finalizeMission()
       ↓
MISSION OVER
```

That cannot remain the multiplayer semantic.

Introduce per-player outcomes:

```cpp
struct PlayerMissionState {
    bool alive;
    bool ejected;
    bool landed;
    bool objectiveCompleted;
    int score;
    ...
};
```

and shared mission state:

```cpp
struct MissionState {
    MissionPhase phase;
    TeamState teams[MAX_TEAMS];
    ...
};
```

Then:

```text
Pilot #1 dies
    ≠
mission necessarily ends
```

Possible outcomes include:

```text
pilot killed
pilot ejected
pilot landed
pilot disconnected
team objective complete
mission complete
```

---

# 18. Player-Centric Global Logic

Audit code such as:

```text
g_closestThreatIndex
currentTarget
radar target
incoming missile
nearest enemy
```

Anything implicitly meaning:

> closest threat to THE player

must eventually mean:

> closest threat to PlayerSim N

This is likely one of the most dangerous parts of the multiplayer conversion.

It deserves explicit tests.

---

# 19. AI Uses the Same Game Protocol

External AI should not get a completely separate backdoor into the simulation.

Conceptually:

```text
Human client
     │
   input
     │
     ▼
  server

AI client
     │
   input
     │
     ▼
  server
```

An AI connection can identify itself during session setup:

```text
role = human
```

or:

```text
role = ai
```

Both ultimately control a `PlayerSim`.

This makes AI-vs-human and AI-vs-AI natural consequences of the multiplayer architecture.

---

# 20. AI Observation API

AI does not necessarily need rendered pixels.

Give agents a structured observation.

For example:

```cpp
struct AiObservation {
    Tick tick;

    OwnAircraft ownship;

    VisibleAircraft aircraft[MAX_VISIBLE];
    VisibleMissile missiles[MAX_VISIBLE_MISSILES];

    RadarState radar;
    WeaponState weapons;
    MissionObservation mission;
};
```

Important: AI should normally receive **information the pilot is entitled to know**, not the entire omniscient server state.

For example:

```text
own aircraft state       YES
radar contacts           YES
RWR information          YES
visible objects          YES
known mission objectives YES

hidden enemy exact state NO
```

This lets AI compete under approximately the same information constraints as humans.

For debugging/training, an optional privileged observation mode can expose full state.

---

# 21. AI Action API

AI actions should map onto the same semantic control model:

```text
roll
pitch
throttle
rudder

fire gun
fire selected weapon

select weapon
select target
radar commands
ECM
chaff
flare
gear
airbrake
autopilot
...
```

Then:

```text
AI policy
    ↓
NetInput
    ↓
same command adapter
    ↓
legacy simulation
```

Do not let AI directly modify:

```text
position
velocity
missile state
enemy health
```

unless explicitly operating in a special testing mode.

---

# 22. Synchronous AI Mode

Real-time networking is inconvenient for reinforcement learning.

Add:

```bash
f15server --sync-step --seed 12345
```

In this mode:

```text
Server sends observation for tick N
              ↓
AI sends action for tick N
              ↓
Server advances exactly one tick
              ↓
Server sends observation N+1
```

Therefore training speed is independent of real time.

The simulation could potentially run:

```text
1× realtime
10× realtime
100× realtime
```

depending on CPU performance.

No renderer is required.

---

# 23. Reproducibility

Expose the random seed:

```bash
--seed 12345
```

An episode can then be described by approximately:

```text
mission
seed
initial configuration
input/action stream
```

This is valuable for:

```text
AI training
bug reproduction
multiplayer debugging
replays
regression testing
```

However, do not assume fixed-point + LCG automatically guarantees cross-platform determinism.

Audit:

```text
signed integer overflow
signed shifts
uninitialized data
iteration order
endianness
type sizes
padding
timing dependencies
undefined C/C++ behavior
```

---

# 24. State Hashing

Add a canonical simulation-state hash.

For example:

```text
tick 5000
state hash = 0x8F37...
```

Only simulation-relevant canonical data goes into it.

Do not hash:

```text
pointers
struct padding
rendering state
audio state
wall-clock timestamps
```

This gives a powerful debugging mechanism:

```text
Server: tick 8120 = ABCD
Client: tick 8120 = ABCD

OK
```

or during deterministic tests:

```text
Server: tick 8121 = 91E2
Client: tick 8121 = 76A1
                     ↑
              first divergence
```

---

# 25. Replay Support Comes Almost for Free

Once inputs are tick-based, store:

```text
protocol version
game version
mission
seed
initial state
input stream
important external events
```

A replay can rerun the simulation.

Even if perfect determinism is not achieved initially, periodic authoritative checkpoints can make replay robust:

```text
checkpoint
inputs
inputs
inputs
checkpoint
...
```

This will also be extremely useful for diagnosing multiplayer and AI failures.

---

# 26. Serialization

Never send raw C/C++ structures:

```cpp
send(&myStruct, sizeof(myStruct)); // don't
```

That creates problems with:

```text
endianness
alignment
padding
compiler ABI
32/64-bit builds
WASM
protocol evolution
```

Use explicit serialization:

```cpp
writer.u32(entityId);
writer.i16(roll);
writer.i16(pitch);
writer.u32(flags);
```

Define one canonical wire byte order.

This also prepares the project for big-endian builds.

---

# 27. Protocol Versioning

Start versioning immediately.

For example:

```text
magic
protocolVersion
messageType
payloadLength
sequence/tick
payload
```

Handshake can exchange:

```text
protocol version
game build/version
mission checksum
asset/data compatibility
role: human/AI/spectator
capabilities
```

Fail cleanly when incompatible versions connect.

---

# 28. Security Boundary

Assume Internet clients are untrusted.

The server validates:

```text
input ranges
legal commands
fire rate
weapon availability
player identity
message sizes
entity references
tick ranges
```

A client must never be able to send something equivalent to:

```text
SET_HEALTH 100
DESTROY_PLAYER 2
SPAWN_MISSILE_AT ...
```

as an ordinary player action.

AI clients receive the same restrictions unless running in an explicitly trusted training environment.

---

# 29. Prediction: Do Not Implement It Yet

At 15 Hz, start with:

```text
authoritative server
+
snapshot interpolation
```

Then test actual gameplay.

Only add local prediction/reconciliation if latency makes the player's own aircraft controls unpleasant.

Do not begin the project with rollback networking, sophisticated
