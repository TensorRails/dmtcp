#!/usr/bin/env python3
"""Real checkpoint/restore regressions for the opt-in memory readers (Linux).

Run after make, with --root pointing to the build or installed prefix.
Only this runner's coordinators/processes are stopped. Logs survive failures.
"""
import argparse
import gzip
import hashlib
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import time


def run(root, output, command, env):
    port_file = output.with_suffix('.port')
    port_file.unlink(missing_ok=True)
    port = None
    with output.with_suffix('.coord.log').open('w') as log:
        coord = subprocess.Popen(
            [str(root / 'bin/dmtcp_coordinator'), '--port', '0', '--port-file',
             str(port_file), '--exit-on-last'], stdout=log, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, start_new_session=True, env=env)
        try:
            deadline = time.monotonic() + 15
            while not port_file.exists():
                if coord.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Coordinator failed: ' + str(output))
                time.sleep(.02)
            port = port_file.read_text().strip()
            with output.open('w') as child_log:
                result = subprocess.run(
                    [str(root / 'bin' / command[0]), '--join-coordinator',
                     '--coord-port', port, *command[1:]],
                    stdout=child_log, stderr=subprocess.STDOUT, env=env,
                    stdin=subprocess.DEVNULL, start_new_session=True, timeout=180)
            text = output.read_text()
            if result.returncode:
                raise RuntimeError(f'{output}: exit {result.returncode}\n{text[-6000:]}')
            return text
        finally:
            if coord.poll() is None and port:
                subprocess.run([str(root / 'bin/dmtcp_command'), '--coord-port',
                                port, '--quit'], env=env, timeout=10,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                coord.wait(timeout=10)
            except subprocess.TimeoutExpired:
                coord.kill()
                coord.wait()


def images(directory):
    result = sorted(directory.glob('ckpt_*.dmtcp'))
    if not result:
        raise RuntimeError('No images in ' + str(directory))
    return list(map(str, result))


def digests(files):
    result = []
    for name in files:
        h = hashlib.sha256()
        with open(name, 'rb') as f:
            for data in iter(lambda: f.read(8 * 1024 * 1024), b''):
                h.update(data)
        result.append(h.hexdigest())
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plugin', type=Path)
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).resolve().parent
    for fixture in ('restore-memory-bench', 'restore-partial-file', 'restore-fdkeep',
                    'restore-many-areas'):
        subprocess.run(['gcc', '-O2', '-I' + str(source.parent / 'include'),
                        str(source / (fixture + '.c')), '-o', str(output / fixture)], check=True)
    # Last entries exercise both the old scan-length bug and prefix matching.
    env = dict(os.environ)
    if args.plugin:
        env['DMTCP_PLUGIN'] = str(args.plugin.resolve())
    env.update(DMTCP_RESTART_PROFILE='1', DMTCP_RESTART_MMAP='0',
               DMTCP_RESTART_READERS='1', DMTCP_RESTART_HUGEPAGES='0',
               DMTCP_BENCH_RESTART_ENV_SUFFIX='wrong', DMTCP_BENCH_RESTART_ENV='donor')
    # 1025 MiB crosses the parallel-reader threshold with an uneven tail.
    # zeroed: resident stripes of zeros, which the image keeps as zero runs.
    cases = [('ordinary', 257, 'dense', 1, False, False, False),
             ('parallel2', 1025, 'dense', 2, False, False, False),
             ('parallel4', 1025, 'dense', 4, True, False, False),
             ('parallel8', 1025, 'dense', 8, True, False, False),
             ('parallel16', 1025, 'dense', 16, True, False, False),
             ('sparse', 257, 'sparse', 16, True, False, False),
             ('zeroed', 257, 'zeroed', 1, False, False, False),
             ('zeroed16', 1025, 'zeroed', 16, True, False, False),
             ('mmap', 257, 'dense', 1, False, True, False),
             ('mmap-sparse', 257, 'sparse', 1, False, True, False),
             ('mmap-zeroed', 257, 'zeroed', 1, False, True, False),
             ('gzip-fallback', 1025, 'dense', 16, True, True, True)]
    stored = {}
    for name, mib, layout, readers, huge, mapped, compressed in cases:
        case = output / name
        case.mkdir()
        first, second, tmp = [case / n for n in ('first', 'second', 'tmp')]
        for d in (first, second, tmp):
            d.mkdir()
        env.update(DMTCP_TMPDIR=str(tmp), DMTCP_BENCH_SECOND_DIR=str(second),
                   DMTCP_BENCH_RESTART_ENV='donor', DMTCP_GZIP='0')
        run(root, case / 'checkpoint.log', ['dmtcp_launch', '--no-gzip', '--ckptdir', str(first),
            str(output / 'restore-memory-bench'), str(mib), layout, 'cow'], env)
        files = images(first)
        stored[name] = sum(os.path.getsize(f) for f in files)
        if layout == 'zeroed':
            # The bench clears every odd 64 MiB stripe; none of it may be saved.
            # Beyond the payload, the image holds what the ordinary one does.
            other = stored['ordinary'] - 257 * 2**20
            zero = sum(min(64, mib - s) for s in range(64, mib, 128)) * 2**20
            limit = mib * 2**20 - zero + other + 2**20
            assert stored[name] <= limit, \
                f'{name}: {stored[name]} bytes stored, at most {limit} expected'
        if compressed:
            # ARM64's launcher deliberately disables gzip at checkpoint due to
            # an upstream SIGRETURN limitation. Compress our owned raw fixture
            # externally to test the restart pipe/fallback on both platforms.
            for image in files:
                zipped = image + '.gz.tmp'
                with open(image, 'rb') as src, gzip.open(zipped, 'wb', compresslevel=1) as dst:
                    shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                os.replace(zipped, image)
                with open(image, 'rb') as f:
                    assert f.read(2) == b'\x1f\x8b', 'gzip case must use a compressed image'
        before = digests(files)
        env.update(DMTCP_BENCH_RESTART_ENV='receiver', DMTCP_RESTART_READERS=str(readers),
                   DMTCP_RESTART_HUGEPAGES=str(int(huge)), DMTCP_RESTART_MMAP=str(int(mapped)))
        text = run(root, case / 'restart.log', ['dmtcp_restart', *files], env)
        assert 'BENCH_VERIFIED' in text, text[-6000:]
        if (layout == 'zeroed' and readers > 1 and mib >= 1024 and not mapped
                and platform.machine() == 'x86_64'):
            # Zero runs cut the area into 64 MiB runs: read together, not one by one.
            assert any(map(int, re.findall(r'\bparallel_kib=(\d+)', text))), \
                f'{name}: the readers did not share the runs between zero runs'
        field = 'filemap_kib' if mapped else 'parallel_kib'
        values = list(map(int, re.findall(r'\b' + field + r'=(\d+)', text)))
        parallel = platform.machine() == 'x86_64' and readers > 1 and mib >= 1024 and layout == 'dense'
        if not compressed and (mapped or parallel):
            assert any(values), f'{name}: optimized reader was not exercised'
        if platform.machine() != 'x86_64' and not mapped:
            assert values and not any(values), 'unsupported architecture must use ordinary reader'
        if compressed:
            values = list(map(int, re.findall(r'\b(?:filemap_kib|parallel_kib)=(\d+)', text)))
            assert values and not any(values), 'compressed stream must use ordinary reader'
        text = run(root, case / 'second-restart.log', ['dmtcp_restart', *images(second)], env)
        assert 'BENCH_SECOND_VERIFIED' in text, text[-6000:]
        assert before == digests(files), 'source image changed'
        for d in (first, second, tmp):
            shutil.rmtree(d)
        print(name + ': PASS (all words, dirty pages, second checkpoint, source SHA-256)', flush=True)
    # More writable areas than restart defers at once, between read-only ones.
    for readers in (1, 16):
        case = output / f'many-areas-{readers}'
        case.mkdir()
        env.update(DMTCP_TMPDIR=str(case), DMTCP_RESTART_MMAP='0', DMTCP_GZIP='0',
                   DMTCP_RESTART_READERS=str(readers), DMTCP_RESTART_HUGEPAGES='0')
        run(root, case / 'checkpoint.log', ['dmtcp_launch', '--no-gzip', '--ckptdir',
            str(case), str(output / 'restore-many-areas')], env)
        text = run(root, case / 'restart.log', ['dmtcp_restart', *images(case)], env)
        assert 'MANY_VERIFIED' in text, text[-6000:]
        batches = sum(map(int, re.findall(r'\bbatches=(\d+)', text)))
        parallel = sum(map(int, re.findall(r'\bparallel_kib=(\d+)', text)))
        if readers == 1:
            assert batches == 0 and parallel == 0, f'one reader deferred reads: {text[-3000:]}'
        elif platform.machine() == 'x86_64':
            assert batches >= 2 and parallel > 0, f'deferred reads were not shared: {text[-3000:]}'
        for f in case.glob('ckpt_*.dmtcp'):
            f.unlink()
        print(f'many-areas-{readers}: PASS (every byte, protections, {batches} batches)', flush=True)
    case = output / 'partial-file'
    case.mkdir()
    env.update(DMTCP_TMPDIR=str(case), DMTCP_RESTART_MMAP='0', DMTCP_GZIP='0')
    run(root, case / 'checkpoint.log', ['dmtcp_launch', '--no-gzip', '--ckptdir',
        str(case), str(output / 'restore-partial-file')], env)
    text = run(root, case / 'restart.log', ['dmtcp_restart', *images(case)], env)
    assert 'PARTIAL_FILE_VERIFIED' in text, text[-6000:]
    for f in case.glob('ckpt_*.dmtcp'):
        f.unlink()
    print('partial-file: PASS (private dirty bytes past EOF)', flush=True)
    if args.plugin:
        case = output / 'fdkeep'
        case.mkdir()
        env.update(DMTCP_TMPDIR=str(case), DMTCP_RESTART_MMAP='0', DMTCP_GZIP='0')
        run(root, case / 'checkpoint.log', ['dmtcp_launch', '--no-gzip', '--ckptdir',
            str(case), str(output / 'restore-fdkeep')], env)
        text = run(root, case / 'restart.log', ['dmtcp_restart', *images(case)], env)
        assert 'FDKEEP_VERIFIED' in text, text[-6000:]
        for f in case.glob('ckpt_*.dmtcp'):
            f.unlink()
        print('fdkeep: PASS (eventfd, pipe bytes, Unix listener, epoll watch)', flush=True)


if __name__ == '__main__':
    main()
