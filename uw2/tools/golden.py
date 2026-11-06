r"""Golden references for the replay sessions, and the port checked against them (docs/BUILDING.md,
"Testing"). tools/replay.py's `golden` and `verify` commands are this module:

    python3 tools/replay.py golden [SESSION ...|all] [-j N] [--backend dosbox-x|jsdos] [--check]
        replay each session in DOS twice, at once, check the two runs agree (the dumps, checkpoint
        by checkpoint and section by section, and the saved games, byte for byte), and write
        tests/replay/golden/SESSION/: golden.json and a PNG of the screen at every full
        checkpoint. --check writes nothing and compares the new DOS runs with the committed
        goldens instead (that is how DOSBox-X was shown to give js-dos's goldens).
    python3 tools/replay.py verify [SESSION ...|all] [-j N] [--debug | --cov]
        replay each session in the port only and compare it with its golden: the first
        checkpoint and the sections that differ, a DOS | port | difference PNG of every screen that
        differs, the saved games, and the port's count of sound driver reads that differ from
        DOS's. --debug replays with make port-debug's UBSan build and counts its reports; --cov
        with the coverage build (tools/coverage.py).

Sessions run concurrently, one per core up to -j (8 by default; 4 for js-dos, whose every run is
a headless Chrome). `load` replays the saved game the `items` session makes, so it waits for
items: golden takes it from items' first DOS run, verify from items' port run once that is shown
to be the golden's.

golden.json holds no game data and no dump, only digests. For each checkpoint: its header (kind,
number, hook calls, clock) and a 64-bit BLAKE2b digest of each section, taken over the section as
tools/replay.py compares DOS with the port: the ranges ALWAYS and CROSS list zeroed, the byte
after a string GRCORE copies zeroed, and the words in SEG_SLOTS that hold a far block's segment
replaced by which block they name. NULL and SEGS are left out: they are the DOS set-up's own
(the vector table, the load segment), so they differ between js-dos and DOSBox-X and the port
has stand-ins; replay.py's null-pointer check reads them at golden time. Beside the digests:
the SHA-256 of the recording, its sound configuration, the staged saved game and the saved
games the session writes, and the SHA-256 of the replay DOS build that made it. A golden whose
recording or configuration has changed is stale and fails; one made by another replay DOS build
is flagged, since the replay build's state dumps could differ.
"""
import os, sys, json, time, struct, shutil, hashlib, zlib, threading, subprocess
from concurrent.futures import ThreadPoolExecutor

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
sys.path.insert(0, here)
import replay as R

GOLDEN = os.path.join(root, 'tests', 'replay', 'golden')
WORK = os.path.join(R.OUT, 'golden-work')
VWORK = os.path.join(R.OUT, 'verify')
# every recording committed under tests/replay is a session (NAME.rec, with NAME.cfg for a
# sound card), the first eight in the order they were made
_ORDER = ['newgame', 'walk', 'sound', 'soundfm', 'soundmt', 'items', 'talk', 'load']
_RECS = sorted(f[:-4] for f in os.listdir(os.path.join(root, 'tests', 'replay')) if f.endswith('.rec'))
SESSIONS = [n for n in _ORDER if n in _RECS] + [n for n in _RECS if n not in _ORDER]
# replayed with the saved game (SAVE1) another session makes; weightwrap's comes from a setup
# session (tests/replay/setup/weightless/), which only the port replays: its cheats make a carried
# weight of 0 with a candle in the pack, as a saved game edited elsewhere can hold it
STAGE_FROM = {'load': 'items', 'weightwrap': 'setup/weightless'}
FORMAT = 1
# Words of a section where a far pointer's segment is kept: compared as which far block they name
# (SEGS), as tools/replay.py's segment_words finds them between two dumps. These are all the eight
# sessions have, between DOS and the port and between js-dos and DOSBox-X, which load the game at
# different segments: seg003's data at 07D2 (the frame buffer's), 0958, 0B0C and 0B1A, the
# far pointers GRDISP.ASM keeps at 553C..5541, and the saved SS at 558C and 55EC.
SEG_SLOTS = {'GFX ': (0x7D2, 0x958, 0xB0C, 0xB1A, 0x553C, 0x553E, 0x5540, 0x558C, 0x55EC)}

_print_lock = threading.Lock()


def say(*a):
    with _print_lock: print(*a, flush=True)


