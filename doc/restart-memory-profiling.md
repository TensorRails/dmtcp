# Restart memory profiling experiments

These opt-in switches diagnose large CPU process images. Configure DMTCP
normally. The default reader and checkpoint format stay unchanged.

| Environment variable | Value | Effect |
| --- | --- | --- |
| `DMTCP_RESTART_PROFILE` | `1` | Log memory restoration, large/slow regions, page faults, the restart barrier and plugin phase. |
| `DMTCP_RESTART_HUGEPAGES` | `1` | Advise huge pages for eligible large private anonymous mappings before loading their contents. |
| `DMTCP_RESTART_MMAP` | `1` | Map eligible raw anonymous payloads of at least 64 MiB directly from the image using private mappings. |
| `DMTCP_RESTART_READERS` | `2`–`8`, `16` | On x86-64, copy raw regular-file payloads with parallel `pread` helpers: writable payloads of at least 1 MiB together, in batches, and any other payload of at least 1 GiB on its own. Default is one reader. |
| `DMTCP_DISABLE_ZERO_SCAN` | `1` | At checkpoint, save resident anonymous pages that hold only zeros, as before, instead of recording them as zero runs. |

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
anonymous mmap setup time, bytes read or file-mapped in KiB, minor/major
faults, and `batches`, the number of batched reads. `phase=batch` reports each
batch: its payloads (`segments`), KiB, microseconds and whether the helpers
(`pread`) or the parent alone (`read`) read it. `pid` is the real process PID; `virtual_pid` is the checkpoint PID.
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
are unmapped and every helper is reaped before the next memory area is mapped
or the application resumes. Helpers share the address space but do not use
libc, TLS or pthreads. Failed helper creation falls back to reading its range in
the parent; failed reads fall back to the ordinary reader after all helpers exit.

With readers, the payload of a writable area of at least 1 MiB is not read
when its header is: the pass records where it goes and seeks past it, and the
helpers read the recorded payloads together, every 1024 of them and at the end
of the image, each helper taking an equal share of the bytes. A zero run cuts
a large area into runs that are smaller than the 1 GiB a single payload needs
for the helpers; batched, they still share them. Read-only areas are read at
once, as they are protected right after, and so are areas under 1 MiB: putting
thousands of them off turns the pass over the headers into a seek per area,
which costs more on a cold or network file than the helpers save. The batch
array lives on the restart stack, never at an address an area could need.
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

## Resident zero pages

A process can hold gigabytes of resident zeros: a pinned host buffer sized
larger than what it holds, a pool cleared after use. The page residency scan
saves every resident page. The writer now also reads resident private
anonymous memory 1 MiB at a time and records each all-zero MiB as a zero run,
the header the residency scan already writes for absent pages. A restart maps
the area and reads only the other runs, so zeros cost neither image bytes nor
reads. Restarts that predate this read such images too: the format is the same.

The check stops at the first nonzero word of each MiB, so ordinary data costs
almost nothing; only zero MiBs are read whole, and they are no longer written.
Resident zeros are left in place (no `MADV_DONTNEED`): the process may run on
after the checkpoint, and a page pinned for DMA must keep its frame. Runs
under 1 MiB are saved as before, so a run never costs more header than data.
`DMTCP_DISABLE_ZERO_SCAN=1` restores the old images byte for byte.

`test/restore-memory-bench.c` has a `zeroed` layout, which writes every other
64 MiB stripe and then clears it. `test/restore-memory-check.py` checks that
none of those stripes reach the image, that every word comes back, and that
with readers the runs between them are still shared.
`test/restore-many-areas.c` checkpoints 2,400 areas: writable ones of at least
1 MiB between small read-only ones, so restarts batch more payloads than one
batch holds, with shares starting and ending inside areas, and read the
read-only ones at once. It verifies every byte and each protection.
