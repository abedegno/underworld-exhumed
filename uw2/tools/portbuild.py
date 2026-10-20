"""Build the native port (`make port`, docs/PORT.md): compile every game C source for the host as
tools/portcheck.py does, compile the port's own C, UW2's under src/port and the runtime's in
Exhume's runtime/port (tools/exhume.py, where it is: nothing is copied), and link them into
build/port/uw2port.

    python3 tools/portbuild.py          build and link; exit 1 if any step fails
    python3 tools/portbuild.py --run    then run it, passing UW2PORT_ARGS (docs/BUILDING.md)
    python3 tools/portbuild.py --debug  the debug build (make port-debug): -g and UBSan's
                                        -fsanitize=null, in build/port-debug/uw2port; a null
                                        dereference is reported with its file and line and the
                                        program goes on, so a replay lists every one it meets
    python3 tools/portbuild.py --release  the build the release packages are made from (make
                                        port-release), in build/port-release: as make port, but
                                        Nuked OPL3 is a shared library beside the program (so a
                                        user can replace it, as its LGPL asks) and the program
                                        looks for its libraries beside itself and in ../lib and
                                        ../Frameworks; --arch A (repeatable, macOS) builds for
                                        each architecture A, a universal binary
    python3 tools/portbuild.py --coverage  clang's source-based coverage, in
                                        build/port-cov/uw2port: each run writes a .profraw
                                        (LLVM_PROFILE_FILE), for llvm-profdata and llvm-cov, to
                                        see which of the port's routines the replays run
    python3 tools/portbuild.py --frame-single  the EMS frame's 64 KB mapped once, as the web
                                        build has it, in build/port-single/uw2port; the 64 KB
                                        after it fault, so a pointer the game's C runs past the
                                        frame's end is reported where it happens
    python3 tools/portbuild.py --web    the WebAssembly build (make web), with Emscripten's emcc
                                        (Exhume's tools/setup-emsdk.sh) and the libraries in
                                        tools/libs-web (SETUP_WEB=1 tools/setup-libs.sh): the
                                        page's program in build/web (uw2port.js and .wasm), and
                                        the same objects linked for Node.js in build/web-node,
                                        with uw2port there a script that runs it, so
                                        tools/replay.py replays the sessions in it (EXHUME_PORT)

The port's own C is compiled with its headers and the game's: UW2's bindings and headers in
src/port first, then the runtime's (docs/PORT.md, "The runtime"); the platform backend (SDL3,
the runtime's platform/sdl3) with SDL's flags from pkg-config, and the link takes SDL's
libraries. What the port does not replace yet is still the generated stubs (src/port/stubs,
written by tools/portstubs.py), and the port stops at the first one it reaches. The DOS build
is untouched.

The sound hardware's emulators are not in the repository (docs/BUILDING.md, "Sound"): Nuked
OPL3 is compiled from tools/nuked-opl3 when tools/setup-sound.sh has fetched it, and libmt32emu
is linked when pkg-config finds it (brew install mt32emu); without either the port builds and
that chip is silent. Libraries that tools/setup-libs.sh built into tools/libs are found without
setting PKG_CONFIG_PATH, and on Linux the program is linked to find them there when run.
"""
import os, re, sys, argparse, subprocess
from concurrent.futures import ThreadPoolExecutor

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
sys.path.insert(0, here)
import sources, portcheck, exhume

