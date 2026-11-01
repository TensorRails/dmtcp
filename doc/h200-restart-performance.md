# H200 local restart measurements, 2026-11-01

Model: `nvidia/Qwen3.8-Flash-Next-NVFP4`, one H200. A local 135.43 GiB
snapshot, with file cache hot, on a Xeon Platinum 8558 host; cgroup CPU quota
20.4 CPUs and RAM limit 233.76 GiB. GPU weights occupy 76.50 GiB. This
measures restart through HTTP readiness and the first streaming token, not
pod provisioning, runtime installation or snapshot transfer.

All modes used the same snapshot and H200. Earlier observations of 52.8 s
were on another host with a subsequently lost snapshot and are not an A/B
baseline for these measurements.

| Mode | Runs to health (seconds) | Median |
| --- | --- | --- |
| Ordinary reader | 77.36, 82.14 | 79.75 |
| Huge pages, one reader | 35.95, 37.46 | 36.71 |
| Private mmap, unaligned image (fallback) | 79.24, 75.98 | 77.61 |
| Private mmap, aligned copy | 77.02, 52.83 | 64.93 |
| Ordinary reader, aligned copy | 83.12 | 83.12 |
| Huge pages, four readers | 34.27, 26.58 | 30.42 |
| Huge pages, eight readers | 38.73, 27.03 | 32.88 |
| Huge pages, eight readers after cache preparation | 23.45, 23.77 | 23.61 |
| Huge pages, sixteen readers after cache preparation | 20.21, 23.43, 20.27, 21.39 | **20.83** |
| One-reader huge-page control after cache preparation | 44.97 | 44.97 |

Cache preparation evicted only unused experimental aligned-image and CPU
benchmark pages with `POSIX_FADV_DONTNEED`. It did not evict the primary
snapshot, model or host-wide page cache. `memory.current` fell from 163.61
to 135.07 GiB. The four fast runs peaked at 213.08 GiB, with no additional
direct reclaim scans. This preparation and the small sample sizes matter:
these are experiments, not a p95 or a <=20 s guarantee. The final binary,
including helper parent-death protection, produced the last two fast runs.
Median streaming first token from start across the four: **20.92 s**.

One complete before/after pair:

| Phase | Ordinary | Huge pages + 16 readers |
| --- | ---: | ---: |
| Setup and filesystem | 3.09 s | 3.33 s |
| DMTCP | 54.09 s | 4.04 s |
| CUDA | 13.52 s | 6.73 s |
| Wake and reload weights | 5.62 s | 5.12 s |
| KV, health, canary | 0.99 s | 0.99 s |
| External health polling | 0.05 s | 0.07 s |
| Total | 77.36 s | 20.27 s |

The slow worker's memory pass took 53.61 s, with 14,154,801 minor faults,
zero major faults and zero disk `read_bytes`. A single 47.6846 GiB anonymous
payload took 47.05 s. Its size matches the model's CPU PLE/Engram table,
allowing for allocation rounding; direct tensor-pointer attribution was
not performed. Other processes finished in 0.08–3.80 s and waited at the
barrier; adding their barrier waits would double-count elapsed time.

THP reduced that worker's minor faults about 49-fold. Sixteen readers loaded
the dominant region in 1.27–1.33 s and the whole worker in 3.56–3.73 s.
Private mmap reached 2.68–2.84 s for DMTCP but 39.81–60.66 s for CUDA.
Observed anonymous RSS growth is consistent with COW during CUDA host-memory
registration, though the driver's internal function was not traced.

## Correctness and limits

All 20 GPU runs matched weight fingerprints, returned streaming READY and
a valid forced echo tool call. All 640 short smoke requests succeeded.
Numerical equivalence is not proven: identical donor requests *before*
checkpoint, seed 42 and temperature 0, already differed in token sequence
and by up to 1.15547 logprob on the five-token common prefix. Both reported
zero cached tokens. The canary tolerance stayed 0.001; variability before
checkpoint does not prove absence of restoration bugs.

CPU tests verified every word of dense/sparse mappings, dirty-page writes,
second checkpoint/restore, source-image digests, uneven parallel chunks,
parent cancellation and private dirty bytes after file EOF. CI repeats
these checks against both the build and the packaged runtime. The fork
switches remain opt-in; CUDA mmap remains experimental and disabled in
the recommended configuration.
