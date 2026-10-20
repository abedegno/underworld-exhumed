r"""Record a session in DOS and replay it in DOS and in the port, comparing the state dumps
(docs/PORT.md, "The differential test"; Exhume's runtime/replay/replay.c has the formats).

    python3 tools/replay.py build                       the replay DOS build: build/replay/UW2.EXE
    python3 tools/replay.py record OUT [step ...]       record a session in DOS (steps: tools/replaydos.mjs)
    python3 tools/replay.py record OUT --session NAME   ... with a session named below (SESSIONS)
    python3 tools/replay.py dos REC OUT                 replay REC (a RECORD.OUT) in DOS
    python3 tools/replay.py port REC OUT [--debug]      replay REC in the port (build/port/uw2port, or
                                                        make port-debug's UBSan build)
    python3 tools/replay.py compare A B [--same-build]  compare two STATE.OUT files (or their directories)
    python3 tools/replay.py log REC                     what a recording holds
    python3 tools/replay.py show STATE [PNGDIR]         list a dump's checkpoints; write each full one's screen as a PNG
    python3 tools/replay.py nulls DIR                   the null-pointer write check of a DOS run's directory
    python3 tools/replay.py saves A B                   compare the saved games (SAVE1..SAVE4) two runs made
    ... --stage DIR                                     with DIR's files (a saved game, SAVE1/) put into the
                                                        game's directory first, in DOS and in the port
    python3 tools/replay.py check REC OUT               replay REC in DOS twice and in the port, compare all
                                                        (with a sound card, the port's sound drivers too)
    python3 tools/replay.py golden [SESSION ...|all] [-j N] [--backend B] [--check]
                                                        make the sessions' golden references from DOS
                                                        (twice, checked identical): tests/replay/golden/
    python3 tools/replay.py verify [SESSION ...|all] [-j N] [--debug]
                                                        replay the sessions in the port only, against
                                                        the goldens (tools/golden.py has both)

The replay DOS build is the modding build with every source that uses the hooks of
portable.h compiled with -DREPLAY (and every source with a NULLTRAP mark with
-DNULLTRAP, so the marked null pointers it reaches go to NULLTRAP.LOG), and Exhume's runtime/replay/replay.c
linked in as one more resident module (tools/link.py --mod --add). Recording runs in js-dos
(tools/replaydos.mjs), the DOS replays in DOSBox-X when it is installed and otherwise js-dos
(replaydos.mjs --backend, or UW2_REPLAY_DOS); the port replays with --replay.

A session with a sound card has a sound configuration, DATA\UW.CFG's two lines (CFGS below):
recording writes it to OUT/UW.CFG beside OUT/RECORD.OUT, and a replay of REC uses the .cfg
file beside it (tests/replay/sound.cfg for tests/replay/sound.rec) or the UW.CFG in its
directory, in DOS (replaydos.mjs --cfg) and in the port (its home directory's DATA\UW.CFG).

compare walks two dumps checkpoint by checkpoint. Each checkpoint names the event it was taken
at (the count of hook calls) and the clock, so a checkpoint whose header differs means the two
runs went different ways before it, and the comparison stops there. Within a checkpoint each
section is compared byte for byte, the screen also as the CRT controller would show it.
Sections in IGNORE are not compared between DOS and the port (their bytes are the program's
own machinery, not game state); --same-build compares everything, for two runs of one build.

Everything goes under build/replay unless an OUT says otherwise. The game data comes from
UW2_DIR, else the directory of UW2_EXE, else ~/UWGOG/UW2.
"""
import os, sys, re, json, struct, zlib, hashlib, shutil, subprocess

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
sys.path.insert(0, here)
OUT = os.path.join(root, 'build', 'replay')
PY = os.path.join(root, '.venv', 'bin', 'python')
if not os.path.exists(PY): PY = sys.executable
DATA = os.environ.get('UW2_DIR') or (os.path.dirname(os.environ['UW2_EXE']) if os.environ.get('UW2_EXE')
                                     else os.path.expanduser('~/UWGOG/UW2'))
HOOKS = re.compile(r'\b(GAME_TIME|KEY|MOUSE|MBUTTONS|JOY_READ|JOY_BUTTONS|WALL_TIME|SRAND|CHECKPOINT|STACK_JUNK|SND_READ|SLAVE_TIMER)\(')
KINDS = {1: 'checkpoint', 2: 'periodic', 3: 'input', 4: 'end', 5: 'DESYNC', 6: 'hook'}
STREAMS = {1: 'TIME', 2: 'KEY', 3: 'MOUSE', 4: 'BUTTONS', 5: 'JOY', 6: 'JOYB', 7: 'MISC', 8: 'SOUND'}
# Byte ranges of a section that are the program's machinery rather than game state:
# (section, start, end, why). ALWAYS differ between any two runs, even of one build; CROSS
# between DOS and the port.
ALWAYS = [
    ('GFX ', 0x4E00, 0x4FA8, "seg003's private stack (GRCORE.ASM switches to 370D:4FA8): "
                             "interrupts push onto it whenever they come"),
    ('GFX ', 0x5480, 0x5548, "the stack GRENTRY.ASM and GRDISP.ASM switch to (SP 5548h, saved at 5588h); "
                             "with a sound card the Sound Blaster's interrupt pushes onto it too, "
                             "down to 54E9h (the bytes below are never written)"),
    ('GFX ', 0x652, 0x654, "GRMISC.ASM's _46: the low word of the real clock after each 3D frame, "
                           "for the retrace wait's time-out"),
    ('NULL', 0x33 + 4 * 0x0D, 0x33 + 4 * 0x0E, "IRQ 5's vector (int 0Dh), which the digital driver points "
                                               "at its handler while a buffer plays"),
    ('NULL', 0x33 + 4 * 0x0F, 0x33 + 4 * 0x10, "IRQ 7's vector (int 0Fh), the same at the Sound Blaster's "
                                               "usual IRQ"),
]
CROSS = [
    ('GFX ', 0x558A, 0x558E, "GRDISP.ASM's saved SS:SP"),
]

