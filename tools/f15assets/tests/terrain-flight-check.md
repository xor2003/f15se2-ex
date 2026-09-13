# SVN hill approach check

`svn_hill_approach_x86_64.gdb` exercises the replacement-terrain collision path
using the normal flight model. It requires a Linux x86-64 debug build and the
current SVN terrain layout. It is an interactive integration check, not a
portable unit test.

Run from the repository root with an isolated directory for pilot data:

```sh
mkdir -p /tmp/svn-flight-check
F15_REPLACEMENT_ROOT="$PWD/converted_assets_all" \
F15_REPLACEMENT_ROOT_ONLY=1 SDL_AUDIODRIVER=dummy \
gdb -q -batch -x tools/f15assets/tests/svn_hill_approach_x86_64.gdb \
  --args ./build/f15se2-ex --game /tmp/svn-flight-check \
  --campaign SVN --nointro
```

Select a pilot and start a mission normally. The script then places the aircraft
outside the hill at `(557056, 588800, 1500)`, facing north at 800 knots with
full thrust, no autopilot, and Ace collision behavior. It changes position once,
not on every frame. Leave the flight controls untouched afterward.

The script clears the legacy visibility-derived collision flag each step so
the test cannot pass merely because the original collision path fired. It
does not disable the replacement mesh collision check. The mission-ending
argument is read from the x86-64 System V argument register, `$rdi`.

Expected result: `FLIGHT_END code=2` after more than one physics step, followed
by GDB exiting successfully. A timeout is inconclusive, not a pass. Software
OpenGL rendering under a debugger can make the approach take over a minute.

Observed on the current SVN campaign:

```text
FLIGHT_START x=557056 y=588800 z=1500
PATH step=200 x=557056 y=615384 z=2977 heading=0 pitch=271 speed=705
FLIGHT_END code=2 steps=206 x=557056 y=616140 z=3019
```

This demonstrates one flight into a generated slope. It does not establish
swept collision detection: the implementation still tests the current position,
so a sufficiently narrow obstacle crossed entirely between steps can be missed.
Changing the campaign's hill placements invalidates these fixture coordinates.