OUT = os.path.join(root, 'build', 'port')
PORT = os.path.join(root, 'src', 'port')
RT = exhume.PORT                # the runtime's port C (Exhume's runtime/port)
EXE = os.path.join(OUT, 'uw2port')
# The port's own C: C11, with the port's headers and the game's (port C that includes a game
# header includes compat.h first, and gets Borland's stand-in headers through the runtime's
# include). UW2's directory comes first, so its bindings (portgame.h, asmgame.h, ailgame.h) are
# the ones the runtime's headers include; then the runtime's directories, whose headers UW2's
# C includes as "port.h", "plat.h", "x86/asmrt.h", "sound/audio.h" and the generated stubs as
# "stub.h"; then the game's headers and the runtime's portable.h.
PORT_FLAGS = ['-x', 'c', '-std=gnu11', '-fsigned-char', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wno-comment', '-Wno-unused-function',
              '-Wno-pragma-pack', '-I', PORT, '-I', RT, '-I', os.path.join(RT, 'platform'),
              '-I', os.path.join(RT, 'include'), '-I', os.path.join(RT, 'sound'), '-I', os.path.join(RT, 'stubs'),
              '-iquote', os.path.join(root, 'src', 'include'), '-iquote', exhume.INCLUDE]
# The port's own C that only computes: the modules translated from the assembly, the machine
# they run on, and the graphics and renderer C written by hand. It shares nothing with another
# thread but what the platform layer reads to draw, so it is compiled with -O2, which makes the
# replays two to four times faster (docs/BUILDING.md, "Testing"); the rest of the port's C (the
# PIT and the other threads' code, the sound drivers, memory, the platform layer) and the game's
# C stay unoptimised. The debug and coverage builds compile everything without optimisation.
OPTIMISED = [os.path.join(d, x) + os.sep for d in (PORT, RT) for x in ('3d', 'gfx', 'x86')]
OPT = ['-O2']
# The one backend the port builds with today (docs/PORT.md, "The platform layer"): only files
# under src/port/platform/<backend>/ see its headers, and the link takes its libraries.
BACKEND = 'sdl3'


LIBS = os.path.join(root, 'tools', 'libs')
RELEASE = False
ARCHS = []      # -arch flags for a universal macOS build
WEB = []        # --web: the flags every object is compiled with (threads: shared memory)


def pkg_env():
    """The environment for pkg-config: tools/libs (tools/setup-libs.sh) searched first."""
    env = dict(os.environ)
    pc = os.path.join(LIBS, 'lib', 'pkgconfig')
    if os.path.isdir(pc):
        env['PKG_CONFIG_PATH'] = pc + (os.pathsep + env['PKG_CONFIG_PATH'] if env.get('PKG_CONFIG_PATH') else '')
    return env


def strip_rpaths(flags):
    """Link flags without any run path a .pc file adds (SDL3's sdl3.pc has
    -Wl,-rpath,${libdir}): the release program must look only beside itself, so a run path
    into the build tree (tools/libs/lib) cannot come before $ORIGIN/../lib."""
    out, skip = [], False
    for f in flags:
        if skip: skip = False; continue
        if f == '-Wl,-rpath' or f == '-rpath': skip = True; continue     # the path follows
        if f.startswith('-Wl,'):
            parts, keep, i = f[4:].split(','), [], 0
            while i < len(parts):
                if parts[i] in ('-rpath', '--rpath', '-R'): i += 2; continue
                if parts[i].startswith(('-rpath=', '--rpath=')) or parts[i] == '--enable-new-dtags': i += 1; continue
                keep.append(parts[i]); i += 1
            if keep: out.append('-Wl,' + ','.join(keep))
            continue
        out.append(f)
    return out


def pkg_config(*args):
    r = subprocess.run(['pkg-config'] + list(args) + [BACKEND], capture_output=True, text=True, env=pkg_env())
    if r.returncode:
        raise SystemExit(f'portbuild.py: pkg-config cannot find {BACKEND} (install SDL3: brew install sdl3, or make setup-libs on Linux)')
    return r.stdout.split()


def port_sources():
    """Every .c under src/port and the runtime's port directory but the stand-in headers, and
    the platform backends other than BACKEND."""
    out = []
    for top in (PORT, RT):
        plat = os.path.join(top, 'platform')
        for d, _, fs in os.walk(top):
            if d.startswith(os.path.join(top, 'include')): continue
            if d.startswith(plat + os.sep) and os.path.relpath(d, plat).split(os.sep)[0] != BACKEND: continue
            out += [os.path.join(d, f) for f in sorted(fs) if f.endswith('.c')]
    return sorted(out)