# Recorded sessions: steps for tools/replaydos.mjs. Timings are generous: the recording does
# not depend on them, only on the inputs arriving where the game reads them.
SESSIONS = {
    # boot, the title cutscene, the main menu, a new character, into the game
    # (no saved game, so the introduction starts after the title: Escape skips its two
    # cutscenes), then Create Character with Enter for every default (male, right-handed,
    # fighter, two skill picks, the first portrait, standard difficulty), the name Avatar,
    # Yes, and sixteen seconds in the game; tools/replaydos.mjs then sends F12
    'newgame': ['w:14000', 'k:Escape', 'w:2500', 'k:Escape', 'w:3500', 'k:Enter', 'w:4000'] +
               ['k:Enter', 'w:2000'] * 8 + ['t:Avatar', 'w:1000', 'k:Enter', 'w:2000', 'k:Enter',
               'w:4000', 's:ingame1', 'w:6000', 's:ingame2', 'w:6000'],
    # newgame's way into the game, then about forty seconds in the 3D view: walk forward
    # (w held), turn left and right (a, d), look up and down and level again (1, 3, 2), step
    # back (s), slide (z, c) and click in the view, so that frames from many positions, a
    # pitched view and a pick frame are compared
    'walk': ['w:14000', 'k:Escape', 'w:2500', 'k:Escape', 'w:3500', 'k:Enter', 'w:4000'] +
            ['k:Enter', 'w:2000'] * 8 + ['t:Avatar', 'w:1000', 'k:Enter', 'w:2000', 'k:Enter',
            'w:6000', 's:ingame1', 'h:w,1200', 'w:800', 'h:a,700', 'w:600', 'h:w,1500', 'w:600',
            'h:d,1400', 'w:600', 'k:1', 'w:800', 'k:1', 'w:800', 's:lookup', 'k:3', 'w:600', 'k:3',
            'w:600', 'k:3', 'w:800', 's:lookdown', 'k:2', 'w:800', 'h:s,600', 'w:600', 'h:z,600',
            'w:600', 'h:c,600', 'w:600', 'h:d,900', 'w:600', 'h:w,2000', 'w:800', 's:ingame2',
            'c:left,150', 'w:2000', 'h:a,1200', 'w:600', 'h:w,1500', 'w:3000', 's:ingame3'],
    # newgame's way into the game with a sound card (CFGS), then about forty seconds in the
    # first rooms: fight mode (F1, which draws the weapon and asks for theme 5) and attacks into the air (the right button held in the
    # view: the swing's effects, SOUNDS.DAT's, on the digital channel), walking and turning,
    # and the music playing on, long enough for theme 5 (60 s) to end and the game to choose
    # the next: the effects, the digital buffers and the music driver's state all reach the
    # game
    'sound': ['w:14000', 'k:Escape', 'w:2500', 'k:Escape', 'w:3500', 'k:Enter', 'w:4000'] +
             ['k:Enter', 'w:2000'] * 8 + ['t:Avatar', 'w:1000', 'k:Enter', 'w:2000', 'k:Enter',
             'w:6000', 's:ingame1', 'k:F1', 'w:800', 'c:right,1200', 'w:1500', 'c:right,1200', 'w:1500',
             'h:w,1200', 'w:800', 'h:a,700', 'w:600', 'c:right,1200', 'w:1500', 'h:w,1500',
             'w:600', 'h:d,1400', 'w:600', 'c:right,1500', 'w:2000', 's:ingame2', 'w:12000',
             's:ingame3', 'w:40000', 's:ingame4'],
}
SESSIONS['soundfm'] = SESSIONS['sound']   # the same with every effect on the FM chip (CFGS)
# the same on a Roland MT-32 (CFGS), with every wait doubled: DM05.ADV uploads each theme's
# timbres as system exclusive messages with a wait of vertical retraces after each, so the
# way into the game takes longer in DOS and fixed waits would send the keys too early
SESSIONS['soundmt'] = [f'w:{2 * int(x[2:])}' if x.startswith('w:') else x for x in SESSIONS['sound']]
# a saved game loaded from the main menu: run with --stage DIR, DIR holding SAVE1/ (the one the
# items session saves; DOS's or the port's, which are the same bytes): the title, Escape twice,
# Journey Onward (Enter, the default when a save exists), slot 1 "first" (Enter), and a step
SESSIONS['load'] = ['w:14000', 'k:Escape', 'w:2500', 'k:Escape', 'w:3500', 's:menu', 'k:Enter', 'w:3000',
                    's:slots', 'k:Enter', 'w:6000', 's:loaded', 'h:w,800', 'w:1500', 's:load_end']
_ENTRY = ['w:14000', 'k:Escape', 'w:2500', 'k:Escape', 'w:3500', 'k:Enter', 'w:4000'] + \
         ['k:Enter', 'w:2000'] * 8 + ['t:Avatar', 'w:1000', 'k:Enter', 'w:2000', 'k:Enter', 'w:6000']
