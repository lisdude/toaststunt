# Native implode benchmark and validation

Date: 2026-11-19.
Base: upstream master `2152ad4f6b57e82989c4fa110fc1ce70278f0cf7`.

Both paths ran on the same modified Release binary using a disposable copy of
a ToastCore database. The old path is the
exact reference verb supplied for this change, installed on a temporary object;
it is not a replacement of ToastCore's own utility verb. The measured binary exposed the native implementation under its original name,
`join()`. The current API name is `implode()`. No live callers or database verbs were migrated.

## Original benchmark before the rename

Median process CPU microseconds and MOO ticks per complete call (including the
common benchmark driver loop). CPU time and tick accounting are separate measures.

| Elements | Separator | Reference CPU µs/call | Native CPU µs/call | CPU speedup | Reference ticks/call | Native ticks/call |
|---:|---|---:|---:|---:|---:|---:|
| 5 | comma-space | 1.376 | 0.210 | 6.55× | 30.00 | 4.00 |
| 5 | empty | 0.432 | 0.185 | 2.33× | 9.00 | 4.00 |
| 1,000 | comma-space | 470.473 | 13.509 | 34.83× | 4010.01 | 4.00 |
| 1,000 | empty | 7.459 | 7.385 | 1.01× | 9.00 | 4.00 |

The long-list empty-separator case is effectively unchanged; a 1% timing
difference in one run does not establish a repeatable gain. The nonempty-separator
path eliminates repeated copying of the growing result and MOO-level iteration,
showing a much larger benefit on these inputs. These are synthetic workloads, not
measurements of whole-MOO CPU reduction. Tick savings do not imply equal CPU
savings, and a single native call does not yield between elements.

## Method

- AMD Ryzen 5 5600G, GCC 15.2, 64-bit CMake Release with jemalloc disabled.
- One MOO process pinned to allowed CPU 0; no other test/build run concurrently
  during timed samples. CPU was not reserved from unrelated host work; the host
  governor remained `powersave`.
- Waited at least 60 seconds after startup. Three warmups and 16 samples per path
  per case. Measurement order alternated between old/new and new/old.
- Each path calibrated independently to about 0.15 CPU seconds per sample. CPU
  reports use `usage()` user plus system time divided by iteration count. Ticks
  use `ticks_left()` differences divided by iteration count; the small fractional
  remainder is fixed measurement overhead spread across the sample.
- Every element was the six-character string `Ab~00 `, preserving literal escape
  text. Five and 1,000 elements; separators `", "` and `""`. Verified identical
  results before timing each case.
- Foreground tick/seconds limits raised to 2,147,483,647 and 60, with a 10,000,000
  character string limit, only in the disposable benchmark database.
- Outbound connections disabled. Input database unchanged; clean server shutdown.

Input SHA256: `8013b703c61a9894866f836f2b934eada7118cdf0b3cd56181e4bf9205b2f557`.
Binary SHA256: `651faf5418a4db8cc057b7e9dd222f94414e61407c801cce2e79654f2d41716f`.
Reproduce using `test/benchmark_implode.py`; see [implode documentation](implode.md).

## Validation

- Differential integration tests run on both repository `test/Test.db` and
  ToastCore: 157 checks per database. They compare the native builtin and the separate compatibility
  snippet with the exact MOO reference, including mixed values, separators,
  binary escape text, arity, non-list legacy inputs, exact/beyond size boundaries,
  unused oversized separators, and catchable/task-aborting overflow behavior.
- Existing suites: string operations, collection improvements, limits, system
  builtins, and primitives: 62 tests, 293 assertions, no failures or errors.
- AddressSanitizer (`LeakCheck` build) runs the differential integration tests on
  Test.db: 157 checks passed. Leak detection disabled; this check targets invalid memory access,
  not whole-engine leak freedom.
- The full repository suite was not run. An initial system-builtin test runner
  failure came from different PATH environments for Bundler and the server; the
  environment was matched and the suite rerun successfully.

## 10,000-element follow-up

Same database and executable SHA256 hashes as above; a fresh disposable ToastCore
server settled for 60 seconds. Six-character elements, three warmups and 16
alternating samples per path, with independent calibration as before.

| Separator | Reference CPU ms/call | Native CPU ms/call | CPU change | Reference ticks/call | Native ticks/call |
|---|---:|---:|---:|---:|---:|
| comma-space | 25.362125 | 0.128006 | -99.50% | 40011.000 | 4.003 |
| empty | 0.067155 | 0.069076 | +2.86% | 9.002 | 4.002 |

With comma-space, native join used about 198× less CPU time per call (99.50%
reduction). The output was 79,998 characters. With an empty separator, native
join used 2.86% more CPU time in this run, an absolute difference of about 1.92
microseconds per call; output was 60,000 characters. This single run does not
establish the size of a repeatable empty-separator regression.

