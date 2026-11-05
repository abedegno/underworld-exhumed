"""The test tiers (docs/BUILDING.md, "Testing"): `make test` and `make test-full`.

    python3 tools/test.py fast     make test: the gate (make check), the port build, the routine
                                   fuzzing's quick run, and every session replayed in the port
                                   against its golden; what the pre-push hook runs
    python3 tools/test.py full     make test-full: the gate, both port builds, the goldens made
                                   again from DOS (each session twice, checked identical), every
                                   session against them in the port and in the UBSan build, the
                                   sound drivers against the real ones (tools/ailcheck.py), the
                                   fuzzing's deep run and the coverage report (docs/COVERAGE.md)

Each step's output goes to the terminal as it runs; at the end a summary lists every step with
its time and result, and the exit status is 1 if any step failed. A step that needs an earlier
one (the replays need the port) is skipped when that one failed.
"""
import os, sys, time, subprocess

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
PY = os.path.join(root, '.venv', 'bin', 'python')
if not os.path.exists(PY): PY = sys.executable
SOUND = ['sound', 'soundfm', 'soundmt']


def tool(name, *args): return [PY, os.path.join(here, name)] + list(args)


def enhcheck():
    """Exhume's tools/enhcheck.py all (the enhancements: options, settings, recordings, the
    presentation check and the port-made baselines), on this tree through Exhume's UW2 example
    configuration."""
    sys.path.insert(0, here)
    from exhume import EXHUME
    env = dict(os.environ, UW2DECOMP=os.path.dirname(here))
    return subprocess.run([PY, os.path.join(EXHUME, 'tools', 'enhcheck.py'), '--config',
                           os.path.join(EXHUME, 'examples', 'uw2', 'exhume.toml'), 'all'], env=env).returncode


def setcheck():
    """Exhume's tools/setcheck.py all (the settings screen: F11, the pause, held keys, volume,
    the window's scale), as enhcheck is run."""
    sys.path.insert(0, here)
    from exhume import EXHUME
    env = dict(os.environ, UW2DECOMP=os.path.dirname(here))
    return subprocess.run([PY, os.path.join(EXHUME, 'tools', 'setcheck.py'), '--config',
                           os.path.join(EXHUME, 'examples', 'uw2', 'exhume.toml'), 'all'], env=env).returncode


def movecheck():
    """Exhume's tools/movecheck.py (the player moves the way the keys say in a live run, the 3D
    frames paced: issue 6), as enhcheck is run."""
    sys.path.insert(0, here)
    from exhume import EXHUME
    env = dict(os.environ, UW2DECOMP=os.path.dirname(here))
    return subprocess.run([PY, os.path.join(EXHUME, 'tools', 'movecheck.py'), '--config',
                           os.path.join(EXHUME, 'examples', 'uw2', 'exhume.toml')], env=env).returncode


def blackboxcheck():
    """Exhume's tools/blackboxcheck.py (a player's sessions each recorded to a folder of their own,
    recordings/YYYYMMDD-HHMMSS, the newest five kept, stage/ without the recordings), as enhcheck
    is run."""
    sys.path.insert(0, here)
    from exhume import EXHUME
    env = dict(os.environ, UW2DECOMP=os.path.dirname(here))
    return subprocess.run([PY, os.path.join(EXHUME, 'tools', 'blackboxcheck.py'), '--config',
                           os.path.join(EXHUME, 'examples', 'uw2', 'exhume.toml')], env=env).returncode


def modcheck():
    """The ports' mod settings (the repository's tools/modcheck.py: --start-level and
    --start-square, a new game started with them and its replay), as enhcheck is run."""
    sys.path.insert(0, here)
    from exhume import EXHUME
    env = dict(os.environ, UW2DECOMP=os.path.dirname(here))
    return subprocess.run([PY, os.path.join(os.path.dirname(root), 'tools', 'modcheck.py'), '--config',
                           os.path.join(EXHUME, 'examples', 'uw2', 'exhume.toml')], env=env).returncode


def ailcheck():
    """The port's music drivers against the real .ADV files: each sound session replayed in the
    port with its logs, then tools/ailcheck.py (replay.py's driver_check)."""
    sys.path.insert(0, here)
    import replay, golden
    bad = 0
    for s in SOUND:
        d = os.path.join(replay.OUT, 'test-full', s)
        os.makedirs(d, exist_ok=True)
        replay.run_port(golden.rec_of(s), d, ['--ail-log', os.path.join(d, 'ail.log'), '--hw-log', os.path.join(d, 'hw.log')],
                        quiet=True)
        print(f'== {s}')
        bad = max(bad, replay.driver_check(d))
    return bad


def main(argv):
    tier = argv[0] if argv else 'fast'
    if tier not in ('fast', 'full'): sys.exit('usage: tools/test.py fast|full')
    steps = [('make check', tool('uw2.py', 'check'), None),
             ('port build', tool('portbuild.py'), None)]
    if tier == 'fast':
        steps += [('fuzzing, quick', tool('fuzzasm.py'), 'port build'),
                  ('sessions against the goldens', tool('replay.py', 'verify', 'all'), 'port build'),
                  ('enhancements', enhcheck, 'port build'),
                  ('settings screen', setcheck, 'port build'),
                  ('player movement', movecheck, 'port build'),
                  ('the black box', blackboxcheck, 'port build'),
                  ('mod settings', modcheck, 'port build')]
    else:
        steps += [('port-debug build', tool('portbuild.py', '--debug'), None),
                  ('goldens from DOS, twice each', tool('replay.py', 'golden', 'all'), None),
                  ('sessions against the goldens', tool('replay.py', 'verify', 'all'), 'port build'),
                  ('sessions in the UBSan build', tool('replay.py', 'verify', 'all', '--debug'), 'port-debug build'),
                  ('sound drivers (ailcheck)', ailcheck, 'port build'),
                  ('fuzzing, deep', tool('fuzzasm.py', '--deep'), 'port build'),
                  ('enhancements', enhcheck, 'port build'),
                  ('settings screen', setcheck, 'port build'),
                  ('player movement', movecheck, 'port build'),
                  ('the black box', blackboxcheck, 'port build'),
                  ('mod settings', modcheck, 'port build'),
                  ('coverage report', tool('coverage.py'), 'port build')]
    results = []; failed = set(); t0 = time.time()
    for name, cmd, needs in steps:
        if needs in failed:
            results.append((name, None, 'skipped: ' + needs + ' failed')); failed.add(name); continue
        print(f'\n==== {name}', flush=True)
        t = time.time()
        rc = cmd() if callable(cmd) else subprocess.run(cmd, cwd=root).returncode
        results.append((name, time.time() - t, 'ok' if rc == 0 else f'FAILED (exit {rc})'))
        if rc: failed.add(name)
    if tier == 'full':
        r = subprocess.run(['git', 'status', '--porcelain', '--', 'tests/replay/golden'], cwd=root,
                           capture_output=True, text=True)
        changed = [l for l in r.stdout.splitlines() if l.strip()]
        if changed:
            print(f'\nnote: the regenerated goldens differ from the committed ones in {len(changed)} files '
                  '(git status tests/replay/golden): review them, and commit them with the change that explains them')
    print(f'\n==== make test{"-full" if tier == "full" else ""}: {time.time() - t0:.0f} s')
    for name, dt, res in results:
        took = '' if dt is None else '%6.1f s' % dt
        print(f'  {name:32s} {took:8s}  {res}')
    print('PASSED' if not failed else 'FAILED: ' + ', '.join(n for n, _, r in results if r != 'ok'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