# newgame's way into the game, then the Avatar's room (pointer moves are a corner slam, M, then
# a move in the 640 by 400 frame): get mode (F3) and the book on the floor into the second
# inventory slot; a step towards the sack in the corner, the sack into the first slot; use
# mode (F2) on it, which opens it; the map in it, which opens the automap; a note there,
# "hello"; Escape back; look mode (F5) at the next thing in the sack; the statistics panel
# and back (F7 twice: the panel flip, MODEX.ASM's grab); a save to slot 1 described "first"
# (Ctrl+S, Enter), a turn, a restore of slot 1 (Ctrl+R, Enter) and a few steps
SESSIONS['items'] = _ENTRY + [
    'k:F3', 'w:500', 'M', 'm:395,265', 'w:300', 'c:right,200', 'w:1500', 'M', 'm:530,185', 'w:300',
    'c:left,200', 'w:1500', 's:items1', 'h:d,390', 'w:500', 'h:w,250', 'w:600', 'M', 'm:232,235', 'w:300',
    'c:right,200', 'w:1500', 'M', 'm:500,178', 'w:300', 'c:left,200', 'w:1500', 'k:F2', 'w:500', 'M',
    'm:500,178', 'w:300', 'c:right,200', 'w:2000', 's:items2', 'M', 'm:500,178', 'w:300', 'c:right,200',
    'w:3000', 'M', 'm:300,200', 'w:300', 'c:left,200', 'w:1500', 't:hello', 'w:500', 'k:Enter', 'w:1500',
    's:automap', 'k:Escape', 'w:2500', 'k:F5', 'w:500', 'M', 'm:535,178', 'w:300', 'c:right,200', 'w:2000',
    'k:F7', 'w:2000', 's:stats', 'k:F7', 'w:2000', 'k:Control+s', 'w:1500', 'k:Enter', 'w:1500', 't:first',
    'w:800', 'k:Enter', 'w:4000', 's:saved', 'h:a,1000', 'w:800', 'k:Control+r', 'w:1500', 'k:Enter',
    'w:5000', 's:restored', 'h:w,600', 'w:1500', 's:items3']
# newgame's way into the game, then out of the Avatar's room (left, forward, use mode on the
# door, through it), east along the corridor to the great hall and into it, where Nystul is:
# talk mode (F4) on him, and the answers to his conversation (1). The screenshots are part of
# the session: they take time, and where the people in the hall stand depends on it
SESSIONS['talk'] = _ENTRY + [
    'h:a,2400', 'w:500', 'h:w,700', 'w:400', 'h:w,700', 'w:400', 'h:w,700', 'w:400', 'k:F2',
    'w:400', 'M', 'm:240,150', 'w:300', 'c:right,200', 'w:2500', 'h:w,1500', 'w:400', 'h:w,1500',
    'w:400', 'h:a,1200', 'w:400', 'h:d,2400', 'w:400', 'h:w,2000', 'w:400', 'h:w,2000', 'w:400',
    'h:a,1200', 'w:400', 's:corridor', 'h:d,440', 'w:400', 's:east', 'h:w,2600', 'w:400',
    's:hall_door', 'h:a,780', 'w:400', 's:north', 'h:w,900', 'w:400', 's:hall', 'h:w,600', 'w:400',
    's:nystul_near', 'k:F4', 'w:400', 'M', 'm:240,170', 'w:300', 'c:right,200', 'w:3000',
    's:nystul', 'k:1', 'w:3000', 's:talk1', 'k:1', 'w:3000', 's:talk2', 'k:1', 'w:3000',
    's:talk_end']

# a female character with the last portrait (issue 8): newgame's way in, but Down before Enter
# at the sex question and Down four times before Enter at the portraits. Her bodies are
# CHRBTNS.GR's pictures 22 to 26, and every one after the first starts past 7FFFh in the
# pictures' block, where CHARGEN.C's offsets were signed on the host and the port stopped
SESSIONS['female'] = ['w:14000', 'k:Escape', 'w:2500', 'k:Escape', 'w:3500', 'k:Enter', 'w:4000',
                      'k:ArrowDown', 'w:800', 's:sex'] + ['k:Enter', 'w:2000'] * 6 + \
                     ['s:portraits'] + ['k:ArrowDown', 'w:600'] * 4 + ['s:portraitsel', 'k:Enter', 'w:2000',
                      's:portrait', 'k:Enter', 'w:2000', 't:Avatar', 'w:1000', 'k:Enter', 'w:2000', 'k:Enter',
                      'w:4000', 's:ingame1', 'w:6000', 's:ingame2', 'w:3000']

# boot with no saved game and no key at all: the title, then the whole introduction (Lord
# British's letter: the festival street's horizontal pan, the castle's vertical pan over two
# lback pictures, the banquet), to the main menu. The only session that plays the cutscenes
# rather than skipping them
SESSIONS['intro'] = ['w:30000', 's:street', 'w:7000', 's:castle', 'w:6000', 's:castle_pan',
                     'w:60000', 's:intro_end', 'w:20000', 's:menu']

# DATA\UW.CFG for the sessions with sound: the music card (3, Sound Blaster FM, DM03.ADV, or 5,
# Roland MT-32, DM05.ADV, on js-dos's MPU-401 at 330h) and the speech card (1, Sound Blaster
# digital, DD01.ADV, or 0 for none, so every effect is played on the music card), each with its
# IRQ, port and DMA (SOUND.C's seg016_1E73_2FCB reads them)
CFGS = {
    'sound': '3 7 220 1 sound\r\n1 7 220 1 speech\r\n',
    'soundfm': '3 7 220 1 sound\r\n0 -1 -1 -1 speech\r\n',
    'soundmt': '5 2 330 -1 sound\r\n0 -1 -1 -1 speech\r\n',
}


def cfg_of(rec):
    """The sound configuration of recording rec: the .cfg beside it, else its directory's UW.CFG."""
    for c in (os.path.splitext(rec)[0] + '.cfg', os.path.join(os.path.dirname(os.path.abspath(rec)), 'UW.CFG')):
        if os.path.exists(c): return c
    return None


def opts_of(src):
    m = re.search(r'/\*\s*opts:\s*([^*]+?)\s*\*/', open(src, encoding='latin1').read(3000))
    return m.group(1) if m else '-mm -1 -G -O -Z'


