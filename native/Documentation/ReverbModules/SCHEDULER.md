# Amortized convolution scheduler

## Purpose and scope

`Source/DSP/PreparedConvolution.h` supplies the prepared, stereo FIR engine used by convolution reverb. Its API is unchanged:

- `prepare(leftIR, rightIR, length)` allocates and transforms coefficients off-thread
- `reset()` clears audio history and any incomplete jobs without allocating
- `process(left, right, samples)` processes in place, using one serialized audio owner

This layer adds zero latency: coefficient `h[k]` affects output sample `k` after an input impulse at zero. The convolution processor's separate sample-rate-conversion support/dry alignment is outside this scheduler and may declare its own latency.

No callback invokes JUCE FFT/convolution, a shared FFT cache, a background worker, an allocation, a free, or a lock. Coefficients and all FFT tables/buffers are prepared ahead of audio ownership. The existing off-thread pending/retired engine handoff continues to own lifetime management.

## Partition layout

| Region | Coefficient indices | Partition length | Execution |
| --- | --- | --- | --- |
| Direct head | 0–63 | 64-tap direct FIR | Per sample |
| First FFT tier | 64–255 | 64 | Synchronous, at most 128-point FFT |
| Second FFT tier | 256–2047 | 256 | Synchronous, at most 512-point FFT |
| Middle scheduled tier | 2048–8191 | 1024 | Fully incremental, 2048-point FFT |
| Long scheduled tier | 8192 onward | 4096 | Fully incremental, 8192-point FFT |

The two scheduled regions begin at twice their partition length. This gives each job one complete partition interval after input collection and before its first output is due. The previous large synchronous region was moved into this schedule; no coefficient is dropped, shifted, or double counted.

## Exact scheduling and deadlines

For a scheduled partition length `P`, input block `k` is collected during frames `kP` through `(k+1)P-1`. Its FIR contribution begins at `(k+2)P`, because that tier's first coefficient is at offset `2P`. The engine therefore executes its job during the next `P` samples and swaps the completed output block exactly at the deadline.

Two incoming blocks and two output blocks per channel are preallocated. Audio collection/playback use one pair while the current job owns the other. A job runs these phases for each channel:

1. Copy captured input and zero-pad the complex FFT work buffer
2. Incremental bit reversal and every forward FFT butterfly
3. Copy the positive-frequency bins into the input-spectrum history ring
4. Accumulate every partition/bin product in coefficient order
5. Reconstruct negative-frequency bins by Hermitian symmetry
6. Incremental bit reversal and every inverse FFT butterfly
7. Scale the inverse FFT and assemble output/overlap samples

No phase performs an uncounted whole-block transform, history clear, output clear, or overlap copy at its boundary. Starting a job and swapping buffers only change scalar fields.

A work unit is one copy/zero, bit-reversal visit, complex FFT butterfly, spectral product/accumulate, mirrored bin, or overlap sample. An `N=2P` FFT requires exactly `N + (N/2)*log2(N)` units. With `C` coefficient partitions, a complete stereo job requires:

`W = 2 * (2P + 2*FFTWork + (P+1) + C*(P+1) + (P-1) + P)`

Each sample executes at most `ceil(W/P)` units for that tier. All operations are included, so the job finishes after at most `P` advances. Debug builds assert that the job is already complete when its output buffer becomes active. There is no deadline-time catch-up path that could reintroduce a giant callback burst.

At the current maximum prepared length of 3,841,536 frames, the middle tier has 6 coefficient partitions and a quota of 75 units/sample. The long tier has 936 partitions and a quota of 1,943 units/sample. Unit counts are a deterministic work bound, not a hardware-specific nanosecond guarantee; different phases and memory conditions have different costs.

## Arithmetic and lifetime

One owned FFT table set is shared by all job phases and both channels within each tier. Incremental transforms have the same butterfly order as the synchronous radix-2 reference. Incremental inverse transforms leave normalization to the scheduled overlap phase.

The dominant complex products use explicit finite-signal real/imaginary arithmetic, allowing the compiler to vectorize them without `std::complex` exceptional-value fallback calls. The surrounding convolution core is responsible for input validation/headroom and prepared-IR validation. Direct callers must provide valid buffers and prepared finite coefficients, as before.

A reset clears double buffers, spectrum histories, overlap, cursors, and job state. Reprepare disposes of old owned storage only off-thread. Neither operation may run concurrently with process on the same instance.

