# Porting UW2 to modern systems

This page is the design for a native port of UW2 built from the matched sources. Milestones 1 to 5 are done: the C compiles on a modern host with no errors, `make port` links it into a native binary (on macOS, Linux and Windows), the binary boots the real game data in an SDL3 window and plays its music and effects through C versions of the game's own sound drivers, and twelve sessions recorded in DOS replay in the port with the same game state and the same video memory as DOS at every checkpoint: from boot through the title cutscene, the main menu and character creation into the game, a female character with the last portrait, walking and looking around its first rooms in the 3D view, fighting with a Sound Blaster, an FM chip or a Roland MT-32, picking things up, opening a container and the automap, writing a map note, saving and restoring, talking to Nystul, loading the saved game from the main menu, the whole introduction, the introduction again with the game's clock past FFFFh ticks, and the first conversation with Lord British with the cutscene and scheduled events that follow it. The saves the port writes are byte for byte the ones DOS writes. The 3D renderer is seg004 and the parts of seg003 it uses, translated from the matched assembly instruction by instruction; each routine of the graphics library is either that translation or C written by hand, never both. Milestone 6a made that test fast enough to run on every push: each session has a golden reference made from DOS, the port replays every session against its golden in a few seconds, single routines are fuzzed against the original's bytes, and `make test` runs it all with the gate in about twenty seconds. Milestone 6 is under way: the port builds on Linux and Windows too, the replays pass on Linux, CI replays them on Windows and macOS against the same goldens, and tagged versions are packaged for macOS (signed and notarised), Linux (a tarball and an AppImage, for glibc 2.35 on) and Windows as draft releases ([BUILDING.md](../../docs/BUILDING.md#releases)). The results of each milestone are below, followed by the plan for the rest of Milestone 6 and the phase 2 enhancements. What the later milestones describe does not exist yet.