def build_plan():
    """What the replay DOS build compiles: [(stem, path, options)] for the hook users (with
    -DREPLAY), the NULLTRAP users (with -DNULLTRAP) and REPLAY.C, and the key of their sources'
    hashes and options, which names the build. The key also takes the source hashes the last
    exact link recorded (build/LINK/base/layout.json): the rest of the build is the matched
    objects of that link, so a change to any other source (an assembly module's segment class,
    say) makes a new build too."""
    from sources import all_sources, replay_sources, stem
    from srcdeps import source_hash
    todo = []
    for src in all_sources() + replay_sources():
        if not src.upper().endswith('.C'): continue
        text = open(src, encoding='latin1').read()
        defs = []
        if HOOKS.search(text) or src in replay_sources(): defs.append('-DREPLAY')
        if 'NULLTRAP(' in text or 'NULLREC(' in text: defs.append('-DNULLTRAP')
        if defs: todo.append((stem(src), src, (opts_of(src) if src not in replay_sources() else '-mm -1 -G -O -Y -d') + ' ' + ' '.join(defs)))
    lay = os.path.join(root, 'build', 'LINK', 'base', 'layout.json')
    base = json.load(open(lay))['sources'] if os.path.exists(lay) else None
    key = hashlib.sha1(json.dumps([[(s, source_hash(p), o) for s, p, o in todo], base], sort_keys=True).encode()).hexdigest()
    return todo, key


def build(quiet=False):
    """Compile the hook users with -DREPLAY (the NULLTRAP users with -DNULLTRAP too) and
    REPLAY.C, and link them as the modding build with REPLAY added. Cached by the sources'
    hashes."""
    from dosbatch import compile_many
    todo, key = build_plan()
    exe = os.path.join(OUT, 'UW2.EXE'); stamp = os.path.join(OUT, 'build.sha1')
    if os.path.exists(exe) and os.path.exists(stamp) and open(stamp).read() == key:
        if not quiet: print(f'replay build: {os.path.relpath(exe, root)} (up to date)')
        return exe
    objdir = lambda s: os.path.join(OUT, 'obj', s)
    print(f'replay build: compiling {len(todo)} sources with -DREPLAY or -DNULLTRAP')
    res = compile_many(todo, objdir)
    again = [t for t in todo if res.get(t[0])]
    if again: res.update(compile_many(again, objdir, batch=1))
    bad = {k: v for k, v in res.items() if v}
    if bad: sys.exit('replay build failed:\n' + '\n'.join(f'{k}: {v}' for k, v in bad.items()))
    cmd = [PY, os.path.join(here, 'link.py'), '--mod', '--out', os.path.join(OUT, 'link')]
    for s, _, _ in todo:
        obj = os.path.join(objdir(s), s + '.OBJ')
        cmd += ['--add' if s == 'REPLAY' else '--obj', f'{s}={obj}']
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL if quiet else None)
    shutil.copy(os.path.join(OUT, 'link', 'UW2.EXE'), exe)
    open(stamp, 'w').write(key)
    print(f'replay build: {os.path.relpath(exe, root)}, {os.path.getsize(exe)} bytes')
    return exe


# ---- reading the files --------------------------------------------------------------------

def read_dump(path):
    """STATE.OUT as a list of checkpoints: dicts with kind, n, events, time and sections."""
    if os.path.isdir(path): path = os.path.join(path, 'STATE.OUT')
    d = open(path, 'rb').read(); p = 0; out = []
    while p + 20 <= len(d):
        if d[p:p + 4] != b'CKPT': raise SystemExit(f'{path}: no checkpoint at byte {p}')
        kind, n, ev, t, ns = struct.unpack_from('<HHIIH', d, p + 4); p += 18
        secs = {}
        for _ in range(ns):
            if p + 8 > len(d): break
            tag = d[p:p + 4].decode('latin1'); ln = struct.unpack_from('<I', d, p + 4)[0]; p += 8
            secs[tag] = d[p:p + ln]; p += ln
        out.append(dict(kind=kind, n=n, events=ev, time=t, secs=secs))
    return out


def read_log(path):
    """RECORD.OUT (the runtime's replay/replay.c): the streams' runs decoded. Returns {stream: [runs]},
    each run (count, value), a SOUND run (count, value, moment) from version 3 (version 4's
    repeats expanded), and the call count the recording stopped at."""
    d = open(path, 'rb').read()
    if d[:4] != b'UW2R' or d[4] not in (2, 3, 4, 5): raise SystemExit(f'{path}: not a version 2 to 5 recording')
    ver = d[4]
    stop = struct.unpack_from('<I', d, 8)[0]
    data = {}; p = 12
    while p + 3 <= len(d):
        s, n = d[p], struct.unpack_from('<H', d, p + 1)[0]; p += 3
        data.setdefault(s, bytearray()).extend(d[p:p + n]); p += n
    out = {}
    for s, b in data.items():
        if s == 9:                     # format 5: the enhancements, names comma-separated
            out['ENH'] = [x for x in b.decode('ascii', 'replace').split(',') if x]
            continue
        runs = []; q = 0; t = 0
        while q < len(b):
            if s == 7:
                tag = b[q]; q += 1
                if tag == 7: runs.append(('WALL', struct.unpack_from('<I', b, q)[0])); q += 4
                else: runs.append(({8: 'SRAND', 9: 'CKPT'}.get(tag, tag), struct.unpack_from('<H', b, q)[0])); q += 2
                continue
            c = struct.unpack_from('<H', b, q)[0]; q += 2
            if s == 8 and ver >= 4 and c == 0:       # a repeat: the last k runs, n times
                k, n = struct.unpack_from('<BH', b, q); q += 3
                pat = runs[-k:]
                for _ in range(n): runs.extend(pat)
                continue
            if s == 1:
                dt = b[q]; q += 1
                if dt == 0xFF: t = struct.unpack_from('<I', b, q)[0]; q += 4
                else: t += dt
                runs.append((c, t))
            elif s == 2:
                r, n = struct.unpack_from('<HB', b, q); q += 3
                q += 0x89 if n == 0xFF else 2 * n
                runs.append((c, r))
            elif s == 8 and ver >= 3: runs.append((c, *struct.unpack_from('<HH', b, q))); q += 4
            elif s in (4, 8): runs.append((c, struct.unpack_from('<H', b, q)[0])); q += 2
            else: runs.append((c, struct.unpack_from('<hh', b, q))); q += 4
        out[STREAMS.get(s, s)] = runs
    return out, stop