def rec_of(name):
    """NAME's recording: NAME.rec, or for a directory session (a name with a /, setup/NAME)
    NAME/session.rec, with session.cfg beside it."""
    if '/' in name: return os.path.join(root, 'tests', 'replay', name, 'session.rec')
    return os.path.join(root, 'tests', 'replay', name + '.rec')


def port_only(name):
    """A session only the port replays (a setup session's, format 6, with a test's cheats): a
    stage made by it comes from the port's replay, in golden and verify alike, so that DOS judges
    the session from the saved game the port made (as Exhume's tools/golden.py does)."""
    rec = rec_of(name)
    return os.path.exists(rec) and not R.dos_can_replay(rec)[0]


def port_stage_run(name, d):
    """Replays port-only session name in the port into d (its saved games copied there, as
    replay.py's run_port does): (exit status, d, seconds)."""
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    t = time.time()
    rc = R.run_port(rec_of(name), d, [], quiet=True)
    if rc == 0 and not os.path.isdir(os.path.join(d, 'SAVE1')): rc = 7
    say(f'  {name}: port run (a setup session DOS cannot replay) {"done" if rc == 0 else f"failed ({rc})"} in {time.time() - t:.1f} s')
    return (rc, d, time.time() - t)


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def rel(p): return os.path.relpath(p, root)


def seg_identity(w, segs):
    """Which far block a word names in a build whose far blocks are segs (SEGS): 'S<n>' for block
    n, 'R<para>' for a paragraph of the EXE's image counted from the load segment, else None."""
    s = struct.unpack(f'<{len(segs) // 2}H', segs)
    if w in s: return f'S{s.index(w)}'
    r = (w - ((s[0] - 0x370D) & 0xFFFF)) & 0xFFFF
    return f'R{r:04X}' if 0x3705 <= r < 0x7600 else None


def canon(ck):
    """The digests of a checkpoint's sections, as DOS and the port are compared."""
    out = {}
    segs = ck['secs'].get('SEGS')
    for tag, x in ck['secs'].items():
        if tag in ('NULL', 'SEGS'): continue
        b = bytearray(x)
        for t, lo, hi, _ in R.ALWAYS + R.CROSS:
            if t == tag: b[lo:hi] = bytes(len(b[lo:hi]))
        if tag == 'GFX ':
            z = x.find(b'\0', 0x4FA8, 0x4FA8 + 0x84)
            if z >= 0: b[z + 1:0x4FA8 + 0x84] = bytes(len(b[z + 1:0x4FA8 + 0x84]))
        ids = []
        for off in SEG_SLOTS.get(tag, ()):
            if segs and off + 2 <= len(b):
                i = seg_identity(b[off] | b[off + 1] << 8, segs)
                if i: ids.append(f'{off:X}={i}'); b[off:off + 2] = b'\0\0'
        h = hashlib.blake2b(bytes(b), digest_size=8)
        if ids: h.update(';'.join(ids).encode())
        out[tag.strip()] = h.hexdigest()
    return out


def header(ck): return [ck['kind'], ck['n'], ck['events'], ck['time']]


def label(h):
    return R.label(dict(kind=h[0], n=h[1], events=h[2], time=h[3]))


def save_hashes(d):
    out = {}
    for s in ('SAVE1', 'SAVE2', 'SAVE3', 'SAVE4'):
        sd = os.path.join(d, s)
        if os.path.isdir(sd):
            for f in sorted(os.listdir(sd)):
                if not f.startswith('.') and os.path.isfile(os.path.join(sd, f)):
                    out[f'{s}/{f.upper()}'] = sha256(os.path.join(sd, f))
    return out


def exe_sha():
    exe = os.path.join(R.OUT, 'UW2.EXE')
    return sha256(exe) if os.path.exists(exe) else None


# ---- PNG ------------------------------------------------------------------------------------

