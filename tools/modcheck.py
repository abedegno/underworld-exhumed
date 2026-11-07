"""The ports' mod settings (docs/MODDING.md): --start-level and --start-square, in either game.

    python3 tools/modcheck.py --config PATH

PATH is the game's configuration for Exhume's tools, as make test gives it (uw1/exhume.toml, or
Exhume's examples/uw2/exhume.toml with UW2DECOMP set); the port must be built. It checks:

  - --features lists start-square and start-level with this game's levels, and the fixes among
    the enhancements (plain-debris, a fix in both games);
  - a start level outside them, or not a number, stops the start with a message;
  - a start level that LEV.ARK has no map for stops it too: a LEV.ARK in the home folder's DATA,
    read in place of the game's as a mod's is, with one level's block taken out;
  - --replay takes no mods from the command line;
  - a new game played with a start level and square (the newgame session's keys, at the times
    the setup sessions use) starts on that level and on the nearest square the player fits on,
    which the port's log says ("mods: a new game starts on level L, square (X, Y)") and its
    position at the end shows (UWnPORT_POS_LOG); the recording keeps them in MODS.TXT. The square
    is the same every run (MAINMENU.C's port_start_fit, not find_good_x_and_y, whose half-cleared
    visited table made it hang on the stack's leftovers);
  - that recording replayed from an empty home starts in the same place again, from MODS.TXT.

Each game's square is one the player does not fit on (UW1's level 2 (30, 30), UW2's level 10
(29, 29), a diagonal wall), next to one it does, so the nearest-square search is checked as
well. Exit status 1 if anything differs.
"""
import os, re, sys, shutil, struct, subprocess, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(here), 'exhume', 'tools'))
import replay as R                      # takes --config from the command line (Exhume's portcfg)

NAME = R.RC.port_name                   # uw1port or uw2port
GAME = {
    'uw1port': dict(levels=9, offsets=2, level=2, square=(30, 30), placed=(31, 30), motion=(8064, 7808),
                    # title, Escape twice, Create Character, every default, the name, Yes
                    keys=[(9000, 'esc'), (12000, 'esc'), (15000, 'enter')] + [(18000 + 2000 * i, 'enter') for i in range(8)]
                    + [(34000 + 200 * i, c) for i, c in enumerate('avatar')] + [(36000, 'enter'), (38000, 'enter')],
                    play=42000),
    'uw2port': dict(levels=80, offsets=6, level=10, square=(29, 29), placed=(30, 29), motion=(7808, 7552),
                    keys=[(14000, 'esc'), (16500, 'esc'), (20000, 'enter')] + [(24000 + 3000 * i, 'enter') for i in range(8)]
                    + [(48000 + 200 * i, c) for i, c in enumerate('avatar')] + [(50000, 'enter'), (53000, 'enter')],
                    play=58000),
}[NAME]
LEVELS = GAME['levels']
failures = []


def check(ok, what):
    print(('  ok    ' if ok else '  FAIL  ') + what, flush=True)
    if not ok: failures.append(what)


def run(home, *args, data=None, timeout=120):
    env = R.port_env()
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    env[NAME[:3].upper() + 'PORT_POS_LOG'] = '1'
    os.makedirs(home, exist_ok=True)
    cmd = [R.port_exe(), R.RC.port_data_flag, data or DATA, R.RC.port_home_flag, home,
           '--hidden', '--no-recording', '--sound', '0,0'] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, timeout=timeout)
    return r.returncode, r.stdout + r.stderr


def mods_lines(log):
    return [l.split(': mods: ', 1)[1] for l in log.splitlines() if ': mods: ' in l]


def motion(log):
    m = re.search(r': motion: x (\d+) y (\d+)', log)
    return (int(m.group(1)), int(m.group(2))) if m else None