## Verification

From the native project directory:

```sh
g++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror \
    Tests/ReverbSchedulerTests.cpp -o /tmp/hem-reverb-scheduler-tests
/tmp/hem-reverb-scheduler-tests
/tmp/hem-reverb-scheduler-tests --benchmark
```

The suite checks:

- Incremental forward/inverse FFT equivalence at lengths 128, 512, 2048, and 8192, with budgets 1, 7, 33, 201, and 4096
- Exact work-count accounting and no call exceeding its unit quota
- Coefficients immediately around every direct, synchronous, and scheduled boundary
- Twelve host block sizes: 1, 7, 17, 31, 63, 64, 127, 256, 511, 1024, 4096, and 8192
- Independent sparse-input/dense-IR FIR references through 100,003 taps and the full 3,841,536-frame maximum
- Independent dense-input FIR reference, not only impulses
- Bit-identical host-block partition invariance
- Stereo independence, exact zero-latency head, full tail support, reset determinism, and mid-job reprepare
- Zero observed allocations during processing and reset

The optimized numerical suite passed 9,430,825 assertions. Maximum error in the reported long-reference checks was 1.47e-8. Runtime/allocation checks of the containing convolution processor remain part of its separate integration suite.

## Performance interpretation

Before scheduling, an independent optimized 64-frame-block probe observed a 14.94 ms maximum callback at approximately 3.84 million taps, because all long-tier transforms and spectral products ran together at one boundary. The scheduled version bounds that work per audio sample and also removes the middle-tier synchronous FFT burst.

The benchmark reports mean, 99th-percentile, and maximum thread-CPU and wall times. Thread-CPU time helps distinguish execution cost from descheduling; it does not eliminate shared-cache, bandwidth, frequency-scaling, or virtualization effects. A 64-frame callback has only 333 microseconds at 192 kHz and 1,333 microseconds at 48 kHz. This scheduler does not claim that every supported IR/rate/host-buffer combination is realtime-safe on every machine. Windows release-build hardware measurements, representative multi-plugin chains, and replacement crossfades (which temporarily run two engines) are still required.

### Observed optimized cloud timing, 2026-10-06

Three separate runs, each with 128 warm-up blocks and 2,048 measured 64-frame blocks, produced the following ranges. No sanitizer run was active during these samples; the shared executor is still not an isolated realtime machine.

| Prepared taps | CPU mean | CPU p99 | CPU maximum |
| --- | --- | --- | --- |
| 48,000 | 29.7–32.3 µs | 87.4–141.5 µs | 296.6–1,023.3 µs |
| 480,000 | 43.7–45.2 µs | 117.1–175.0 µs | 302.1–497.8 µs |
| 3,841,536 | 228.8–260.3 µs | 388.8–501.5 µs | 1,619.1–2,970.4 µs |

For the largest IR, wall-time means were 229.2–262.2 µs, p99 values 389.2–507.9 µs, and maxima 1,622.7–3,280.6 µs. The largest configuration therefore still exceeds the 333 µs / 64-frame / 192-kHz budget in these runs. Work is spread deterministically rather than executed as one large tier-boundary job, but this is not proof of a universal realtime deadline. Replacement crossfades temporarily process two prepared engines and require additional headroom.

The complete numerical suite also passed AddressSanitizer and UndefinedBehaviorSanitizer on the final source. Leak detection was disabled because LeakSanitizer cannot run in this traced executor. The standalone numerical/sanitizer checks do not certify Windows hardware audio or the containing application's complete release build.

### Bounded optimization audit

The final code already uses explicit real/imaginary multiplication in the synchronous and incremental FFTs and both scheduled spectral-product loops. GCC's optimization report confirms that the scheduled product loops are vectorized with 16-byte vectors, with runtime alias checks. The optimized standalone binary has no undefined `__mulsc3` or `__muldc3` complex-arithmetic fallback symbol. Fast-math was not enabled.

An additional unchanged-code audit run measured CPU mean/p99/max of 52.5/193.6/377.5 µs at 480,000 taps and 266.8/592.8/1,503.0 µs at 3,841,536 taps. Corresponding wall maxima were 379.5 and 1,505.4 µs. Since thread-CPU and wall times were close in that run, those maxima cannot be dismissed as wall-clock-only preemption; execution and shared-cache/memory variability remain material. No unverified storage-layout rewrite was retained, and the realtime qualification limits above remain in force.