def port_object(out, path):
    """The object of port source PATH in build directory OUT: UW2's under port/, the runtime's
    under port/rt_ (two files may share a name, mem/ems.c and the runtime's mem/emm.c being
    near it)."""
    if path.startswith(RT + os.sep):
        return os.path.join(out, 'port', 'rt_' + os.path.relpath(path, RT).replace(os.sep, '_')[:-2] + '.o')
    return os.path.join(out, 'port', os.path.relpath(path, PORT).replace(os.sep, '_')[:-2] + '.o')


def is_backend(path):
    return any(os.path.join(top, 'platform', BACKEND) + os.sep in path for top in (PORT, RT))


NUKED = os.path.join(root, 'tools', 'nuked-opl3')


def sound_deps():
    """The sound emulators found: (flags for the runtime's sound/audio.c, link flags, extra sources)."""
    cflags, libs, extra = [], [], []
    if os.path.exists(os.path.join(NUKED, 'opl3.c')):
        cflags += ['-DAUDIO_HAVE_OPL', '-I', NUKED]
        extra.append(os.path.join(NUKED, 'opl3.c'))
    r = subprocess.run(['pkg-config', '--cflags', '--libs', 'mt32emu'], capture_output=True, text=True, env=pkg_env())
    if r.returncode == 0:
        flags = r.stdout.split()
        cflags += ['-DAUDIO_HAVE_MT32EMU'] + [f for f in flags if f.startswith('-I')]
        libs += [f for f in flags if not f.startswith('-I')]
    return cflags, libs, extra


def opl_library_name():
    """The release build's shared Nuked OPL3, by the host's convention."""
    if sys.platform == 'darwin': return 'libnukedopl3.dylib'
    if os.name == 'nt' or sys.platform in ('msys', 'cygwin') or os.environ.get('MSYSTEM'): return 'nukedopl3.dll'
    return 'libnukedopl3.so'


ICON = os.path.join(root, 'tools', 'dist', 'icon')


def windows_resource(cc):
    """On Windows, the program's icon (tools/dist/icon/uw2.ico) as a resource object for the
    link, compiled by llvm-windres (MSYS2 CLANG64's llvm package) or windres; [] elsewhere, or
    when neither is found (the program then has Windows's default icon)."""
    # decided by the compiler's target, as portcheck.layout_flags does: the Python running this
    # need not be a Windows one (in the release job it was not, and the icon was lost silently)
    if not portcheck.layout_flags(cc): return []
    import shutil
    tool = shutil.which('llvm-windres') or shutil.which('windres')
    print(f'portbuild.py: Windows target; resource compiler {tool or "none"}')
    if not tool:
        print('portbuild.py: no llvm-windres or windres; the program gets no icon')
        if RELEASE: sys.exit('portbuild.py: a release build for Windows needs its icon')
        return []
    rc, obj = os.path.join(OUT, 'uw2port.rc'), os.path.join(OUT, 'uw2port-res.o')
    ico = os.path.join(ICON, 'uw2.ico').replace(os.sep, '/').replace('\\', '/')
    with open(rc, 'w') as f: f.write(f'1 ICON "{ico}"\n')
    cmd = [tool, '-O', 'coff', '-i', rc, '-o', obj]
    if 'llvm' in os.path.basename(tool):        # for the compiler's target, not the tool's host
        m = subprocess.run([cc, '-dumpmachine'], capture_output=True, text=True).stdout.strip()
        if m: cmd.append('--target=' + m)
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
    if r.returncode:
        print(f'portbuild.py: {os.path.basename(tool)} failed; the program gets no icon\n{r.stderr[-1000:]}')
        if RELEASE: sys.exit('portbuild.py: a release build for Windows needs its icon')
        return []
    print(f'portbuild.py: icon resource {os.path.relpath(obj, root)}')
    return [obj]


