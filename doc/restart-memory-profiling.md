# Restart memory profiling experiments

These opt-in switches diagnose large CPU process images. Configure DMTCP
normally. The default reader and checkpoint format stay unchanged.

| Environment variable | Value | Effect |
| --- | --- | --- |
| `DMTCP_RESTART_PROFILE` | `1` | Log memory restoration, large/slow regions, page faults, the restart barrier and plugin phase. |
| `DMTCP_RESTART_HUGEPAGES` | `1` | Advise huge pages for eligible large private anonymous mappings before loading their contents. |
| `DMTCP_RESTART_MMAP` | `1` | Map eligible raw anonymous payloads of at least 64 MiB directly from the image using private mappings. |
| `DMTCP_RESTART_READERS` | `2`–`8`, `16` | On x86-64, copy raw regular-file payloads of at least 1 GiB with parallel `pread` helpers. Default is one reader. |

Use one optimization at a time for A/B measurements. Example:

```sh
DMTCP_RESTART_PROFILE=1 DMTCP_RESTART_HUGEPAGES=1 warmctl up SNAPSHOT --mode in-place
DMTCP_RESTART_PROFILE=1 DMTCP_RESTART_MMAP=1 warmctl up SNAPSHOT --mode in-place
```

The profiling fields in `RestoreInfo` are transient. Modified `libdmtcp.so`
must match the library present at checkpoint creation: rebuild the runtime and
bake a new snapshot before testing its post-restart instrumentation. The MTCP
memory reader uses raw syscalls and stores its counters in relocated state.

`phase=memory` reports microseconds spent in the memory pass and payload reads,
anonymous mmap setup time, bytes read or file-mapped in KiB, and minor/major
faults. `pid` is the real process PID; `virtual_pid` is the checkpoint PID.
`phase=area` reports large or slow payloads, mmap eligibility, offset alignment
and mmap errors. An eligible payload with nonzero `offset_mod` falls back to
reading. `restart_barrier`,
`restart_directory`, `restart_plugins`, and `restart_maps_and_resume` are
sequential post-memory phases, identified by DMTCP's virtual PID. Barrier wait
times across processes overlap and must not be added to the critical path.

Huge-page advice is best effort and leaves zero runs unpopulated. Actual THP
availability, NUMA placement, pressure and pinning determine its effectiveness.

The mmap experiment replaces only a region created by the restore itself,
including bounded children of an anonymous parent region. It leaves stacks,
small payloads, original file-backed regions and compressed/pipe inputs on the
ordinary reader. Private mappings protect the source image against writes.
Keep the source image immutable for the entire restored process lifetime.
Do not truncate or rewrite it while mapped.

Older raw checkpoints can have unaligned payloads after a partial file-backed
mapping. The writer now saves the complete final page of regular files,
bounded by the mapping. This also fixes loss of private dirty bytes between
EOF and the end of that page. Existing readers honor the rounded
`mmapFileSize`; no format version changes. `test/restore-partial-file.c`
reproduces the old dirty-tail loss and verifies the corrected writer.
This writer correction affects newly created snapshots, not existing ones.

Mmap shifts faults to first access. Measure first useful work, CUDA restoration,
and steady-state inference, not just the memory-pass duration. CUDA host-memory
registration may force private copies and remove some or all of the benefit.
Neither experiment is qualified for GPU production solely by CPU tests.

Parallel readers can be combined with huge-page advice. Their temporary stacks
are unmapped and every helper is reaped before moving to another memory area or
resuming the application. Helpers share the address space but do not use libc,
TLS or pthreads. Failed helper creation falls back to reading its range in the
parent; failed reads fall back to the ordinary reader after all helpers exit.
Helpers arm a parent-death signal and recheck their parent, so cancellation
does not leave them holding the restore's address space.
`parallel_kib` identifies actual use. `child_minflt` and `child_majflt` are the
helpers' faults, separate from the parent's `minflt` and `majflt`.

`test/restore-memory-bench.c` checkpoints dense or sparse anonymous memory,
verifies every word after restart, and optionally writes to every page and
checkpoints/restores again. The investigation runner compares both modes on the
same images, explicitly warms their file cache, and checks source SHA-256 for
mmap runs. Its wall time includes full data verification and process exit.

During instrumentation, `dmtcp_get_restart_env()` was found to bound its scan
by `sizeof(char *)`, which examined only the first environment entry. The fix
uses the actual read length and requires an exact name followed by `=`. The
benchmark verifies a later variable whose longer prefixed name occurs first.
