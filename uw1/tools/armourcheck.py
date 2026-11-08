"""Checks the fix armour-spells (docs/ENHANCEMENTS.md; docs/FINDINGS.md, "The armour spells add no
armour"). parse_spells (game/PLAYDATA.C) shifts the bonuses word right four times in its stealth
loop and only then adds its high nibble, the armour level player_affected_by kept there, to the
four hit locations: by then it reads the original bits 8 to 11, which nothing sets, so Resist
Blows, Thick Skin and Iron Flesh add no armour. With --enhance armour-spells the level is added.

The player's armour (Creature[63], playerdat) is not in the state dumps, and what it changes in
play is damage, which the dice decide; so the port's armour probe (UW1PORT_ARMOUR_PROBE, port
only) reports it on stderr each time FixPlayerEquips works it out: 'armour-probe: armour A B C D
spell L', the four locations and the level the spells left.

The setup session tests/replay/setup/armour-spells (port-made, with a test's cheats) writes a
saved game: a new character, wearing no armour, with a scroll of Resist Blows (level 2) and one
of Iron Flesh (level 5) in the pack. The session tests/replay/enhanced/armour-spells loads it and
reads the two scrolls one after the other. It is replayed with the probe as it was recorded (with
the fix), and again with its enhancements chunk naming skip-intro alone (the original).

    python3 tools/armourcheck.py [--config PATH]

Passes when both replays end by themselves, report the same levels (0, then 2, then 5: the best
level of the spells cast, not their sum), and the armour is 0 at every location before the first
scroll and then the level at every location with the fix, and 0 throughout without it. Exit
status 0 when every check passes."""
import os, re, sys, struct, shutil, tempfile
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
from exhume import EXHUME
sys.path.insert(0, os.path.join(EXHUME, 'tools'))
if '--config' not in sys.argv: sys.argv[1:1] = ['--config', os.path.join(os.path.dirname(here), 'exhume.toml')]
import replay as R

SETUP = os.path.join(R.RC.sessions, 'setup', 'armour-spells', 'session.rec')
SESSION = os.path.join(R.RC.sessions, 'enhanced', 'armour-spells', 'session.rec')
PROBE = 'UW1PORT_ARMOUR_PROBE'
FIX = 'armour-spells'
LEVELS = [0, 2, 5]                      # none, Resist Blows, Iron Flesh


def without_fix(data):
    """The recording with its enhancements chunk (id 9, a 16-bit length, the names separated by
    commas) naming skip-intro alone."""
    i = data.find(FIX.encode())
    j = data.rfind(b'\x09', 0, i)
    while j >= 0:
        n = struct.unpack_from('<H', data, j + 1)[0]
        if j + 3 <= i < j + 3 + n: break
        j = data.rfind(b'\x09', 0, j)
    if j < 0: return None
    names = ','.join(x for x in data[j + 3:j + 3 + n].decode().split(',') if x != FIX).encode()
    return data[:j] + b'\x09' + struct.pack('<H', len(names)) + names + data[j + 3 + n:]


def probed(log):
    """[(armour a, b, c, d), level] from the probe's lines, a repeat of the line before dropped."""
    out = []
    for m in re.finditer(r'armour-probe: armour (\d+) (\d+) (\d+) (\d+) spell (\d+)', log):
        v = ((int(m[1]), int(m[2]), int(m[3]), int(m[4])), int(m[5]))
        if not out or out[-1] != v: out.append(v)
    return out


def main():
    bad = []
    def check(what, ok, detail=''):
        print(('ok    ' if ok else 'FAIL  ') + what + ('' if ok or not detail else f'\n      {detail}'))
        if not ok: bad.append(what)
    work = tempfile.mkdtemp(prefix='armourcheck-')
    try:
        setup = os.path.join(work, 'setup')
        rc = R.run_port(SETUP, setup, [], quiet=True)
        save = os.path.join(setup, 'SAVE1')
        check('setup/armour-spells replayed, writing its saved game (slot I)', rc == 0 and os.path.isdir(save), f'status {rc}')
        if bad: return 1
        stage = os.path.join(work, 'stage')
        shutil.copytree(save, os.path.join(stage, 'SAVE1'))
        data = open(SESSION, 'rb').read()
        off = without_fix(data)
        check(f'the session carries {FIX}, and a copy without it can be made', off is not None and FIX.encode() not in off[:64])
        if bad: return 1
        offrec = os.path.join(work, 'off.rec'); open(offrec, 'wb').write(off)
        shutil.copy(os.path.splitext(SESSION)[0] + '.cfg', os.path.splitext(offrec)[0] + '.cfg')  # its settings, which replay reads from beside it
        runs = {}
        for tag, rec in (('with the fix', SESSION), ('without it', offrec)):
            out = os.path.join(work, tag.replace(' ', '-'))
            rc = R.run_port(rec, out, [], stage=stage, quiet=True, env={PROBE: '1'})
            log = open(os.path.join(out, 'port.log'), errors='replace').read()
            check(f'{tag}: replayed to its end (status 0)', rc == 0, f'status {rc}: ' + '\n      '.join(log.splitlines()[-6:]))
            runs[tag] = probed(log)
            check(f'{tag}: the levels cast, {LEVELS} (Iron Flesh\'s replaces Resist Blows\', not added to it)',
                  [lv for _, lv in runs[tag]] == LEVELS, runs[tag])
        on, off = runs['with the fix'], runs['without it']
        check('with the fix: no armour before the scrolls, then the level at every hit location',
              [a for a, _ in on] == [(lv,) * 4 for lv in LEVELS], on)
        check('without it (the original): no armour from either spell', [a for a, _ in off] == [(0,) * 4] * len(LEVELS), off)
        rc = R.run_port(SESSION, os.path.join(work, 'unprobed'), [], stage=stage, quiet=True, env={PROBE: None})
        log = open(os.path.join(work, 'unprobed', 'port.log'), errors='replace').read()
        check('the probe reports nothing when off', rc == 0 and 'armour-probe' not in log)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    n = 9
    print(f'armourcheck: {n - len(bad)} of {n} checks pass')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