def compile_port(cc, path, sound_cflags=()):
    if path.startswith(NUKED + os.sep):     # third-party: its own flags, optimised
        if RELEASE:                         # a shared library of its own, beside the program
            lib = os.path.join(OUT, opl_library_name())
            cmd = [cc] + ARCHS + ['-x', 'c', '-std=c99', '-O2', '-w', '-shared', '-o', lib, path]
            if lib.endswith('.dylib'): cmd += ['-dynamiclib', '-install_name', '@rpath/' + os.path.basename(lib)]
            elif lib.endswith('.so'): cmd += ['-fPIC', '-Wl,-soname,' + os.path.basename(lib)]
            else: cmd += ['-Wl,--out-implib,' + os.path.join(OUT, 'libnukedopl3.dll.a')]
            r = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
            return path, r.returncode, r.stderr if r.returncode else '', 'opl3-shared' if r.returncode == 0 else None
        obj = os.path.join(OUT, 'deps', os.path.basename(path)[:-2] + '.o')
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        r = subprocess.run([cc] + ARCHS + WEB + ['-x', 'c', '-std=c99', '-O2', '-w', '-c', '-o', obj, path],
                           capture_output=True, text=True, cwd=root)
        return path, r.returncode, r.stderr if r.returncode else '', obj if r.returncode == 0 else None
    obj = port_object(OUT, path)
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    extra = pkg_config('--cflags') if is_backend(path) else []
    if path.endswith(os.path.join('sound', 'audio.c')): extra = list(sound_cflags)
    # the ROM finder needs libmt32emu's flags alone, as Exhume's [[port.pkg]] gives them
    if path.endswith(os.path.join('sound', 'mt32roms.c')):
        extra = [f for i, f in enumerate(sound_cflags) if f != '-DAUDIO_HAVE_OPL'
                 and not (f == '-I' or (i and sound_cflags[i - 1] == '-I'))]
    opt = OPT if any(path.startswith(d) for d in OPTIMISED) else []
    # the game's struct layout (portcheck.layout_flags): port C that includes a game header
    # must see the same layout as the game's C
    r = subprocess.run([cc] + ARCHS + opt + PORT_FLAGS + portcheck.layout_flags(cc) + extra + ['-c', '-o', obj, path],
                       capture_output=True, text=True, cwd=root)
    return path, r.returncode, r.stderr, obj if r.returncode == 0 else None


# wasm-ld's signature mismatches the web build accepts, each with its reason (any other fails it)
WEB_ALLOW_MISMATCH = {
    'valloc': "UW2's valloc (src/port/gfx/valloc.c) replaces the C library's weak one, as on the desktop; "
              "the library never calls it",
}


def web_mismatches(stderr):
    """wasm-ld's signature mismatches in a link's messages: a direct call whose declaration
    differs from the definition is linked as a trap, with only a warning, so each is a failure
    unless WEB_ALLOW_MISMATCH names it. True when one is not. As Exhume's tools/portbuild.py."""
    bad = []
    for b in re.split(r'\n(?=wasm-ld: )', stderr):
        m = re.match(r'wasm-ld: warning: function signature mismatch: (\S+)', b)
        if not m: continue
        if m.group(1) in WEB_ALLOW_MISMATCH:
            print(f'portbuild.py: signature mismatch {m.group(1)} allowed: {WEB_ALLOW_MISMATCH[m.group(1)]}')
        else: bad.append(b.strip())
    if bad:
        print('portbuild.py: the link makes these calls traps (a declaration differs from the definition; '
              'an adapter, or WEB_ALLOW_MISMATCH with the reason):\n' + '\n'.join(bad))
    return bool(bad)