def log_summary(path):
    runs, stop = read_log(path)
    parts = [f'stopped at call {stop}' if stop else 'never stopped']
    for k, v in runs.items():
        if k == 'MISC': parts.append('MISC ' + ' '.join(f'{a}:{b:X}' for a, b in v)); continue
        if k == 'ENH': parts.append('enhancements ' + (', '.join(v) or 'none')); continue   # names (format 5), not runs
        parts.append(f'{k} {sum(r[0] for r in v)} calls in {len(v)} runs')
    keys = [r for c, r in runs.get('KEY', []) if r]
    if keys: parts.append('keys ' + ' '.join(f'{k:04X}' for k in keys))
    if 'TIME' in runs: parts.append(f"clock {runs['TIME'][0][1]:X} to {runs['TIME'][-1][1]:X}")
    return '; '.join(parts)


def scanout(secs):
    """The visible screen from the VGA and CRTC sections, as the CRT controller shows it
    (the runtime's gfx/vga.c, vga_scanout without pixel panning, which the dump does not hold):
    320 by 200 (or 400) palette indices."""
    vga, c = secs['VGA '], secs['CRTC']
    r07, r09, r0c, r0d, r13, r18 = c
    msl = (r09 & 0x1F) + 1; rows = min(400 // msl, 400)
    start = r0c << 8 | r0d; pitch = r13 * 2
    lc = r18 | (r07 & 0x10) << 4 | (r09 & 0x40) << 3
    pix = bytearray(320 * rows); addr = start
    planes = [vga[i * 0x10000:(i + 1) * 0x10000] for i in range(4)]
    for y in range(rows):
        sl = y * msl
        if y and sl > lc and sl - msl <= lc: addr = 0
        for x in range(320):
            pix[y * 320 + x] = planes[x & 3][(addr + (x >> 2)) & 0xFFFF]
        addr += pitch
    return bytes(pix), rows


def write_png(path, pix, w, h, pal6):
    pal = bytes(min(255, (v << 2) | (v >> 4)) for v in pal6)
    raw = b''.join(b'\0' + pix[y * w:(y + 1) * w] for y in range(h))
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0)) + \
        chunk(b'PLTE', pal) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def label(ck):
    k = KINDS.get(ck['kind'], str(ck['kind']))
    n = ck['n']
    if ck['kind'] == 3: n = f"buttons {n & 0xFF}" if n & 0x8000 else f"key {n:04X}"
    return f"{k} {n} at event {ck['events']}, clock {ck['time']:X}"


# ---- comparing ------------------------------------------------------------------------------

def ranges(a, b, skip=()):
    """The differing byte ranges of two equal-length byte strings, merged when close."""
    out = []
    if a == b: return out
    for i in range(min(len(a), len(b))):
        if a[i] != b[i] and not any(lo <= i < hi for lo, hi in skip):
            if out and i - out[-1][1] <= 8: out[-1][1] = i + 1
            else: out.append([i, i + 1])
    return out


def segment_words(x, y, sa, sb):
    """The words where x holds one far block's segment in its build and y the same block's in
    the other (SEGS, src/include/rpgame.h): stored far pointers, equal in meaning."""
    na, nb = struct.unpack(f'<{len(sa) // 2}H', sa), struct.unpack(f'<{len(sb) // 2}H', sb)
    pairs = {(u, v) for u, v in zip(na, nb) if u != v}
    # The EXE's own segments (the far data from 3705 to 6061, seg021's, DGROUP) all move by one
    # load segment, which seg_370D's pair gives: a word that is an EXE paragraph plus the load
    # segment in each build is one.
    delta = (na[0] - nb[0]) & 0xFFFF
    lo_a, lo_b = (na[0] - 0x370D) & 0xFFFF, (nb[0] - 0x370D) & 0xFFFF
    def same(u, v):
        return (u, v) in pairs or ((u - v) & 0xFFFF == delta and 0x3705 <= (u - lo_a) & 0xFFFF < 0x7600
                                   and 0x3705 <= (v - lo_b) & 0xFFFF < 0x7600)
    out = []; i = 0
    while i + 1 < len(x):
        if x[i:i + 2] != y[i:i + 2] and same(x[i] | x[i + 1] << 8, y[i] | y[i + 1] << 8):
            out.append((i, i + 2)); i += 2
        else: i += 1
    return out


