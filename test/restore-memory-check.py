#!/usr/bin/env python3
"""Real checkpoint/restore regressions for the opt-in memory readers (Linux).

Run after make, with --root pointing to the build or installed prefix.
Only this runner's coordinators/processes are stopped. Logs survive failures.
"""
import argparse
import hashlib
import os
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
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).resolve().parent
    for fixture in ('restore-memory-bench', 'restore-partial-file'):
        subprocess.run(['gcc', '-O2', '-I' + str(source.parent / 'include'),
                        str(source / (fixture + '.c')), '-o', str(output / fixture)], check=True)
    # Last entries exercise both the old scan-length bug and prefix matching.
    env = dict(os.environ)
    env.update(DMTCP_RESTART_PROFILE='1', DMTCP_RESTART_MMAP='0',
               DMTCP_RESTART_READERS='1', DMTCP_RESTART_HUGEPAGES='0',
               DMTCP_BENCH_RESTART_ENV_SUFFIX='wrong', DMTCP_BENCH_RESTART_ENV='donor')
    # 1025 MiB crosses the parallel-reader threshold with an uneven tail.
    cases = [('ordinary', 257, False, 1, False, False, False),
             ('parallel2', 1025, False, 2, False, False, False),
             ('parallel4', 1025, False, 4, True, False, False),
             ('parallel8', 1025, False, 8, True, False, False),
             ('parallel16', 1025, False, 16, True, False, False),
             ('sparse', 257, True, 16, True, False, False),
             ('mmap', 257, False, 1, False, True, False),
             ('mmap-sparse', 257, True, 1, False, True, False),
             ('gzip-fallback', 257, False, 16, True, False, True)]
    for name, mib, sparse, readers, huge, mapped, compressed in cases:
        case = output / name
        case.mkdir()
        first, second, tmp = [case / n for n in ('first', 'second', 'tmp')]
        for d in (first, second, tmp):
            d.mkdir()
        env.update(DMTCP_TMPDIR=str(tmp), DMTCP_BENCH_SECOND_DIR=str(second),
                   DMTCP_BENCH_RESTART_ENV='donor', DMTCP_GZIP='1' if compressed else '0')
        run(root, case / 'checkpoint.log', ['dmtcp_launch',
            *([] if compressed else ['--no-gzip']), '--ckptdir', str(first),
            str(output / 'restore-memory-bench'), str(mib),
            'sparse' if sparse else 'dense', 'cow'], env)
        files = images(first)
        before = digests(files)
        env.update(DMTCP_BENCH_RESTART_ENV='receiver', DMTCP_RESTART_READERS=str(readers),
                   DMTCP_RESTART_HUGEPAGES=str(int(huge)), DMTCP_RESTART_MMAP=str(int(mapped)))
        text = run(root, case / 'restart.log', ['dmtcp_restart', *files], env)
        assert 'BENCH_VERIFIED' in text, text[-6000:]
        field = 'filemap_kib' if mapped else 'parallel_kib'
        values = list(map(int, re.findall(r'\b' + field + r'=(\d+)', text)))
        if mapped or (readers > 1 and mib >= 1024 and not sparse and not compressed):
            assert any(values), f'{name}: optimized reader was not exercised'
        if compressed:
            assert values and not any(values), 'compressed stream must use ordinary reader'
        text = run(root, case / 'second-restart.log', ['dmtcp_restart', *images(second)], env)
        assert 'BENCH_SECOND_VERIFIED' in text, text[-6000:]
        assert before == digests(files), 'source image changed'
        for d in (first, second, tmp):
            shutil.rmtree(d)
        print(name + ': PASS (all words, dirty pages, second checkpoint, source SHA-256)', flush=True)
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


if __name__ == '__main__':
    main()