work = tempfile.mkdtemp(prefix='modcheck-')
DATA = R.port_data(work)
try:
    print(f'{NAME}: the mod settings (docs/MODDING.md)')
    rc, out = run(os.path.join(work, 'features'), '--features')
    lines = out.splitlines()
    check(rc == 0 and 'start-square' in lines and f'start-level 1-{LEVELS}' in lines,
          f'--features lists start-square and start-level 1-{LEVELS}')
    enh = next((l.split(' ', 1)[1].split(',') for l in lines if l.startswith('enhance ')), [])
    check('plain-debris' in enh, 'the enhance line of --features lists the fixes: ' + ','.join(enh))

    for bad in ('0', str(LEVELS + 1), '3x'):
        rc, out = run(os.path.join(work, 'bad'), '--start-level', bad, '--exit-after', '100')
        check(rc != 0 and f'the start level must be 1 to {LEVELS}, or off, not "{bad}"' in out,
              f'--start-level {bad} is refused')

    # a mod's LEV.ARK, in the home folder's DATA, without level 5's block
    home = os.path.join(work, 'nolevel')
    os.makedirs(os.path.join(home, 'DATA'))
    src = next(os.path.join(DATA, d, f) for d in os.listdir(DATA) if d.upper() == 'DATA'
               for f in os.listdir(os.path.join(DATA, d)) if f.upper() == 'LEV.ARK')
    ark = bytearray(open(src, 'rb').read())
    struct.pack_into('<I', ark, GAME['offsets'] + 4 * 4, 0)
    open(os.path.join(home, 'DATA', 'LEV.ARK'), 'wb').write(ark)
    rc, out = run(home, '--start-level', '5', '--exit-after', '100')
    check(rc != 0 and 'level 5 has no map in' in out and 'cannot start there' in out,
          "a start level the home folder's LEV.ARK has no map for is refused")

    rc, out = run(os.path.join(work, 'replayargs'), '--replay', os.path.join(work, 'none.rec'), '--start-level', '2')
    check(rc != 0 and "a replay takes its recording's mods" in out, '--replay with --start-level is refused')

    # a new game with a start level and square, recorded
    home = os.path.join(work, 'newgame')
    os.makedirs(home)
    script = os.path.join(work, 'newgame.script')
    open(script, 'w').write(''.join(f'{t} key {k}\n' for t, k in GAME['keys']))
    level, (x, y), (px, py) = GAME['level'], GAME['square'], GAME['placed']
    rc, out = run(home, '--record', '--input-script', script, '--exit-after', str(GAME['play']),
                  '--start-level', str(level), '--start-square', f'{x},{y}', timeout=300)
    want = [f'start level {level}; start square ({x}, {y}) (not as DOS)',
            f'square ({x}, {y}) has no room for the player, so the nearest that has',
            f'a new game starts on level {level}, square ({px}, {py})']
    got = mods_lines(out)
    check(got == want, f'a new game with --start-level {level} --start-square {x},{y} starts on level {level}, square ({px}, {py})'
          + ('' if got == want else f': the log says {got}'))
    check(motion(out) == GAME['motion'], f'the player stands in the middle of ({px}, {py}) at the end: {motion(out)}')
    mods = os.path.join(home, 'MODS.TXT')
    kept = [l for l in open(mods).read().splitlines() if not l.startswith('#')] if os.path.exists(mods) else None
    check(kept == [f'start-level {level}', f'start-square {x},{y}'], f'MODS.TXT keeps them: {kept}')

    # replayed from an empty home: the mods from MODS.TXT beside the recording
    rec = os.path.join(home, 'RECORD.OUT')
    if os.path.exists(rec):
        rc, out = run(os.path.join(work, 'replay'), '--replay', rec, '--exit-on-halt', timeout=300)
        got = mods_lines(out)
        check(got == want and motion(out) == GAME['motion'], 'its replay starts in the same place, from MODS.TXT'
              + ('' if got == want else f': the log says {got}'))
    else:
        check(False, 'the new game left a recording')
finally:
    shutil.rmtree(work, ignore_errors=True)

print(f'{NAME}: ' + ('mods all ok' if not failures else f'{len(failures)} failed'))
sys.exit(1 if failures else 0)
