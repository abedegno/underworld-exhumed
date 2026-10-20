"""Checks that the port survives a map note with no terminator in its 50 bytes (issue 7;
docs/PORT.md, "Host hazards"). UW1's LEV.ARK has no length table: get_arc (ARC.C) measures a
block to the next larger offset, so a notes block of no length that shares its offset with a
later block (the layout the original writes when every note of a level is erased and a new
level's automap is saved after it) is read as that block, 4096 bytes, 75 "notes". A run of more
than 49 bytes that are not 0 in it gives a note with no terminator, and RedisplayStrings copied
it with str_copy into its 50-byte stack buffer: DOS returns into low memory, and the port wrote
past its buffer too (on Apple silicon onto the stack canary, an abort; on x86-64 into the frame).

The archive is made here, never taken from a player's save: the game's own DATA\\LEV.ARK (read
from [replay] data, written only to a temporary home directory, which goes after the check),
with level 3's automap block appended, its first 200 bytes 0Bh, and the notes blocks of levels
1 and 2 given the same offset. A new game copies DATA\\LEV.ARK to SAVE0 (the home directory's
DATA\\LEV.ARK is the one the game sees), and an input script then creates a character, opens
the map on level 1 (Z, --enhance modern-keys), goes to level 2 (S) and back (W), and closes it.

    python3 tools/notescheck.py [--config PATH]

Passes when the port exits by itself (--exit-after), with no fault, having drawn the map three
times (DATA\\blnkmap.byt read by ShowAutoMapLevel each time, UW1PORT_TRACE's log of the opens).
Exit status 0 when every check passes."""
import os, re, sys, struct, shutil, tempfile, subprocess
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
from exhume import EXHUME
sys.path.insert(0, os.path.join(EXHUME, 'tools'))
if '--config' not in sys.argv: sys.argv[1:1] = ['--config', os.path.join(os.path.dirname(here), 'exhume.toml')]
import replay as R

RUN_MS = 30000
AUTOMAP, NOTES = 0x1A, 0x23             # level.h's LEVARK_AUTOMAP and LEVARK_NOTES, plus the level
RUN = 200                               # bytes that are not 0, from the aliased block's start

# the main menu (skip-intro), Create Character, Enter for every default, the name A, Yes; then
# the map on level 1, a level on, a level back, and the map closed
SCRIPT = """# notescheck.py: into the game, then the map's three levels
2000 key enter
""" + ''.join(f'{3500 + 1500 * i} key enter\n' for i in range(8)) + """16000 key a
17000 key enter
18500 key enter
24000 key z
26000 key s
28000 key w
30000 key esc
"""


def archive(src):
    """The game's LEV.ARK with level 3's automap appended (RUN bytes of 0Bh, then 0s) and the
    notes blocks of levels 1 and 2 at its offset, of no length."""
    d = bytearray(open(src, 'rb').read())
    n = struct.unpack_from('<H', d, 0)[0]
    offs = list(struct.unpack_from(f'<{n}I', d, 2))
    at = len(d)
    d += bytes([0x0B] * RUN) + bytes(4096 - RUN)
    offs[AUTOMAP + 3] = at
    offs[NOTES + 1] = offs[NOTES + 2] = at
    struct.pack_into(f'<{n}I', d, 2, *offs)
    return bytes(d)


def find(dirpath, name):
    for f in os.listdir(dirpath):
        if f.upper() == name.upper(): return os.path.join(dirpath, f)
    return None


def main():
    bad = []
    def check(what, ok, detail=''):
        print(('ok    ' if ok else 'FAIL  ') + what + ('' if ok or not detail else f'\n      {detail}'))
        if not ok: bad.append(what)
    work = tempfile.mkdtemp(prefix='notescheck-')
    try:
        home = os.path.join(work, 'home'); os.makedirs(os.path.join(home, 'DATA'))
        data = R.port_data(work)
        src = find(os.path.join(data, 'DATA'), 'LEV.ARK')
        if not src: sys.exit(f'notescheck: no DATA/LEV.ARK in {data}')
        open(os.path.join(home, 'DATA', 'LEV.ARK'), 'wb').write(archive(src))
        script = os.path.join(work, 'notes.script')
        open(script, 'w').write(SCRIPT)
        env = R.port_env(); env['UW1PORT_TRACE'] = '1'
        cmd = [R.port_exe(), R.RC.port_data_flag, data, R.RC.port_home_flag, home, '--hidden', '--sound', '0,0',
               '--enhance', 'skip-intro,modern-keys', '--input-script', script, '--exit-after', str(RUN_MS + 4000)]
        r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, timeout=240)
        log = r.stdout + r.stderr
        tail = '\n      '.join(l for l in log.splitlines()[-6:])
        check('the port ran the script to its end and exited by itself (status 0)', r.returncode == 0,
              f'status {r.returncode}: {tail}')
        check('no host fault', 'host fault' not in log, tail)
        shown = sum(1 for l in log.splitlines() if re.search(r'open\("DATA\\blnkmap\.byt"', l, re.I))
        check('the map was drawn three times (level 1, level 2, level 1 again)', shown >= 3, f'{shown} reads of blnkmap.byt')
        lev = find(os.path.join(home, 'SAVE0'), 'LEV.ARK') if os.path.isdir(os.path.join(home, 'SAVE0')) else None
        ok = False
        if lev:
            d = open(lev, 'rb').read()
            n = struct.unpack_from('<H', d, 0)[0]
            offs = struct.unpack_from(f'<{n}I', d, 2)
            ok = offs[NOTES + 1] != 0 and offs[NOTES + 1] == offs[NOTES + 2] == offs[AUTOMAP + 3]
        check('SAVE0\\LEV.ARK still has the notes of levels 1 and 2 aliasing level 3\'s automap', ok)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f'notescheck: {4 - len(bad)} of 4 checks pass')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
