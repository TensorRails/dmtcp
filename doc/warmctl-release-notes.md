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
[the measured phase breakdown](h200-restart-performance.md).

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
targets Ubuntu 24.04 x86-64 and excludes warmctl's fdkeep plugin. Use identical
runtime libraries for bake and restore; bake new snapshots after upgrading.