def compare(pa, pb, same_build=False, quiet=False):
    A, B = read_dump(pa), read_dump(pb)
    print(f'{pa}: {len(A)} checkpoints; {pb}: {len(B)}')
    worst = 0; first_bad = None
    for i in range(min(len(A), len(B))):
        a, b = A[i], B[i]
        if (a['kind'], a['n'], a['events'], a['time']) != (b['kind'], b['n'], b['events'], b['time']):
            print(f'checkpoint {i}: the runs went different ways: {label(a)} against {label(b)}')
            if 'CNTS' in a['secs'] and 'CNTS' in b['secs']:
                ca = struct.unpack(f"<{len(a['secs']['CNTS']) // 4}I", a['secs']['CNTS'])
                cb = struct.unpack(f"<{len(b['secs']['CNTS']) // 4}I", b['secs']['CNTS'])
                print('  calls by stream: ' + ', '.join(f'{STREAMS[k + 1]} {x}' + (f' against {y}' if x != y else '')
                                                    for k, (x, y) in enumerate(zip(ca, cb))))
            return 2
        notes = []
        for tag in sorted(set(a['secs']) | set(b['secs'])):
            if tag == 'NULL' and not same_build: continue
            x, y = a['secs'].get(tag), b['secs'].get(tag)
            if x is None or y is None: notes.append(f'{tag.strip()} only in one'); continue
            if tag == 'SEGS' and not same_build: continue
            skip = [(lo, hi) for s, lo, hi, _ in ALWAYS + ([] if same_build else CROSS) if s == tag]
            if len(x) != len(y): notes.append(f'{tag.strip()} lengths {len(x)} and {len(y)}'); continue
            if not same_build and 'SEGS' in a['secs'] and 'SEGS' in b['secs']:
                skip += segment_words(x, y, a['secs']['SEGS'], b['secs']['SEGS'])
            if tag == 'GFX ' and not same_build:
                # GRCORE's string entries copy the string, its 0 and one byte more to 370D:4FA8;
                # that byte is whatever followed the string in the caller's buffer, often stack junk
                z = x.find(b'\0', 0x4FA8, 0x4FA8 + 0x84)
                if z >= 0: skip.append((z + 1, 0x4FA8 + 0x84))   # and the junk of longer strings before it
            r = ranges(x, y, skip)
            if not r: continue
            if tag == 'VGA ':
                sa, ha = scanout(a['secs']); sb, hb = scanout(b['secs'])
                px = sum(1 for k in range(min(len(sa), len(sb))) if sa[k] != sb[k])
                notes.append(f'VGA {sum(h - l for l, h in r)} bytes differ, screen {px} of {len(sa)} pixels')
            else:
                notes.append(f'{tag.strip()} {sum(h - l for l, h in r)} bytes differ: ' +
                             ', '.join(f'{l:X}..{h - 1:X}' for l, h in r[:6]) + (' ...' if len(r) > 6 else ''))
        if notes:
            worst = 1
            if first_bad is None: first_bad = i
            print(f'checkpoint {i} ({label(a)}): ' + '; '.join(notes))
        elif not quiet and a['kind'] != 2:
            print(f'checkpoint {i} ({label(a)}): identical')
    if len(A) != len(B):
        print(f'one run has {abs(len(A) - len(B))} more checkpoints (the shorter stopped earlier)')
        worst = max(worst, 1)
    print('identical' if worst == 0 else f'differences, the first at checkpoint {first_bad}' if first_bad is not None else 'differences')
    return worst


def saves(a, b):
    """Compares the saved games two runs made (their SAVE1..SAVE4 directories), file for file
    and byte for byte, case-blind on the names."""
    bad = 0; n = 0
    for d in ('SAVE1', 'SAVE2', 'SAVE3', 'SAVE4'):
        da, db = os.path.join(a, d), os.path.join(b, d)
        fa = {f.upper(): f for f in os.listdir(da)} if os.path.isdir(da) else {}
        fb = {f.upper(): f for f in os.listdir(db)} if os.path.isdir(db) else {}
        for f in sorted(set(fa) | set(fb)):
            n += 1
            if f not in fa or f not in fb:
                print(f'{d}/{f}: only in {a if f in fa else b}'); bad = 1; continue
            x = open(os.path.join(da, fa[f]), 'rb').read(); y = open(os.path.join(db, fb[f]), 'rb').read()
            if x == y: print(f'{d}/{f}: identical, {len(x)} bytes'); continue
            r = ranges(x, y) if len(x) == len(y) else None
            print(f'{d}/{f}: differ' + (f', {len(x)} and {len(y)} bytes' if r is None else
                                         ': ' + ', '.join(f'{l:X}..{h - 1:X}' for l, h in r[:8])))
            bad = 1
    if not n: print('no saved games in either run')
    return bad


def show(path, pngdir=None):
    for i, ck in enumerate(read_dump(path)):
        secs = ' '.join(f'{t.strip()}:{len(v)}' for t, v in ck['secs'].items())
        print(f'{i:4d} {label(ck)}  [{secs}]')
        if pngdir and 'VGA ' in ck['secs']:
            os.makedirs(pngdir, exist_ok=True)
            pix, h = scanout(ck['secs'])
            write_png(os.path.join(pngdir, f'ck{i:04d}.png'), pix, 320, h, ck['secs']['PAL '])


def nulls(d):
    """The null-pointer write check of a DOS run: the NULL sections of its dumps (DS:0..30h and
    the vector table as the game saw them, and C0's checksum) and dos-mcp's reads after the game
    exited (memory.json)."""
    bad = 0
    dump = read_dump(d)
    first = None
    for i, ck in enumerate(dump):
        s = ck['secs'].get('NULL')
        if not s: continue
        ds, chk, ivt = s[:0x31], struct.unpack_from('<H', s, 0x31)[0], s[0x33:]
        if first is None: first = (i, ds, ivt)
        if chk:
            print(f'checkpoint {i} ({label(ck)}): C0\'s checksum of DS:4..30h is off by {chk:04X}: a null pointer write'); bad = 1
        if ds[3] != 0 or not (ds[:3] == b"\x27\x06\x00" or ds[0] == 6):
            print(f'checkpoint {i}: DS:0..3 is {ds[:4].hex()}, neither form of ovr167\'s stub entry'); bad = 1
    if first:
        i, ds, ivt = first; last = dump[-1]['secs']['NULL'][0x33:]
        changed = [v for v in range(256) if ivt[v * 4:v * 4 + 4] != last[v * 4:v * 4 + 4]]
        print(f'vector table: {len(changed)} vectors changed between checkpoint {i} and the last: ' +
              ' '.join(f'{v:02X}' for v in changed))
        print(f'DS:0..3 at the first checkpoint {ds[:4].hex()}, at the last {dump[-1]["secs"]["NULL"][:4].hex()}')
    mj = os.path.join(d, 'memory.json')
    if os.path.exists(mj):
        m = json.load(open(mj))
        for g in m.get('groups', []):
            b = bytes.fromhex(g['ds0_3f'])
            print(f"after exit, DGROUP at linear {g['ds_linear']:X}: DS:0..3 {b[:4].hex()}, C0 checksum "
                  f"{(sum(b[4:0x31]) - 0xCA5) & 0xFFFF:04X} (0 when intact)")
    nl = os.path.join(d, 'NULLTRAP.LOG')
    if os.path.exists(nl):
        hits = open(nl, encoding='latin1').read().split()
        print(f'NULLTRAP.LOG: {len(hits)} null pointers met at marked sites: ' + ' '.join(sorted(set(hits))))
    else:
        print('NULLTRAP.LOG: none written (no marked null pointer met)')
    return bad


