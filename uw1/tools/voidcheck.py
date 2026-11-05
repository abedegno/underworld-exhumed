"""Checks that the port survives the arrival in the Ethereal Void, level 9 (docs/PORT.md, "Another
null pointer, on level 9"; docs/FINDINGS.md, "The Slasher of Veils in the void follows no one").
The Slasher of Veils there has goal 3 (follow) and goal target 0, so AI.C's set_up_target reads
its target through a null far pointer as soon as the player arrives; DOS reads the interrupt
vector table, and the port stopped with a host fault until it read its copy of the table
(FARNULLTRAP).

The way in is the game's own: the setup session tests/replay/setup/talisman (port-made, with a
test's cheats) writes a saved game on level 8's central walkway with one talisman left to destroy,
and the session tests/replay/void (port-made from its script, no cheats) loads it, throws the
taper into the lava, and goes through check_victory's moongate to level 9 (27,23). Both are
replayed in the port, the setup first for its saved game.

The session is not one of the DOS goldens ([replay] order): DOS replays it to the same end, but
the Slasher's target height comes from int 0's segment, the program's load segment plus 1DEAh,
which differs between js-dos, DOSBox-X and the port, and a periodic checkpoint taken while the
Slasher climbs to it differs. What does not depend on it is checked here against what DOS's
replay showed (js-dos, the end checkpoint identical to the port's).

    python3 tools/voidcheck.py [--config PATH] [--debug]

--debug replays in the UBSan build. Passes when the replay ends by itself with no fault, the
null pointer was met at set_up_target (the port's log), the last talisman is gone (the player
record's talismans FFh, which check_victory sets after the move), and the Slasher (mobile 184,
item 7Ch) went as it did in DOS: a jump to (57,2) with the goal and target the level gives it,
then, unable to reach (60,0), it wanders, stands, and check_out_player gives it the player as its
target, whom it follows back to (28,22). Exit status 0 when every check passes."""
import os, re, sys, struct, shutil, tempfile
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
from exhume import EXHUME
sys.path.insert(0, os.path.join(EXHUME, 'tools'))
if '--config' not in sys.argv: sys.argv[1:1] = ['--config', os.path.join(os.path.dirname(here), 'exhume.toml')]
import replay as R

SETUP = os.path.join(R.RC.sessions, 'setup', 'talisman', 'session.rec')
VOID = os.path.join(R.RC.sessions, 'void', 'session.rec')
TALISMANS = 0x6D                        # player.h's struct Player
MOBILES, MOBILE_SIZE = 0x4000, 27       # level.h: the mobile objects after the 64 x 64 tiles
SLASHER, SLASHER_ITEM = 184, 0x7C
JUMP, BACK = (57, 2), (28, 22)          # the Slasher's tiles in DOS's replay of the session


def main():
    debug = '--debug' in sys.argv
    extra = ['--debug'] if debug else []
    bad = []
    def check(what, ok, detail=''):
        print(('ok    ' if ok else 'FAIL  ') + what + ('' if ok or not detail else f'\n      {detail}'))
        if not ok: bad.append(what)
    work = tempfile.mkdtemp(prefix='voidcheck-')
    try:
        setup = os.path.join(work, 'setup')
        rc = R.run_port(SETUP, setup, extra, quiet=True)
        save = os.path.join(setup, 'SAVE1')
        check('setup/talisman replayed, writing its saved game (slot I)', rc == 0 and os.path.isdir(save), f'status {rc}')
        if bad: return 1
        stage = os.path.join(work, 'stage')
        shutil.copytree(save, os.path.join(stage, 'SAVE1'))
        out = os.path.join(work, 'void')
        rc = R.run_port(VOID, out, extra, stage=stage, quiet=True)
        log = open(os.path.join(out, 'port.log'), errors='replace').read()
        tail = '\n      '.join(log.splitlines()[-6:])
        check('void replayed to its end (status 0)', rc == 0, f'status {rc}: {tail}')
        check('no host fault', 'host fault' not in log, tail)
        if debug:
            ub = sorted(set(l for l in log.splitlines() if 'runtime error' in l))
            check('UBSan reported nothing', not ub, '\n      '.join(ub[:10]))
        met = [l for l in log.splitlines() if re.search(r'far null pointer read at .*AI\.C:\d+', l, re.I)]
        check('the null pointer met at AI.C\'s set_up_target (the port\'s copy of the vector table read)', bool(met),
              f'{len(met)} reads logged')
        dump = R.read_dump(os.path.join(out, 'STATE.OUT')) if os.path.exists(os.path.join(out, 'STATE.OUT')) else []
        last = dump[-1]['secs'] if dump else {}
        plyr, levl = last.get('PLYR', b''), last.get('LEVL', b'')
        check('the last talisman gone and check_victory\'s move made (talismans FFh)',
              len(plyr) > TALISMANS and plyr[TALISMANS] == 0xFF,
              f'talismans {plyr[TALISMANS]:02X}h' if len(plyr) > TALISMANS else 'no player record in the last dump')
        def slasher(secs):
            """Mobile 184's item, goal, goal target and tile in a dump's level, or None."""
            levl = secs.get('LEVL', b'')
            at = MOBILES + SLASHER * MOBILE_SIZE
            if len(levl) < at + MOBILE_SIZE: return None
            m = levl[at:at + MOBILE_SIZE]
            goal_word, home = struct.unpack_from('<H', m, 0x0B)[0], struct.unpack_from('<H', m, 0x16)[0]
            return (struct.unpack_from('<H', m, 0)[0] & 0x1FF, goal_word & 0xF, (goal_word >> 4) & 0xFF,
                    (home >> 10, (home >> 4) & 0x3F))
        seen = [slasher(ck['secs']) for ck in dump]
        # the jump with target 0 and the stand that makes the player its target come within a
        # few frames, between two checkpoints: the first after the arrival finds it at (57,2)
        # with the player as its target already, as DOS's does
        at = [i for i, x in enumerate(seen) if x == (SLASHER_ITEM, 3, 1, JUMP)]
        check(f'on level 9, the Slasher (mobile {SLASHER}) jumped to {JUMP}, as in DOS', bool(at),
              f'mobile {SLASHER} at the checkpoints: {sorted(set(x for x in seen if x))}')
        check(f'then back beside the player at {BACK}, following the player (target 1), as at the end of DOS\'s replay',
              bool(at) and seen[-1] == (SLASHER_ITEM, 3, 1, BACK), f'at the end: {seen[-1] if seen else None}')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    n = 8 if debug else 7
    print(f'voidcheck: {n - len(bad)} of {n} checks pass')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
