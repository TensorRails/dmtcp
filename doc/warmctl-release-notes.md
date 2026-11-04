# Unreleased

- The checkpoint writer records resident all-zero MiBs of private anonymous
  memory as zero runs instead of saving them. Images are smaller and restarts
  read less; existing restarts read the new images, and
  `DMTCP_DISABLE_ZERO_SCAN=1` turns it off.
- With `DMTCP_RESTART_READERS`, writable payloads of at least 1 MiB are read by
  the helpers in batches, so a large area cut by zero runs is still read in
  parallel. Smaller and read-only payloads are read as before.

On a CPU test process like the H200 worker (a 12 GiB area with a 25% zero
tail, plus 10,000 small areas), 16 readers and huge pages: the image fell from
14.07 to 11.07 GiB and the checkpoint from 16.5 to 14.3 s; the memory pass from
8.66 to 6.86 s with a cold file cache and from 1.35 to 1.19 s with a warm one.
With zeros in every other 64 MiB stripe of 12 GiB, the image halved and the
pass fell from 7.28 to 3.68 s cold.

On a GPU, warmctl beta.17 baking and restoring Qwen3-0.6B on one RTX 3090
(8 readers, huge pages): the process images fell from 8,582 to 6,065 MiB (the
vLLM worker's from 5,701 to 3,215 MiB), the worker's memory pass from 1.05 to
0.74 s, and time to `/health` from 4.6–5.1 s to 4.0–4.4 s, with the same
greedy answers. Pinned CUDA host memory is a shared mapping, whose zeros the
content scan already skipped, so the H200 Qwen3.8 worker's pinned embedding
table gains nothing from the writer change.

# v5.0.0-warmctl.1

First versioned TensorRails fork release, based on DMTCP 5.0. It includes the
existing warmctl branch's socket, shared-memory, dlopen, fcntl64 and checkpoint
storage fixes, plus the large-memory restart changes below.

- Opt-in transparent huge-page advice reduces first-touch page-fault overhead.
- Opt-in 2–8 or 16 parallel `pread` readers restore raw payloads of at least
  1 GiB on x86-64. Helpers are reaped before resumption, die with the parent,
  and use raw syscalls; failed reads/creation fall back to ordinary reading.
- Restart profiling reports per-process memory reads, faults, mappings,
  barriers and plugin time.
- Restart environment lookup scans all entries and matches exact names.
- The checkpoint writer preserves private dirty bytes in a regular file's
  last partial page. The checkpoint format is unchanged.
- Experimental private mmap of raw images remains opt-in and is **not
  recommended for CUDA restores**: measured work moved into the CUDA phase.

On one H200, Qwen3.8-Flash-Next-NVFP4, same local snapshot with hot file cache:
ordinary restores took 77.36/82.14 s; huge pages + 16 readers took
20.21/23.43/20.27/21.39 s (median 20.83 s to health, 20.92 s to first streaming
token). In the illustrated pair, DMTCP fell from 54.09 to 4.04 s. This is not
a 20-second SLA or a measurement of provisioning/network transfer. See
[the measured phase breakdown](https://github.com/TensorRails/dmtcp/blob/v5.0.0-warmctl.1/doc/h200-restart-performance.md).

All 20 GPU restores matched weight fingerprints and passed streaming/tool
smoke tests; 640 short requests completed without errors. Numerical canary
equivalence remains unqualified: the donor already varied before checkpoint
(maximum common-prefix logprob difference 1.15547 at tolerance 0.001).

Enable the measured reader configuration with:

```sh
DMTCP_RESTART_HUGEPAGES=1 DMTCP_RESTART_READERS=16 DMTCP_RESTART_MMAP=0 \
  warmctl up SNAPSHOT --mode in-place
```

The switches are opt-in in DMTCP itself. THP and reader count depend on RAM,
CPU and storage; compressed streams use ordinary reading. The binary asset
targets Debian 12-compatible x86-64 or ARM64 systems (glibc >= 2.36) and includes the warmctl fdkeep plugin.
The plugin now lives in this fork and has native syscall implementations and
CPU descriptor-restoration tests on both architectures.
Both architectures run native CPU checkpoint/restore tests; the H200 results
apply to x86-64. ARM64 currently uses the ordinary reader with optional THP,
and GPU serving is not qualified on ARM64. These are Linux packages, not
macOS/Windows binaries. Use identical
runtime libraries for bake and restore; bake new snapshots after upgrading.

ARM64 checkpoint creation keeps the upstream gzip-disable safeguard. CI also
checks restoration of an externally compressed owned fixture on both platforms.