def png_rgb(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def read_png_indexed(path):
    """A PNG as tools/replay.py's write_png writes it (8-bit palette, filter 0): (w, h, indices,
    palette as 768 bytes of RGB)."""
    d = open(path, 'rb').read(); p = 8; idat = b''; pal = b''; w = h = 0
    while p < len(d):
        n = struct.unpack_from('>I', d, p)[0]; t = d[p + 4:p + 8]; b = d[p + 8:p + 8 + n]; p += 12 + n
        if t == b'IHDR': w, h = struct.unpack_from('>II', b)
        elif t == b'PLTE': pal = b
        elif t == b'IDAT': idat += b
    raw = zlib.decompress(idat)
    pix = b''.join(raw[y * (w + 1) + 1:(y + 1) * (w + 1)] for y in range(h))
    return w, h, pix, pal


def triptych(golden_png, secs, path):
    """DOS's screen (the golden's PNG), the port's, and their difference: the differing pixels in
    red over DOS's screen darkened. Returns the count of differing pixels."""
    w, h, gpix, gpal = read_png_indexed(golden_png)
    ppix, ph = R.scanout(secs)
    ppal = bytes(min(255, (v << 2) | (v >> 4)) for v in secs['PAL '])
    H = max(h, ph); W = w * 3 + 8
    img = bytearray(b'\x40' * (W * H * 3)); bad = 0
    for y in range(H):
        for x in range(w):
            g = gpal[gpix[y * w + x] * 3:gpix[y * w + x] * 3 + 3] if y < h else b'\0\0\0'
            q = ppal[ppix[y * 320 + x] * 3:ppix[y * 320 + x] * 3 + 3] if y < ph else b'\0\0\0'
            o = (y * W + x) * 3; img[o:o + 3] = g
            o = (y * W + w + 4 + x) * 3; img[o:o + 3] = q
            o = (y * W + 2 * w + 8 + x) * 3
            if g != q: img[o:o + 3] = b'\xff\x00\x00'; bad += 1
            else: img[o:o + 3] = bytes(c // 4 for c in g)
    png_rgb(path, W, H, bytes(img))
    return bad


# ---- golden ---------------------------------------------------------------------------------

def golden_meta(name, d1, backend, stage_files):
    rec = rec_of(name); cfg = R.cfg_of(rec)
    return {
        'format': FORMAT,
        'session': name,
        'recording': {'path': rel(rec), 'sha256': sha256(rec)},
        'cfg': {'path': rel(cfg), 'sha256': sha256(cfg)} if cfg else None,
        'stage': {'session': STAGE_FROM[name], 'files': stage_files} if name in STAGE_FROM else None,
        # the replay DOS build by its bytes only: its sources' key names its cache, and changes
        # with any edit to a DOS source, even a port-only one that leaves the build the same
        'replay_build': {'exe_sha256': exe_sha()},
        'dos': {'backend': backend, 'runs': 2, 'identical': True},
        'digest': 'BLAKE2b-64 of each section as DOS and the port are compared (tools/golden.py, canon)',
        'saves': save_hashes(d1),
    }


def write_golden(name, d1, meta):
    gdir = os.path.join(GOLDEN, name)
    os.makedirs(gdir, exist_ok=True)
    for f in os.listdir(gdir):
        if f.endswith('.png'): os.remove(os.path.join(gdir, f))
    lines = []
    for i, ck in enumerate(R.read_dump(d1)):
        e = {'i': i, 'ck': header(ck), 'secs': canon(ck)}
        if 'VGA ' in ck['secs']:
            pix, h = R.scanout(ck['secs'])
            e['png'] = f'ck{i:04d}.png'
            R.write_png(os.path.join(gdir, e['png']), pix, 320, h, ck['secs']['PAL '])
        lines.append(json.dumps(e, separators=(',', ':')))
    text = json.dumps(meta, indent=1)
    text = text[:text.rindex('}')].rstrip() + ',\n "checkpoints": [\n  ' + ',\n  '.join(lines) + '\n ]\n}\n'
    open(os.path.join(gdir, 'golden.json'), 'w').write(text)
    return len(lines)


def load_golden(name):
    p = os.path.join(GOLDEN, name, 'golden.json')
    return json.load(open(p)) if os.path.exists(p) else None


def compare_golden(name, g, d, kind, diffdir=None):
    """Compares run d's dump (and saved games) with golden g. Returns (ok, lines)."""
    lines = []; ok = True
    cks = R.read_dump(d)
    gc = g['checkpoints']
    first = None; nbad = 0
    for i, (e, ck) in enumerate(zip(gc, cks)):
        if e['ck'] != header(ck):
            lines.append(f'checkpoint {i}: the runs went different ways: golden {label(e["ck"])}, {kind} {label(header(ck))}')
            if 'CNTS' in ck['secs']:
                c = struct.unpack(f"<{len(ck['secs']['CNTS']) // 4}I", ck['secs']['CNTS'])
                lines.append('  ' + kind + ' calls by stream: ' + ', '.join(f'{R.STREAMS[k + 1]} {x}' for k, x in enumerate(c)))
            ok = False; nbad += 1; first = first if first is not None else i
            break
        mine = canon(ck)
        bad = sorted(t for t in set(mine) | set(e['secs']) if mine.get(t) != e['secs'].get(t))
        if not bad: continue
        ok = False; nbad += 1
        if first is None: first = i
        if nbad <= 3:
            note = f'checkpoint {i} ({label(e["ck"])}): ' + ', '.join(bad) + ' differ'
            if 'VGA' in bad and e.get('png') and diffdir:
                os.makedirs(diffdir, exist_ok=True)
                out = os.path.join(diffdir, f'diff_ck{i:04d}.png')
                px = triptych(os.path.join(GOLDEN, name, e['png']), ck['secs'], out)
                note += f'; screen {px} pixels differ: {rel(out)}'
            lines.append(note)
    if len(gc) != len(cks):
        ok = False
        lines.append(f'{len(gc)} checkpoints in the golden, {len(cks)} in the {kind} run')
    if nbad > 3: lines.append(f'... {nbad} checkpoints differ in all, the first at {first}')
    sv = save_hashes(d)
    if sv != g.get('saves', {}):
        ok = False
        for f in sorted(set(sv) | set(g.get('saves', {}))):
            if sv.get(f) != g['saves'].get(f):
                lines.append(f'saved game {f}: ' + ('missing' if f not in sv else 'not in the golden' if f not in g['saves'] else 'differs'))
    return ok, lines


def stale(name, g):
    """Why golden g no longer describes session name: a list of (fatal, reason)."""
    out = []
    rec = rec_of(name); cfg = R.cfg_of(rec)
    if g.get('format') != FORMAT: out.append((True, f'golden format {g.get("format")}, this tool writes {FORMAT}'))
    if g['recording']['sha256'] != sha256(rec): out.append((True, f'{rel(rec)} has changed since the golden was made'))
    gcfg = (g.get('cfg') or {}).get('sha256')
    if gcfg != (sha256(cfg) if cfg else None): out.append((True, 'the sound configuration has changed since the golden was made'))
    return out


def build_flag(g):
    """The replay DOS build has changed since golden g was made (flagged, not fatal)."""
    cur = exe_sha()
    if cur and cur != g['replay_build']['exe_sha256']:
        return ('the replay DOS build (build/replay/UW2.EXE) differs from the one that made the golden; '
                'regenerate it with replay.py golden (make test-full)')
    return None


def pool(jobs):
    return ThreadPoolExecutor(max_workers=max(1, jobs))


def cmd_golden(names, jobs, backend, check):
    backend = backend or ('dosbox-x' if _have_dosbox_x() else 'jsdos')
    if jobs is None: jobs = 8 if backend == 'dosbox-x' else 4
    exe = R.build(quiet=True)
    t0 = time.time()
    # load needs items' saved game: items' first DOS run, made here if items is not asked for
    need = list(names)
    for n in names:
        if n in STAGE_FROM and STAGE_FROM[n] not in need: need.insert(0, STAGE_FROM[n])
    runs = {}; results = {}

    def dos_run(name, k):
        if name in STAGE_FROM:
            src = runs[(STAGE_FROM[name], 1)].result()
            if src[0] != 0 or not os.path.isdir(os.path.join(src[1], 'SAVE1')):
                return (9, None, 0)
            st = os.path.join(WORK, name, f'stage{k}')
            shutil.rmtree(st, ignore_errors=True); os.makedirs(st)
            shutil.copytree(os.path.join(src[1], 'SAVE1'), os.path.join(st, 'SAVE1'))
        else: st = None
        d = os.path.join(WORK, name, f'dos{k}')
        shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
        t = time.time()
        rc = R.run_dos(d, rec_of(name), stage=st, log=os.path.join(WORK, name, f'dos{k}.log'), backend=backend, exe=exe)
        if rc == 0 and not os.path.exists(os.path.join(d, 'STATE.OUT')): rc = 8
        say(f'  {name}: DOS run {k} ({backend}) {"done" if rc == 0 else f"failed ({rc})"} in {time.time() - t:.1f} s')
        return (rc, d, time.time() - t)

    with pool(jobs) as ex:
        for n in need:
            if n not in names and port_only(n):         # a setup session: the port makes its saved game
                runs[(n, 1)] = ex.submit(port_stage_run, n, os.path.join(WORK, n, 'port1'))
                continue
            for k in ((1, 2) if n in names else (1,)):
                runs[(n, k)] = ex.submit(dos_run, n, k)
        for n in names:
            r1, r2 = runs[(n, 1)].result(), runs[(n, 2)].result()
            results[n] = finish_golden(n, r1, r2, backend, check)
    print(f'\n== golden ({backend}, {"check against the committed goldens" if check else "written"}), '
          f'{time.time() - t0:.1f} s in all')
    worst = 0
    for n in names:
        rc, msg = results[n]
        print(f'{n:8s} {msg}')
        worst = max(worst, rc)
    return worst


def finish_golden(name, r1, r2, backend, check):
    if r1[0] or r2[0]:
        return 1, f'FAILED: a DOS run did not finish (see {rel(os.path.join(WORK, name))}/dos1.log, dos2.log)'
    d1, d2 = r1[1], r2[1]
    # in processes of their own, since the other sessions' runs are printing meanwhile
    me = [R.PY, os.path.join(here, 'replay.py')]
    out = []
    def sub(*a):
        r = subprocess.run(me + list(a), capture_output=True, text=True)
        out.append(f'$ replay.py {" ".join(a)}\n' + r.stdout + r.stderr)
        return r.returncode
    same = sub('compare', d1, d2, '--same-build')
    sv = sub('saves', d1, d2) if (os.path.isdir(os.path.join(d1, 'SAVE1')) or os.path.isdir(os.path.join(d2, 'SAVE1'))) else 0
    nl = sub('nulls', d1)
    open(os.path.join(WORK, name, 'determinism.txt'), 'w').write('\n'.join(out))
    if same or sv:
        return 1, f'FAILED: the two DOS runs differ ({rel(os.path.join(WORK, name, "determinism.txt"))}); no golden written'
    if nl:
        return 1, f'FAILED: the null-pointer check ({rel(os.path.join(WORK, name, "determinism.txt"))})'
    snd = ''
    sc = os.path.join(d1, 'SNDCHECK.OUT')
    if os.path.exists(sc):
        snd = '; ' + open(sc, errors='replace').read().strip().split('\n')[0]
    ncks = len(R.read_dump(d1))
    timing = f'{r1[2]:.0f} s and {r2[2]:.0f} s'
    if check:
        g = load_golden(name)
        if not g: return 1, 'no committed golden to check against'
        ok, lines = compare_golden(name, g, d1, backend)
        if ok: return 0, f'DOS twice identical ({timing}), {ncks} checkpoints, identical to the committed golden{snd}'
        return 1, 'DIFFERS from the committed golden:\n' + '\n'.join('         ' + l for l in lines)
    stage_files = {}
    if name in STAGE_FROM:
        stage_files = save_hashes(os.path.join(WORK, name, 'stage1'))
    meta = golden_meta(name, d1, backend, stage_files)
    n = write_golden(name, d1, meta)
    return 0, f'DOS twice identical ({timing}), {n} checkpoints written to {rel(os.path.join(GOLDEN, name))}{snd}'


def _have_dosbox_x():
    x = os.environ.get('UW2_DOSBOX_X')
    return os.path.exists(x) if x else bool(shutil.which('dosbox-x'))


# ---- verify ---------------------------------------------------------------------------------

def cmd_verify(names, jobs, debug=False, cov=False):
    if jobs is None: jobs = min(8, os.cpu_count() or 4)
    variant = 'port-debug' if debug else 'port-cov' if cov else 'port'
    port = R.port_exe(os.path.join(root, 'build', variant, 'uw2port'))
    if not os.path.exists(port): sys.exit(f'replay.py verify: build the port first (build/{variant}: make port, '
                                          f'make port-debug or tools/portbuild.py --coverage)')
    if not R.have_toolchain():
        say('replay.py verify: no Turbo C++ (make setup), so no replay DOS build; not checking the goldens against it')
    else:
        try: R.build(quiet=True)      # the replay DOS build's hash, to flag goldens it did not make
        except SystemExit as e: say(f'replay.py verify: the replay DOS build failed ({e}); not checking the goldens against it')
    t0 = time.time()
    need = list(names)
    for n in names:
        if n in STAGE_FROM and STAGE_FROM[n] not in need: need.insert(0, STAGE_FROM[n])
    futs = {}; results = {}

    def one(name):
        g = load_golden(name)
        if not g: return 1, f'no golden (tests/replay/golden/{name}); make one with replay.py golden {name}', 0
        st = stale(name, g)
        if any(f for f, _ in st):
            return 3, 'STALE: ' + '; '.join(r for _, r in st) + f'; regenerate with replay.py golden {name}', 0
        stage = None
        if name in STAGE_FROM:
            src = STAGE_FROM[name]
            rc = futs[src].result()
            sd = os.path.join(VWORK, src, 'port')
            want = g['stage']['files']
            have = {k: v for k, v in save_hashes(sd).items() if k.startswith('SAVE1/')}
            if have != want:
                return 1, f'its stage, the saved game {src} writes, is not the golden\'s ({src} differs)', 0
            stage = os.path.join(VWORK, name, 'stage')
            shutil.rmtree(stage, ignore_errors=True); os.makedirs(stage)
            shutil.copytree(os.path.join(sd, 'SAVE1'), os.path.join(stage, 'SAVE1'))
        d = os.path.join(VWORK, name, 'port')
        shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
        t = time.time()
        rc = R.run_port(rec_of(name), d, ['--debug'] if debug else ['--cov'] if cov else [], stage=stage, quiet=True)
        dt = time.time() - t
        if not os.path.exists(os.path.join(d, 'STATE.OUT')):
            return 1, f'the port wrote no dumps (exit {rc}; {rel(os.path.join(d, "port.log"))})', dt
        ok, lines = compare_golden(name, g, d, 'port', diffdir=d)
        log = open(os.path.join(d, 'port.log'), errors='replace').read()
        for l in log.splitlines():
            if 'sound reads:' in l and ' 0 where' not in l:
                ok = False; lines.append(l.strip())
        if debug:
            ub = sorted(set(l for l in log.splitlines() if 'runtime error' in l))
            if ub: ok = False; lines.append(f'UBSan: {len(ub)} reports, e.g. {ub[0]}')
        notes = [x for x in [build_flag(g)] if x]
        n = len(g['checkpoints'])
        if ok:
            return 0, f'identical to DOS at all {n} checkpoints' + (f', saves identical' if g.get('saves') else '') + \
                ''.join('\n         note: ' + x for x in notes), dt
        return 1, 'DIFFERS:\n' + '\n'.join('         ' + l for l in lines) + \
            f'\n         (the dump is {rel(os.path.join(d, "STATE.OUT"))}; for byte ranges, replay in DOS and use replay.py compare)' + \
            ''.join('\n         note: ' + x for x in notes), dt

    def setup(name):
        return port_stage_run(name, os.path.join(VWORK, name, 'port'))[0]

    with pool(jobs) as ex:
        for n in need: futs[n] = ex.submit(setup if n not in names and port_only(n) else one, n)
        for n in names: results[n] = futs[n].result()
    print(f'== verify: {len(names)} sessions in the port{" (UBSan build)" if debug else " (coverage build)" if cov else ""} against the goldens, '
          f'{time.time() - t0:.1f} s')
    worst = 0
    for n in names:
        rc, msg, dt = results[n]
        print(f'{n:8s} {dt:5.1f} s  {"ok  " if rc == 0 else "FAIL"}  {msg}')
        worst = max(worst, rc)
    print('verify: all sessions identical to DOS' if worst == 0 else 'verify: FAILED')
    return worst


def parse(args):
    names, jobs, backend, check, debug, cov = [], None, None, False, False, False
    i = 0
    while i < len(args):
        a = args[i]
        if a == '-j': jobs = int(args[i + 1]); i += 2; continue
        if a.startswith('-j') and a[2:].isdigit(): jobs = int(a[2:]); i += 1; continue
        if a == '--backend': backend = args[i + 1]; i += 2; continue
        if a == '--check': check = True
        elif a == '--debug': debug = True
        elif a == '--cov': cov = True
        elif a == 'all': names += SESSIONS
        elif a in SESSIONS: names.append(a)
        else: sys.exit(f'replay.py: unknown session or option {a} (sessions: {" ".join(SESSIONS)})')
        i += 1
    if not names: names = list(SESSIONS)
    return list(dict.fromkeys(names)), jobs, backend, check, debug, cov


def main(cmd, args):
    names, jobs, backend, check, debug, cov = parse(args)
    if cmd == 'golden': return cmd_golden(names, jobs, backend, check)
    return cmd_verify(names, jobs, debug, cov)