# ---- running ----------------------------------------------------------------------------------

STAGE = None   # --stage DIR: files (a saved game) put into the game's directory, DOS's and the port's home


def dos_can_replay(rec):
    """(True, '') if the DOS build can replay recording rec; else (False, why). A format 5
    recording was made in the port with enhancements, which DOS does not have."""
    d = open(rec, 'rb').read(15)
    if len(d) >= 5 and d[4] == 5:
        names = ''
        if len(d) >= 15 and d[12] == 9:
            n = struct.unpack_from('<H', d, 13)[0]
            names = open(rec, 'rb').read(15 + n)[15:].decode('ascii', 'replace')
        return False, f'{rec} was recorded in the port with enhancements ({names}); DOS cannot replay it'
    return True, ''


def run_dos(out, rec=None, steps=(), timeout=900, cfg=None, stage=None, log=None, backend=None, exe=None):
    """Runs the replay DOS build in tools/replaydos.mjs: records with steps, or replays rec.
    stage (default --stage's) is put into the game's directory first; log, a file the run's
    output goes to instead of the terminal; backend, replaydos's --backend (jsdos, dosbox-x;
    by default DOSBox-X for a replay when it is installed)."""
    if rec:
        ok, why = dos_can_replay(rec)
        if not ok: raise SystemExit('replay.py: ' + why)
    exe = exe or build()
    stage = stage or STAGE
    cmd = ['node', os.path.join(here, 'replaydos.mjs'), exe, out, '--timeout', str(timeout)]
    if stage: cmd += ['--stage', stage]
    if rec: cmd += ['--replay', rec]
    if backend: cmd += ['--backend', backend]
    cfg = cfg or (cfg_of(rec) if rec else None)
    if cfg: cmd += ['--cfg', cfg]
    os.makedirs(out, exist_ok=True)
    if log:
        with open(log, 'w') as f: r = subprocess.run(cmd + list(steps), stdout=f, stderr=subprocess.STDOUT)
    else: r = subprocess.run(cmd + list(steps))
    return r.returncode


def port_exe(path):
    """The port's program: path, or path.exe on Windows. EXHUME_PORT names another in place of
    the plain build's (Exhume's tools/pkgcheck.py: the program from a player's package)."""
    if os.environ.get('EXHUME_PORT') and os.path.basename(os.path.dirname(path)) == 'port':
        return os.environ['EXHUME_PORT']
    return path + '.exe' if not os.path.exists(path) and os.path.exists(path + '.exe') else path


def have_toolchain():
    """Turbo C++ is set up (make setup): the replay DOS build can be made. verify does not need
    it; without it (a port-only machine, the Windows and macOS verify jobs) the goldens are not
    checked against the replay build."""
    return os.path.exists(os.path.join(root, 'TC', 'TCC.EXE'))


def port_env():
    """The environment to run the port in. On Windows a development build finds its DLLs on
    PATH, and those tools/setup-libs.sh built (libmt32emu) are in tools/libs/bin, which no shell
    puts there: without it the port exits 0xC0000135 (a DLL not found) before writing anything."""
    env = dict(os.environ)
    if os.name == 'nt':
        dirs = [os.path.join(root, 'tools', 'libs', 'bin'), os.path.join(root, 'tools', 'libs', 'lib')]
        prefix = os.environ.get('MINGW_PREFIX')     # /clang64 in MSYS2's CLANG64 shell: SDL3 and the C++ runtime
        if prefix and shutil.which('cygpath'):
            w = subprocess.run(['cygpath', '-w', prefix + '/bin'], capture_output=True, text=True).stdout.strip()
            if w: dirs.append(w)
        extra = [d for d in dirs if os.path.isdir(d)]
        env['PATH'] = os.pathsep.join(extra + [env.get('PATH', '')])
    return env

def exit_after(cmd):
    """CMD with its --exit-after limit (milliseconds of the port's clock) replaced by
    $EXHUME_EXIT_AFTER when that is set: the web build's Node.js replays (make webcheck) run
    some sessions twenty to eighty times slower than the desktop's, close to the usual limit."""
    v = os.environ.get('EXHUME_EXIT_AFTER')
    if v and '--exit-after' in cmd:
        i = cmd.index('--exit-after')
        cmd = cmd[:i + 1] + [v] + cmd[i + 2:]
    return cmd


