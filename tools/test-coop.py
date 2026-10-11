"""Run native co-op transport tests without game files, SDL or Borland tools."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--adapter', action='store_true', help='also test both game adapters (needs the Exhume submodule)')
    args = parser.parse_args()
    out = ROOT / 'build' / 'coop-tests'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test-coop'
    flags = ['-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror', '-g']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(shlex.split(args.cc) + flags + ['coop/protocol.c', 'coop/session.c',
                   'tests/coop/test_coop.c', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)
    if args.adapter:
        runtime = ROOT / 'exhume' / 'runtime' / 'port'
        for game in (1, 2):
            binary = out / f'test-uw{game}-adapter'
            includes = ['-I', str(ROOT / f'uw{game}' / 'src' / 'port'), '-I', str(runtime),
                        '-I', str(runtime / 'platform'), '-I', str(runtime / 'include'),
                        '-iquote', str(ROOT / f'uw{game}' / 'src' / 'include'),
                        '-iquote', str(ROOT / 'exhume' / 'runtime' / 'include')]
            subprocess.run(shlex.split(args.cc) + flags + ['-Wno-unknown-pragmas', '-Wno-comment', '-Wno-pragma-pack', '-pthread',
                           f'-DCOOP_GAME={game}'] + includes + ['coop/protocol.c', 'coop/session.c',
                           'tests/coop/test_adapter.c', '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)


if __name__ == '__main__':
    main()
