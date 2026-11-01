# TensorRails DMTCP releases

The fork has its own tags, `v5.0.0-warmctl.N`, independently of warmctl's
CLI versions. `VERSION` remains the upstream checkpoint ABI version (5.0);
the tag and full commit in the installed `BUILDINFO` identify the fork build.
Do not change the upstream checkpoint signature just to number a fork release.

Merge a reviewed PR into `main`, with `Fixes #N` for its performance issue.
The PR workflow builds natively in pinned Debian 12 containers on x86-64 and ARM64, runs the memory-reader and
process regressions, packages an installed runtime, and tests that package.
After the checks pass, tag the merge commit and push the tag:

```sh
git tag -a v5.0.0-warmctl.1 MERGE_COMMIT -m v5.0.0-warmctl.1
git push origin v5.0.0-warmctl.1
```

The workflow reruns the checks and publishes a prerelease with source,
Debian 12 x86-64 and ARM64 runtimes, SHA-256 checksums and GitHub build provenance.
Release notes come from `doc/warmctl-release-notes.md`. Before each later
release, update those notes in the PR. Keep the historical `warmctl` branch
fast-forwarded to the released commit for consumers still using that branch.

The binary archive contains `dmtcp/`, to extract under `/opt/warmctl`.
It includes the separately licensed warmctl fdkeep plugin; cuInterpose is a
separate GPU dependency. fdkeep is built from plugin/fdkeep and tested on both
release architectures. The archive requires Debian 12-compatible glibc
(2.36 or newer) and libstdc++ on the matching architecture; it is not a
portable Linux binary. DMTCP runs on Linux, not macOS or Windows. Parallel
pread helpers currently support x86-64 only; ARM64 uses the ordinary reader
with optional huge pages and is CPU-tested, not GPU-qualified by this release.
The package tests also bake and restore from a relocated installation path.
warmctl bundles these released packages and uses the same assets in its runtime
Dockerfile, verifying source-pinned SHA-256 checksums. For an older runtime
base, build the tagged source and fdkeep inside that base instead.

```sh
sha256sum --ignore-missing -c dmtcp-v5.0.0-warmctl.1-checksums.txt
gh attestation verify dmtcp-v5.0.0-warmctl.1-linux-amd64-debian12.tar.gz \
  --repo TensorRails/dmtcp
```

Donor and receiver must use byte-identical mapped DMTCP libraries and runtime
files. A new release requires a fresh bake and the same resulting runtime
image on receivers. Never overwrite `libdmtcp.so` underneath a live engine or
an old snapshot and assume compatibility. Preserve that snapshot's old image.
