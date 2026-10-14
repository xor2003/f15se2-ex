# DOS benchmark checkpoint

This commit preserves previously uncommitted DOS tuning and benchmark work.
It is not a validated deterministic whole-sortie replay implementation and is
not merged into integration/latest. Source review on 2026-09-19 found:

* benchmarkInitialize runs at gameMainLoop entry, after mission setup calls
  seedRng. Resetting libc srand there does not freeze earlier mission generation
  or the separate game RNG state. Determinism must start before setup.
* Benchmark timerPump advances counters once per invocation, not according to
  simulation time. Call-count changes can change simulation behavior.
* Benchmark mode suppresses input and presentation and exits the process at the
  frame limit. It is for profiling, not ordinary mission lifecycle validation.
* DOS pacing now uses an optional fixed cap rather than an adaptive slow-frame
  target. DOS hardware/emulator timing needs separate validation.

Do not use framebuffer hashes from this checkpoint as the sole acceptance
criterion for the fixed/float migration. The required oracle is a frozen initial
state and tick-indexed inputs with per-tick state comparison.

This checkpoint has source/diff review only; a DOS build and playthrough have not
been rerun as part of the branch housekeeping.