Ticks include driver-loop and fixed measurement overhead; the reference
comma-space path used four iterations per sample, so the fixed four measurement
ticks contribute one tick per reported call. Native comma-space used 1,171
iterations; reference/native empty used 2,234/2,172 iterations respectively.

Reproduce with `test/benchmark_implode.py MOO TOASTCORE_DB OUTPUT.json --elements 10000`.
Both paths returned identical results; the input database remained unchanged and
the server shut down cleanly.

## Element-count / element-width matrix

Same executable and input database hashes as above. A fresh disposable ToastCore
server settled for 60 seconds, followed by three warmups and 16 alternating
samples per path per case. Elements contained repeated `x` characters at the
specified width. All 24 cases returned matching outputs of the expected length;
clean shutdown and unchanged input confirmed. CPU 0 affinity and independent
per-path calibration were retained. CPU times include common driver overhead.

Each cell below is **reference → native CPU microseconds per call (speedup)**.

Comma-space separator:

| Elements | 1 character/element | 10 characters/element | 100 characters/element |
|---:|---:|---:|---:|
| 1 | 0.489 → 0.149 (3.29×) | 0.486 → 0.145 (3.36×) | 0.540 → 0.189 (2.86×) |
| 10 | 2.412 → 0.266 (9.05×) | 2.404 → 0.307 (7.83×) | 3.208 → 0.419 (7.66×) |
| 100 | 24.590 → 1.650 (14.91×) | 30.509 → 1.757 (17.36×) | 52.480 → 2.040 (25.73×) |
| 1,000 | 365.150 → 14.214 (25.69×) | 566.138 → 14.488 (39.08×) | 3247.633 → 18.284 (177.62×) |

Empty separator:

| Elements | 1 character/element | 10 characters/element | 100 characters/element |
|---:|---:|---:|---:|
| 1 | 0.411 → 0.149 (2.77×) | 0.406 → 0.145 (2.81×) | 0.456 → 0.189 (2.41×) |
| 10 | 0.469 → 0.210 (2.24×) | 0.523 → 0.256 (2.04×) | 0.652 → 0.364 (1.79×) |
| 100 | 1.158 → 0.914 (1.27×) | 1.260 → 1.006 (1.25×) | 1.680 → 1.401 (1.20×) |
| 1,000 | 7.639 → 7.576 (1.01×) | 7.671 → 7.598 (1.01×) | 12.078 → 11.701 (1.03×) |

MOO ticks are independent of string width in these cases. Approximate ticks/call
(including driver/measurement overhead):

| Elements | Reference comma-space | Reference empty | Native, either separator |
|---:|---:|---:|---:|
| 1 | 14 | 9 | 4 |
| 10 | 50 | 9 | 4 |
| 100 | 410 | 9 | 4 |
| 1,000 | 4,010 | 9 | 4 |

The largest empty-separator cases differed by only about 1–3%; this single matrix
run does not establish a repeatable benefit at that scale. Nonempty-separator
speedups grow substantially with accumulated output copying in the reference.
Results describe homogeneous synthetic strings, not application-wide savings.

Reproduce with `test/benchmark_implode.py MOO TOASTCORE_DB OUTPUT.json --elements 1 10 100 1000 --widths 1 10 100`.

## Verification under the final API name

The Release binary with `implode()` has SHA256
`3a53f3f02d3927a1a7ab6dcc342e11ad6489937fe5b5d65ff5d8a30dba17d777`.
The same ToastCore input, six-character elements and sampling method described
above produced the following medians after 60 seconds of startup settling.

| Elements | Separator | Verb CPU µs/call | Builtin CPU µs/call | Speedup | Verb ticks/call | Builtin ticks/call |
|---:|---|---:|---:|---:|---:|---:|
| 5 | comma-space | 1.402 | 0.208 | 6.73× | 30.00 | 4.00 |
| 5 | empty | 0.445 | 0.182 | 2.44× | 9.00 | 4.00 |
| 1,000 | comma-space | 474.814 | 13.527 | 35.10× | 4010.01 | 4.00 |
| 1,000 | empty | 7.411 | 7.335 | 1.01× | 9.00 | 4.00 |

All four cases matched the reference and expected output length. The server
exited cleanly and the input database hash was unchanged.

The Release build passed 161 differential checks each on Test.db and ToastCore.
Five Ruby engine suites passed 63 tests and 296 assertions, including the
programmer-level call to `implode()` in the standard string suite.
The AddressSanitizer build passed the same 161 checks on Test.db with leak
detection enabled. A separate run on that build made 50,000 calls producing
a 1,024-byte result and 50,000 raising `E_QUOTA` in one server process.
LeakSanitizer completed its exit scan without a report.
