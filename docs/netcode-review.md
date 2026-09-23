# Multiplayer review guide

This PR adds an opt-in authoritative multiplayer path (`F15_NET=ON`).
The default build remains single-player. The networking implementation is
intended for Linux; the host launcher uses POSIX process APIs.
The latest edits have not been built or tested at the author's request.

## Build and try it

Install the normal build dependencies plus protobuf development headers,
`protoc`, and OpenSSL development headers. On Debian/Ubuntu the additional
packages are `libprotobuf-dev protobuf-compiler libssl-dev`.

```sh
cmake -S . -B build-net -DF15_NET=ON
cmake --build build-net -j2
ctest --test-dir build-net --output-on-failure
./build-net/f15se2-ex --game /path/to/game --host
# A second client, using the same protocol version and game assets:
./build-net/f15se2-ex --game /path/to/game --connect 127.0.0.1
# Dedicated server (also available as build-net/f15server):
./build-net/f15se2-ex --server --game /path/to/game --bind 127.0.0.1 --port 27015
```

`--connect` currently accepts an IP literal, optionally with a port; it does
not resolve DNS names. The default UDP port is 27015. Original game assets
are required to run the game/server, but not the CTest suite.
`F15_PROTOBUF_ROOT` is an optional unpacked-package root with `usr/include`,
`usr/lib` (or `usr/lib/x86_64-linux-gnu`), and `usr/bin/protoc`.

## Suggested review order

| Area | Files | Main question |
| --- | --- | --- |
| Input and pilot state | `src/egplayer.*`, `src/net/commands.*` | Does each pilot retain their own controls, flight, combat and mission state? |
| Simulation orchestration | `src/egframe.c`, `src/egframeseg.c`, `src/f15world.c`, `src/egtarget_net.c` | Do world updates run once and player-relative updates under the correct context? |
| Server lifecycle | `src/server/f15server.cpp` | Are join/leave, command consumption, threat ownership and event recipients correct? |
| Wire protocol | `src/net/protocol.h`, `serialize.h`, `codec.cpp`, `snapshot.cpp` | Are versioning, bounds and snapshot commit rules consistent? |
| Client presentation | `src/net/netclient.cpp`, `netrender.c`, `src/egsnap.h`, `src/egsys.c` | Does interpolation restore authoritative state, and do cameras/HUD follow snapshots? |
| Build and entry points | `CMakeLists.txt`, `src/f15.c`, `src/f15host.*`, `.github/workflows/ci.yml` | Does default single-player still work, and is networking actually built in CI? |

The extraction commit `563c5eb` moves substantial bodies out of
`egframe.c` and `egtarget.c`; those files are not simply untouched upstream
glue. Compare moved code with `git diff --color-moved`, then inspect semantic
changes separately. Existing-code changes also include simulation-side hit
testing/target acquisition, RNG separation, projectile ownership and decoy
allocation. They affect single-player too.

## Automated evidence

- `net_codec_tests`: serialization and command mapping.
- `player_ctx_tests`: context swaps and remote input queue behavior.
- `net_sim_tests`: fabricated combat, capacity, snapshot rejection, camera
  interpolation, damage events, decoy allocation and observation entitlement.
- `net_server_tests`: actual server handlers with a recording transport;
  rejected re-HELLO releases its slot, observation generation preserves
  pilot state/hash, victim context swaps route private events correctly,
  and per-pilot warning bits survive context swaps and wire encoding.
- CI includes a Linux `F15_NET=ON` build/test job alongside the default jobs.

The earlier lifecycle/observation/event-routing regressions were reproduced
before their fixes. Subsequent radar, warning-light, protocol-v5 and pacing
edits have not been built or tested. Previous green results do not validate
this revision. Historical manual/sanitizer results are in
[the review history](netcode-review-history.md); they are not a fresh
certification of the current tree.

## Latest review fixes awaiting validation

- Remote aircraft remain in the tactical map but skip the radar ground-site
  pass, which previously painted a SAM icon over the aircraft symbol.
- Per-pilot R/I warning bits come from the authoritative targeting pass,
  including its lock, bearing and decoy gates. Protocol v5 appends these bits
  to the player block; rebuild both server and clients together.
- Axes-only traffic is paced at 30 Hz; discrete commands and fire-button edges
  send promptly. Rendering has a 120 FPS fallback cap when vsync does not pace it.
- GNS debug callbacks enqueue bounded messages without waiting or writing to
  stderr under GNS locks. The application drains them outside GNS callbacks;
  queue overflow is reported. Scheduling/latency effects still need profiling.

## Remaining limits and acceptance work

- Extended two-human dogfight/coop play and latency feel still need manual
  acceptance. Unit tests do not establish full multiplayer gameplay parity.
- The server world pass retains player-relative reads and chooses a resident
  pilot. Slot-order invariance and rendered/headless equivalence have not
  been established by a complete replay test.
- `--sync-step` waits indefinitely for input from every participating pilot;
  the planned 500 ms stale-input policy is not implemented.
- The state hash is a diagnostic over selected in-memory state, not a
  portable deterministic replay contract. The client currently ignores it;
  no automatic divergence detection is implemented.
- Snapshots replicate aircraft, missiles, map state and decoys, but do not
  replicate the `bulletTracks` gun-tracer table. Full rendering parity is
  not claimed.
- This is a trusted-peer prototype: no account authentication, input timeout
  policy, or complete validation of all received setup/render indices.
  The wire version rejects incompatible protocol versions, not arbitrary
  different builds that advertise the same version.
- Browser transport, reconnect/resume, DNS resolution and Windows network
  hosting remain outside the implemented scope.

[networking.md](networking.md) is the design plan, including future work.
[netcode-review-history.md](netcode-review-history.md) preserves earlier
review rounds; later entries supersede older FIXED/PASS claims.