def link_web(cc, objs, libs):
    """--web's link, twice from the same objects: the page's program (EXE, uw2port.js and .wasm
    in build/web: a module the page starts itself, with the file system's IDBFS for the saved
    games) and a Node.js one in build/web-node, which reads and writes the host's files
    (NODERAWFS), with the script uw2port beside it that runs it under node. As Exhume's
    tools/portbuild.py --web."""
    # EMULATE_FUNCTION_POINTER_CASTS: the game's C calls functions through pointers of other
    # types, which Turbo C's and the desktop's calling conventions let through and
    # WebAssembly's call_indirect traps on ("function signature mismatch")
    common = ['-pthread', '-O2', '-sPTHREAD_POOL_SIZE=4', '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=128MB',
              '-sSTACK_SIZE=1MB', '-sDEFAULT_PTHREAD_STACK_SIZE=1MB', '-sEXIT_RUNTIME=1',
              '-sEMULATE_FUNCTION_POINTER_CASTS=1']
    name = os.path.basename(EXE)
    page = [cc, '-o', EXE] + objs + libs + common + ['-lidbfs.js', '-sENVIRONMENT=web,worker', '-sMODULARIZE=1',
            '-sEXPORT_NAME=uw2port', '-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,UTF8ToString',
            '-sINVOKE_RUN=0', '-sEXPORTED_FUNCTIONS=_main,_web_open_settings,_exhume_quit,_audio_underruns,_web_test_stop']
    nodeout = os.path.join(root, 'build', 'web-node'); os.makedirs(nodeout, exist_ok=True)
    node = [cc, '-o', os.path.join(nodeout, name)] + objs + libs + common + ['-sENVIRONMENT=node', '-sNODERAWFS=1']
    for cmd in (page, node):
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
        if r.returncode:
            print(f'link failed ({os.path.relpath(cmd[2], root)})\n' + r.stderr[-3000:])
            return 1
        if web_mismatches(r.stderr): return 1
    # Emscripten's output is CommonJS, which node reads as an ES module under a package.json
    # that says "type": "module" (UW2's, for its tools' .mjs): this directory says otherwise
    with open(os.path.join(nodeout, 'package.json'), 'w') as f: f.write('{"type": "commonjs"}\n')
    wrap = os.path.join(nodeout, 'uw2port')
    with open(wrap, 'w') as f: f.write(f'#!/bin/sh\nexec node "{os.path.join(nodeout, name)}" "$@"\n')
    os.chmod(wrap, 0o755)
    print(f'linked {os.path.relpath(EXE, root)} and {os.path.relpath(wrap, root)}')
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description='Build and link the native port.')
    ap.add_argument('--cc', default=portcheck.host_cc())
    ap.add_argument('--run', action='store_true')
    ap.add_argument('--debug', action='store_true')
    ap.add_argument('--coverage', action='store_true')
    ap.add_argument('--frame-single', action='store_true', help='the EMS frame mapped once, as WebAssembly has it (a desktop check)')
    ap.add_argument('--release', action='store_true')
    ap.add_argument('--arch', action='append', default=[])
    ap.add_argument('--web', action='store_true', help='the WebAssembly build (emcc): the page\'s and a Node.js one')
    a = ap.parse_args(argv)
    global OUT, EXE, RELEASE, ARCHS, LIBS, WEB
    link_extra = []
    global OPT
    if a.debug or a.coverage: OPT = []
    if a.debug:
        OUT = os.path.join(root, 'build', 'port-debug')
        EXE = os.path.join(OUT, 'uw2port')
        portcheck.OUT = OUT
        san = ['-g', '-fsanitize=null']
        portcheck.FLAGS = portcheck.FLAGS + san
        PORT_FLAGS.extend(san)
        link_extra = ['-fsanitize=null']
    if a.coverage:
        OUT = os.path.join(root, 'build', 'port-cov')
        EXE = os.path.join(OUT, 'uw2port')
        portcheck.OUT = OUT
        cov = ['-fprofile-instr-generate', '-fcoverage-mapping']
        portcheck.FLAGS = portcheck.FLAGS + cov
        PORT_FLAGS.extend(cov)
        link_extra = ['-fprofile-instr-generate']
    if a.frame_single:
        OUT = os.path.join(root, 'build', 'port-single')
        EXE = os.path.join(OUT, 'uw2port')
        portcheck.OUT = OUT
        portcheck.FLAGS = portcheck.FLAGS + ['-DPORT_FRAME_SINGLE']
        PORT_FLAGS.append('-DPORT_FRAME_SINGLE')
    if a.release:
        RELEASE = True
        OUT = os.path.join(root, 'build', 'port-release')
        EXE = os.path.join(OUT, 'uw2port')
        portcheck.OUT = OUT
    if a.arch:
        ARCHS = [f for x in a.arch for f in ('-arch', x)]
        portcheck.FLAGS = portcheck.FLAGS + ARCHS
    if a.web and (a.debug or a.coverage or a.release or a.arch or a.frame_single or a.run):
        sys.exit('portbuild.py: --web is a build of its own (no --debug, --coverage, --release, --arch, --frame-single or --run)')
    if a.web:
        # the EMS frame mapped once (port.h turns PORT_FRAME_SINGLE on under __EMSCRIPTEN__);
        # threads (the PIT, sound) are Web Workers on shared memory, which every object must
        # be compiled for; everything -O2
        import shutil
        a.cc = shutil.which('emcc') or sys.exit('portbuild.py: --web needs emcc (. ~/emsdk/emsdk_env.sh; Exhume\'s tools/setup-emsdk.sh)')
        OUT = os.path.join(root, 'build', 'web')
        portcheck.OUT = OUT
        EXE = os.path.join(OUT, 'uw2port.js')
        # the build's own paths kept out of the program (__FILE__ in the null traps): the page is
        # published (web/deploy.sh refuses a site naming a local path)
        WEB = ['-pthread', f'-ffile-prefix-map={root}/=', f'-ffile-prefix-map={exhume.EXHUME}/=exhume/']
        portcheck.FLAGS = portcheck.FLAGS + WEB + ['-O2']
        PORT_FLAGS.extend(WEB)
        LIBS = os.path.join(root, 'tools', 'libs-web')
        if not os.path.isdir(os.path.join(LIBS, 'lib', 'pkgconfig')):
            sys.exit('portbuild.py: --web needs SDL3 and libmt32emu built for the web in tools/libs-web '
                     '(SETUP_WEB=1 sh ../exhume/tools/setup-libs.sh tools/libs-web)')
    exhume.need()
    os.makedirs(OUT, exist_ok=True)
    game = [p for p in sources.all_sources() if p.upper().endswith('.C') and not portcheck.dos_only(p)]
    game += sources.replay_sources()     # the record and replay hooks' code, shared with the replay DOS build
    snd_cflags, snd_libs, snd_extra = sound_deps()
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        gres = list(ex.map(lambda p: portcheck.compile_one(a.cc, p), game))
        pres = list(ex.map(lambda p: compile_port(a.cc, p, snd_cflags), port_sources() + snd_extra))
    bad = [(p, e) for p, rc, e, o in gres + pres if not o]
    for p, e in bad:
        print(f'{os.path.relpath(p, root)}: does not compile\n' + '\n'.join(l for l in e.split('\n') if 'error' in l)[:2000])
    if bad: return 1
    objs = [o for p, rc, e, o in gres + pres if o != 'opl3-shared']
    print(f'runtime: {exhume.EXHUME}')
    print(f'compiled {len(gres)} game sources and {len(pres)} port sources'
          f' (layout flags: {" ".join(portcheck.layout_flags(a.cc)) or "none"})')
    warn = [(p, e) for p, rc, e, o in pres if o and 'warning' in e]
    for p, e in warn:
        print(f'{os.path.relpath(p, root)}: warnings\n' + '\n'.join(l for l in e.split('\n') if 'warning' in l)[:2000])
    print('sound: ' + ', '.join([('Nuked OPL3' if '-DAUDIO_HAVE_OPL' in snd_cflags else 'no OPL emulator (tools/setup-sound.sh)'),
                                 ('libmt32emu' if '-DAUDIO_HAVE_MT32EMU' in snd_cflags else 'no libmt32emu (brew install mt32emu, or make setup-libs)')]))
    # the sound drivers' threads: in the C library on macOS and on Linux's glibc 2.34 and later,
    # in winpthreads on Windows (MinGW), which -pthread links
    threads = [] if sys.platform == 'darwin' else ['-pthread']
    rpath = []
    if RELEASE:
        # the libraries beside the program, or in the package's lib (Linux) or Frameworks (macOS)
        if '-DAUDIO_HAVE_OPL' in snd_cflags: snd_libs = ['-L', OUT, '-lnukedopl3'] + snd_libs
        if sys.platform == 'darwin':
            rpath = ['-Wl,-rpath,@executable_path', '-Wl,-rpath,@executable_path/../Frameworks', '-Wl,-headerpad_max_install_names']
        elif sys.platform.startswith('linux'):
            # only these: RUNPATH (new dtags), so LD_LIBRARY_PATH can still override it
            rpath = ['-Wl,--enable-new-dtags', '-Wl,-rpath,$ORIGIN', '-Wl,-rpath,$ORIGIN/../lib']
    elif sys.platform.startswith('linux') and any(f.startswith('-L' + LIBS) for f in pkg_config('--libs') + snd_libs):
        rpath = ['-Wl,-rpath,' + os.path.join(LIBS, 'lib')]     # runs without LD_LIBRARY_PATH
    libs = pkg_config('--libs') + snd_libs
    if not a.web and portcheck.layout_flags(a.cc): libs += ['-lws2_32', '-Wl,--wrap=plat_audio_open']  # co-op transport/audio on Windows
    if a.web: return link_web(a.cc, objs, libs)
    if RELEASE and sys.platform.startswith('linux'): libs = strip_rpaths(libs)
    objs += windows_resource(a.cc)
    r = subprocess.run([a.cc, '-o', EXE] + ARCHS + link_extra + objs + libs + threads + rpath,
                       capture_output=True, text=True, cwd=root)
    if not os.path.exists(EXE) and os.path.exists(EXE + '.exe'): EXE += '.exe'     # Windows
    if r.returncode:
        und = sorted(set(re.findall(r'"_?([A-Za-z_]\w*)", referenced from', r.stderr)))
        print(f'link failed: {len(und)} undefined names' + (': ' + ' '.join(und) if und else ''))
        print(r.stderr[-3000:])
        return 1
    print(f'linked {os.path.relpath(EXE, root)} ({os.path.getsize(EXE)} bytes)')
    if portcheck.layout_flags(a.cc):                    # a Windows target: is the icon in?
        import shutil
        if r.stderr.strip(): print('portbuild.py: the linker said:\n' + r.stderr[-2000:])
        ro = shutil.which('llvm-readobj')
        if ro:
            res = subprocess.run([ro, '--coff-resources', EXE], capture_output=True, text=True).stdout
            print(f'portbuild.py: resources in the program: {res.count("Type: ICON")} icon, '
                  f'{res.count("Type: GROUP_ICON")} icon group')
            if RELEASE and 'Type: ICON' not in res:
                print('portbuild.py: link command: ' + ' '.join([a.cc, '-o', EXE] + ARCHS + link_extra + objs[-3:] + libs + threads + rpath))
                sys.exit('portbuild.py: the Windows release program has no icon')
    if a.run:
        r = subprocess.run([EXE] + os.environ.get('UW2PORT_ARGS', '').split(), cwd=root)
        print(f'exit status {r.returncode}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
