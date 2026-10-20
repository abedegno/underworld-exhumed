# uw1: Ultima Underworld: The Stygian Abyss

This folder is *Ultima Underworld: The Stygian Abyss* in [Underworld Exhumed](../README.md), with [uw2/](../uw2/README.md) (Ultima Underworld II) beside it. The repository's [README](../README.md) is the place to start; this page has this game's details.


Ultima Underworld: The Stygian Abyss (DOS, 1992; developed by Blue Sky Productions, later Looking Glass Technologies, published by Origin Systems), as source code. This repository has two products, built from the same sources:

- **A byte-matching decompilation.** The C and assembly in `src/` compile with the original Borland tools to the same bytes as the shipped `UW.EXE`.
- **A native cross-platform port, uw1port.** The same C, compiled for macOS, Linux and Windows with SDL3, plays the game in a window with its music and speech. Eleven recorded DOS sessions replay in it with DOS's game state and screen at every checkpoint ([docs/PORT.md](docs/PORT.md)).

It is the sibling of [uw2/](../uw2/README.md), the matched decompilation of Ultima Underworld II, and was seeded from it: the two games share a compiler, libraries and much of an engine, a year apart. It is built with [Exhume](https://github.com/abedegno/Exhume), the toolkit both projects use.

No game data is included. You need your own copy of UW1 (the GOG release). This is a fan research project, not affiliated with or endorsed by the rights holders; see [Licence and legal notice](#licence-and-legal-notice).

## Play it

1. Get the port: download the package for your system from the [releases](https://github.com/abedegno/underworld-exhumed/releases) page, or [build it from source](#build-the-port-from-source).
2. Install Ultima Underworld from GOG. The port finds it in GOG's usual folders (on Windows, where GOG's installer recorded it in the registry), including inside GOG's Mac app and its `game.gog` CD image. If it finds nothing, it asks you to choose the folder that holds `UW.EXE` (or the GOG install folder, or the GOG app), and remembers it. From a command line, `uw1port --data /path/to/UW1` names it.
3. Start it: `UW1.app` on macOS (a package whose `README.txt` says it is not notarised needs right-click and Open the first time), `uw1port.exe` on Windows, and on Linux the AppImage (`chmod +x UW1-*.AppImage`, then run it) or `./uw1` from the tarball. The Linux packages need glibc 2.35 or later (Ubuntu 22.04 or newer).

The controls are the game's own: the mouse, and the keys in the game's manual. The game's cursor follows the system pointer; `--mouse lock` captures the pointer on a click instead, as DOSBox does, with Ctrl+F10 to release it. Saved games and settings go to `~/.uw1port` (`%APPDATA%\uw1port` on Windows), never to the game folder. Each session's inputs are recorded to `recordings/` there (the newest five are kept; `--no-recording` turns it off), so if the game crashes, the newest folder there replays it exactly: please attach it to a bug report.

Options (`uw1port --help` lists them all):

- `--sound MUSIC,SPEECH` chooses the sound cards, and is remembered. The first run gets a Sound Blaster with its FM music and digitised speech (`3,1`), or, when MT-32 ROMs are given (below), the MT-32 for the music and the Sound Blaster for the speech (`6,1`). Music: 0 none, 1 PC speaker, 2 Ad Lib, 3 Sound Blaster, 4 Sound Blaster Pro, 5 Pro Audio Spectrum, 6 Roland MT-32. Speech: 0 none, 1 Sound Blaster, 2 Sound Blaster Pro, 3 Pro Audio Spectrum.
- Roland MT-32 music needs your own MT-32 or CM-32L ROM images, which are not included. The port finds them by itself (a `roms` folder in its home folder, beside the game or the program, or where DOSBox keeps MT-32 ROMs), under any file names, or you give them: `--mt32-roms DIR` on the first run (or `--sound 6,1 --mt32-roms DIR` later), with `CM32L_CONTROL.ROM` and `CM32L_PCM.ROM` (or `MT32_CONTROL.ROM` and `MT32_PCM.ROM`) in DIR. The folder is remembered.
- `--scale N` sets the window's starting size, `--no-aspect` shows square pixels instead of a 4:3 CRT's shape, `--no-integer` scales freely.

## Build the port from source

The port needs no Borland toolchain, DOS emulator or game data to build.

```sh
make setup-port     # SDL3, libmt32emu and Nuked OPL3 for this OS (Exhume is the submodule: clone with --recursive)
make port           # build/port/uw1port
build/port/uw1port  # or: build/port/uw1port --data /path/to/UW1
```

- macOS: the Xcode command line tools and Homebrew.
- Linux (Debian, Ubuntu): `make setup-port` prints the `apt-get install` line it needs, then builds SDL3 and libmt32emu into `tools/libs`.
- Windows: MSYS2's CLANG64 shell; then `make PY=python port`.

[BUILDING.md](docs/BUILDING.md#the-native-port) has the details, and [Releases](../docs/BUILDING.md#releases) how the packages are made.

## Status

Every code segment is matched. Each source compiles with Turbo C++ 1.01 (C) or assembles with Turbo Assembler 2.0 (assembly) to its segment's bytes in `UW.EXE`, and its fixups, data and names are verified: 353,727 of the program's 369,167 bytes of code, in 89 C files and 50 assembly modules. The rest is Borland's C runtime library, which the link takes from the library, as UW2's does.

The relinked EXE is identical to `UW.EXE` (the same MD5) and boots to the main menu, and the modding build with no change gives the same EXE ([LINKING.md](docs/LINKING.md)). What no source holds yet (four small pieces of code, 442 bytes, and the far data and DGROUP gaps no source owns) is extracted from your own `UW.EXE` at build time and never committed.

Still to do ([docs/PLAN.md](docs/PLAN.md)):

1. The rest of the data as source.
2. The layout audit for the modding build, and the readability pass: shared headers that match UW1 (the matched files still declare some of UW1's differences from UW2 locally), named constants and struct fields, and notes on what each file does in the game.

The port is done to the same standard as UW2's: every recorded session is identical to DOS ([docs/PORT.md](docs/PORT.md)), and `make test` checks it ([Testing](docs/BUILDING.md#testing)).

[docs/NOTES.md](docs/NOTES.md) collects what matching found: where UW1 differs from UW2 (an earlier sound library, a graphics library of 18 modules where UW2 has 14, a 3D renderer of 9 modules with no 386 code, an options panel and screen-frame dragons of its own), the module boundaries, and items for the readability pass. [docs/FINDINGS.md](docs/FINDINGS.md) collects what it revealed about the game: likely bugs in the shipped program, the game rules the code implements, engine findings, dead code and open questions.

## Finding a routine

`map/crosswalk.tsv` lists every function by the disassembly listing's name and address (the names in UWReverseEngineering's `UW1_asm.asm`, which UnderworldGodot cites), with the matched source's name for it and the file and line that define it. `python3 tools/crosswalk.py --find NAME_OR_ADDRESS` looks one up; an address inside a routine (`ovr119_3CE`) finds the routine that holds it, and a segment may be given with or without the listing's paragraph (`seg032_6A9` or `seg032_2DCA_6A9`). [uw2/](../uw2/README.md) has the same table for UW2. Segment numbers mean different code in the two games, so a bare address resolves in both, and the citing code's context says which game it means. Between the two tables, 1,077 of the 1,085 listing names and addresses UnderworldGodot's sources cite resolve to a matched routine. `python3 tools/crosswalk.py` rebuilds the table from the target tables and the built objects. seg005 is Borland's C library and has no source line.

[docs/UW1-UW2-DIFFERENCES.md](../docs/UW1-UW2-DIFFERENCES.md) catalogues where UW1's rules differ from UW2's, routine by routine.

[docs/FORMATS.md](../docs/FORMATS.md) covers both games' data file layouts, with UW1's own in its last section.

[docs/behaviour/](../docs/behaviour/README.md) describes what both games do, as rules with exact numbers, for readers who don't read the C: NPC AI, combat, magic, conversations, player upkeep, and traps and triggers, with UW1's rules marked and its sources linked.

## Names

UW1 has no build with symbols. Its names come from UW2's matched sources, whose names are the FM Towns build's originals: a UW1 function whose code pairs with a UW2 function (`tools/kin.py` in Exhume, `map/kin.tsv`) takes that name. Where the overlay manager's stub order rules a name out (the linker numbers an overlay's entries in the order of a hash of their names), a function takes a descriptive name whose hash fits, and the source says so.

## Building

This needs your own `UW.EXE`, the Turbo C++ 1.01 disk images (which Borland released free of charge) and the Turbo Assembler 2.0 disk image, Python 3 with Exhume's requirements, and the toolchain, which `make setup-toolchain` at the top unpacks into `uw2/TC` and `uw2/TASM`. None of these is in the repository.

`exhume.toml` names where things are, each overridable from the environment: `UW1_EXE` (default `~/UWGOG/UW1/UW.EXE`), `UW1_ASM` (the disassembly listing that the map was made from), and `EXHUME_TC` and `EXHUME_TASM` (the Borland toolchain). With Exhume checked out:

```sh
make check                                                 # the gate: every source matches and verifies,
                                                           # symbols.tsv rebuilds, the exact link is UW.EXE,
                                                           # the modding build with no change is the same
make hooks                                                 # git push runs the gate first
python3 path/to/Exhume/tools/match.py src/ui/OPTIONS.C     # compile in DOS and compare with the segment
python3 path/to/Exhume/tools/verify.py src/ui/OPTIONS.C    # fixups, data and names
python3 path/to/Exhume/tools/build.py --all                # every object
python3 tools/link.py                                      # build/LINK/out/UW.EXE, compared with yours
python3 tools/link.py --mod                                # the modding build
```

The tools find Exhume through `tools/exhume.py`, as every tool in the repository does: `$EXHUME`, else the repository's submodule `../exhume` (an empty one is an error: `git submodule update --init`), else `.exhume`, else `~/Exhume` ([docs/BUILDING.md](../docs/BUILDING.md#exhume)). The DOS build depends on Exhume's runtime as well as its tools: the shared headers include its `runtime/include/portable.h`, which the build stages beside `src/include`, so the gate proves the runtime's macros cost no byte. The submodule's commit is the one both games were proved with.

`make repocheck` at the top of the repository runs the checks CI runs on every push: no game data or Borland binary is committed, and the Markdown links resolve.

## Licence and legal notice

The project's own work (the tools, the documentation, and the comments, names and structure added to the decompiled code) is under the MIT licence ([LICENSE](LICENSE)). The decompiled code is derived from `UW.EXE`, whose copyright belongs to its owners; the licence grants no right in it. [NOTICE](NOTICE) has the details, including the Borland and Miles code in the repository.
