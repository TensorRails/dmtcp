# warmctl fdkeep plugin

Imported from `TensorRails/warmctl` at `7e082c0`, under its Apache-2.0
license (LICENSE). This fork now owns and versions the plugin together with
the DMTCP runtime, so warmctl consumes a complete released native package.

The plugin preserves driver eventfds, real pipes, thread names, Unix listeners
and epoll watches missed by DMTCP wrappers. It is loaded explicitly, never
enabled for arbitrary DMTCP applications by default.

```sh
make -C plugin/fdkeep
make -C plugin/fdkeep install PREFIX=/opt/warmctl/dmtcp
```

Linux x86-64 and ARM64 use native raw syscalls. CI verifies an untracked
eventfd, pipe contents, Unix listener and epoll watch through checkpoint and
restore. H200 GPU evidence is x86-64 only; ARM64 GPU support, including
cuInterpose, is not qualified by the CPU package tests.
