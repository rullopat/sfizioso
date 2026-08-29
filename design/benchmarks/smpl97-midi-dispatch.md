# SMPL-97 MIDI dispatch benchmark

The standalone harness in `benchmarks/midi_dispatch/` compares identical public
MIDI/audio workloads across engine revisions without requiring Google
Benchmark. It counts C++ heap allocations only between the first event of the
first measured block and the end of that block; SFZ parsing, control-thread
configuration, buffer creation and benchmark reporting are outside the count.
Each timing includes event dispatch plus one 256-frame stereo render. Cleanup is
outside the timed interval.

## Frozen comparison

- Baseline: SMPL-93 `a713070d7341763a2460a05a3f30da55b00b5cb1`
- Current: scoped-routing engine `f8557e041a12b1de7537b6ae2ee89068ac0a7c97`
- Compiler: GCC 16.2.1, Release, x86-64
- Host: AMD Ryzen 7 255, Linux 7.1.9
- Repetitions: 50 sequential iterations per case
- Region counts: 1, 100 and 1,000
- Dense controller counts: 1, 16 and 128

The complete raw and comparison data is in
[`smpl97-midi-dispatch/`](smpl97-midi-dispatch/). Representative results:

| Traffic in one block | Regions | Controllers | Baseline mean | Current mean | Ratio | First-block allocations |
|---|---:|---:|---:|---:|---:|---:|
| Idle render | 100 | 16 | 6.2 us | 8.3 us | 1.34x | 0 -> 0 |
| Legacy note + 192 expression events | 100 | 16 | 0.877 ms | 0.822 ms | 0.94x | 0 -> 0 |
| MPE notes + 192 Member events | 100 | 16 | 8.175 ms | 8.431 ms | 1.03x | 300 -> 0 |
| Malformed overlapping MPE + broadcast | 100 | 16 | 28.604 ms | 30.068 ms | 1.05x | 120 -> 0 |
| Dense MPE, 1,536 expression events | 100 | 128 | 10.343 ms | 11.840 ms | 1.14x | 1,340 -> 0 |
| MPE, largest instrument/control case | 1,000 | 128 | 82.881 ms | 91.740 ms | 1.11x | 300 -> 0 |

The very small idle cases are sensitive to scheduler and clock noise; their
microsecond-scale ratios are not used as throughput claims. Across active
legacy cases the geometric-mean ratio is 1.04x, and across active MPE matrix
cases it is 1.13x. The architectural result is the removal of all observed
first-use Member allocations: 300 allocations in ordinary MPE, 120 in the
malformed-overlap case, and 1,340 in dense traffic all become zero. The harness
also confirms zero measured allocations for legacy and idle traffic before and
after the refactor.

## Reproduction

From a clean sfizioso checkout:

```sh
ITERATIONS=50 benchmarks/midi_dispatch/run_comparison.sh \
  a713070d f8557e04 /tmp/sfizz-midi-comparison
```

The script creates detached worktrees, checks out each revision's exact
submodules, copies the same compatibility harness into the baseline, builds
both with the same Release compiler flags, runs them sequentially, and writes
raw CSV, comparison CSV and environment metadata.
