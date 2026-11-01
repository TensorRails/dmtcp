# TensorRails DMTCP releases

The fork has its own tags, `v5.0.0-warmctl.N`, independently of warmctl's
CLI versions. `VERSION` remains the upstream checkpoint ABI version (5.0);
the tag and full commit in the installed `BUILDINFO` identify the fork build.
Do not change the upstream checkpoint signature just to number a fork release.

Merge a reviewed PR into `main`, with `Fixes #N` for its performance issue.
The PR workflow builds on Ubuntu 24.04 x86-64, runs the memory-reader and
process regressions, packages an installed runtime, and tests that package.
After the checks pass, tag the merge commit and push the tag:

```sh
git tag -a v5.0.0-warmctl.1 MERGE_COMMIT -m v5.0.0-warmctl.1
git push origin v5.0.0-warmctl.1
```

The workflow reruns the checks and publishes a prerelease with source,
an Ubuntu 24.04 x86-64 runtime, SHA-256 checksums and GitHub build provenance.
Release notes come from `doc/warmctl-release-notes.md`. Before each later
release, update those notes in the PR. Keep the historical `warmctl` branch
fast-forwarded to the released commit for consumers still using that branch.

The binary archive contains `dmtcp/`, to extract under `/opt/warmctl`.
It does not include warmctl's fdkeep plugin or cuInterpose. Build fdkeep from
the warmctl revision paired with the runtime. The archive requires Ubuntu
24.04-compatible glibc and libstdc++; it is not a portable Linux binary.
For other runtime bases, build the tagged source inside that base as
warmctl's runtime Dockerfile does, pinning the full commit.

```sh
sha256sum --ignore-missing -c dmtcp-v5.0.0-warmctl.1-checksums.txt
gh attestation verify dmtcp-v5.0.0-warmctl.1-linux-amd64-ubuntu24.04.tar.gz \
  --repo TensorRails/dmtcp
```

Donor and receiver must use byte-identical mapped DMTCP libraries and runtime
files. A new release requires a fresh bake and the same resulting runtime
image on receivers. Never overwrite `libdmtcp.so` underneath a live engine or
an old snapshot and assume compatibility. Preserve that snapshot's old image.