def run_port(rec, out, extra=(), stage=None, quiet=False):
    extra = list(extra)
    debug = '--debug' in extra
    if debug: extra.remove('--debug')
    cov = '--cov' in extra              # the coverage build (tools/coverage.py), writing LLVM_PROFILE_FILE
    if cov: extra.remove('--cov')
    stage = stage or STAGE
    exe = port_exe(os.path.join(root, 'build', 'port-debug' if debug else 'port-cov' if cov else 'port', 'uw2port'))
    if not os.path.exists(exe): sys.exit('replay.py: build the port first (make port)')
    home = os.path.join(out, 'home'); shutil.rmtree(home, ignore_errors=True); os.makedirs(home)
    if stage: shutil.copytree(stage, home, dirs_exist_ok=True)
    cfg = cfg_of(rec)
    if cfg:
        os.makedirs(os.path.join(home, 'DATA'), exist_ok=True)
        shutil.copy(cfg, os.path.join(home, 'DATA', 'UW.CFG'))
    cmd = [exe, '--data', DATA, '--home', home, '--hidden', '--exit-on-halt', '--exit-after', '600000',
           '--replay', os.path.abspath(rec)] + list(extra)
    r = subprocess.run(exit_after(cmd), capture_output=True, text=True, env=port_env())
    open(os.path.join(out, 'port.log'), 'w').write(r.stdout + r.stderr)
    for f in ('STATE.OUT',):
        if os.path.exists(os.path.join(out, f)): os.remove(os.path.join(out, f))
        if os.path.exists(os.path.join(home, f)): shutil.copy(os.path.join(home, f), os.path.join(out, f))
    for d in ('SAVE1', 'SAVE2', 'SAVE3', 'SAVE4'):         # the saved games, as DOS's runs copy theirs
        shutil.rmtree(os.path.join(out, d), ignore_errors=True)
        if os.path.isdir(os.path.join(home, d)):
            shutil.copytree(os.path.join(home, d), os.path.join(out, d))
    ub = [l for l in (r.stdout + r.stderr).splitlines() if 'runtime error' in l]
    if quiet: return r.returncode
    if debug: print(f'port: UBSan reported {len(ub)} null dereferences' + ''.join('\n  ' + l for l in sorted(set(ub))[:20]))
    tail = [l for l in (r.stdout + r.stderr).splitlines() if 'uw2port' in l][-3:]
    print('port:', r.returncode, *tail, sep='\n  ')
    return r.returncode


def sound_check(*dirs):
    """With a sound card: each run's count of the reads where its own sound driver differed from
    the recording (DOS's SNDCHECK.OUT, the port's log); the port's must be 0, DOS's are the
    reference's own timing noise (docs/PORT.md, "Replays with a sound card")."""
    lines = []
    for d in dirs:
        for f, pat in (('SNDCHECK.OUT', 'sound reads'), ('port.log', 'uw2port: sound reads')):
            fp = os.path.join(d, f)
            if os.path.exists(fp):
                lines += [(d, l.strip()) for l in open(fp, errors='replace') if l.strip().startswith(pat)]
    if not lines: return 0
    print('\n== sound drivers against the recording')
    bad = 0
    for d, l in lines:
        print(f'{os.path.basename(d)}: {l}')
        m = re.search(r'sound reads: \d+, (\d+) where the port', l)
        if m and int(m.group(1)): bad = 1
    return bad


def driver_check(p):
    """With a music card: tools/ailcheck.py on the port's logs, the real .ADV driver against
    the port's C driver write for write (needs Unicorn in the .venv; skipped without)."""
    al, hl = os.path.join(p, 'ail.log'), os.path.join(p, 'hw.log')
    if not (os.path.exists(al) and os.path.exists(hl)): return 0
    if 'register_driver' not in open(al, errors='replace').read(200000): return 0
    print('\n== the port\'s music driver against the real one (tools/ailcheck.py)')
    r = subprocess.run([PY, os.path.join(here, 'ailcheck.py'), al, hl], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if 'needs Unicorn' in out:
        print('skipped: no Unicorn in', PY); return 0
    print(out)
    return 1 if r.returncode else 0


def main(argv):
    global STAGE
    if '--stage' in argv:
        i = argv.index('--stage'); STAGE = os.path.abspath(argv[i + 1]); argv = argv[:i] + argv[i + 2:]
    if not argv: print(__doc__); return 2
    cmd, a = argv[0], argv[1:]
    if cmd == 'build': build(); return 0
    if cmd in ('golden', 'verify'):
        import golden
        return golden.main(cmd, a)
    if cmd == 'record':
        out = a[0]; steps = a[1:]; cfg = None
        if steps[:1] == ['--session']:
            name = steps[1]; steps = SESSIONS[name]
            if name in CFGS:
                os.makedirs(out, exist_ok=True)
                cfg = os.path.join(out, 'UW.CFG')
                open(cfg, 'w', newline='').write(CFGS[name])
        rc = run_dos(out, None, steps, cfg=cfg)
        if os.path.exists(os.path.join(out, 'RECORD.OUT')):
            print('recorded:', log_summary(os.path.join(out, 'RECORD.OUT')))
        return rc
    if cmd == 'log': print(log_summary(a[0])); return 0
    if cmd == 'dos': return run_dos(a[1], a[0])
    if cmd == 'port': return run_port(a[0], a[1], a[2:])
    if cmd == 'compare': return compare(a[0], a[1], '--same-build' in a)
    if cmd == 'show': show(a[0], a[1] if len(a) > 1 else None); return 0
    if cmd == 'saves': return saves(a[0], a[1])
    if cmd == 'nulls': return nulls(a[0])
    if cmd == 'check':
        rec, out = a[0], a[1]
        d1, d2, p = os.path.join(out, 'dos1'), os.path.join(out, 'dos2'), os.path.join(out, 'port')
        for d in (d1, d2, p): os.makedirs(d, exist_ok=True)
        run_dos(d1, rec); run_dos(d2, rec)
        logs = ['--ail-log', os.path.join(p, 'ail.log'), '--hw-log', os.path.join(p, 'hw.log')] if cfg_of(rec) else []
        run_port(rec, p, logs)
        if os.path.exists(os.path.join(root, 'build', 'port-debug', 'uw2port')):
            pd = os.path.join(out, 'port-debug'); os.makedirs(pd, exist_ok=True); run_port(rec, pd, ['--debug'])
        print('\n== DOS against DOS'); r1 = compare(d1, d2, True, quiet=True)
        print('\n== DOS against the port'); r2 = compare(d1, p)
        print('\n== null pointers (DOS)'); r3 = nulls(d1)
        r4 = max(sound_check(d1, d2, p), driver_check(p))
        if os.path.isdir(os.path.join(d1, 'SAVE1')) or os.path.isdir(os.path.join(p, 'SAVE1')):
            print('\n== saved games, DOS against DOS'); r4 = max(r4, saves(d1, d2))
            print('\n== saved games, DOS against the port'); r4 = max(r4, saves(d1, p))
        return max(r1, r2, r3, r4)
    print(__doc__); return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