The half of the port that is not UW2's own (the portability layer, the platform layer and its SDL3 backend, the emulated VGA, EMS memory and PIT, Borland's library, the sound library and its drivers, the x86 machine the translated modules run on, and record and replay with the black box) is [Exhume](https://github.com/abedegno/Exhume)'s runtime, which this repository's builds compile where it is, from an Exhume checkout ([The runtime](#the-runtime), below). It was written here and moved there so that UW2's port and a UW1 port build on one harness. The milestone sections below name the files where they were at the time.

## Goals and non-goals

The port comes in two phases. Phase 1 is a faithful port, and phase 2 adds enhancements on top of it.

Phase 1 runs the original game as it was. It draws the original 320 by 200 picture with 256 colours, keeps the original timing (the 256 Hz game clock, the 18.2 Hz BIOS tick the game assumes, the 16 Hz effects timer), plays the original music and sound, and shows the result in a scaled window. Save files and data files stay byte-compatible with DOS, so a save moves between the port and DOS in both directions. The likely bugs in [FINDINGS.md](FINDINGS.md) are reproduced by default, and each fix becomes an option later.

Phase 2 adds options to the same port where they are cheap, e.g. integer scaling, aspect correction, mouselook and the FINDINGS fixes. A higher resolution or widescreen 3D view needs a new renderer, and that work goes in a fork that keeps phase 1 as its reference. Chocolate Doom and Crispy Doom split their work the same way.

The first platform was macOS on Apple Silicon. The code is 64-bit clean, and Linux (x86-64 and arm64) and Windows (x86-64, MSYS2's CLANG64) needed only build changes and the few fixes in [BUILDING.md](BUILDING.md#linux-and-windows). Where it costs little, the platform layer and the renderer are kept independent of UW2, so that a later UW1 decompilation can use them too.

These are not goals:

- emulating the DOS executable, or bundling DOSBox;
- shipping game data, or any build that embeds it (users bring their own copy; the repository and the binary hold none);
- changing the matched sources in a way that changes `UW2.EXE` (the gate keeps proving the DOS build);
- networking, new content, or mod tools in phase 1.

## One tree, two builds

The DOS build and the port are built from the same sources. The SM64 decompilation works this way too, and its PC port is a second build of the same tree.

The DOS build is unchanged. `make check` still compiles every source with Turbo C++ 1.01 and TASM in DOS, and still proves the exact link and the modding build. The port compiles the same C files with a modern compiler, swaps the assembly modules for C written for the port, and links them with a platform layer.

Every change to a shared source has to pass the gate. So a change made for the port must compile to the same bytes under Turbo C. Most portability changes can do that, because a typedef, a macro that expands to the original tokens, or a prototype that agrees with the definition costs no bytes. [MATCHING.md](MATCHING.md) lists what Turbo C allows. A change that would alter the DOS bytes goes in port-only code, never in a shared source.

### Directory layout

| Path | What is in it | Seen by the DOS build |
| --- | --- | --- |
| `src/<subsystem>/*.C` | the game's C, shared by both builds | yes |
| `src/<subsystem>/*.ASM` | the original assembly, DOS build only | yes |
| `src/include/` | the shared headers | yes |
| `src/include/hookgame.h`, `src/include/rpgame.h` | UW2's bindings for the runtime's `portable.h` (the original tokens of the record and replay hooks) and `replay.c` (what the hooks read, how the game shuts down, the dump's sections); 8.3 names, since the DOS builds stage them | yes (`rpgame.h`: only the replay build) |
| `src/port/portgame.h`, `asmgame.h`, `ailgame.h` | UW2's bindings for the runtime's port C: the program's name and main, DGROUP and `_ctype`, the exit chain, the black box, the window's title and icon, how the game's folder is found, the far data blocks (`portgame.h`); the translated modules' code blocks and seg003's own dispatcher (`asmgame.h`); the game's declarations of the AIL API and the FM extension (`ailgame.h`) | no |
| `src/port/3d/`, `src/port/gfx/`, `src/port/sys/`, `src/port/sound/` | C written for the port to replace UW2's assembly modules and DOS-only C files, named after the module each replaces (the startup and input of seg021, the mouse driver, the divide trap's chain, EMS.C's entry points, the graphics library, the renderer's glue); the modules translated by `tools/asm2c.py` are here too (`3d/interp.c` .. `gfx/grlibn.c`, with a `_x` suffix where a hand-written file has the module's other routines: see One implementation per routine); `sound/tvfx.c` is the FM drivers' time-variant effects, translated from DM03.ADV, plugged into the runtime's yamaha.c; `sys/main.c` is the port's main and its options | no |
| `src/port/x86/` | where the translated modules meet the hand-written C (`glue.c`), the renderer's divide fault handlers (`divfault.c`), and the table of translated modules (`modtab.c`, written by `tools/asm2c.py`) | no |
| `src/port/3d/render.h` | the render interface: the frame buffer the renderer draws into and the per-object sprite hook | no |
| `src/port/mem/` | the far data loaded from the user's EXE and its check (`fardata.c`), EMS.C's entry points on the runtime's EMS memory (`ems.c`), and what DOS reads through a null pointer (`nulls.c`) | no |
| `src/port/platform/sdl3/icon.h` | the window's icon (`PLAT_ICON`), written by `tools/dist/icon/make-icons.py` | no |
| `src/port/stubs/` | the link stubs, one file per source of the names (written by `tools/portstubs.py`); a replacement deletes its stubs | no |
| `tools/exhume.py` | where the Exhume checkout is (the repository's submodule `exhume/`, whose commit is the one this tree is proved with) | no |
| Exhume's `runtime/` | the runtime: `include/portable.h`, `replay/replay.c`, and `port/` (compat.h and the stand-in headers, port.h, the platform layer and its SDL3 backend, the paragraph map and far heap, the EMS memory, the VGA, Borland's library, the black box, the crash handler, finding the game and the settings file, the PIT, the sound library and its drivers, the x86 machine, the stubs' body) | `portable.h` (staged), `replay.c` (only the replay build) |
| `tools/portcheck.py` | the compile measurement (`make port-check`) | no |
| `tools/portbuild.py` | the port build and link (`make port`) | no |
| `tools/portstubs.py` | writes `src/port/stubs/` from the names a link still needs and the port's C does not define | no |
| `tools/portshot.py` | the screen comparison with DOS (Milestone 3's exit test) | no |
| `tools/replay.py`, `tools/replaydos.mjs` | the replay DOS build, recording and replaying sessions in DOS and the port, and comparing the state dumps (Milestone 4) | no |
| `tools/asm2c.py` | translates the renderer's and the graphics library's assembly modules to C (the 3D renderer, below) | no |
| `tools/widths.py` | rewrites integer declarations to the explicit widths | no |
| `tools/layoutcheck.py` | compares every struct's layout under Turbo C (in DOS) and the port | no |
| `tools/intaudit.py` | the promotion and overflow audit, on clang's AST | no |
| `tools/setup-sound.sh` | fetches Nuked OPL3 into `tools/nuked-opl3` (ignored by git) | no |
| `tools/setup-port.sh`, `tools/setup-libs.sh` | install what the port needs to build, per host (`make setup-port`) | no |
| `tools/package.py`, `tools/dist/` | the release packages (`make package`, `.github/workflows/uw2-release.yml`) | no |
| `tools/ailcheck.py` | the port's sound drivers against the real `.ADV` files, in an x86 emulator | no |
| `build/port/` | port objects and logs, never committed | no |

The DOS tools never pick up port files, for three reasons:

- `tools/sources.py` lists every `.C` and `.ASM` under `src/` except `src/include` and `src/port`, so the gate, the link and the modding build never compile a port file.
- The other way round, a game source whose header comment says `port: dos-only` is never compiled by the port. Only EMS.C says it: its int 67h calls are replaced by the port's EMS emulation.
- `tools/tcc.mjs` copies only `src/include` and the runtime's `portable.h` into the DOS build directory, so the stand-in headers never shadow Turbo C's own.
- `tools/srcdeps.py` hashes only those headers, so editing a port header never recompiles a DOS object (editing the runtime's `portable.h` recompiles every source that includes it, and the gate proves it costs no byte).

Port files use lower-case `.c` names, and each one starts with a comment naming the module it replaces.

## The runtime

The port's generic half is Exhume's `runtime/`, compiled where it is: nothing of it is copied into this repository. `tools/exhume.py` finds the checkout: `$EXHUME`, else the repository's submodule `../exhume` (an empty one is an error: `git submodule update --init`), else `.exhume`, else `~/Exhume`; `tools/tcc.mjs` finds it the same way. The submodule's commit is the one this tree was last proved with, and CI checks out exactly that commit. [BUILDING.md](BUILDING.md#exhume) has the set-up.

The builds take the runtime in place:

- **The gate and every DOS build** stage the runtime's `include/portable.h` beside `src/include`, so `uw2.h`'s `#include "portable.h"` finds it, and hash it with the sources that include it (`tools/srcdeps.py`). `portable.h` includes `hookgame.h` first: UW2's original tokens for the hooks. Under Turbo C every macro is the original tokens, so the gate proves the runtime's `portable.h` costs no byte.
- **The replay DOS build** compiles the runtime's `replay/replay.c` (`tools/sources.py`'s `replay_sources`), which includes UW2's `rpgame.h`, and links it in as one more resident module.
- **The port** (`tools/portbuild.py`) compiles the game's C with the runtime's `compat.h` force-included and its stand-in headers, and the port's own C from both trees: `src/port` and the runtime's `port/`, sorted together, the runtime's objects named `rt_...` in `build/port/port/`. The include path puts `src/port` first, so the runtime's headers find UW2's bindings (`portgame.h`, `asmgame.h`, `ailgame.h`), then the runtime's `port/`, `port/platform`, `port/include`, `port/sound` and `port/stubs`, so UW2's C names the runtime's headers as `"port.h"`, `"plat.h"`, `"x86/asmrt.h"` and `"sound/audio.h"`, and the generated stubs `"stub.h"`. The translated modules, their machine and the graphics C are `-O2`, in either tree.
- `tools/portcheck.py`, `portstubs.py`, `layoutcheck.py`, `intaudit.py`, `fuzzasm.py` and `coverage.py` see the same trees; `make coverage` counts the runtime's files under `runtime/`.

What stays here is UW2's own: the bindings above; the far data loader and its check of `UW2.EXE` (`mem/fardata.c`); EMS.C's entry points (`mem/ems.c`, on the runtime's `emm.c`); seg021's start-up, keyboard and mouse driver (`sys/sysentry.c`, `keyqueue.c`, `mousedrv.c`); the divide trap's chain into the game (`sys/int0trap.c`); the port's main and its options (`sys/main.c`); the C written for the graphics library and the renderer (`3d/render.c`, whose `cRender` calls the runtime's 3D frame pacing) and the translated modules; the glue and the divide fault handlers (`x86/`); the FM drivers' TVFX (`sound/tvfx.c`); and what DOS reads through UW2's null pointers (`mem/nulls.c`). Each holds UW2's facts or is translated from UW2's code, which Exhume never holds. A fix to the runtime is made in Exhume, proved there on UW2 (`examples/uw2/prove-port.sh`), and taken here by moving the `exhume` submodule to it.

## The portability layer

The portability layer is the runtime's `port/compat.h` and its stand-in headers (`port/include`). The port compiles every game source with `-include` of that `compat.h` and `-I` of the stand-ins, so the game sources need no `#ifdef` to get it. The DOS build never sees these files. `compat.h` includes UW2's `src/port/portgame.h` after the host's headers.

The layer does these things:

- It defines away Turbo C's memory-model keywords (`far`, `near`, `huge`, `interrupt`, `cdecl`, `pascal`).
- It turns `MK_FP`, `FP_SEG` and `FP_OFF` into calls into the paragraph map (below).
- It maps the pseudo-registers `_AX`, `_AH` and the rest onto a register file that the port's interrupt emulation reads and writes.
- It gives Borland's values to the `open` flags and permission bits. The sources also write them as numbers, e.g. `open(name, 0x8001)` in MISCUTIL.C and the mode `0x80` in ARC.C, and Borland's `O_RDONLY` is 1 where the host's is 0.
- It renames the Borland library calls whose host versions behave differently to `bc_` names, so that the port supplies Borland's behaviour. These are `open`, `read`, `write`, `close`, `lseek`, `creat`, `access`, `unlink`, `mkdir`, `chdir`, `stat`, `fstat`, `fopen`, `rand`, `srand`, `time` and `clock`. The reasons are DOS paths and case, Borland's text mode and flags, one-argument `mkdir`, Borland's `rand` (its own generator with `RAND_MAX` 7FFFh, called 272 times in the game), `clock` at 18.2 Hz, and the need for the replay harness to control `time` and `rand`.
- It declares Borland's extra library names (`itoa`, `ltoa`, `strupr`, `strnicmp`, `max`, `min`, `environ`) and Borland's `_ctype` table, which GAMESTRN.C reads directly.
- It renames the game's `main` to `uw2_main` (`PORT_GAME_MAIN` in `portgame.h`), because the port's own `main` (`src/port/sys/main.c`) sets up the platform and runs the game's on a thread of its own, and `exit` to `bc_exit`, which runs the termination chain seg021 hooked and ends the program through the platform layer.
- It packs every struct the game declares (`#pragma pack(1)`), as Turbo C does with no `-a`, after it has included every host header the game uses, so the host's own structs keep their alignment. Port C that includes a game header must include `compat.h` first.

The game's translation units see only ISO C and POSIX 2008 names from the host (`-D_POSIX_C_SOURCE=200809L`). Without that, macOS's headers declare `valloc`, which collides with VALLOC.ASM's routine of the same name.

The shared sources also include `portable.h` (the runtime's), through `uw2.h`. It holds what both builds need: under Turbo C (`__TURBOC__`) every name in it is the original type or the original tokens, so the DOS bytes cannot change, and on the host it takes the host's meaning. It has the explicit-width types (`int16`, `uint16`, `int32`, `uint32`), `NEARPTR` and `UNEARPTR`, `OLDSTYLE`, `FAR_COPY`, `NULLTRAP` and `FARNULLTRAP`, `HOST_LAYOUT_BEGIN` and `HOST_LAYOUT_END`, and `AX_RESULT`, for a function the original wrote with no return statement whose callers use what it left in AX (an empty statement under Turbo C, `return` of that value on the host). Each is described below where it is used.

## Memory model and data layout

UW2 is a 16-bit medium-model program. Code is far, data is near unless a declaration says `far`, `int` is 16 bits and `long` is 32. On macOS on Apple Silicon, `int` is 32 bits, `long` is 64 bits and pointers are 64 bits. Each difference below is a place where the port can silently compute something DOS does not.

### Integer widths and wrap

Turbo C's `int` wraps at 16 bits, and the game relies on that. For example, headings are words with 10000h to the turn, so adding two headings wraps for free. Another example is `move_along` in UTIL.C, which multiplies a sine by a distance in an `int`.

The port handles this in two parts.

The first part is explicit widths. `portable.h` defines `int16`, `uint16`, `int32` and `uint32`. Under Turbo C they are `int`, `unsigned`, `long` and `unsigned long`, so the gate proves that a declaration written with them compiles to the same bytes. On the host they are `int16_t` and the rest, so a stored value wraps as it did in DOS and `long` is 32 bits again. `tools/widths.py` rewrote the declarations by this policy:

- `long` and `unsigned long` become `int32` and `uint32` everywhere, since the host's `long` differs from DOS's both in storage and in arithmetic.
- Every struct and union field, every file-scope variable (globals, statics and `extern` declarations) and every static local is written with an explicit width. Their storage is what files, saves and other modules see.
- The pointee of every pointer to an integer, and the element of every array of them, is explicit everywhere (locals, parameters, return types and casts), because it is DOS-layout data and it sets the stride.
- `sizeof` of an integer type is explicit, so `sizeof(int16)` is 2 as `sizeof(int)` was in DOS.
- A scalar local, parameter or return type stays plain `int` or `unsigned`. Any width of at least 16 bits holds its values. The exceptions are the locals and parameters whose address is passed as an `int16 *` (clang finds them), and the ones the promotion audit shows depend on the 16-bit wrap.
- `char` and its signed and unsigned forms never change.

The second part is the promotion audit. C promotes a 16-bit operand to the host's 32-bit `int` before arithmetic. A result that is stored back into 16 bits is truncated and comes out as in DOS. A result that is compared, divided, shifted right or converted to a wider type before it is stored can differ. A `uint16` is also promoted to a signed `int`, so `u - 1 > 0` is true in DOS for `u == 0` and false on the host. `tools/intaudit.py` walks clang's AST (`-Xclang -ast-dump=json`). It gives each expression the type Turbo C would give it, computes the range of its true value from the ranges of its leaves, and lists every expression whose value can leave its DOS type's range before it is truncated, with where the value goes. Each listed site either gets a cast that gives the DOS result, proved by the gate, or is recorded. The differential replay test (below) catches what the audit misses.

The alternative of defining `int` as `short` with a macro was rejected. It breaks `long int` and the host's headers, it does not stop promotion, and it hides the types from the debugger.

`char` is signed in Turbo C and on Apple Silicon, and the port passes `-fsigned-char` so that other hosts agree.

### Far pointers and segments

In the port every pointer is a flat 64-bit host pointer. The sources still build far pointers from segment and offset, take them apart, and do arithmetic on segment numbers. Examples:

- `MK_FP(EmsBuff + 0x800, off)` addresses page 2 of the EMS page frame (`EmsBuff` is a segment number, and 800h paragraphs is 32 KB);
- `MK_FP(FP_SEG(stdat) + 0x134, 0)` in CUTS.C, and `FP_SEG(drv_mem[n]) + 1` in SOUND.C;
- `movedata(FP_SEG(src), FP_OFF(src), FP_SEG(dst), FP_OFF(dst), n)`, 34 lines in 14 files;
- `(unsigned)list < FP_OFF(LastActiveMob)` in SCDEVENT.C, which compares far pointers by their offsets only, as Turbo C does for `<`.

The port keeps a paragraph map. The map is a table of regions, and each region is a block of host memory with a range of virtual paragraph numbers. The EMS page frame, the workspace, every `farmalloc` block and each far data array (`stdat` and the rest of FARDATA.ASM, the graphics and renderer data) is a region. `MK_FP(seg, off)` finds the region that holds linear address `seg * 16 + off` and returns the host address. `FP_SEG` and `FP_OFF` find the region that holds a host pointer and return its paragraph and the offset from it. So segment arithmetic inside a region works as in DOS. A pointer that is in no region, e.g. a near DGROUP array passed to `movedata`, gets a temporary region based at that pointer, so that `MK_FP(FP_SEG(p), FP_OFF(p) + n)` is `p + n`. Debug builds report each temporary region, so that the idiom can be replaced with a named macro later.

The common idioms are macros whose Turbo C expansion is the original tokens, so they compile to the same bytes. `FAR_COPY(dst, src, n)` in `portable.h` expands to `movedata(FP_SEG(src), FP_OFF(src), FP_SEG(dst), FP_OFF(dst), n)`, and 32 of the 34 `movedata` calls use it. On the host it is a forward byte copy with no paragraph map lookup. The other two stay as they are: GRFX.C adds to the offsets, and LIGHTING.C passes a segment of 0 for a match reason. An offset comparison is written `FP_OFF(p) < FP_OFF(q)`, which is Turbo C's own `((unsigned)(p))`. The paragraph map is then needed only for real segment numbers such as `EmsBuff`.

Offsets wrap at 64 KB inside a DOS segment, and a flat pointer does not wrap. No case of the game relying on the wrap is known. The replay test would show one.

`huge` pointers are not used anywhere.

### EMS

UW2 needs EMS 4.0. The port gives it an EMS emulation with the same calls EMS.C makes, a store of 16 KB logical pages and a 64 KB page frame that is a region of the paragraph map. Mapping a page copies the page that was in that frame slot back to the store and copies the new page in. The game keeps track of what is mapped (PGCACHE.ASM, PANELS.C, SOUND.C), so the frame must hold exactly what DOS would hold. Apple Silicon's memory pages are 16 KB, the same size as an EMS page, so the copies can later be replaced by `mmap` of the store into the frame with `MAP_FIXED`.

### Near pointers kept in integers

A near pointer is 16 bits in DOS, so the sources pass and store them as `int` or `unsigned`. On the host these are 64-bit pointers, and storing one in 16 or 32 bits truncates it. Milestone 1 found 33 of these, e.g. `gronk_critid(..., (int)&copy, ...)` in SCDEVENT.C, which passes a pointer through an `int` parameter, and `*cDbbase = (int)dbptr` in GRDB.C, which writes a pointer into the render database.

Milestone 2 removed all of them. A value that carries a near pointer is a `NEARPTR` (`int` under Turbo C, `intptr_t` on the host), or `UNEARPTR` where two near pointers are compared as Turbo C compares them, unsigned. The channels that carry one are retyped: the argument of the `gronk_` family and `WhoamiFn`, the argument of input handlers (`InputFn` and the dispatch tables' `arg` field), and `seg009_2CC`'s two output pointers. Handlers whose own parameter is a plain `int` are cast where they are registered, as handlers of other types already were. Where the sources store a far pointer's offset in an `int` (the render database's `Clk` and `gr_entry`, `cPerror`, `lget`), they now say `FP_OFF`, which is Turbo C's own `((unsigned)(p))`, so the render database holds offsets as the paragraph map gives them and never host pointers.

### Struct layout and bitfields

Turbo C lays structs out with byte alignment and puts bitfields in 16-bit units. [MATCHING.md](MATCHING.md) records how Borland places each bitfield: in the 16-bit window that starts at the byte holding the next free bit, so a field can straddle a word boundary. The bits pack as one stream, low bit first, and a struct takes only the bytes it needs.

On the host the port packs every struct (`#pragma pack(1)` in `compat.h`), and with 16-bit bitfield types clang then also packs bitfields as one stream, low bit first. `tools/layoutcheck.py` tests this. For every struct and union in `src/include` it generates one C program that sets each field in turn to all ones in a zeroed record (a bitfield is assigned -1) and prints which bytes changed, with their values, and the record's size. It compiles the program with Turbo C in DOS and with clang, runs both and compares the outputs line by line. All 38 records without pointer fields come out identical, including the ten with bitfields, so the packed-struct hypothesis holds. The 21 file records the tool knows (`struct Player` (`PlayerDat`, 37Eh bytes), `struct LevelBlock` (7E08h bytes), `struct Object`, `struct StaticObj`, `struct Tile`, the animation and automap note records, the SCD rows, clocks and work area, the critter, weapon, armour, container and light tables from OBJECTS.DAT, `struct ComObj`, the font header and the bitmap header) are among them. Without the packing, `struct Player` is 388h bytes and `struct LevelBlock` 8108h on the host.

A compiler for Windows (MSYS2's CLANG64, llvm-mingw) lays bitfields out as Microsoft's compiler does unless told otherwise: a bitfield whose type differs from the one before it starts a new unit of its own type. That made `struct Player` 381h bytes and changed `ComObj`, so every replay differed from its first checkpoint. `tools/portcheck.py` (`layout_flags`) adds `-mno-ms-bitfields` when the compiler's target is Windows, for the game's C and the port's, and `tools/layoutcheck.py` uses the same flags; with it every record comes out as on macOS.

A struct with pointer fields cannot keep its DOS layout, since a host pointer is 8 bytes. Eight in the headers and eleven in the sources have them, and none is a file record. They are bracketed by `HOST_LAYOUT_BEGIN` and `HOST_LAYOUT_END` (`portable.h`, nothing under Turbo C), which give them natural alignment in the port: packed, their pointers would sit at odd offsets, and the macOS linker refuses a pointer in initialised data that is not aligned (SPELLS.C's `area_spells`). The layout check lists every struct in the headers with a pointer field and fails if a source copies one as bytes to or from a file or buffer; none does. Some are laid over shared buffers and grow on the host, which the port has to allow for (see the Milestone 2 results).

All file records are little-endian, and so are Apple Silicon and x86-64. The port reads them as packed structs and has no byte swapping.

### Names longer than 31 characters

Turbo C and TASM keep 32 characters of a name with its underscore, so the C name `CallbackFunctionSleepRelated_seg021_22FD_CB7` and the assembly label `_CallbackFunctionSleepRelated_seg` are the same symbol in DOS. On the host they differ. The C written for the port defines the full C name.

### Function prototypes and char parameters

Some functions were declared without a prototype, because their callers push an `int` where the definition takes a `char`, or pass an argument the definition does not take ([CONTRIBUTING.md](CONTRIBUTING.md#where-files-disagreed)). Some function pointer tables also mix signatures, e.g. the spell table in SPELLS.C. Clang rejects both, and on the host an argument of the wrong type is undefined behaviour. Milestone 2 gave each one real type, every change proved by the gate:

- `OLDSTYLE((params))` (`portable.h`) is `()` under Turbo C and the parameter list on the host, so the DOS callers keep their pushes and the port's callers convert as the DOS callee reads. `RestoreGame`, `SaveGame` and `create_sprite` (variadic on the host, since it is called with one argument and with three) use it.
- A function whose callers pass an argument it ignores now takes it, as an unused `int`: `editexit`, `clearobj`, `player_look_grave`, `seg011_6` and `seg011_2C6`. An unused parameter costs Turbo C no byte.
- Function pointers of mixed types are cast where they are stored or passed (the spell tables, `UseThing`'s handlers, `bab_fun`'s built-ins through a `BablFn` typedef, PANELS.C's `adjust` table, the collision handlers' `special`), as handlers of other types already were.
- `postodir`'s old-style definition with `char` parameters takes `int`s and casts them to `char`, which compiles the same.

No change needed a port-only wrapper. Calling a handler through a pointer of another type is still undefined behaviour on the host; it works on arm64 and x86-64 because both pass the arguments in registers and the callee reads only the bits its type has. The remaining cases are listed in the Milestone 2 results.

### Null pointers

A null pointer does not crash a DOS program, and where the game dereferences one the port has to give the same result.

**What DOS reads.** UW2 is a medium-model program, so a near pointer is an offset in DGROUP, and a near null pointer reads DS:0. In `UW2.EXE` (the GOG release) DGROUP's paragraph starts 4 bytes before C0's `_DATA`, so DS:0 to DS:2 are the last three bytes of the segment before it, the overlay stubs. They are the tail of the last stub entry of ovr167, the one for `FarWrite_ovr167_627` (MISCUTIL.C). While ovr167 is not loaded the entry is `CD 3F 27 06 00` (`int 3Fh`, the function's offset 0627h, 0), so DS:0 holds 27h 06h 00h. When the overlay manager loads ovr167 it rewrites each of its entries as a far jump, `EA 27 06 ss ss`, so DS:0 holds 06h and DS:1 and DS:2 hold the overlay's load segment, and it writes the `int 3Fh` form back when it unloads it (OVERLAY.ASM, `_67F` and `_69D`). What a near null read sees therefore depends on whether ovr167 is in the overlay buffer at that moment.

| DS: | Bytes | What they are |
| --- | --- | --- |
| 0-2 | 27 06 00, or 06 ss ss while ovr167 is loaded | the tail of ovr167's last stub entry |
| 3 | 00 | padding before `_DATA` |
| 4-7 | 00 00 00 00 | C0's null area |
| 8-30h | `Turbo C++ - Copyright 1990 Borland Intl.` and its 0 | C0's copyright |
| 31h on | `Null pointer assignment`, CR LF, then `Divide error` and `Abnormal program termination` | C0's messages |

So a string read through a near null pointer is `'` followed by byte 06h, or byte 06h followed by the segment's bytes up to a 0. A far null pointer is 0000:0000, the interrupt vector table. Its first entry, the one a null record pointer reads first, is int 0, which `init_world` points at `int0_trap` (INT0TRAP.ASM) at start-up: offset 0019h, which the link fixes, and segment 1FC7h plus the program's load segment, which depends on how much memory DOS and its drivers take below the game.

**What DOS checks.** UW2's C0 keeps Turbo C's null pointer check (tools/link.py's `c0_source` rebuilds it). The game leaves through `exit`, from `main`'s return or from `exit` calls in MAINMENU.C, ERROR.C and GRDB.C, and C0's `__exit` then sums the 2Dh bytes from DS:4 to DS:30h and compares the sum with 0CA5h. The code is at file offset 11460h: `mov ax,4; mov si,ax; xor ax,ax; mov cx,2Dh`, the add loop, then `sub ax,0CA5h`. If the sum differs it prints "Null pointer assignment". This is one of UW2's three changes to Turbo C's C0: the stock C0 sums from DS:0, and UW2's starts at `DATASEG@`, DS:4. So a write through a near null pointer is reported only if it lands in DS:4 to DS:30h. A write to DS:0 to DS:2 goes unreported and corrupts the stub entry, so a later call to `FarWrite_ovr167_627` goes astray. A far null write changes the interrupt vectors, and nothing checks them.

**What the host does.** On macOS every 64-bit process has a `__PAGEZERO` segment with no access over the low 4 GB, and an arm64 process cannot map memory at address 0, so any null dereference crashes. The port cannot reproduce DOS by mapping page 0. It finds each site instead, and decides each one.

**Finding them.** Writes and reads are found differently.

- Writes: a recorded session (Milestone 4's replay) is replayed in real DOS through dos-mcp (`tools/rungame.mjs`), the game is quit through its menu, and the output is checked for "Null pointer assignment". That covers DS:4 to DS:30h. DS:0 to DS:3 and the vector table are covered by reading them with dos-mcp's `read_memory` before and after each session.
- Reads, in three ways. clang's static analyzer runs over the port build (done in Milestone 2; the results are below). The `NULLTRAP` hook (`portable.h`) marks each audited dereference: a modding build of the sources concerned compiled with `-DNULLTRAP` (`tools/tcc.mjs`, then `tools/link.py --obj STEM=PATH`) appends the file and line of each null it sees to `NULLTRAP.LOG`, and goes on with the null pointer, as DOS does, so a replayed session in DOS lists the marked sites it reaches. And once the port runs, its debug build is compiled with UBSan's `-fsanitize=null` and run over the replay corpus, which finds every null dereference the sessions reach, marked or not.

**Deciding each site.** Each site gets one of three treatments:

1. The port serves the bytes DOS would read. `NULLTRAP(p)` and `FARNULLTRAP(p)` are `(p)` under Turbo C, so the gate proves they change nothing. On the host a null `p` becomes a pointer to the port's copy of DGROUP from DS:0 (`port_null_near`), or of the vector table (`port_null_far`), and the hit is logged. The copy of DS:0 to DS:2 follows the overlay state where the port models it, and otherwise holds the unloaded form; the vector table holds int 0 with the segment of the DOS set-up the replays are recorded on. Writes land in the copy, so they change nothing else, and they are logged.
2. A plain guard, where nothing depends on the bytes read.
3. A real bug in the original: it is recorded in [FINDINGS.md](FINDINGS.md) with its fix as a phase 2 option, and the default reproduces DOS.

The faithful port never crashes on a null pointer: a site the audits missed is a bug in the port.

## Replacing the assembly modules

Every assembly module gets a C replacement in `src/port/`, written from the matched assembly. The matched assembly is the authority on behaviour. The modules fall into four groups:

| Group | Modules | Code bytes | Port |
| --- | --- | --- | --- |
| 3D renderer, seg004 | 14 modules in `src/3d` (EXPAND to PGCACHE), and SETPNT | 33,008 | C translated instruction by instruction from DOS (the 3D renderer, below); SETPNT by hand, and EXPAND's decoders by hand where the replays reach them |
| Graphics library, seg003, and its neighbours | 14 modules in `src/gfx` (GRMISC to GRLIBN), SPRITE (seg000), VALLOC (seg001), MODEX (seg017) | 23,907 plus 2,915 | each routine either C written by hand, drawing into an emulated mode X screen, or translated like the renderer (One implementation per routine, below) |
| System layer, seg021 | 17 modules in `src/sys` (STARTUP to C3DENTRY), INT0TRAP (seg018) | 4,067 plus 95 | C on top of the platform API |
| Sound, seg022, and the `.ADV` drivers | AIL | 3,321 | AIL's API and the drivers in C (see Sound) |
| Dropped | the overlay manager (seg046), LPFDELTA and PLANECPY (never called) | 4,670 | not needed |

FARDATA.ASM's buffers become C arrays registered in the paragraph map. The far data that `tools/extract.py` still takes from the user's EXE (about 83 KB, mostly the renderer's tables and the 3D object models) is loaded from the user's own `UW2.EXE` at start-up, by the same offsets extract.py uses. That way the port never holds game data. The tables become C source only where their meaning is known and they hold no game content.

Each C replacement keeps the module's entry points and data names, so that the shared C calls it without a change. Where an assembly module switched stacks or data segments (seg021's private stack, seg003's own DS), the C version needs neither.

### The FM Towns build as a template

The FM Towns release of UW2 is a flat 32-bit build with 3,237 original symbol names ([BUILDING.md](BUILDING.md)). Its renderer and graphics library are hand-written assembly, not compiled C. Four observations show it:

- **Names.** Watcom C adds a trailing underscore to every C function, and 1,584 of the 2,050 code symbols have one. The other 466 have none, and they sit in runs where the assembly libraries are. Those runs are `render_3d`, `mxmul`, `do_movec`, `_asm_texture_map` and the rest of the renderer (198 names in one run), the graphics library (`show`, `cline`, `rectangle`, `fbuf_draw_ylrpp` and others), the keyboard handler, and AIL.
- **No frames.** The routines take their arguments in registers and have no stack frame. `do_movec` is four instructions, then `jmp dword [eax*2+vector_table]`, so the model interpreter dispatches each handler straight into the next one, as INTERP.ASM does in DOS.
- **Self-modifying code.** `_asm_texture_map_scanline_lin_tp` writes into its own code and does a far `jmp 000Ch:...` to flush the prefetch queue.
- **Thin C wrappers.** The C-callable entry points are thin Watcom wrappers around the assembly, e.g. `show_` moves its stack arguments into registers and calls `show`.

So the FM Towns code is not compiled C to lift, but it is a better template than the DOS assembly in three ways. First, it is flat 32-bit code, with no segment loads, no stack switches and no 64 KB limits, which is the memory model the port wants. Second, every routine and every data label has its original name, e.g. `fbufytab`, `_uleft`, `vector_table`, `dbase_data` and `gouraud_base_col`, where DOS has `seg003_0272_...` offsets. Third, its graphics library already separates the device: a table of 24 vectors (`_grSolidYLR`, `_grPageFlip`, `_grDoPalette`, `_grCopyFbToScreen`, `_grV4CopyYLRPP` and the rest) is the whole hardware edge, and everything above it draws through spans and bitmaps in memory.

The method for each renderer and graphics routine is to write the C from the DOS assembly, read the FM Towns routine beside it for names and for the 32-bit shape, and take DOS as the authority wherever they differ. Known differences include `do_fetchmap`'s `tmcolor`, the `mm3x9` saturation and `do_bcompact_map` ([3d.md](subsystems/3d.md#open-questions)). `tools/fmt.py` disassembles any FM Towns routine by name.

The FM Towns vector table is a good shape for the port's own graphics edge. Phase 1 keeps the DOS mode X behaviour behind it, and a phase 2 renderer can implement the same 24 primitives some other way.

### The screen

The DOS graphics library draws into VGA mode X with two pages, latched copies, a virtual screen wider than 320 for the cutscenes, pixel panning and a split line. CUTS.C also writes planar video memory itself, through `outportb(0x3C4, 2)` and `MK_FP(0xA000, 0)`. The port emulates the parts of the VGA that the game uses: 256 KB of video memory in four planes, the sequencer's map mask, the graphics controller's read map, the latches, the display start, pixel panning, the line compare and the DAC with 6-bit colour. `outportb` to the VGA ports goes to that emulation. Each frame the platform layer scans the visible page out to a 320 by 200 indexed image and converts it to RGB with the current DAC. The scan-out matches the hardware, so the port's screenshots can be compared with DOS byte for byte. The live scan-out runs on the platform layer's thread, at the host's refresh, so it latches the display start and the pixel panning as the CRT controller does: the frame on the screen is the one after the last vertical retrace to have ended, with the start as it was when that retrace began and the panning as it was when it ended (`vga.c`). Reading the registers as they stand showed a new start with the old panning for one frame in four of a horizontal pan, a picture up to three pixels out. Replays and `--shot-at-flip` read the registers as they stand.

## The platform layer

The platform API is the runtime's `port/platform/plat.h`. It names no SDL type, and the runtime's `port/platform/sdl3/plat_sdl3.c` is the only file that includes SDL. Everything above it is portable C. SDL3 itself runs on macOS, Windows, Linux, iOS and iPadOS, Android and Emscripten; because the layer stays SDL-free, a web build without SDL or a libretro core is one more backend file, not a change to the port.

The model is the PC's. The game runs on a thread of its own, as the CPU did. The backend owns the window and the event loop on the host's main thread (which macOS and iOS require), and calls into the port the way the PC's hardware interrupted the CPU. Its hooks run on the backend's thread and do only what an interrupt handler could. The port's own timer thread plays the PIT.

- **Video.** The game never presents anything. Each pass of the event loop the backend calls the `scanout` hook, which reads the emulated VGA as its CRT controller would (the visible page, the offset, the panning and the line compare) and returns an indexed picture with the DAC's 256 6-bit entries. The backend converts it to RGB, `(v << 2) | (v >> 4)` as DOSBox does, and draws it scaled. The scaling is the backend's: integer multiples by default, and the 4:3 aspect correction from 200 to 240 lines, so it never touches game code.
- **Pointer events, not "mouse".** A pointer is a mouse, a pen or a finger. Each event carries its position in the game's 320 by 200 screen pixels, already undone from the window's scaling and letterbox, its motion, and its buttons. The port's mouse driver (`src/port/sys/mousedrv.c`) builds int 33h's state from them: the position in the driver's 640 by 200 range, the buttons, and the motion in mickeys at the driver's default rate. The game reads only the motion and the buttons and keeps its own cursor, so a touch screen drives it the same way. Fed the host's motion as it comes, the game's cursor drifts from the host's pointer (the driver's scaling drops half a pixel on an odd count of mickeys, and the game stops its cursor at its confining box while the host's pointer goes on), so by default the driver reports, once per move of the host's pointer, the motion that puts the game's cursor (`mouse_getxy`) where the pointer is, and the backend hides the host's pointer over the picture; the game's own moves, its keyboard warps and `mouse_putxy`, stand until the pointer moves again. `--mouse lock` captures the pointer on a click instead, as DOSBox does (Ctrl+F10 releases it), and passes its motion on in whole pixels.
- **Keyboard.** The backend turns each SDL key event into PC set-1 scan code bytes, make or break, with the E0 and E1 prefixes, through one table, and hands them over one byte at a time, as port 60h did. The port's int 9 handler (`src/port/sys/keyqueue.c`) stores them in KBDINT.ASM's 64-byte ring buffer at the same offsets of seg021's data, and `key`, KEYQUEUE.ASM in C, reads them out unchanged. The backend drops the host's own key repeat; the port's keyboard repeats a held key itself at the BIOS default (500 ms, then 10.9 a second), so the game's filter of repeats sees what it saw in DOS. The enhanced keyboard flag is set, as for a 101-key keyboard.
- **Time.** `plat_counter` and `plat_counter_hz` are the host's high-resolution counter, and `plat_sleep_ns` a precise sleep. The PIT (the runtime's `port/sys/pit.c`) runs on a thread of its own from them: the AIL timers at their own rates (the game clock, SOUND.C's `cllbck_tst`, at 256 Hz), the BIOS tick at 18.2 Hz for `clock`, and the keyboard's repeat. A timer is due whenever its period has passed, so its rate is exact on average, and a late wake-up runs the missed calls at once, as queued interrupts did. Under replay (Milestone 4) the game reads the recorded values instead. The 3D view is paced: `cRender` (`src/port/3d/render.c`) calls the runtime's `port_pace_frame` (`port/sys/pace.c`, shared with UW1 since its issue 6), which waits until 8 ticks of the game clock have passed since the last 3D frame, 32 frames a second at most. The game moves the player by the time since the last frame and writes the position back in whole 1/256 tiles every frame, dropping the fraction (down for a move in a positive direction, up for a negative one). A PC of 1992 took 10 to 15 ticks a frame and lost little; the host takes under one, so a slow move in a positive direction, walking backwards with x, went nowhere (rc8). DOSBox at high cycles does the same, which is Ultima Underworld's well-known trouble on fast machines. Replays are not paced: their clock is the recording's.
- **Files: the data root.** The user's own copy of UW2 is the data root, and it is never written. `--data` names it, or the runtime's `port/sys/gamedir.c` finds it, with what `portgame.h` names (`PORT_GAME_EXE` and the rest) and `port_check_exe` in `mem/fardata.c` (GOG's install folders, the folder used last; [BUILDING.md](BUILDING.md#running-the-port)). The port's home directory (`--home`, default `~/.uw2port`) plays the part of `UWHOME`: it is laid over the data root, so the game sees one tree. Every DOS path the game names (`DATA\uw.cfg`, `data`, `SAVE0\LEV.ARK`, `CRIT\CR01.00`, the scratch files `a.tmp` to `h.tmp`) is looked up one component at a time, without case, first in the home directory and then in the data root (the runtime's `port/platform/files.c`). A file the game creates goes to the home directory. A file it opens to change stays in the data root until its first write, and is copied into the home directory then. Borland's text mode (CR LF, Ctrl-Z) is kept for handles opened without `O_BINARY`. The far data no source defines yet, and seg021's and seg003's data segments, are read from the user's `UW2.EXE` in the data root, which the port checks by its size and CRC-32 first.
- **Lifecycle events.** The backend reports suspend (a phone going to the background, a window minimised), resume, and a request to quit (the window's close box). A phone build has to stop the timers and save on suspend; the desktop build only logs them.
- **Audio.** An output stream of 16-bit stereo samples with a callback on the audio thread (`plat_audio_open`), which drains the ring `sound/audio.c` renders into (Sound, below).
- **No JIT and no self-modifying code** anywhere in the port. iOS refuses writable executable memory. The DOS code that patched itself (seg004's texture mappers, seg003's polygon clipper, SCALEBM.ASM's generated sprite code) becomes C that chooses with data what the patch chose: the translated code reads a patched immediate, displacement or jump condition from the code segment's bytes, which are data in the port, and SCALEBM.ASM's generated scalers are interpreted (the 3D renderer, below).
- **Debugging.** `--screenshot-after MS` writes the scan-out to a PNG, `--window-shot FILE` the window's own scaled contents, `--shot-at-flip K:FILE` the screen right after the game's K-th page flip, `--hidden` runs with no window, `--exit-after MS` quits, `--exit-on-halt` quits where the port stops instead of leaving the window up, and `-v` traces file opens, the paragraph map's windows and the call stack at a stub. `UW2PORT_ASMTRACE=1` in the environment writes every entry into the translated assembly (the address and the registers) to the standard error.

## Sound

SOUND.C stays shared. The port replaces what is under it: AIL.ASM, the `.ADV` drivers AIL loads from `SOUND\`, and the sound card itself. The aim is that the port programs the sound hardware with the same register writes and MIDI bytes DOS's drivers did, at the same moments of the game's clock, and that the state the game reads back from the drivers is DOS's.

### The drivers, from Miles' source and Origin's binary

John Miles released AIL 2.14's source on 26 May 2000 as "open-source freeware, usable by anyone for any purpose, commercial or otherwise, without restriction or limitation" (its `READ.ME`). The port's reference copy is the release as mirrored at github.com/dah4k/AIL2, commit `54eb81dd4c5313eac003096d220cb809f79e8432` (directory `A214_D3`); github.com/Tronix286/AIL2 at `f9c05599bdeb05a0a2b05557f6a23cdafb8550ce` holds the same sources with CR LF line ends and a Creative Music System driver added. None of it is in the repository: the port's C is written from it, as the rest of the port is written from the matched assembly.

Assembling those sources with TASM 2.0 (the repository's own, in emu2) and comparing the results with the GOG release's driver files byte for byte showed which source each driver is ([FINDINGS.md](FINDINGS.md#3-engine-findings)):

| File | Card | What it is |
| --- | --- | --- |
| `DD01.ADV`, `DD02.ADV`, `DD03.ADV` | Sound Blaster, Sound Blaster Pro, Pro Audio Spectrum digital | AIL 2.14's `SBDIG`, `SBPDIG`, `PASDIG.ADV`, byte for byte |
| `DM05.ADV` | Roland MT-32 (MPU-401) | MT32.INC and MPU401.INC as in 2.14, with an older XMIDI.ASM (82 bytes shorter: before 1.10's time signature function) |
| `DM02`, `DM03`, `DM04`, `DM06`, `DM07.ADV` | Ad Lib, Sound Blaster FM, SB Pro 1 (two YM3812s), Pro Audio Spectrum, SB Pro 2 (YMF262) | YAMAHA.INC 1.02 assembled with `OSI_ALE`, Origin's time-variant effects (TVFX), whose `ALE.INC` was never released, with the same older XMIDI.ASM |
| `DM01.ADV` | PC speaker | not emulated: the port registers it as a driver that plays nothing |

With a stand-in `ALE.INC` holding DM03's own bytes, 2.14's sources assemble to DM03.ADV exactly except in XMIDI.ASM's rewind, time signature, beat counting and `serve_driver`, so everything else in the drivers is 2.14's source, and the rest was read from DM03's disassembly (`ALE.INC`'s data, 400h bytes of per-slot words, and its four routines, 0530h to 0ADAh). The C is the runtime's `port/sound/`, but for `tvfx.c`, which is UW2's own, in `src/port/sound/`:

| File | Replaces | From |
| --- | --- | --- |
| `ail.c` | AIL.ASM (seg022) | the matched AIL.ASM (AIL 2.11, see FINDINGS.md): `API_timer`'s DDA, the driver table, the calls the game makes, and the logs below |
| `xmidi.c` | XMIDI.ASM, the shell of every music driver | 2.14's XMIDI.ASM, with UW2's older beat and bar counting from DM03 |
| `yamaha.c`, `yamaha.h` | YAMAHA.INC: the FM drivers | 2.14's YAMAHA.INC, routine by routine, with the YM3812, dual YM3812 and YMF262 variants; where YAMAHA.INC calls ALE.INC (`IFDEF TV_phase`, `IFDEF TV_switch_voice`, XMIDI.ASM's `IFDEF serve_synth`) it calls the hooks of `struct AilFmExt` (yamaha.h), and without one it is YAMAHA.INC without `OSI_ALE` |
| `tvfx.c` | ALE.INC: the FM drivers' TVFX | DM03.ADV's code at 0530h..0ADAh (`TV_switch_voice`, `TV_cmd`, `serve_synth`, `TV_phase`), plugged into yamaha.c as `uw2_tvfx` (`AIL_FM_EXT` in `src/port/ailgame.h`) |
| `mt32.c` | MT32.INC and MPU401.INC: DM05 | 2.14's MT32.INC: the timbre cache, the system exclusive messages that upload `UW.MT`'s timbres and point patches at them, the sysex controllers |
| `dmasound.c` | DMASOUND.ASM: DD01 to DD03 | 2.14's DMASOUND.ASM for what UW2 uses: `.VOC` block parsing, the double buffer and its statuses, start, stop, pause, resume, volume and pan as each card applies them (SBDIG ignores both, FINDINGS.md) |
| `audio.c` | the sound card | the chips and the mixer (below) |

`AIL_register_driver` recognises which driver the game loaded by the device names in the user's own `.ADV` file and reads the driver's description table from it (the offset `describe_driver` loads into AX), so the game's view of the driver (`struct DrvrDesc`: type, data suffix, factory port, IRQ and DMA, service rate) is the file's.

### The timers and the hardware

AIL's timers are `API_timer` as AIL.ASM has it: one PIT programmed for the shortest period any registered timer needs (3906 µs once the 256 Hz game clock exists), and per timer a period and an accumulator to which each PIT tick adds the PIT's period, firing the timer when it reaches its period; a change of the PIT's period clears every accumulator. So the music driver's 120 Hz service fires on the same PIT ticks relative to the game clock as in DOS. The ticks come from the port's timer thread in real time, or, under replay, from the replayed game clock (`port_clock_read`, below). The drivers' own clock, which the digital transfers are timed by, is the PIT's real period: AIL programs the divisor `period * 10000 / 8380` (`set_PIT_period`), 4661 for 3906 µs, so a tick lasts 4661 / 1193182 s, 3906.36 µs.

The drivers write to `audio.c`, which is the card:

- **FM.** Nuked OPL3 (github.com/nukeykt/Nuked-OPL3, LGPL-2.1, the most accurate OPL emulator, in C), one chip for the Ad Lib and the Sound Blaster, two in OPL2 mode, left and right, for the dual-YM3812 cards, one in OPL3 mode for the SB Pro 2. `tools/setup-sound.sh` (`make setup-sound`) fetches `opl3.c` and `opl3.h` at commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8` into `tools/nuked-opl3` (ignored by git) and checks their SHA-256; the port compiles them when they are there. The alternatives were DOSBox's OPL emulators (GPL-2.0, C++) and ymfm (BSD-3-Clause, C++, less exact on the OPL3); Nuked OPL3's LGPL is satisfied by source distribution and dynamic or relinkable linking, and nothing of it is committed.
- **MT-32 and CM-32L.** libmt32emu (munt, LGPL-2.1-or-later) from Homebrew (`brew install mt32emu`, 2.8.3 tested), found by pkg-config at build time, with the user's own ROMs: found by their contents under any names (`--mt32-roms` with a folder or a file, `UW2PORT_MT32_ROMS`, the remembered setting, or a search; Exhume's `runtime/port/sound/mt32roms.c`), a CM-32L pair preferred. The repository holds no ROM and never will. The MPU-401's bytes go to `mt32emu_parse_stream`.
- **The DAC.** The Sound Blaster's 8-bit DAC as a mixer channel: each buffer the digital driver starts is copied and played from the microsecond it started, resampled from the `.VOC` time constant's rate, `1000000 / (256 - tc)`, at the card's levels.

At every PIT tick, before the tick's timers run, `audio.c` renders everything up to that tick's time with the registers as they are, so a register written during a tick sounds from that tick, as on the card, and the output is in the AIL clock's time whatever the host does. The frames go to a ring that the platform's audio stream (SDL3) drains, and, with `--audio-wav FILE`, to a 44100 Hz stereo WAV file. Under replay the port runs faster than real time and the device gets whatever fits in the ring; the WAV file gets every frame.

### Replays with a sound card

With a card, the game reads state that time and the card change behind its back, and runs game code from the timer interrupt. Two hooks in `portable.h` make that deterministic, both the original tokens under Turbo C:

- `SND_READ(drv, x)` marks the 17 reads of driver state the game makes (`AIL_sequence_status`, `AIL_sound_buffer_status`, `AIL_lock_channel`, `AIL_timbre_status`, `AIL_timbre_request`, `AIL_detect_device`, in SOUND.C and CUTS.C). REPLAY.C records each value in a stream of its own (stream 8, `SOUND`) and replays it, while `x` is still evaluated so the driver sees the same calls. A read of no driver (-1) is not recorded, so the sessions recorded without a card replay as before.
- `SLAVE_TIMER(f)` marks the 16 Hz effects timer (`digi_fx_timer` or `fx_timer`), game code that sets `dsfx_playing` and restarts digital playback, which AIL ran at any instruction. Under REPLAY and in the port, AIL gets a timer that does nothing and REPLAY.C runs `f` at reads of the game clock, as many times as AIL's DDA would have fired it by then, a function of the clock alone (`t * 3906 / 62500`).

The port's own drivers run beside the recorded values: under replay, `port_clock_read` advances AIL's PIT by the replayed clock, one PIT tick for each tick of the clock (the 256 Hz clock's period is the shortest, so it is the PIT's, as in DOS), so the music driver plays the session's music in the session's time, and `port_sound_read` compares each value the port's driver had with DOS's recorded one and counts the reads where they differ. `replay.py check` fails when there is one.

Two more things are needed for a digital driver to agree with DOS's, read for read:

- **The moment of each read.** The game polls a digital buffer's status in a loop, many times in one tick of the clock, and a buffer ends at any moment of a tick. So the replay build records, with each run of reads of the `SOUND` stream, when within the tick DOS made the first of them (REPLAY.C's `pit_moment`, recording format 3): the PIT's count and output, latched with the read-back command (AIL runs the PIT in mode 3, which counts each half period down by twos), an interrupt the PIC holds but has not delivered (its request register), and the ticks since the clock the game last read, all in PIT input clocks. The port asks its driver at that moment (`port_sound_moment`). Recordings of format 2 have no moments and replay as before.
- **DOS's Sound Blaster.** The DOS side of the replays is js-dos's DOSBox, whose Sound Blaster ends a single-cycle transfer when its mixer, pulling the DMA bytes in whole milliseconds, finds at most `sb.dma.min` (3 ms of samples) left, and takes them all at once (`sblaster.cpp`, `GenerateDMASound`); and js-dos's mixer pulls by the host's wall clock (`jsdos-mixer.cpp`, `MIXER_Mix`). So DOS's interrupt comes up to about 3 ms before the samples have played, by an amount that changes from run to run: DOS's own driver, replaying its own recording, gives other values than the recording in about 14,500 of 1,795,103 reads, a different number each run (the replay build writes `SNDCHECK.OUT`; the FM-only and MT-32 sessions, with no digital driver, show 1 and 2). Under replay the port therefore ends a transfer at the moment a recorded read shows DOS's ended (the buffer done, or the next one started), when that is within 5 ms before or 1 ms after its own end (`digi_sync`, `SB_WIN_EARLY`, `SB_WIN_LATE`), and otherwise at its own end; outside a replay a transfer ends when its samples have played, `length * (256 - tc)` µs after it started, as on the card.

### Checking the drivers against DOS's

`--ail-log FILE` writes every AIL call the game makes, with the data it passes (the XMIDI image, each timbre) and every driver service tick; `--hw-log FILE` every register write and MIDI byte, with its tick. `tools/ailcheck.py AIL_LOG HW_LOG` loads the user's own driver file into an x86 emulator (Unicorn, `pip install unicorn`, GPL-2.0, a development tool only), makes the same calls with the same data in the same order, records every byte the real driver writes to the chip's or the MPU-401's ports (the MPU-401 answering each command with its acknowledgement), and compares the two streams write for write and tick for tick, and the values the calls return. `replay.py check` runs it on every session with a music card, when Unicorn is in the `.venv`. The Milestone 5 results below have what it showed.

## The differential test: input record and replay

The test of faithfulness is to run the same session in DOS and in the port and compare the results. Doom's demo files and OpenRCT2's replays use the same method.

### What is recorded

Wall-clock input timing is not enough, because DOS UW2 reads its clock and its input devices at points that depend on how long the code takes. So the recording is made at the game's own boundary. It records each value the game reads from the outside world, in order:

- each read of `*Time`;
- each key event taken from the ring buffer;
- each mouse poll (position, buttons, motion);
- each joystick poll;
- the values of `time()` and the seeds given to `srand`.

The file (`RECORD.OUT`, format in the runtime's `replay/replay.c`) is one stream of runs per kind of input. Version 4 adds repeats to the sound stream: the game polls the two digital buffers' status alternately, 2, 0, 2, 0, a run of one read each, and the port's black box made 150 million such runs, 900 MB, in six minutes of play; written as repeats of the last runs, the same minute of sound reads takes a few hundred bytes.

Replayed in that order, the same values give the same `rand` sequence and the same decisions, whatever the speed of the machine.

### Where the hooks go

The hooks go in the shared sources, at the few places where the game reads the outside world (`portable.h`): every read of `*Time` is `GAME_TIME()`, and `key`, `mouse`, `mbuttons`, the joystick reads, `time()` and `srand` are `KEY()`, `MOUSE()`, `MBUTTONS()`, `JOY_READ()`, `JOY_BUTTONS()`, `WALL_TIME()` and `SRAND()`. Under the normal DOS build each macro expands to the original tokens, so the gate passes. A replay DOS build is the modding build with those sources compiled with `-DREPLAY` and `src/replay/REPLAY.C` linked in, which records or replays through a file. The port has the same hooks in its build. So the recording can be made in DOS (`tools/replaydos.mjs` drives it in headless DOS through dos-mcp) and played in the port, or the other way round. The Milestone 4 results have the formats.

### What is compared

At chosen points, e.g. every N reads of the clock, at each level change and at each save, both builds call a shared `state_dump()` that writes the game state as bytes. The dump holds the player record, the level block (tiles, objects, free lists, animations, timers), the SCD state, the quest variables, the 3D frame buffer and the palette. The two dumps are compared byte for byte. The first difference names the subsystem and the moment where the port went wrong. Saves made in each build are compared byte for byte too, and are loaded in the other build. Screens are compared from the frame buffer dump and from the port's VGA scan-out, not from a scaled screenshot.

dos-mcp's `read_memory` and `search_memory` stay useful for questions the dump does not answer.

### Bugs from FINDINGS.md

The faithful port reproduces each likely bug, and the replay test proves it does. A fix becomes a named option, off by default, and the replay test runs with every option off.

## Phases and milestones

| Milestone | Work | Exit criteria |
| --- | --- | --- |
| 1. Measure (done) | `src/port/compat.h`, the stand-in headers, `make port-check` | every C source compiles or fails with a categorised reason; the unresolved names are listed; the gate passes |
| 2. Compile and link (done) | gated source changes for the errors and the pointer, prototype and Borland issues; explicit widths; the layout check; the promotion audit; the null-pointer static pass; the far pointer macros; link stubs; `make port` | `make port-check` shows 0 errors and no pointer truncation in the shared C; `make port` links a native binary with stubs that abort; `tools/layoutcheck.py` shows every file record identical; the gate passes |
| 3. Boot to the title (done) | the platform layer on SDL3 (video, pointer events, keyboard, time, files, lifecycle, silent audio); the paragraph map, the far heap and EMS; Borland's library; seg021 in C; the parts of seg003, MODEX, VALLOC and the VGA emulation the opening screens need; AIL with no driver; `port_null_near` and `port_null_far` | the port runs `init_world` past `display_screen(5, 6)` and shows the first title screen, and its VGA scan-out matches a DOS screenshot of that frame byte for byte |
| 4. Replay, the way into the game, and the 3D view (done) | `GAME_TIME()` and the input hooks in the shared sources; the replay DOS build; `state_dump()`; the null-pointer write check; the cutscene player's screen access, the rest of seg003, the sprites and the mouse cursor, text; a `-fsanitize=null` debug build; seg004 and the seg003 modules it uses translated to C (`cRender`), the render interface and the sprite hook | a DOS session recorded and replayed in DOS gives identical dumps twice in a row; the same session from boot through the title cutscene, the main menu and character creation into the game replays in the port with identical dumps at each checkpoint, and a session in the game's first rooms (walking, turning, looking up and down, a click in the view) replays to its end with identical dumps, its 3D frames included |
| 5. Sound, then the rest of the game loop (done) | the AIL API, the XMIDI sequencer, the OPL and MT-32 backends, the digital channel; then the panels, inventory, message scroll and automap screens, conversations, the locals read before they are set, saves | every theme plays; effects and the cutscene audio play; music timing matches a DOS recording; recorded sessions through those screens and into a conversation replay with identical dumps |
| 6a. Fast and comprehensive accuracy tests (done) | golden references from DOS; unthrottled replays in the port and in native DOSBox-X; sessions in parallel; differential fuzzing of single routines; `make test`, `make test-full`, `make coverage` | `make test` passes in well under two minutes and runs on every push; goldens for every session; DOSBox-X's goldens equal js-dos's; the fuzzing runs clean or its differences are documented |
| 6. Other platforms and the faithful release (phase 1; under way) | Linux and Windows builds; settings for the sound cards, the ROMs and the window in the port; the remaining stubs; the replay suite over whole sessions; every null-pointer site decided | the replay suite passes on each platform; a set of recorded sessions, from character creation into several worlds, replays in the port with identical dumps and saves, with no null dereference reported by `-fsanitize=null`; saves load both ways |
| 7. Enhancements (phase 2) | the readable renderer, routine by routine; scaling, mouselook, the FINDINGS fixes as options | each option is off by default, and the replay suite still passes with all options off |

## Milestone 1 baseline

These are Milestone 1's numbers, measured before any source was changed; Milestone 2's follow. The measurement is `make port-check`, which runs `tools/portcheck.py`. The tool compiles all 99 C sources with Apple clang 21 (`cc -x c -std=gnu89 -fsigned-char`) and the portability layer, with no game source edited. It writes the objects and the full log to `build/port/`. Each diagnostic is counted once, so a header's diagnostic is counted once, not once for each source that includes it. Run on 2 October 2026, the results are these.

92 of the 99 sources compile, and 45 of them have no diagnostic in the file itself. The 7 that fail have 18 errors between them, and there are 233 warnings over the whole tree:

| Category | Errors | Warnings | What it is |
| --- | --- | --- | --- |
| inline asm | 2 | 0 | EMS.C's `asm cld` and `asm mov bx,0` |
| far pointers and segments | 1 | 9 | lines with `MK_FP`, `FP_SEG`, `FP_OFF` or `movedata`, e.g. offset-only far pointer comparisons |
| near pointer held in an int | 0 | 33 | pointers cast to or from `int` or `unsigned` |
| 64-bit long and pointer differences | 0 | 67 | `long` arithmetic narrowed to `int`, and pointer differences, which are 64 bits on the host |
| Borland library internals | 3 | 0 | `grfp->fd`, a field of Borland's `FILE` (LOADGR.C) |
| missing declaration | 1 | 0 | `play_slot_hit_abs` called before its definition in BARTER.C |
| char parameters against old-style declarations | 11 | 0 | `f()` declarations whose definitions take `char`, and function pointers of mixed types (USEITEMS.C, OBJUSE.C, SPELLS.C, GAMEWRAP.C) |
| calls without prototypes | 0 | 92 | 51 of them in CONVERSE.C |
| pointer signedness and types | 0 | 14 | `char *` against `unsigned char *` |
| unsigned abs and always-true tests | 0 | 6 | `abs` of an unsigned expression, and an address tested against null |
| empty if bodies | 0 | 6 | kept for matching |
| return and control flow | 0 | 5 | non-void functions that fall off the end |
| comments | 0 | 1 | `/*` inside a comment |

The 7 sources that fail are these:

| File | Errors | Why |
| --- | --- | --- |
| `src/obj/USEITEMS.C` | 5 | prototyped handlers with `char` parameters passed as `void (*)()` |
| `src/gfx/LOADGR.C` | 4 | `FILE`'s `fd` field; `fileno(fp)` is Borland's macro for the same field and compiles to the same bytes |
| `src/game/GAMEWRAP.C` | 2 | `RestoreGame` and `SaveGame`, declared without prototypes and defined with `char` |
| `src/sys/EMS.C` | 2 | inline assembly; replaced by the port's EMS emulation |
| `src/combat/SPELLS.C` | 2 | the spell table holds functions of three signatures |
| `src/obj/OBJUSE.C` | 2 | handlers with `char` parameters passed as `void (*)()` |
| `src/conv/BARTER.C` | 1 | a call before the function's declaration |

These sources have the most warnings: CONVERSE.C (55), BABL.C (16), PANELS.C (13), SCDEVENT.C (11), GRDB.C (10) and MOTION.C (9).

The warnings most likely to break the port at run time are the 33 near pointers held in integers and the long arithmetic among the 67 narrowing warnings. The calls without prototypes are numerous but mechanical. Clang does not warn about 16-bit wrap or promotion at all, so the measurement cannot count them. The promotion audit in Milestone 2 counts them.

### What a link still needs

The objects reference 308 names that no compiled object defines and the host C library does not provide. Of those, 90 are defined in the 7 sources that do not compile yet (SPELLS.C 20, BARTER.C 25, GAMEWRAP.C 11, LOADGR.C 11, OBJUSE.C 10, USEITEMS.C 8, EMS.C 5), and they will resolve once those sources compile. The other 218 are the port's work. Names that only the failing sources use are not in the count yet.

| Where from | Names | Port work |
| --- | --- | --- |
| `src/gfx/GRCORE.ASM` | 36 | graphics library entry points and state: `show`, `rectangle`, `string_to_screen`, `setup_font`, `set_the_window`, `grPageFlip`, `palette`, `Ytab`, the window bounds |
| `src/sound/AIL.ASM` | 37 | the AIL API |
| `src/sys/SYSENTRY.ASM` | 22 | input and start-up: `key`, `mouse`, `mbuttons`, `Time`, `Shift`, `Ctrl`, `Alt`, `key_on`, `Asc`, `MouseDx`, `joy_position`, `cPerror` |
| `src/sys/C3DENTRY.ASM` | 19 | the 3D entry points and maths: `cRender`, `cInit3d`, `cFBtoScreen`, `cPlaceFB`, `cZoom`, `cSinCos`, `cFstSinCos`, `cAtan2`, `cSqRt`, `cFrmtoRaw`, `cPlayer`, `cDbase` |
| `src/3d/TMAPOPS.ASM` | 13 | the renderer's EMS page state and caches (`EmsBuff`, `tmap_fpage`, `crit_fpage`, `grs_3dinf`) |
| `src/gfx/MODEX.ASM` | 12 | the palette loader and the far string helpers (`str_len`, `str_copy`, `mem_set`) |
| `src/gfx/SPRITE.ASM` | 8 | sprites |
| `src/gfx/VALLOC.ASM` | 4 | video memory allocation, and saving and restoring rectangles |
| `src/sys/FARDATA.ASM`, `src/sys/INT0TRAP.ASM` | 3 and 3 | buffers; the divide trap |
| `src/gfx/SCALEBM.ASM`, `src/3d/PGCACHE.ASM`, `src/3d/SETPNT.ASM`, `src/lib/OVERLAY.ASM` | 3, 2, 1, 1 | `cXfer`, `grs_off`, `obj_tab`, `SetPnt`, `_OvrInitEms` |
| far data taken from the EXE | 10 | `smooth_div`, `smooth_base`, `smooth_lowpass`, `bmsegoff`, `bmhgtoff`, `_dblen`, model data, `ATM_Strings` (a name inside `stdat`), `cmpbuf2_start`, `sound_fpage` |
| DGROUP gaps placed by `tools/extract.py` | 7 | `Inventory`, `curelem`, the `STRINGS.PAK` state, `CutsceneOrConversationStringBlock` |
| Borland's library | 18 | `farmalloc`, `farfree`, `farcoreleft`, `coreleft`, `movedata`, `intdosx`, `getvect`, `setvect`, `outportb`, `filelength`, `tell`, `eof`, `getcurdir`, `itoa`, `ltoa`, `strupr`, `strnicmp`, `_ctype` |
| the portability layer | 19 | `port_mk_fp`, `port_fp_seg`, `port_fp_off`, the pseudo-register file, and the `bc_` file, time and random functions |

The C calls only the entry points, so the assembly modules' work is larger than these names. The whole of seg004 and seg003 has to be written, not only the routines C calls.

One name the game defines, `errmsg`, is also in the host C library. The game's definition wins in the link, but the port renames it to avoid confusion.

## Milestone 2 results

Milestone 2 made the shared C compile and link on the host, with no change to the DOS bytes. It took eight steps. Each ended with `make check` passing and `make port-check` run again, and the whole was proved at the end with `make check-all`. The gate rejected none of the changes. Run on 2 October 2026, with Apple clang 21, the counts are these:

| After step | Sources that compile | Errors | Warnings | What changed |
| --- | --- | --- | --- | --- |
| Milestone 1 | 92 of 99 | 18 | 233 | |
| 1. Errors | 98 of 98 | 0 | 225 | EMS.C left the port's list |
| 2. Near pointers in integers | 98 | 0 | 195 | 30 near pointers in integers to 0 |
| 3. Prototypes | 98 | 0 | 108 | 87 calls without prototypes to 0 |
| 4. Explicit widths | 98 | 0 | 68 | 64-bit narrowing from 67 to 33 |
| 5 to 8 | 98 | 0 | 61 | far pointer warnings from 9 to 0 |

The 61 warnings left are these: 33 narrowings of the host library's 64-bit results (`strlen`, `tell`, `filelength`, `ftell`, `st_size`) and of pointer differences, none of them a pointer; 14 `char *` against `unsigned char *`; 6 empty `if` bodies and 5 functions that fall off their end, both kept for matching; one `abs` of an unsigned value, one `/*` in a comment and one `printf` format in a call Turbo C drops (`if (0) printf(...)`).

### 1. Errors

- LOADGR.C reads `fileno(grfp)`, which is Borland's macro for `grfp->fd` and compiles the same.
- `RestoreGame` and `SaveGame` are declared with `OLDSTYLE` (above).
- The spell tables cast their two odd entries to `SpellFn`; OBJUSE.C and USEITEMS.C cast the eight handlers they pass to `UseThing`.
- `play_slot_hit_abs` is declared in `conv.h`.
- EMS.C is marked `port: dos-only`. Its pseudo-registers and inline assembly talk to the EMS driver, which the port replaces as a whole (`src/port/mem/`), so the port never compiles it, and `tools/portcheck.py` lists its names as the port's to provide.

### 2. Near pointers in integers

All 30 left after step 1 are gone (EMS.C had the other 3), as described under "Near pointers kept in integers". The input handlers and the `gronk_` callbacks whose own parameter stays an `int` are cast where they are registered: 62 input handlers and 4 critter callbacks.

### 3. Prototypes

All 87 calls without a prototype that were left after step 1 have one, as described under "Function prototypes and char parameters". The 51 built-ins CONVERSE.C binds with `bab_fun` are cast to `BablFn`. The six declarations CONTRIBUTING.md lists as kept in their files keep their `char` parameters; they already had prototypes, so the port compiles them unchanged.

### 4. Explicit widths

`tools/widths.py` rewrote 1,210 type specifiers by the policy above: 302 in the headers and 908 in the sources. clang then found 60 or so locals and parameters whose address is passed where an `int16 *` or `uint16 *` is now expected, and those were changed by hand to the explicit width, which is their DOS width. No narrowing of `long` to `int` is left.

### 5. Layout check

`tools/layoutcheck.py` (results above): 46 records in the headers, 38 identical and 8 different, all 8 with pointer fields. All 21 file records are identical, including every record with bitfields, so no file record needs accessor macros in place of its bitfields.

### 6. Promotion audit

`tools/intaudit.py` listed 1,894 sites on its first run. Fifteen sites in ten files are fixed, each with a cast or an explicit width that gives the DOS result and keeps the DOS bytes:

- the heading arithmetic that relies on the 16-bit wrap: DRAWOBJ.C's critter facing (`cPlayer->heading + headmod[quad]`, which without the wrap could index `dirtab` out of range), PATHFIND.C's `theta` and `vector`, PHYSICS.C's `h` in two functions with `abs((int16)(PlayerFacing - h))` and `(uint16)(PlayerFacing - h)`, and MOTION.C's `diff`;
- unsigned 16-bit results that DOS compares or shifts as unsigned and that can go below 0: a light's quality minus 1 in PLAYTIME.C (at quality 0 DOS takes the large unsigned value), a tile's height minus 1 in WORLDEV.C and in three slope tests in VIEW3D.C (at height 0), CUTS.C's frame and page counts minus 1 in three places, and TMPALLOC.C's EMS page count;
- GRDB.C's `loc[lab] == (uint16)-1`, where `loc` became `uint16` in step 4 and the host would compare 65535 with -1.

The final run lists 1,867 sites, by where the value goes and why it can differ:

| Category | Overflow | Signedness |
| --- | --- | --- |
| compare | 212 | 49 |
| divide | 222 | 8 |
| shift-right | 30 | 2 |
| condition | 8 | 1 |
| widen | 45 | 3 |
| index | 206 | 13 |
| int-store | 449 | 95 |
| int-arg | 511 | 35 |
| return | 19 | 0 |
| other | 23 | 0 |

Most of them cannot happen with the game's values: the tool assumes any `int` can hold any 16-bit value, so `skill + 0x1F` counts. The list (`build/port/intaudit.txt`) is the place to look when a replay disagrees. Two groups need a decision before the replay suite: the dot products in GRIDDB.C's face tests (lines 555 and 607), which DOS computes in 16 bits and tests for `< 0`, and the input turn rate in PHYSICS.C (`rate * PlayerTurn * (TurnInpRate / 4) / 4`). Both would overflow 16 bits only with values the game probably never has; casting the sum to `int16` before the test is the faithful fix if the replay shows otherwise.

### 7. Null pointers

clang's static analyzer (`clang --analyze`, all default checkers) over the port build reports 121 warnings. Eight concern null pointers: seven null dereferences and one null argument to `strcpy`. Three of them are real in the original, and reading the code found one more:

- COMBAT.C, melee damage: when a blow destroys a door, `def` is set to 0 and `OBJ_Z(def)` then reads the vector table at 0000:0002, the segment of `int0_trap`. The effect's height therefore depends on where DOS loaded the game. A far null read, marked `FARNULLTRAP`. ([FINDINGS.md](FINDINGS.md#smashing-a-door-reads-the-interrupt-vector-table))
- SCROLLIO.C, `wdialog`: Escape copies `initial` into the result, and CONVERSE.C passes 0 for a conversation's typed answer, so the answer becomes the string at DS:0. A near null read, marked `NULLTRAP`. ([FINDINGS.md](FINDINGS.md#escape-at-a-conversations-typed-answer-reads-ds0))
- BABL.C, `convert_string`: `bab_malloc`'s result is not checked, so a full conversation heap makes the substituted text overwrite the vector table, and `bab_realloc` of the null result reads below it. A far null write, marked `FARNULLTRAP`.
- LOADGR.C, `get_pals`: `PalStore` is never set, so `*PalStore` reads and writes DS:0, the overlay stub; but only `.CR` files reach it, and nothing loads one. Marked `NULLTRAP`.

The other four are false: GAMESORT.C's `do_objsort(0L)` reaches `link` only through `Obj_PtrTMem`, which returns 0 for a null link; and INPUT.C goes on past a failed `realloc` only into `pfatal_code`, which does not return. Known cases the analyzer cannot see, since they cross files, are CRITTIME.C's lava rescue, where `Map_GetAddr` returns 0 (already in FINDINGS.md), and SCDEVENT.C's `ev_trigger`, which sets off a trigger with no one setting it off, so that TRIGGER.C's `UseTrigger` reads the vector table (marked `FARNULLTRAP`; [FINDINGS.md](FINDINGS.md#a-scheduled-trigger-reads-the-interrupt-vector-table)). `-DNULLTRAP` compiles under Turbo C (checked with SCROLLIO.C and COMBAT.C).

### 8. Far pointers and the link

`FAR_COPY` replaced 32 of the 34 `movedata` calls, and SCDEVENT.C's two offset comparisons use `FP_OFF`. `tools/portstubs.py` wrote 19 stub files in `src/port/stubs`, with 160 functions and 80 variables, from the 240 names the link needs:

| Where from | Names |
| --- | --- |
| assembly modules | 171: GRCORE 39, AIL 37, SYSENTRY 22, C3DENTRY 19, TMAPOPS 14, MODEX 14, SPRITE 8, VALLOC 5, SCALEBM 3, FARDATA 3, INT0TRAP 3, PGCACHE 2, SETPNT 1, OVERLAY 1 |
| Borland's library | 21 |
| the port layer (`port_`, `bc_`) | 24 |
| far data taken from the EXE, and DGROUP gaps | 19 |
| EMS.C, DOS-only | 5 |

`make port` (`tools/portbuild.py`) compiles the 98 game sources and the stubs and links `build/port/uw2port`, a 788 KB arm64 Mach-O executable. Run, it stops at the first stub it reaches, `getvect` in `init_world`.

### Found on the way, for later milestones

- Structs with pointers laid over shared buffers grow on the host: CUTS.C's `struct Stdat` and `struct CutsState` over `stdat`, and BABL.C's heap blocks (`struct BabBlock`), whose header size the allocator writes as the numbers 8 and 12. The port's `stdat` must be large enough, and the conversation heap needs its header size from `sizeof`.
- `ATM_Strings` is a name inside `stdat` (offset 468Eh). Its stub is a separate array; the real one must sit inside `stdat`, since the notes and the LZSS work area overlap there (FINDINGS.md).
- Handlers called through a pointer of another type: `sp_true_sight` in the area spell table takes `(caster, target)` but is called with `(x, y, target, tile, type)`, and the critter collision handlers take a `struct Phys *` but are given the address of an `unsigned` collision state. Both work in DOS by layout; the port must keep them working the same way.
- The assembly replacements should take explicitly sized parameters: the C passes them `int` values computed without the 16-bit wrap, e.g. `cSinCos(cPlayer->heading + 0x2040, ...)`.

## Milestone 3 results

Milestone 3 boots the port to the title. Run on 2 October 2026 on an Apple Silicon Mac, with Apple clang 21 and SDL 3.4.16, `make port` links `build/port/uw2port` (about 1 MB, arm64), and `build/port/uw2port --data ~/UWGOG/UW2` boots the GOG release's data in an SDL3 window in about a second, as far as this:

1. `init_world` runs to its end. Every step of it works on the user's data: the divide trap; EMS (the emulated driver has 512 pages, and `init_mem` asks for 89 to 103 of them by `rand`); the data directories and the eight scratch files; STRINGS.PAK; the map; input; UW.CFG; sound with no card (UW.CFG says `0 -1 -1 -1 sound`) and the 256 Hz clock; the graphics library; the Origin screen, `display_screen(5, 6)`; the music (none, with no card); the video memory pool; all the art (ALLPALS.DAT and 20 .GR files, ten into EMS and ten into video memory); the Looking Glass screen, `display_screen(6, 7)`; the mouse, the objects, the 3D view's set-up, the player, the AI, lighting, the save directory, the working copies of LEV.ARK and SCD.ARK (written to the home directory's SAVE0), the conversation globals (BGLOBALS.DAT, also in SAVE0), and the game palette.
2. `titlescr` starts cutscene 9. `show_cutscene`, `show_anm` and `cuts_process_anm` run, and `cuts_process_lp` calls `copy_visible_to_hidden`, GRCORE.ASM's first entry with no C yet. The port stops there and leaves the window up (`--exit-on-halt` quits instead).

**The screens match DOS byte for byte.** `tools/portshot.py` builds, for each opening screen, a DOS EXE that stops on it (UWEDIT.C with `for (;;) ;` after the `display_screen` call, compiled with Turbo C and linked as the modding build), boots it in headless DOS and screenshots it, and compares that with the port's scan-out right after the same page flip (`--shot-at-flip`). js-dos doubles mode X to 640 by 400, so every second pixel is compared. Both convert the DAC's 6-bit colour the same way, so equal pixels mean equal palette indices under equal palettes:

| Screen | Call | Flip | Pixels that differ | Colours |
| --- | --- | --- | --- | --- |
| Origin, "presents" | `display_screen(5, 6)` | 1 | 0 of 64,000 | 223 |
| Looking Glass Technologies | `display_screen(6, 7)` | 2 | 0 of 64,000 | 199 |

Neither screen stays up long in either build: the first is replaced when the art has loaded, the second when the title cutscene starts. An unheld DOS boot under js-dos shows neither in screenshots taken 150 ms apart, which is why the comparison holds both builds on the screen instead of timing them.

### What replaces what

Each replacement is written from the DOS assembly, with its comments, and keeps the module's data at the assembly's offsets in the same segment, so a state dump can compare it with DOS. Where seg003's data holds near code offsets (the span writer in 4112, the pens' writers, the pen set-up table, the transfer in 0DC2), they keep their DOS values and the C dispatches on them, and an offset with no C yet stops the port with its address. The FM Towns routines with the same names were read beside them (`show`, `set_the_window`, `init_graphics_`, `lsqrt`, `sincos`, `atan2`, `mouse`, with `tools/fmt.py`). They do the same jobs for the FM Towns' own video hardware, and DOS was followed throughout. One difference touches arithmetic: FM Towns' `mouse` halves the motion with `sar`, which rounds down, where DOS's `_6F8` divides by 200 with `idiv`, which rounds towards zero; the port does what DOS does.

| Replaced | By | What |
| --- | --- | --- |
| SYSENTRY.ASM, STARTUP.ASM, SYSINIT.ASM, CPUTYPE.ASM, VIDSAVE.ASM, SYSLIBL.ASM, TICKS.ASM, TICKREAD.ASM, JOYPORT.ASM's `_30A` | `sys/sysentry.c` | seg021's start-up and shut-down, the exit chain and its message, the far pointers into dseg062_62a6, `mouse`, `mbuttons`, the game clock (a 386, a 101-key keyboard, no joystick) |
| KEYQUEUE.ASM, KBDINT.ASM | `sys/keyqueue.c` | the int 9 ring buffer and `key`, as described under the platform layer |
| MOUSEDRV.ASM | `sys/mousedrv.c` | int 33h from pointer events |
| IMATH.ASM, C3DENTRY.ASM's `cSinCos`, `cFstSinCos`, `cAtan2`, `cSqRt` | `sys/imath.c` | table sines with imul's middle word, arcsines, `atan2`, Newton square roots with div's carry |
| C3DENTRY.ASM's data and `cInit3d` (with GRDISP.ASM's `_5363`) | `sys/c3dentry.c` | the far pointers into the renderer's data; the 3D view's window |
| INT0TRAP.ASM | `sys/int0trap.c` | the divide trap, on SIGFPE |
| OVERLAY.ASM | `sys/overlay.c` | `_OvrInitEms`, which has nothing to do |
| Borland's C library | `sys/borland.c` | the `bc_` functions, handle I/O and text mode, `findfirst`, `intdosx` (3Fh, 40h), `getvect`, `getdfree`, port I/O, the string extras, `_ctype` (from the EXE's DGROUP), Borland's `rand` |
| EMS.C (DOS-only) | `mem/ems.c` | the emulated EMS 4.0 driver: a page store and the frame at E000h, by copying |
| FARDATA.ASM, the far data taken from the EXE, TMPALLOC's page variables | `mem/fardata.c` | the far segments 3705:0000 to 5F5D:104C as one block in the EXE's layout, with the names inside it as labels; dseg062_62a6; PGCACHE.ASM's `lightabs`; all read from the user's EXE |
| segment arithmetic, Borland's far heap | `mem/parmap.c` | the paragraph map, `farmalloc` and the rest, `movedata`, the null-pointer copies |
| GRCORE.ASM (in part) | `gfx/grcore.c` | the graphics globals, `init_graphics`, `init_colors`, `set_the_window`, `set_the_color`, `clear_window`, `rectangle`, `urectangle`, `show`, `setup_font`, `grPageFlip`, `grSoftPageFlip`, the video memory bump allocator `_49AE` |
| VIDMODE.ASM (in part) | `gfx/vidmode.c` | mode X, the mode's pages, Ytab, the edge masks, page flips, the window and its guards, pens, the solid span writer, show's clipping and row records |
| GRLIBF.ASM, GRLIBL.ASM, GRLIBI.ASM (in part) | `gfx/grlibf.c`, `grlibl.c`, `grlibi.c` | clear_window and urectangle; the opaque and transparent bitmap transfers; the text masks and `setup_font` |
| MODEX.ASM (all but `grab`) | `gfx/modex.c` | `local_do_palette`, the console print, the far string helpers, `gr_pixel`, the video memory pictures (2A2, 320, 361) |
| VALLOC.ASM (all but `save_rect` and `restore_rect`) | `gfx/valloc.c` | the video memory pool, `valloc`, `vfree` |
| PGCACHE.ASM's data | `3d/pgcache.c` | `grs_off`, `obj_tab` |
| AIL.ASM | `sound/ail.c` | the timer services on the PIT thread; every driver call returns 0, as AIL does for a handle with no driver |
| the VGA, the PIT | `gfx/vga.c`, `sys/pit.c` | the emulated hardware |

The stubs went from 160 functions and 80 variables in 19 files to 38 functions and 9 variables in 10 files. `tools/portstubs.py` now leaves out every name the port's own C defines. The functions left are the rest of GRCORE.ASM (text, lines, copies between pages, the virtual screen, the frame buffer), SPRITE.ASM, SCALEBM.ASM's `cXfer`, SETPNT.ASM, C3DENTRY.ASM's renderer entries and callbacks, the joystick reads, `save_rect`, `restore_rect` and `grab`. The variables left are DGROUP gaps and `sound_fpage`, which work as zeroed variables.

### Shared-source changes

One, proved by the gate: `AX_RESULT` (`portable.h`), used once, at the end of GRFX.C's `grfx_init`. The original has no return statement there, and `init_world` tests what `grfx_quikfont` left in AL, which is `grfx_load_font`'s result; on the host the function returned nothing defined and the port stopped with error D003. `make check` passes, and `make port-check` shows 0 errors and 60 warnings (61 before).

### Found on the way, for later milestones

- **Four more functions fall off their end** and have callers that use the result: CONVERSE.C (line 805), SCHEDULE.C (113, on some paths), TRIGGER.C's `SetOffTrap` (145) and CUTS.C (414). Each needs `AX_RESULT` with the value DOS leaves in AX, read from the code.
- **A stream and its handle together.** LOADGR.C reads `fileno(grfp)` after `fseek(grfp, 0L, 1)`, which Borland's library made agree with the handle by emptying its buffer. The host's stdio keeps its buffer, so the offsets table came back wrong and every art file failed (error D004). Making the streams unbuffered (`setvbuf`) was enough on macOS and Linux but not on Windows: Microsoft's UCRT reads two bytes ahead even for an unbuffered stream, and its `fseek` to a place inside that buffer only moves the buffer pointer, so the handle stayed a byte ahead and the same D004 came back. The port now does a stream's `fread`, `fwrite`, `fseek`, `ftell`, `fgetc` and `fgets` itself, on the handle (`sys/borland.c`), so the stream and its handle are always at the same place with any C library.
- **String literals are read-only on the host.** `init_sounds` writes the driver's name into the literal `"dm00.adv"`, which crashes the host. It runs only with a sound card configured, so the GOG configuration never reaches it; Milestone 5 met it first and gave it `WRITABLE_STR` (portable.h).
- **Division by zero does not trap on arm64.** x86-64 raises SIGFPE and the port runs the game's int 0 handler; Apple Silicon gives 0 and goes on. The promotion audit's divide sites are where to look if a replay disagrees.
- **CUTS.C writes the screen through a far pointer** (`MK_FP(0xA000, 0)`, with the map mask set by `outportb`). A host pointer cannot apply the map mask, so the planar writes need a macro whose Turbo C expansion is the original store and whose host expansion calls `vga_write`.
- **EMS mapping by copying** cannot show one logical page mapped into two frame slots at once, as real EMS can. The game does it: after a cutscene (which borrows the textures' pages) the textures are reloaded through slots 0 to 2, and the renderer then maps the same pages into slot 3. Copied from the store alone, slot 3 got the cutscene's pictures, and rc8 showed the throne room's floor red after Lord British's talk. `ems.c` now writes a shared page back from whichever slot's copy differs from the store, and refreshes the other slot from it. Writes through both copies between two mappings would still be lost; no such case has been seen. `mmap` aliasing, above, would remove the gap.
- **The clock is read without a barrier.** The game reads `*Time` directly while the PIT thread increments it. The port is built without optimisation, so every read happens; `GAME_TIME()` (Milestone 4) makes it explicit.
- **Paragraph map windows.** `intoFarBuffer_ovr167_5DA` and `FarWrite_ovr167_627` take `FP_SEG` of near buffers on the stack, which makes a window each time (`-v` logs them). The idiom deserves a named macro.

## Milestone 4 results

Milestone 4 built the record and replay harness and took the port from the title into the game, at first as far as the first 3D frame; the 3D renderer that completes it has its own sections below ("The 3D renderer" and its results). This section is the harness and the way into the game, as they stood before the renderer. Run on 2 October 2026 (Apple clang 21, SDL 3.4.16, js-dos through dos-mcp 0.3.0), with the standard session `newgame` (boot, the title cutscene, Escape twice through the introduction, the main menu, Create Character with every default, the name Avatar, then sixteen seconds in the game):

- **DOS replays itself exactly.** The session was recorded in DOS (28 million hook calls, a 27 KB recording) and replayed twice; all 43 checkpoints of the two replays are identical, in every section: the player record, the level, Borland's rand seed, seg003's data, the DAC and all 256 KB of video memory. Two things had to be found first: the stop key, and a local the game reads before setting it (below).
- **The port replays it exactly as far as it goes.** Every checkpoint from boot to the game screen, 37 of them, is identical to DOS's, in every section, including the video memory of every screen: the title cutscene, the introduction's first frames, the main menu, each of character creation's screens with its text and buttons, the name typed, the confirmation, and the game screen drawn before its first 3D frame (`CHECKPOINT(4)`). The port then stops at `cRender`, the 3D renderer, which has no C yet.
- **The exit test was met up to the renderer.** What was left was the first 3D frame and the rest of the session, which needs `cRender` for every frame; the renderer's results below complete it.

### The replay harness

**Hooks.** `portable.h` has the hooks (CONTRIBUTING.md, "Writing for both builds"): `GAME_TIME()` (71 reads of `*Time` in 19 files), `KEY()`, `MOUSE()`, `MBUTTONS()`, `JOY_READ()`, `JOY_BUTTONS()`, `WALL_TIME()`, `SRAND()` and `CHECKPOINT(n)` (five, in `main` and `strt_demscr`). Under Turbo C each is the original tokens, and the gate proves it. Under `-DREPLAY` and on the host they call `src/replay/REPLAY.C`, which in DOS is linked into the modding build as one more resident module (`tools/link.py --mod --add`). Recording, a hook reads the world as the game would and writes what it got; replaying, it returns what was recorded and leaves the world alone (key events restore seg021's key state, `key_on` and the modifiers, from the recording). The DOS build replays when `REPLAY.IN` is in the game's directory and records otherwise; the port takes `--record` or `--replay FILE` and otherwise reads the world.

**The recording.** The game polls its inputs hundreds of thousands of times a minute and almost always gets what it got last time, so each kind of input is a stream of runs, a count and a value; the program makes its calls in the same order on replay, so the streams need not record how they interleave. The streams are written in chunks of up to 200 bytes, tagged with the stream, as their buffers fill. The header holds the call count at which recording stopped: F12 stops it (the game never sees the key), and a replay stops at the same call. The session above is 18.3 million clock reads in 8,443 runs and 3 million polls of each input in 47 to 107 runs.

**The dumps.** Both builds write `STATE.OUT`: a record per checkpoint (a `CHECKPOINT`, every 400h ticks of the replayed clock, each key event, each change of the buttons, the end), named by the call count and the clock. Each has the player record, the rand seed, the level block once one is loaded, the null-pointer area, the far blocks' segments and the calls per stream; the full ones (all but the periodic) add seg003's data, the DAC, six CRTC registers and the four planes of video memory. `src/replay/REPLAY.C` has the formats.

**The tools.** `tools/replay.py` builds the replay DOS EXE, records and replays in DOS through `tools/replaydos.mjs` (js-dos, about forty seconds a run), replays in the port, and compares two dumps checkpoint by checkpoint, the screen also as the CRT controller would show it; `check` does all of it. Two ranges of seg003's data are machinery and are not compared: its private stacks (370D:4E00..4FA7 and 5500..5547), where interrupts push whatever is in the registers, and the tick count its retrace wait keeps (0652). Words that are a far block's segment are compared as segments, since the load segment differs between the replay EXE, DOS set-ups and the port. `UWRPCK` sets the periodic interval, and `UWRPTRACE=lo,hi` writes every hook call in a clock range, with its caller's address, to `TRACE.OUT`; that is how the next point was found.

**Stack junk.** The first DOS replays parted in the game by one poll of the mouse. The trace showed `player_attack` reading its local `held` before setting it when there is no attack key, and polling the mouse only when the junk is zero ([FINDINGS.md](FINDINGS.md#the-attack-check-reads-an-uninitialised-flag)). The timer interrupts write the game's stack, so the junk differs from run to run. `STACK_JUNK(0)` gives it 0 in the replay build and the port; clang lists five more such locals, not reached yet.

**Two port bugs the replays found.** Besides those under "What replaces what": the port's macros that store a word into the emulated data segments evaluated the value twice, so `SETW(o, W(o) + n)` read back its own new low byte and got the high byte wrong whenever the low byte carried (it showed as a lost record in VALLOC's block list and one sprite's background never saved); and the string entries' copy to 370D:4FA8 takes one byte past the string's 0, from the caller's buffer, which is stack junk, so the comparison leaves that byte out.

**The js-dos side.** dos-mcp's `fsRead` refuses a second read of a path, even one that failed because the file was not there yet, so `replaydos.mjs` waits for the batch's `DONE.TXT` by listing the directory, and every call into the page has a time limit.

### Null pointers

- **DOS writes.** Every dump has DS:0 to 30h, C0's null-pointer checksum over DS:4..30h and the interrupt vector table. Over the session the checksum stays intact, DS:0..3 always holds one of the two forms of ovr167's stub entry (`27 06 00`, or `06` and the overlay's segment while it is loaded), and no vector changes after `init_world`. After the game exits, dos-mcp's `read_memory` finds DGROUP by C0's copyright string and reads the same: DS:0..3 `06 77 7A 00` (ovr167 loaded), checksum intact, so no "Null pointer assignment".
- **DOS reads.** The replay build compiles the `NULLTRAP`-marked sources with `-DNULLTRAP`; none of the marked sites was reached.
- **The port.** `make port-debug` builds with UBSan's `-fsanitize=null`; over the whole session it reports no null dereference, and the port's stand-ins for DS:0 and the vector table were not needed.

### What replaces what

Each replacement is written from the DOS assembly, as in Milestone 3, keeping the module's data at the assembly's offsets so that the dumps compare it with DOS.

| Replaced | By | What |
| --- | --- | --- |
| VIDMODE.ASM (the rest) | `gfx/vidmode.c` | the span writers (solid on both pages, copy, copy onto both pages, save, restore, xor), pixels, vertical lines, the virtual screen and `vscreen_focus` (line compare, display start, panning), the linear buffer, and the bitmap entries with their three record builders (show, fbshow, `_5025`, `_511C`, vcopyfb, vcopy) |
| GRLIBF.ASM (the rest) | `gfx/grlibf.c` | page copies, pixel, lines, boxes |
| GRLIBI.ASM (text) | `gfx/grlibi.c` | `string_width`, the rasteriser into the bit buffer, the single-colour blitter, `shift_left_1` and the shadowed string |
| GRLIBL.ASM (the rest) | `gfx/grlibl.c` | the transfers from video memory (plane by plane, latched, transparent), screen to screen both ways, into the linear buffer |
| GRENTRY.ASM | `gfx/grentry.c` | the frame buffer: clear, fill, dim, light, `cPlaceFB`'s set-up (the copier's self-written ret is a variable), `cFBtoScreen`, its span writers and the Gouraud spans |
| GRCORE.ASM (the rest but `grab`) | `gfx/grcore.c` | the C entries, including the string entries' copy to 370D:4FA8 |
| SPRITE.ASM | `gfx/sprite.c` | the sprites, quirks included (FINDINGS.md) |
| VALLOC.ASM's `save_rect`, `restore_rect` | `gfx/valloc.c` | through pens 109h and 10Ah |
| EXPAND.ASM, PGCACHE.ASM's `uncmp_tab` | `3d/expand.c` | the image decoders (the record decoder's self-modifying ret is a flag) |
| SETPNT.ASM | `3d/setpnt.c` | `SetPnt` |
| C3DENTRY.ASM (all but `cRender`) | `sys/c3dentry.c` | `cPlaceFB`, `cFBtoScreen`, `cFillFB`, `cDimFB`, `cLiteFB`, `cZoom`, `cFrmtoRaw` |

The port's own fixes: the EXE's relocations are applied to the far data it loads (the segment words in seg003's data were the EXE's, not the loaded ones); the CPU's window at A000:0000 is a region of the paragraph map whose stores go to the VGA (`PLANAR_STORE`, and `mem_set` into it), with slack either side for offsets that wrap; the EMS page frame is mapped twice in a row, so that a pointer run past E000:FFFF wraps to E000:0000 as a far pointer does (the cutscene player walks a large page's records that far, and on the host it overwrote the paragraph map); and Borland's `srand` keeps only the seed's low word. The stubs left are `cRender`, `grab`, the two joystick reads, and nine zeroed variables.

### Shared-source changes

All proved by the gate (`make check` passes; `make check-all` too):

- `portable.h`: the replay hooks, `PLANAR_STORE`, `STACK_JUNK`, `FILE_RECORDS` and `FILE_RECORDS_END`, `AX_LAST`.
- `GAME_TIME()` in 19 files; the input hooks in MOUSE.C, JOYSTICK.C, UWEDIT.C, CHARGEN.C, BABLHACK.C and BARTER.C; `CHECKPOINT` 1 to 5 in UWEDIT.C.
- CUTS.C's planar writes through `PLANAR_STORE` (five stores).
- COMBAT.C's `held` with `STACK_JUNK(0)`.
- Locals filled by a file read given their DOS width: GAMESTRN.C's `read_string` (on the host the upper bytes were junk, so every string from STRINGS.PAK came back empty) and ARC.C's three block counts.
- CHARGEN.C reads `DATA\chrgen.dat` through `FILE_RECORDS`: its question records are laid straight over the file's bytes, but `struct ChrOpt` holds pointers, so on the host it has another layout.
- `AX_RESULT` or `AX_LAST` at the end of CONVERSE.C's `check_inv_quality`, SCHEDULE.C's `do_migrations`, TRIGGER.C's `SetOffTrap` and CUTS.C's `readlp`, the values read from the code.

`make port-check` shows 0 errors. The tools: `link.py --add`, `sources.py` (src/replay, which the gate skips), `portbuild.py --debug`, `portcheck.py` and `portstubs.py` (which now include REPLAY.C), `replay.py` and `replaydos.mjs`.

### Screens

`tools/portshot.py`'s two held screens still match (0 of 64,000 pixels). The replay comparison is stronger than further hold points: it compares all four planes of video memory and the DAC at every full checkpoint, on every screen the session passes through, so no new hold points were added.

## The 3D renderer

The renderer is seg004 (14 modules in `src/3d`, about 33 KB of 386 code) and the parts of seg003 it calls, entered through seg021's `cRender`. It is hand-written threaded code: the model interpreter's handlers jump into one another through a table with no central loop, and a handler may read a register the previous one left (`do_hull` reads DH, [FINDINGS.md](FINDINGS.md#do_hull-culls-with-the-previous-handlers-dh)); flags cross calls (`sphere_check` returns its verdict in the carry); routines discard their callers' return addresses (`load_cpg`'s `add sp,4` back to the dispatch, CLIP.ASM's `mov sp,[C880]` abort, the polygon clipper's `pop ax; pop ax; ret`); divide faults are part of the control flow (nine int 0 handlers, some resuming after the faulting `idiv` by its length, three dropping the interrupt frame and jumping into a clipping path); and code is patched at run time (immediates, a jump condition, an `add` turned into a `sub`, whole routine bodies copied over `mxmul`) or generated (SCALEBM.ASM's sprite scalers). A C rewrite in the style of the earlier milestones would have to model each of these by hand, and the frame buffer has to match DOS's to the pixel.

So the port translates these modules instruction by instruction, keeping the CPU's state as the CPU kept it, and writes by hand only what the translation cannot express.

### How the assembly becomes C

`tools/asm2c.py` reads each module's matched `.ASM` source and the bytes the gate proves it assembles to (the module's place in `UW2.EXE`, as `tools/match.py` reads it), decodes those bytes with iced_x86, and walks the two together: the source gives the labels, the procs, the data and the comments, the bytes give each instruction's address, length and operands, and the two must agree on every mnemonic (TASM's padding `nop`s after forward jumps it sized for the worst case are recognised; instructions the source writes as bytes, the far jumps TASM would shorten and `do_goursurfv`'s `db 0A8h`, are decoded as what they are). Each module becomes one C function, `asm_mod_NAME(entry)`, in `src/port/3d/` or `src/port/gfx/`, with a label for every instruction (code jumps into the middle of unrolled loops through tables, GRLIBI.ASM's 49C4) and the source line above each translated instruction. A flag is computed only where some instruction can read it, by a liveness pass over the module's flow graph that counts every exit as a reader. 25 modules are translated:

| Segment | Modules | C |
| --- | --- | --- |
| seg004, the renderer | EXPAND, SPHERE, SMOOTH, INTERP, INSTANCE, TMAPOPS, CLIP, PERTLP, PERTP, PROJPOLY, SCANLINE, TEXMAP, TEXMAPV, PGCACHE | `3d/expand_x.c`, `sphere.c`, `smooth.c`, `interp.c`, `instance.c`, `tmapops.c`, `clip.c`, `pertlp.c`, `pertp.c`, `projpoly.c`, `scanline.c`, `texmap.c`, `texmapv.c`, `pgcache_x.c` |
| seg003, the graphics library | GRMISC, GRENTRY, SCALEBM, VIDMODE, GRLIBF, GRLIBG, GRLIBH, GRLIBI, GRDISP, GRLIBM, GRLIBN | `gfx/grmisc.c`, `grentry_x.c`, `scalebm.c`, `vidmode_x.c`, `grlibf_x.c`, `grlibg.c`, `grlibh.c`, `grlibi_x.c`, `grdisp.c`, `grlibm.c`, `grlibn.c` |
| seg021 | IMATH (the renderer's `sincos` and square root) | `sys/imath_x.c` |

The seg003 modules and IMATH are translated because seg004 calls into them with registers. Where a module also has hand-written C (`_x` in the file name), the two share its routines out, each routine in one of them only (One implementation per routine, below). The generated files are committed and `tools/asm2c.py --check` says whether they are up to date; nothing in them is game data that the committed `.ASM` sources do not already hold.

### The machine it runs on

The runtime's `port/x86/asmrt.h` is the 386 the translated code runs on (with UW2's side in `src/port/asmgame.h`). The registers are global, so what one routine leaves in a register is there for the next. The segment registers hold DOS paragraphs as the port's paragraph map numbers them, each with the host address of its offset 0, and every memory access is that address plus a 16-bit offset, which wraps as in DOS. The code segments seg003 and seg004 are one block of host memory holding their bytes from the user's EXE (`mem/fardata.c`), so the data the modules keep in their code segments (`lightabs`, the XFER tables, the polygon clipper's variables) is where they read it, and `cXfer`, `uncmp_pal`, `lightabs` and seg004's 16 bytes at 6D20h are names inside it. The stack is the emulated one, in the memory SS:SP points at (seg021's private stack in FD71, seg003's in 370D).

A call pushes its return address on that stack and runs the callee through `asm_call`, which follows the callee's jumps until a return pops what it pushed; when a routine discards return addresses or resets SP, the C calls in between unwind as far as the stack pointer says, so the stack decides, as it did in DOS. A divide that would fault goes to `asm_divfault`, which runs the handler the renderer installed at FD71:05A5 (saturate, halve and divide again, flag the polygon, or push the interrupt frame and jump to the clipping path); a fault with a handler that would resume in the middle of the instruction stops the port with its address. An instruction whose immediate, displacement or jump condition another instruction patches reads it from the code block at run time, and the tool refuses an instruction with a patched byte it does not read. `x86/glue.c` is where the translated code meets hand-written C: seg021's tick counter, `MousQUp` for `do_mouseq`, GRCORE.ASM's jump table entries, the routines of translated modules that are hand-written C (below), the span writers (through `seg003_call`), and the DOS and EMS interrupts `load_cpg` and the page mapping make.

What the tool's rules do not cover is in its `OVERRIDES`, each with its reason: `mxmul` runs whichever body `check_flat` last copied over it (the code block says which); `modify_axis2` adds or subtracts as its patched opcode byte says; EXPAND.ASM's record decoder returns where its patched `ret` is; `do_uwobj` reaches LOADGR.C's `Palettes` as a C object rather than a DGROUP offset; `do_fetchmap`'s `tmcolor`, which reads upper memory in DOS and is never read, is set to 0 ([FINDINGS.md](FINDINGS.md#do_fetchmap-reads-tmcolor-from-the-wrong-segment)); `in_scalebm` draws through the sprite hook; and `scale_nibble_bitmap`'s call of the code its generator wrote runs `gfx/scalebm_code.c`, which interprets the 14 instructions the generators write. `UW2PORT_ASMTRACE=1` writes every entry into the translated code, with the registers, to the standard error.

### One implementation per routine

Milestones 3 and 4 wrote much of the graphics library by hand, and Milestone 4 then translated whole modules for the renderer, so for a while many routines had two implementations: the game's C called the hand-written one and the translated code the translated one. Milestone 5 keeps one of each, by this rule: the hand-written C where the replays reach it, since they prove it gives DOS's results and it is the readable one, and the translation everywhere else, since it is the instructions themselves. Coverage over all eight sessions (`tools/portbuild.py --coverage`, `xcrun llvm-cov`) said which routines the replays reach, on each side.

`tools/asm2c.py`'s `HANDWRITTEN` lists, per module, the ranges of offsets that are hand-written C, with where the C is. The tool translates the rest of the module only: a jump or fall-through into a hand-written range leaves the module function (`ASM_JMP`), and `asm_exec` sends any call or jump into one to `x86/glue.c`, which does the routine's work on the emulated registers, or in seg003 to `seg003_call`, which runs a span writer with its list at SI. The tool lists every place in the ranges that the remaining translated code calls or jumps to (`asm_hand_calls` in `x86/modtab.c`), and the port checks at the first translated call that each has its C. The other way round, C that needs a routine the port has only as the translation calls it with the registers the assembly's caller loaded: `seg003_asm` (`gfx/grcore.c`) on seg003's data and stack as GRCORE.ASM's entries switched to them, and in the same way `cAtan2` and `seg004_uncmp` for seg021 and seg004. `seg003_call` takes the hand-written C for an offset it has, and the translation for any other.

| Module | Hand-written C | The translation |
| --- | --- | --- |
| GRENTRY (seg003) | `gfx/grentry.c`: `fbuf_draw_ylrpp_x` (`_729`), `setup_frame_buf` (`_7A8`), `cFBtoScreen` (`_7F9`) and its copier (`_888`), `fbuf_save_vylr` (`_9FC`), `fbuf_setcolor` (`_B9B`) | `gfx/grentry_x.c`: the frame buffer's fill (`_6E2`, `_6E4`), dim and light (`_764`, `_788`), its solid and smooth span writers (`_9C9`, `_C13` and the routines its vectors name), the bitmap rows without transparency (`_6F8`), the rows to the screen (`_A7F`) |
| VIDMODE | `gfx/vidmode.c`: the bitmap routines' clipping and row records (`_21D4`, `_21ED`, `_2214`, `_2250`, L2296, L2373, L243F), the mode and its pages, Ytab, the edge masks, the window and its guards, page flips, the display start, the virtual screen and line compare, the solid, copy, save and restore span writers (`_2D83`, `_2F18`, `_2DF3`, `_2F96`), plotting and reading a pixel, the pens | `gfx/vidmode_x.c`: the concave polygon fill (after L249A), the other span writers (`_2C9A`, `_2D0D`, `_2E3A`, `_2E79`, `_2FD6`, `_303B`), `_222B` and `_2B62` (the linear buffer), `_2242` (vcopyfb), `_30E3`, the vertical lines `_3121` and `_3164` |
| GRLIBF | `gfx/grlibf.c`: `clear_window`, `urectangle`, the whole screen, the copies between the pages, `uvline`, `uhline`; `gfx/grcore.c`: the video memory allocator (`_3206`) and `clip_rect` with `rectangle` and `box`'s clip (`_33BE`, `_3416`, `_341E`, `_336E`) | `gfx/grlibf_x.c`: the clipped lines, `ubox`, the pixel, the polygons (`_34C2`, `_34C7`) |
| GRLIBI | `gfx/grlibi.c`: `string_to_screen` (`_3B36`) with the rasteriser (4225h) and the single-colour blitter (`_3C61`), `string_width`, the text masks, `setup_font` | `gfx/grlibi_x.c`: the polygon edges (`_38B4` .. `_3B0D`), the other text entries (unclipped, shadowed, the bitmap blitter L4126, `shift_left_1`) |
| GRDISP | `sys/c3dentry.c`: `cInit3d`'s view window (`_5363`) | `gfx/grdisp.c`: the rest |
| EXPAND (seg004) | `3d/expand.c`: `exp_4str`, `exp_4run`, the palette (`_63`), the record decoder (`_18D`, `_194`), for `cFrmtoRaw` and, through the glue, for `do_uwobj` and `do_uwcrit` | `3d/expand_x.c`: `exp_8str`, `exp_5run`, `_CB`, `exp_8run` |
| IMATH (seg021) | `sys/imath.c`: `sincos` (`_A38`, far `_A34`), `fast_sincos` (`_A69`), `lsqrt` (`_A78`, far `_A30`), leaving the registers as the assembly does | `sys/imath_x.c`: the arcsine, arccosine and `atan2` (`_B78`, `_BA2`, `_BD4`), for `cAtan2` |

The other translated modules (GRMISC, SCALEBM, GRLIBG, GRLIBH, GRLIBM, GRLIBN and the rest of seg004) have no hand-written C, and the other hand-written seg003 modules (GRCORE's entries, GRLIBL, SPRITE, VALLOC, MODEX) no translation. Every routine left as the translation is one the replays do not reach on the hand-written side, so a slip in the C written by hand for it could not have shown. The crossings the replays do reach are the renderer's: its sines and square roots (`_A34`, `_A30`), `exp_4run` through `uncmp_tab`, and the translated `exp_5run`'s calls of the hand-written palette and record decoder (`_63`, `_18D`), all eight sessions identical to DOS with them. The other glue entries and the calls from C into the translation (`seg003_asm`, `cAtan2`, `seg004_uncmp`'s formats 4 and 6) are on paths no session reaches yet; each loads the registers the assembly's caller loaded, read from the code, and the replays will test them when a session gets there. The change took about 12,000 lines of duplicated C out of the port.

### The render interface and the sprite hook

`src/port/3d/render.h` is the renderer's interface, which a phase 2 renderer can stand behind:

- `struct uw_framebuffer` (pixels, width, height, pitch, rows counting up from the bottom of the view as seg003's do) and `port_render_3d(fb)`, which is cRender's work (clear the frame buffer, run the database through the model interpreter, stamp the frame time, age the critter page cache) drawn into `fb`. `cRender` calls it with the faithful frame buffer, `port_render_framebuffer`: stdat's, at the segment 370D:0958 names, laid out by `cPlaceFB`. The translated renderer addresses the frame buffer through seg003's row table (200 rows of 16-bit offsets in one segment), so in phase 1 it draws only into that one; a larger buffer is for a renderer that replaces it behind the same call.
- `struct uw_sprite_draw` and `port_set_sprite_hook(fn)`: every object picture and critter frame the renderer draws goes through one call, made in TMAPOPS.ASM's `in_scalebm` after the sprite is projected and before `scale_nibble_bitmap` draws it. The hook receives the kind (object or critter), the object itself (`struct Object *`, which DRAWOBJ.C names through the `RENDER_TAG(o)` hook in `portable.h`, nothing in DOS), the object or critter type, the critter's animation frame, the tile, the position within it and the height, the heading, the sprite's corner in view space, its projected rectangle on the screen, its distance shade (0 lit to 15) and the frame buffer. It returns 1 when it drew the object itself, say as a voxel model, and 0 to have the sprite drawn as in DOS; with no hook installed (the default) the sprite is drawn as in DOS without the call's cost. `UW2PORT_SPRITEHOOK=1` installs a hook that only counts what it is shown and returns 0: over the walk session it sees about 9,000 object pictures and 8 critter frames, and the dumps are identical to DOS's.

## Milestone 4 results: the 3D renderer

Run on 3 October 2026 (Apple clang 21, SDL 3.4.16, js-dos through dos-mcp 0.3.0), with two recorded sessions:

- `newgame` (Milestone 4's): boot, the title cutscene, the main menu, character creation, then sixteen seconds in the game standing still. DOS's dumps and the port's are identical at all 43 checkpoints, where Milestone 4 stopped at the 37th; the port renders every frame of the session.
- `walk` (new, `tools/replay.py record OUT --session walk`): newgame's way into the game, then about forty seconds in the first rooms: walking forward and back, turning left and right, sliding, looking up and down and level again, and a click in the view (a pick frame). It is 68 checkpoints, 21 of them full ones with a 3D frame on the screen (each with all of video memory), and two DOS replays of it are identical to each other and to the recording run. The port's replay is identical to DOS's at all 68 checkpoints, in every section; so is the `make port-debug` build's, where UBSan reports no null dereference.
- With the frame buffer in every dump (`UWRPFB=1`) and a dump every 20h ticks (`UWRPCK=20`), the walk session gives 344 dumps of the frame buffer, 337 of them after the first 3D frame (82 different frames), and every one of those 337 is identical in DOS and the port. The seven before the first frame differ in 533 bytes at 1012h..1250h of `stdat`, which other code uses until then (the archive code, LZSS, the cutscene player, the automap notes); the game state in those dumps is identical, and what those bytes are is left for Milestone 5.

The 3D frames include textured walls, floors and ceilings with the floor and wall mappers and, while the view is pitched, the perspective span drawers; 3D models (the table); about 9,000 object sprites and 8 critter frames through the scaled-sprite paths and the critter page cache; the distance shading; and a pick frame. `tools/replay.py show` writes the screen of every full checkpoint of a dump as a PNG, for looking at DOS's and the port's side by side.

### Divergences found and fixed

On the way to identical frames the replays found these, each now fixed:

- **Sprites not drawn.** `do_uwobj` loads `mov si,offset DGROUP:_Palettes; mov ax,DGROUP`; the port has no DGROUP segment, so the decoder read a zero palette and every pixel came out 0. Now an override, and the tool refuses any DGROUP offset or segment it is not told about.
- **A propagated return taken for a normal one.** The polygon clipper (GRLIBF.ASM) drops a polygon by popping its own and its caller's return addresses and returning past both; the first runtime returned the same code for "returned to me" and "returned past me", so the caller went on with an empty polygon and a runaway edge wrote over seg003's data. Returns that pass a caller now carry their own code.
- **A patched shift count read as a constant.** PERTLP.ASM and PERTP.ASM write the texture's log2 width into a `shl ax,2`; the first translation took the 2. Only pitched views use those span drawers, so the walk session's look up showed it. The tool now checks that every patched byte of an instruction is read from the code block.
- **`mxmul`'s override** replaced the body's first instruction with the second; again only pitched views run the general body.
- **The view's far pointers.** `fbshow` (the pictures PANELS.C's `do_fbuf_bms` draws into the frame buffer after the 3D view: the weapon and the picture at 5Eh, 80h) passed seg003 the paragraph map's split of a far pointer, where DOS's callers made the pointer at a paragraph (`MK_FP(EmsBuff + slot, 0)` plus the header); seg003 keeps the offset in its row records, which the dumps compare. The paragraph map now remembers the last few far pointers `MK_FP` made, and `fbshow` splits a pointer derived from one as it was made (`port_fp_split_recent`); `show`'s callers, which make their pointers otherwise, keep the old split, which matched DOS in Milestone 4.
- **A critter's heading from the stack.** In the walk session a critter took its new heading from `crit_drunkwalk`'s `head`, which that path reads before setting ([FINDINGS.md](FINDINGS.md#other-locals-read-before-they-are-set)); the port's stack held something else, so the critter turned differently, and two port runs differed from each other. `head` now has `STACK_JUNK(0)`, as COMBAT.C's `held` has, and the walk session was recorded with that replay build.

### Shared-source changes

All proved by the gate (`make check` passes; `make check-all` too):

- `portable.h`: `RENDER_TAG(o)`, nothing under Turbo C.
- DRAWOBJ.C: `RENDER_TAG(o);` before the sprite and critter opcodes.
- AI.C: `head` in `crit_drunkwalk` with `STACK_JUNK(0)`.

`make port-check` shows 0 errors (56 warnings). The tools: `tools/asm2c.py`; `replay.py`'s `walk` session; `replaydos.mjs`'s held-key step (`h:KEY,MS`); REPLAY.C's `UWRPFB`; and the stubs, now `grab`, the two joystick reads and the zeroed DGROUP gaps.

### Found on the way

- Two findings for [FINDINGS.md](FINDINGS.md): `do_hull`'s cull uses the previous handler's DH and the points' shade bytes; `sphere_check` loses the depth's low bit to a `cmp` between a `sar` and the `rcr` that wanted its carry.
- The hand-written C of Milestones 3 and 4 that the renderer would have called (GRENTRY.ASM's smooth spans `_C13`, which never ran before the 3D view) is not used by it: the translated module is, so a slip in the hand-written version cannot show in the 3D view. The two coexisted until Milestone 5, which keeps one of each (One implementation per routine).
- Far pointers in general: a flat host pointer does not know the segment its DOS counterpart had. `port_fp_split_recent` covers the pictures; any other place where the C hands seg003 or seg004 a far pointer whose split matters needs the same. SCROLL.C's `scroll_print` breaks text into chunks of at most 49 bytes at the last space, in a 47-byte array whose copy runs on into `found`'s spill slot; its terminator, `sentinel`, is the byte after those 49 (bp-5, offset 31h from the array at bp-36h), not the 49th, so a space in the 49th byte counts. The port had it one byte early, broke the Lord British speech at other places and drew the same pixels from other strings, which left the text raster's scratch bytes different from DOS's; `FRAME_TAIL(copy, 49, sentinel)` in a 50-byte host array matches DOS. String literals are read-only on the host and writable in DOS, and three places write into one: SOUND.C's driver and music names, INVPANEL.C's `load_inv_pic` (the armour picture file's last letter, by sex; putting armour on crashed rc5 and rc6) and SKILLS.C's `advance` (the new level's digits); each is `WRITABLE_STR`. clang's `-Wwrite-strings` over the game's C lists the 297 places a literal reaches a `char *`, through 52 initialisations and assignments and 32 parameters, and no other is written through. BABL.C's conversation heap is laid out as DOS's: its block tags are DOS's four bytes in DOS's place (`TAG_SLOT`, `TAG_VAL`; a host pointer written as slot `size / 4 - 1` of a pointer array landed a block's length past the block, in the blocks after it), and the table of imported functions takes DOS's four bytes an entry from the heap while the host's pointers live in a table of their own (`DOS_SIZEOF`, `HOST_TABLE`), so every block, and the portrait `switch_pic` draws from one, is where DOS has it. `show` also re-splits at the picture's paragraph any pointer whose split would wrap its offset before the picture's last row: CUTS.C's `STDAT.screen` is made once per cutscene, at 3E29:0000, and by the time `lback_vscreen` draws through it the paragraph map's split, 3CF5:1340, wrapped 60608 bytes in and drew `stdat`'s first bytes as a band across the intro's castle pan.

## Milestone 5 results

Run on 3 October 2026 on an Apple Silicon Mac (Apple clang 21, SDL 3.4.16, Nuked OPL3 at `765ec96`, libmt32emu 2.8.3, js-dos through dos-mcp 0.3.0). The port plays the game's music and effects through C versions of its own sound drivers, and eight recorded sessions replay in it with DOS's state at every checkpoint.

### Sound

The design is under Sound, above: AIL's API from the matched AIL.ASM, and the drivers UW2 loads written in C from Miles' AIL 2.14 source, with what UW2's own driver files do differently read from them (the older XMIDI shell, Origin's TVFX effects in the FM drivers), on Nuked OPL3 for the FM chips, libmt32emu for the MT-32 and a resampling DAC for the Sound Blaster's digital channel. The checks:

- **Every driver read agrees with DOS's.** All of the game's reads of driver state (the music sequence's status, timbre requests and statuses, locked channels, the digital buffers' statuses, the devices' presence) give the value DOS's driver gave, in all three sessions with a card: 1,795,103 reads in `sound` (Sound Blaster FM and digital), 1,832,854 in `soundfm` (FM only) and 3,092,157 in `soundmt` (MT-32). In `sound`, 15 digital transfers ended at the moment DOS's did, from 3,223 µs before their nominal end to 503 µs after (below).
- **The drivers write what DOS's write.** `tools/ailcheck.py` runs each session's calls through the user's own driver file in an x86 emulator and compares every byte it writes to the hardware with the port's: DM03.ADV (Sound Blaster FM) 13,608 register writes over 195 calls in `sound` and 14,041 over 270 calls in `soundfm`, and DM05.ADV (MT-32) 25,726 MIDI bytes over 280 calls in `soundmt`, all identical, tick for tick, and every value the calls return the same.
- **The MT-32 without ROMs.** No MT-32 or CM-32L ROM images were on the machine (the only candidate, `~/Library/Preferences/DOSBox/mt32-roms`, is empty), so the MT-32 was checked by its MIDI stream, which needs none: the `soundmt` session was recorded in DOS with js-dos's MPU-401 (DOSBox's, in intelligent mode with no synthesiser behind it, which acknowledges the driver's commands and takes its UART bytes), and the port's DM05 replaces it read for read and byte for byte. With the user's own ROMs (`--mt32-roms DIR`) the same stream drives libmt32emu; the repository holds no ROM.
- **What the game reads from the digital driver** was the last difference, 130 reads over 17 ticks of the checkpoint's `sound` session. Each was a read made in the tick in which a buffer ended, after DOS's transfer had ended and before the port's: the port asked its driver at the start of each tick, since its clock moved a PIT tick at a time, while DOS reads at any moment of a tick, many times in one (the game polls the buffers in its main loop). The fix records the moment of each read within the tick in DOS (the PIT read back, Replays with a sound card above) and asks the port's driver at that moment. That brought out the second cause: DOS's Sound Blaster is DOSBox's, which raises the interrupt when its mixer has pulled the transfer to within 3 ms of its end, and js-dos's mixer pulls by the host's clock, so DOS's transfers end up to about 3 ms early by an amount that differs from run to run (DOS replaying its own recording disagreed with it in 14,562, 14,839 and 14,237 reads in three runs). The checkpoint covered this with a rule that ended a transfer whenever the game registered its buffer again; it is gone, and under replay a transfer now ends when DOS's did, inside a window of 5 ms before to 1 ms after its own end, and anything outside the window counts as a difference. Outside a replay a transfer takes exactly as long as its samples, as on the card. The `sound` session was recorded again in the new format.
- **The clock under replay.** The port's PIT ran one tick of 3906 µs for each 3906.25 µs of replayed clock, so it fell a tick behind the clock about once a minute; now it runs exactly one PIT tick per tick of the game clock, and the drivers' time is the PIT's real period (3906.36 µs, divisor 4661).

Dependencies and licences, none of them in the repository:

| What | Used for | Licence | How the port gets it |
| --- | --- | --- | --- |
| John Miles' AIL 2.14 source, as mirrored at github.com/dah4k/AIL2 (commit `54eb81dd4c5313eac003096d220cb809f79e8432`, `A214_D3`) and github.com/Tronix286/AIL2 (commit `f9c05599bdeb05a0a2b05557f6a23cdafb8550ce`, the same sources with CR LF line ends and a CMS driver) | the reference the drivers' C (`xmidi.c`, `yamaha.c`, `mt32.c`, `dmasound.c`) was written from | released by its author on 26 May 2000 as "open-source freeware, usable by anyone for any purpose, commercial or otherwise, without restriction or limitation", in effect public domain | read, not copied: the port's C is written from it and checked against UW2's own `.ADV` files |
| Nuked OPL3 (github.com/nukeykt/Nuked-OPL3, commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8`) | the FM chips | LGPL-2.1 | `make setup-sound` fetches `opl3.c` and `opl3.h` into `tools/nuked-opl3` (ignored by git) and checks them by SHA-256 |
| libmt32emu (munt) 2.8.3 | the MT-32 and CM-32L | LGPL-2.1-or-later | `brew install mt32emu`, linked dynamically when `pkg-config` finds it |
| SDL3 3.4.16 | the window, input and audio output | zlib | `brew install sdl3` |
| the user's MT-32 or CM-32L ROMs | the MT-32's sound | Roland's | found by their contents (`--mt32-roms` with a folder or a file, `UW2PORT_MT32_ROMS`, the remembered setting, or a search); never in the repository |
| Unicorn 2.1.4 | `tools/ailcheck.py` only, a development tool | GPL-2.0 | `.venv/bin/pip install unicorn==2.1.4` |

### Sessions

Eight sessions were committed under `tests/replay` then (twelve now: [BUILDING.md](BUILDING.md) lists them), each recorded in DOS with `tools/replay.py record OUT --session NAME`. `replay.py check` replays each twice in DOS and once in each port build, and compares everything:

| Session | What it does | Checkpoints | DOS against DOS | DOS against the port |
| --- | --- | --- | --- | --- |
| `newgame` | boot, the title cutscene, the main menu, character creation, sixteen seconds in the game | 43 | identical | identical |
| `walk` | newgame's way in, then walking, turning, sliding, looking up and down, a click in the view | 68 | identical | identical |
| `sound` | the way in with a Sound Blaster (FM music, digitised effects), fight mode, swings, walking, a minute of music | 71 | identical | identical; driver reads 0 differences of 1,795,103 |
| `soundfm` | the same with every effect on the FM chip | 72 | identical | identical; 0 of 1,832,854 |
| `soundmt` | the same on a Roland MT-32 | 95 | identical | identical; 0 of 3,092,157 |
| `items` | the way in, then picking up, a sack, the map and a note, look mode, the statistics panel, a save to slot 1, a restore | 101 | identical, saves identical | identical, saves identical |
| `talk` | the way in, out of the room and along the corridor to the great hall, a conversation with Nystul | 80 | identical | identical |
| `load` | the main menu, Journey Onward, slot 1 (the items session's save), a step | 20 | identical, saves identical | identical, saves identical |

In every session the `make port-debug` build (UBSan's null check) is identical too and reports no null dereference, and the replay DOS build's C0 null pointer check is intact at the end.

### Saves

The `items` session saves to slot 1 and restores it; the `load` session loads that save from the main menu, staged into the game's directory (`--stage`), in DOS and in the port. In both, DOS and the port leave the same five files in `SAVE1`, byte for byte (`BGLOBALS.DAT` 9,928 bytes, `DESC` 5, `LEV.ARK` 367,277, `PLAYER.DAT` 1,043, `SCD.ARK` 8,950), and the two DOS runs agree with each other. The port writes the same files DOS writes: `PLAYER.DAT` (the encrypted player record), `LEV.ARK`, `SCD.ARK`, `BGLOBALS.DAT` and `DESC`, so a save moves between the port and DOS both ways. The fixes that took (16-bit string ids split as Turbo C splits them, BABL.C's two-word records through `READ_PAIR` and `WRITE_PAIR`, the files the game deletes or renames over the read-only data root) are in the checkpoint's commit.

### One implementation per routine

Above, under the 3D renderer. Seven modules are now split between hand-written C and the translation, in 32 ranges of hand-written routines (`x86/modtab.c` lists them). About 12,000 lines of duplicated C went: 11,500 from the translated files and 700 from the hand-written ones, which gained 400 lines of glue and of calls into the translation. All eight sessions are identical to DOS before and after.

### Locals read before they are set

None that the sessions reach is left. Branch coverage over the eight sessions never takes the path on which `curx` (AUTOMAP.C), `charges` (OBJUSE.C), `count` (PANELS.C) or `file` (CUTS.C) is read unset, and a port with every uninitialised local filled with a pattern (`-ftrivial-auto-var-init=pattern`) replays all eight identically, so no other one the sessions reach changes the state. [FINDINGS.md](FINDINGS.md#other-locals-read-before-they-are-set) has the detail. `held` (COMBAT.C) and `head` (AI.C) keep their `STACK_JUNK(0)`.

### Shared-source changes

All proved by the gate (`make check` and `make check-all` pass):

- `portable.h`: `SND_READ` (now also `rp_sound_at` before the read, for its moment), `SLAVE_TIMER`, `WRITABLE_STR`, `READ_PAIR` and `WRITE_PAIR`, `FRAME_LEN` and `FRAME_TAIL`; each is the original tokens under Turbo C.
- SOUND.C and CUTS.C: the 17 driver reads through `SND_READ`, the effects timer through `SLAVE_TIMER`, `init_sounds`' name through `WRITABLE_STR`. COMBAT.C and OBJUSE.C: `FARNULLTRAP` at the two vector-table reads ([FINDINGS.md](FINDINGS.md#a-bare-handed-blow-reads-the-interrupt-vector-table)). BABL.C: `READ_PAIR`, `WRITE_PAIR` and 16-bit string ids. GAMESTRN.C: 16-bit string ids. SCROLL.C: `FRAME_LEN` and `FRAME_TAIL`.

`make port-check` shows 0 errors. The tools: `replay.py`'s sessions `sound`, `soundfm`, `soundmt`, `items`, `talk` and `load`, `--stage`, the sound check and `ailcheck.py` in `check`; recording format 3; `replaydos.mjs`'s `SNDCHECK.OUT`; `asm2c.py`'s `HANDWRITTEN`; `setup-sound.sh` and `ailcheck.py`.

### Found on the way

- UW2's introduction has no speech, while UW1's has: every `say` in its scripts is subtitles only. UW2's speech files belong to eight other cutscenes ([FINDINGS.md](FINDINGS.md#3-engine-findings)).
- DOSBox's Sound Blaster, and js-dos's in particular, ends single-cycle transfers early by a host-dependent amount (above). A DOS reference that is deterministic for the digital driver would need js-dos's mixer in its no-sound mode, which dos-mcp does not offer.
- The seven frame-buffer dumps of the walk session before its first 3D frame (Milestone 4) still differ in 533 bytes at 1012h..1250h of `stdat`, which the archive code, LZSS, the cutscene player and the automap notes use before the 3D view does; the game state in them is identical. Not looked at again.

## Milestone 6a results

Run on 3 October 2026 on an Apple M4 Pro with 14 cores (Apple clang 21, SDL 3.4.16, DOSBox-X 2026.10.01, js-dos through dos-mcp 0.3.0, Unicorn 2.1.4). Milestone 5's `replay.py check` replays each session twice in js-dos, in roughly real time, before it replays it in the port: twenty minutes for the eight sessions. Milestone 6a separates the two. DOS is replayed once, to make a golden reference for each session, and the port is checked against the goldens on every push. [BUILDING.md](BUILDING.md#testing) has the commands; this section has what was built and what it showed.

### Golden references

`tests/replay/golden/SESSION/golden.json` holds, for each checkpoint of the session, its header and a 64-bit BLAKE2b digest of each section of the dump, taken over the bytes `replay.py compare` compares between DOS and the port: the machinery ranges zeroed, and the words that hold a far block's segment replaced by the block they name. Only GFX has such words in the eight sessions, nine of them (07D2, 0958, 0B0C, 0B1A, 553C to 5540, 558C, 55EC), and digests of the sections normalised that way agree between DOS and the port, and between js-dos and DOSBox-X, at every checkpoint of every session, while the raw sections differ in those words. Beside each full checkpoint is the screen as a PNG, for the pictures `verify` draws when a screen differs. Each golden also records the SHA-256 of its recording, its `.cfg`, the saved games it writes and the one it starts from, and of the replay DOS build that made it, so that a changed recording makes it stale and a changed replay build is flagged. The eight goldens hold 550 checkpoints and 355 PNGs in 7 MB; no dump and no game data are committed.

`replay.py golden` replays a session twice in DOS at the same time and writes the golden only when the two runs are identical, section for section with `compare --same-build`, and their saved games byte for byte, and the null-pointer check passes. `replay.py verify` replays the port alone and compares digests; when a checkpoint differs it names the sections and, for a screen, writes DOS's picture, the port's and their difference side by side. All eight sessions are identical to their goldens in the port and in the UBSan build.

### Unthrottled replays

In a replay nothing the game keeps depends on real time: the clock and every input are the recording's. Three things in the port still waited for it or did work no one used, found with `sample` on a replay:

| Change | Where | Effect |
| --- | --- | --- |
| Under `--replay`, the retrace bit of input status 1 comes from a count of reads (every eighth read starts a two-read retrace), not from the host's clock | `gfx/vga.c` | the waits for retrace in `local_do_palette` and the page flips were 30% of a replay's time; `newgame` went from 3.9 s to 1.2 s, `walk` from 7.8 s to 5.3 s |
| The translated modules, the machine they run on and the graphics C (`src/port/3d`, `gfx`, `x86`) compiled with `-O2`; the rest of the port's C and all of the game's C stay unoptimised, and the debug and coverage builds are unoptimised throughout | `tools/portbuild.py` | the renderer was 85% of what was left; `walk` to 1.9 s, `talk` from 10.9 s to 2.7 s |
| With no audio device and no `--audio-wav` (`--hidden`), the chips' synthesis is skipped; the drivers still program them, and every read the game makes of them is still timed by the recorded moments | `sound/audio.c` | Nuked OPL3 was 43% of the sound sessions; `sound` from 3.2 s to 2.1 s |

None of the three changes what the game does: every session is identical to its golden before and after each, and the sound driver checks still find 0 reads that differ from DOS's and every register write and MIDI byte identical to the real drivers' (`ailcheck.py`).

The DOS side runs in native DOSBox-X (`tools/replaydos.mjs --backend dosbox-x`, the default for a replay when it is installed). A replay needs no input, so DOSBox-X runs headless with the game's directory mounted from the host, the dynamic core at a fixed 300,000 cycles a millisecond, and turbo: its emulated time runs as fast as the host can run the guest, where js-dos is tied to the wall clock. Its hardware is js-dos's (a Sound Blaster 16 at 220h, IRQ 7, DMA 1 and 5, an OPL3, an intelligent MPU-401 at 330h, 16 MB). It loads the game at another segment than js-dos does, which the digests allow for. `replay.py golden all --backend jsdos --check` made all eight sessions again in js-dos, twice each, and every one is identical to the golden DOSBox-X made, checkpoint for checkpoint and section for section, saved games included. The one nuance is the Sound Blaster's, the reason the port ends a transfer where a recorded read says DOS's ended (Replays with a sound card, above): DOS's own driver, replaying a recording made in js-dos, disagrees with the recording in 14,237 to 15,591 reads of the `sound` session in js-dos, a different number each run, and in 20,528 in DOSBox-X, the same number every run, since DOSBox-X's mixer runs on the emulated clock. The game reads the recorded values in either, so its state is the same. The FM-only and MT-32 sessions differ in 1 or 2 reads in both.

| Session | Checkpoints | DOS, js-dos | DOS, DOSBox-X | the port, before | the port, after |
| --- | --- | --- | --- | --- | --- |
| `newgame` | 43 | 38 s | 11 s | 3.9 s | 0.8 s |
| `walk` | 68 | 61 s | 16 s | 7.8 s | 2.0 s |
| `sound` | 71 | 91 s | 27 s | 8.5 s | 1.8 s |
| `soundfm` | 72 | 89 s | 27 s | 8.6 s | 1.8 s |
| `soundmt` | 95 | 155 s | 43 s | 10.5 s | 2.9 s |
| `items` | 101 | 66 s | 18 s | 5.9 s | 1.7 s |
| `talk` | 80 | 70 s | 18 s | 10.9 s | 2.8 s |
| `load` | 20 | 20 s | 5 s | 1.9 s | 0.4 s |
| all, one at a time | 550 | 590 s | 165 s | 58 s | 14 s |

### Sessions in parallel

`golden` and `verify` run the sessions at once, one per core up to eight (`-j`), `load` waiting for `items`, whose saved game it starts from. `verify all` takes 3.5 seconds; `golden all`, sixteen DOS runs, 59 seconds in DOSBox-X and 219 in js-dos (eight Chromes at once, each run a little slower than alone).

### Routine fuzzing

`tools/fuzzasm.py` runs single routines on inputs no session gives them: the original's bytes from `UW2.EXE` in Unicorn, the port's code in `tools/fuzzhost.c` (the port's objects linked with a driver in place of `main.c`), the same registers and memory on both sides, and every register, flag and byte of memory compared afterwards (the stack below the final SP excepted). 35 targets in seven modules:

| Module | Targets | What they test |
| --- | --- | --- |
| IMATH (seg021) | 9 | `sincos` and `lsqrt` through the glue into `sys/imath.c`; `cSinCos`, `cFstSinCos` and `cSqRt`, its C entries; the translated arcsine, arccosine and `atan2`; `cAtan2`, C into the translation |
| INSTANCE (seg004) | 5 | `mm3x9_bpsi`, `mm3x9t` (with the saturation slip in [FINDINGS.md](FINDINGS.md#mm3x9_bpsi-and-mm3x9t-saturate-the-wrong-product)), `mm9x9`, `code_pnt`, `mxmul` |
| SPHERE | 2 | `sphere_check`; `get_dist`, which nothing in the EXE calls |
| GRENTRY (seg003) | 7 | the frame buffer's span writers on a frame buffer laid out as `cPlaceFB` lays it out: solid spans, bitmap rows, the transparent rows (`gfx/grentry.c` through `seg003_call`), Gouraud spans; `cFillFB`, `cDimFB`, `cLiteFB` |
| CLIP | 1 | `_asm_clip_polygon`, polygons across the frustum and behind the eye |
| EXPAND | 11 | every image decoder, the palette builder and the record decoder, hand-written and translated, alone and through `seg004_uncmp`, cFrmtoRaw's C into the translation (formats 4 and 6, which no session reaches) |

The quick run (`make test`) is 5,560 cases in 13 seconds; the deep run is 278,000 cases with a new seed each time. Both run clean. What they found:

- **No port bug.** Every translated routine gives the original's registers, flags and memory on every input, the edge values included, and the hand-written C gives what its callers read.
- **The hand-written C leaves dead registers alone.** `_63` leaves AL, DX and DI where the original leaves the last palette byte, the image's paragraph and the end of `uncmp_pal`; the decoders leave every register but AX; `_18D` leaves AX and CX. Every caller loads them again before reading them (do_uwobj, do_uwcrit and cFrmtoRaw read only AX after a decoder), so the fuzzing compares what the callers read for these, and says so in each target.
- **The private stack.** `cAtan2` and `seg004_uncmp` run the translation on seg021's private stack (FD71:0510 down) outside the renderer, as C3DENTRY.ASM's entries do; what they push there is not compared.
- **A byte of the original's self-modifying code.** The record decoder restores the byte it patches with the other encoding of the same instruction, so DOS's code segment changes where the port's does not ([FINDINGS.md](FINDINGS.md#3-engine-findings)). That byte is not compared.
- **Divide faults agree.** `lsqrt` of a value whose first quotient overflows 16 bits (CX:BX of FFFF0000h or more) faults in DOS; the port stops there too rather than guess what the fault handler of the moment would do.

### Coverage

`make coverage` replays the eight sessions in the coverage build and runs the fuzzing on the same objects, and writes [COVERAGE.md](COVERAGE.md). Over the port's whole source, 45.4% of the lines and 1,335 of its 2,274 functions run: 41.9% of the game's shared C (776 of 1,522 functions), 72.3% of the port's hand-written C and 43.7% of the translated modules. The fuzzing adds 652 lines no session runs, 594 of them in the translated modules, and 7 functions: `cAtan2`, IMATH's translation (the arcsine, arccosine and `atan2`), `seg004_uncmp`'s way into the translation (`translated`, `asm_run_near`), `exp_4str`'s glue and `count4`, and `asm_seg003_fallback`, the glue into seg003's hand-written span writers. The least covered game code is what no session does yet: spells and runes (`src/combat`, 14%), the SCD events and traps (`src/event`, 6%), object use (`src/obj`, 24%) and most of the conversation built-ins (`src/conv`, 30%); COVERAGE.md lists every function nothing reaches, the list for the sessions of Milestone 6's release step.

### make test

`make test` (`tools/test.py fast`) runs `make check`, `make port`, the fuzzing's quick run and `replay.py verify all`, prints each step's time, and exits 1 with the steps that failed; the pre-push hook runs it (bypass with `git push --no-verify`). It takes 23 seconds with nothing to recompile: 4 for the gate, 2 for the port, 13 for the fuzzing and 3.5 for the eight sessions. `make test-full` makes every golden again from DOS (twice each, checked identical), replays the sessions in the port and in the UBSan build, runs `ailcheck.py` on the three sessions with a music card, the deep fuzzing and the coverage report, in 12.5 minutes (61 seconds for the sixteen DOS runs, 625 for the deep fuzzing), and says whether the regenerated goldens differ from the committed ones. Both pass.

### Shared-source changes

None. `make check` and `make check-all` pass, and `make port` and `make port-debug` link.

### Found on the way

- Unicorn 2.1.4's `emu_start` takes a linear address in 16-bit mode and sets IP to it minus CS's base, modulo 64 KB. `ailcheck.py` passes the offset alone, which works only because its driver's segment, 1000h, has a base of 10000h, 0 modulo 64 KB; `fuzzasm.py` passes the linear address.
- js-dos's Sound Blaster timing differs from run to run, DOSBox-X's does not (above). A session with digital sound recorded in DOSBox-X would replay in it with no timing differences at all, but recording needs real-time input, which only js-dos's page takes here.
- DOSBox-X has no equivalent of dos-mcp's `read_memory`, so its replays skip the null-pointer check made after the game exits; the dumps' `NULL` sections, with C0's checksum in every checkpoint, still cover it.

## Milestone 6: other platforms and a release

1. **Other platforms.** Done for Linux, whose replays pass on x86-64 and arm64. On Windows the port is built under MSYS2 CLANG64 and `accuracy.yml` replays every session against the goldens there, and on macOS 15 too, so all three are checked against DOS on every push to `main`; the Windows port opens every host file in binary mode, since the C library's text mode would translate line ends a second time, and finds GOG's install through the registry. Release packages for all three are built by `release.yml`: the macOS app signed with the Developer ID and notarised, a Linux tarball and AppImage built on Ubuntu 22.04 whose run paths are checked to be `$ORIGIN` only, and a Windows zip checked to load every DLL from the package or from Windows; each has the project's icon. The plan was: Linux and Windows on x86-64 builds of the same tree: build files for each (`tools/portbuild.py` with the host's compiler and SDL3), the SDL3 backend unchanged, the sound libraries from each system's packages or built from source, and the replay suite run on each, since the translated renderer and the drivers' timing are host-independent C. Then Emscripten, where the platform layer's model (the game on a thread of its own) needs a look.
2. **Polish.** Partly done: the game folder is found or chosen in a dialog and remembered, a first run gets a Sound Blaster (or the MT-32 for the music when ROMs are given), the cards and the ROM folder are remembered, and a missing or wrong game gets a message box. Still to do: settings in the port rather than on the command line: the music and speech cards (written to the home directory's `UW.CFG`, as `--sound` does now), the MT-32 ROM directory, the volume, the scaling and the window; a clear message when the data, the EXE's checksum or the ROMs are wrong; clean shutdown on the window's close box; the remaining link stubs (`portstubs.py` lists them), each either the routine's C or a decided no-op.
3. **The release (phase 1).** Longer sessions into several worlds, through combat with critters, more conversations, trading, spells and level changes, each replayed with `UWRPFB` dumps so every 3D frame is compared; every null-pointer site decided; the four unreached read-before-set locals given `STACK_JUNK` when a session reaches them; saves loaded both ways in real DOS too (dos-mcp), not only in the replay.
4. **The readable renderer, the start of phase 2.** The translated renderer is exact but is the assembly in C. It can become readable C routine by routine through the same mechanism as above: a routine moves from the translation to `HANDWRITTEN` when its C, written from the translation, replays every session with identical frame buffers, the glue keeping the translated callers working until they move too. The order is from the leaves up: the span drawers and texture mappers, then the clipper and projection, then the model interpreter, which is the last and hardest, since its handlers pass state in registers.

### Host hazards

Places where the C did what DOS does under Turbo C but something else on a 64-bit host, found after UW1's port shipped (its PORT.md has the same list, "Host hazards"):

- **An offset kept signed (UW1's issue 8, which UW2 shares).** CHARGEN.C kept the offsets of CHRBTNS.GR's pictures in their block as `int16`. In DOS a pointer's offset wraps at 64 KB, so the female bodies, which start past 7FFFh, were read where they are; on the host the offset sign-extended, the port read below the block, and choosing any portrait but the first for a female character crashed it. `chroff` and `offs` are `uint16` now, the same bytes under Turbo C. The `female` session (a female character with the last portrait) goes through it; other `int16` offsets added to a far pointer have not been audited for values past 7FFFh.
- **Casts that keep 32 bits on the host.** CUTS.C's `anm_cycle` took `(unsigned)` of a difference of the 32-bit clock, which on the host is no cut at all, so once the clock passed FFFFh ticks every colour cycle turned on every call; and GAMESTRN.C's `read_string` sought by `(unsigned)` of a sum with the signed word `relative`, which for a value from 8000h up is a seek about 4 GB forward where `long` is 64 bits (macOS, Linux) and a seek backwards where it is 32 bits (Windows) (no GOG strings file has one). Both are `(uint16)` now, the same bytes under Turbo C. The `introclock` session (`intro` with the clock moved on by 10000h ticks, `tools/clockshift.py`) failed before the first; the second has no test.
- **A map note with no end (UW1's issue 7).** RedisplayStrings, DetermineBetterHit and ManageDungeonMap copy each note's text with `str_copy` into a 50-byte stack buffer. A note the game writes always ends there (at most 46 characters), and UW2's LEV.ARK has a length table, so the aliased notes block that gave UW1 a note with no end cannot happen here; but a notes block the game did not write (a damaged or edited save) can hold one, and `str_copy` would write past the buffer, where DOS runs wild and the host's frame decides what the extra bytes hit. AUTOMAP.C's `NOTE_COPY` is `str_copy` under Turbo C and, in the port, `str_copy` for a note that ends within its 50 bytes and a copy of 49 characters and a 0 for one that does not. No session or check reaches it in UW2 (UW1's `tools/notescheck.py` does in UW1).

## Phase 2 enhancements

Phase 2 adds options; faithful mode, pixel for pixel DOS, stays the default and the replay suite runs with every option off.

- **Higher-resolution software rendering, with the palette lighting kept.** It stands behind `port_render_3d` and the frame buffer it is given (render.h). The faithful renderer, translated from the assembly, cannot draw into a larger buffer; a higher-resolution one is a new renderer, written from the faithful one with the replays as its reference at 320 by 200. What assumes 320 by 200 today has to be generalised: the frame buffer's row table (370D:095C, 200 rows) and its offsets in `stdat`, the projection constants and the view window (`cPlaceFB`, `place_3d_view`), the span and texture mappers' fixed-point steps, the pick buffer (one colour per object, read back at the cursor), the scaled sprites' clipping, and the 2D layer around the view, which stays at 320 by 200 and is scaled. Lighting keeps going through `lightabs` and the palette, so the look is the game's.
- **Presentation shaders through SDL3's GPU API**: integer and smooth scaling, aspect correction, and CRT-style shaders, applied to the scanned-out picture, never to the game's own drawing.
- **Voxel sprites, as an option.** The per-object sprite hook (render.h, `port_set_sprite_hook`) is where they go: an enhancement draws a voxel model there instead of the sprite, in software, with slab rendering in the style of Build's KVX and lit by the same palette tables. The models come from hand-made `.vox` or KVX files, or are generated on the user's own machine from the game's directional sprite frames by visual-hull carving and cleaned up by hand. Since they are derived from the game's art, generated models are never shipped.
- **A GPU renderer**, through SDL3's GPU API behind the same render interface. Low priority: UnderworldGodot already gives the game a modern 3D engine.

## Risks

- **Silent arithmetic differences.** 16-bit wrap and promotion differences produce no diagnostic and can change game logic far from their cause. The mitigation is the explicit widths, the promotion audit and the replay suite, which together catch them close to the cause.
- **Byte-identical changes may not exist.** Some portability changes may have no spelling that Turbo C compiles to the same bytes. Those changes go into port-only wrappers, which then need their own tests.
- **Bitfield placement.** Settled for every struct in the headers: the layout check shows Turbo C and the packed host layout agree. A struct added later is checked by running `tools/layoutcheck.py` again.
- **Structs with pointers over shared buffers.** They grow on the host, and code that sizes them with numbers (BABL.C's heap) or lays other data after them (`stdat`) needs port-side care; see the Milestone 2 results.
- **Replay determinism in DOS.** The replay DOS build has to record every outside read, including the reads the renderer makes mid-frame through `do_mouseq`. A missed read shows up as two DOS replays that disagree, which Milestone 4 checks first.
- **The renderer's size.** seg004 is 33 KB of hand-written 386 code with self-modifying parts and a threaded interpreter, and seg003 is 24 KB more. The FM Towns names and the frame-buffer comparison keep the work checkable, but it is the largest single piece of the port.
- **Far data taken from the EXE.** About 83 KB of tables have no source yet. Loading them from the user's EXE keeps the port honest, but the offsets tie the port to the GOG `UW2.EXE`, which the port checks by its size and hash at start-up.
- **Asynchronous timers.** In DOS the clock interrupt can land between any two instructions. The port's timer thread gives the same effect, but data races that were harmless on one 386 can matter on a multi-core machine. Every value the timer callbacks share with the game is accessed atomically, and the replay suite runs with the clock recorded.

## Prior art

- **Chocolate Doom and Crispy Doom.** Chocolate Doom reproduces vanilla Doom's behaviour and limits, and uses demo compatibility as its test. Crispy Doom adds enhancements on top of the same code. This port follows the same split for its two phases.
- **The SM64 decompilation and its PC port.** One tree builds the matching N64 ROM and the PC port, with the platform code kept apart. That is the model for the two builds here.
- **Ship of Harkinian.** The Ocarina of Time PC port is built on a separate platform library, libultraship, which other ports have since reused. That is the reason this port keeps its platform layer independent of UW2 for a later UW1.
- **DevilutionX.** It began from Devilution, a Diablo decompilation that kept a build matching the original, and then moved to portable code step by step. It also records and replays demos for its regression tests.
- **OpenRCT2.** It began by replacing the original game's functions one at a time while the original still ran. It also has replays with game-state checksums. This port uses the same incremental replacement and the same kind of state comparison.
