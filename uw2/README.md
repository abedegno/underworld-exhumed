# uw2: Ultima Underworld II: Labyrinth of Worlds

This folder is *Ultima Underworld II: Labyrinth of Worlds* in [Underworld Exhumed](../README.md), with [uw1/](../uw1/README.md) (Ultima Underworld) beside it. The repository's [README](../README.md) is the place to start; this page has this game's details.


Ultima Underworld II: Labyrinth of Worlds (DOS, 1993; developed by Looking Glass Technologies, published by Origin Systems), as source code. This repository has two products, built from the same sources:

- **A byte-identical decompilation.** The C and assembly in `src/` rebuild the shipped `UW2.EXE` with the original Borland tools, byte for byte, and the source is readable: original names, shared headers, named constants and notes on what each file does in the game.
- **A native cross-platform port.** The same C, compiled for macOS, Linux and Windows with SDL3, plays the game in a window with its music and effects. Recorded DOS sessions replay in it with DOS's game state at every checkpoint.

Neither contains any of the game. You need your own copy of UW2 (the GOG release). This is a fan research project, not affiliated with or endorsed by the rights holders; see [Licence and legal notice](#licence-and-legal-notice).

## Play it

1. Get the port: download the package for your system from the [releases](https://github.com/abedegno/underworld-exhumed/releases) page, or [build it from source](#build-the-port-from-source).
2. Install UW2 from GOG. The port finds it in GOG's usual folders (on Windows, where GOG's installer recorded it in the registry), including inside GOG's Mac app and its `game.gog` CD image. If it finds nothing, it asks you to choose the folder that holds `UW2.EXE` (or the GOG install folder), and remembers it. From a command line, `uw2port --data /path/to/UW2` names it.
3. Start it: `UW2.app` on macOS (signed and notarised, so it opens like any app; a package whose `README.txt` says it is not notarised needs right-click and Open the first time), `uw2port.exe` on Windows, and on Linux the AppImage (`chmod +x UW2-*.AppImage`, then run it) or `./uw2` from the tarball. The Linux packages need glibc 2.35 or later (Ubuntu 22.04 or newer).

The controls are the game's own: the mouse, and the keys in the game's manual. The game's cursor follows the system pointer; `--mouse lock` captures the pointer on a click instead, as DOSBox does, with Ctrl+F10 to release it. Saved games and settings go to `~/.uw2port` (`%APPDATA%\uw2port` on Windows), never to the game folder. Each session's inputs are recorded to `recordings/` there (the newest five are kept; `--no-recording` turns it off), so if the game crashes, the newest folder there replays it exactly: please attach it to a bug report.

Options (`uw2port --help` lists them all):

- `--sound MUSIC,SPEECH` chooses the sound cards, and is remembered. The first run gets a Sound Blaster with its FM music and digitised effects (`3,1`), or, when MT-32 ROMs are given (below), the MT-32 for the music and the Sound Blaster for the effects (`5,1`). Music: 0 none, 2 Ad Lib, 3 Sound Blaster, 4 Sound Blaster Pro 1, 5 Roland MT-32, 6 Pro Audio Spectrum, 7 Sound Blaster Pro 2. Speech: 0 none, 1 Sound Blaster, 2 Sound Blaster Pro, 3 Pro Audio Spectrum.
- Roland MT-32 music needs your own MT-32 or CM-32L ROM images, which are not included. The port finds them by itself (a `roms` folder in its home folder, beside the game or the program, or where DOSBox keeps MT-32 ROMs), under any file names, or you give them: `--mt32-roms DIR` on the first run (or `--sound 5,1 --mt32-roms DIR` later), with `CM32L_CONTROL.ROM` and `CM32L_PCM.ROM` (or `MT32_CONTROL.ROM` and `MT32_PCM.ROM`) in DIR. The folder is remembered.
- `--scale N` sets the window's starting size, `--no-aspect` shows square pixels instead of a 4:3 CRT's shape, `--no-integer` scales freely.

## Build the port from source

The port needs no Borland toolchain, DOS emulator or game data to build.

```sh
make setup-port     # installs or builds SDL3, libmt32emu and Nuked OPL3 for this OS
make port           # build/port/uw2port
build/port/uw2port  # or: build/port/uw2port --data /path/to/UW2
```

- macOS: the Xcode command line tools and Homebrew.
- Linux (Debian, Ubuntu): `make setup-port` prints the `apt-get install` line it needs, then builds SDL3 and libmt32emu into `tools/libs`.
- Windows: MSYS2's CLANG64 shell; then `make PY=python port`.

[BUILDING.md](docs/BUILDING.md#building-the-port) has the details, and [Releases](../docs/BUILDING.md#releases) how the packages are made.

## The decompilation

Every code segment is matched. All of UW2's C, 337,327 bytes in 99 files, compiles with Turbo C++ 1.01 to the same machine code as `UW2.EXE`, and all of its assembly, 71,920 bytes, assembles to the same bytes with Turbo Assembler 2.0. Each file's fixups and data are verified too. Only Borland's C runtime library has no source here. The relinked EXE is identical to `UW2.EXE` ([LINKING.md](docs/LINKING.md#the-exact-link)), and the modding build links sources changed by any size into an EXE that runs.

Function and global names are the originals from the FM Towns build wherever it has them. The sources are grouped by subsystem, with shared headers, named constants and struct fields, and every file says what it does in the game. [FINDINGS.md](docs/FINDINGS.md) collects what matching revealed: likely bugs in the original, game rules and engine details.

[FORMATS.md](../docs/FORMATS.md) gives the layout of every data file both games read and write, from the code that reads it, with UW1's differences and corrections to the UW-Formats document.

### How it is verified

- **The gate** (`make check`): every source compiles in DOS to its segment's bytes, with its fixups and data verified; `symbols.tsv` rebuilds from scratch to the committed file; the exact link equals `UW2.EXE`; the modding build with no change equals the exact link.
- **The port against DOS**: twelve recorded sessions (character creation, a female character with the last portrait, the introduction with the game clock past FFFFh ticks, walking, fighting with three sound cards, the inventory, a conversation, saving and loading, the introduction, and Lord British's conversation and the Guardian's cutscene) replay in the port and match golden references made from DOS at every checkpoint, the 3D frames and the saved games included. Single routines are fuzzed against the original's bytes in an x86 emulator.
- `make test` runs the gate, the port build, the fuzzing and the replays in about 20 seconds. CI builds the port on macOS, Linux and Windows on every push and runs `make test` on Linux ([Continuous integration](docs/BUILDING.md#continuous-integration-and-releases)).

### Building the DOS EXE

This needs your own `UW2.EXE` (at `~/UWGOG/UW2/UW2.EXE`, or named by `UW2_EXE`), the Turbo C++ 1.01 disk images (which Borland released free of charge) and the Turbo Assembler 2.0 disk image, and Node 20 or later, Python 3, mtools and 7z. None of these is in the repository.

```sh
make setup TC_DISKS="/path/to/Turbo C++ 1.01" TASM_DISKS="/path/to/Turbo Assembler 2.0"
make            # the modding build; prints the path of the EXE
make check      # the gate: every file matches, and the exact link is identical to UW2.EXE
make boot       # boot the modding build to the main menu and screenshot it
make test       # the gate, the native port, and the port against DOS's golden replays
```

`make setup` unpacks the toolchain into `TC/` and `TASM/`, installs the Python and Node packages and builds emu2, the DOS the toolchain runs in by default (DOSBox-X or js-dos otherwise); it is safe to run again. `make hooks` installs a pre-push hook that runs `make test`. [BUILDING.md](docs/BUILDING.md) has the rest.

### The sources

Each DOS code segment is one original source file. A file's DOS segment is its `/* target: */` line.

| Directory | What is in it | Notes |
| --- | --- | --- |
| `src/game/` | main and start-up (`UWEDIT.C`), the main loop, the player, skills, saving and restoring | [game.md](docs/subsystems/game.md) |
| `src/obj/` | the object lists, object classes, using and looking at objects, animated objects | [objects.md](docs/subsystems/objects.md) |
| `src/inv/` | the inventory | [inventory.md](docs/subsystems/inventory.md) |
| `src/critter/` | critter AI and path finding, critter classes and art pages | [critters.md](docs/subsystems/critters.md) |
| `src/motion/` | physics, collision and movement of the player and objects | [motion.md](docs/subsystems/motion.md) |
| `src/combat/` | combat, damage, missiles, runes and spells | [combat.md](docs/subsystems/combat.md) |
| `src/map/` | the level map, texture maps, lighting | [map.md](docs/subsystems/map.md) |
| `src/3d/` | the 3D view: the render database and object sorting in C, seg004's 14 renderer modules | [3d.md](docs/subsystems/3d.md) |
| `src/gfx/` | seg003's 14 graphics library modules, sprites, art loading, cutscenes, palettes | [gfx.md](docs/subsystems/gfx.md) |
| `src/ui/` | input, the mouse, panels, the message scroll, menus, options, strings, the automap | [ui.md](docs/subsystems/ui.md) |
| `src/conv/` | the conversation interpreter, its built-ins, bartering | [conversations.md](docs/subsystems/conversations.md) |
| `src/event/` | SCD schedules and events, triggers and traps, world events | [events.md](docs/subsystems/events.md) |
| `src/sound/` | sound and music, and the Miles AIL 2.0 API (`AIL.ASM`) | [sound.md](docs/subsystems/sound.md) |
| `src/sys/` | seg021's 17 system modules, memory and EMS, archives and compression, errors, helpers | [sys.md](docs/subsystems/sys.md) |
| `src/lib/` | Borland's overlay manager (`OVERLAY.ASM`, from OVERLAY.LIB) | [sys.md](docs/subsystems/sys.md#the-overlay-manager) |
| `src/include/` | the shared headers, one per subsystem | [CONTRIBUTING.md](docs/CONTRIBUTING.md#shared-headers) |
| `src/port/` | the native port's UW2 half: the C that replaces UW2's assembly modules and DOS-only C, the translated modules, the port's main, UW2's bindings for Exhume's runtime and the link stubs; never compiled by the DOS build. The other half, the portability layer, the platform layer (SDL3), the emulated hardware, the sound library and record and replay, is [Exhume](https://github.com/abedegno/Exhume)'s runtime, compiled where it is | [PORT.md](docs/PORT.md#the-runtime) |

No UW2 build names its source files, so the file names come from System Shock's source release, the FM Towns names, or what the file does. `map/filenames.tsv` gives the evidence for each ([MAP.md](docs/MAP.md#source-file-names)).

The rest of the top level: `docs/`; `tools/`, the build, matching, linking, port and packaging tools ([BUILDING.md](docs/BUILDING.md#the-tools)); `tests/`, the recorded sessions and their golden references; and `map/`, `targets/`, `symbols.tsv`, `matched.txt` and `fmtowns/syms.tsv`, what the tools know about `UW2.EXE` and its FM Towns counterpart ([MAP.md](docs/MAP.md)).

## Status

- Decompilation: every segment matched (tag `v1.0-matched`), and the exact link identical to `UW2.EXE` (tag `v1.1-identical`).
- Port ([PORT.md](docs/PORT.md#phases-and-milestones)): Milestones 1 to 5 are done (it compiles, links, boots, plays from the title into the world with the 3D view, music and effects, and replays DOS sessions exactly), and so is 6a (`make test` against DOS goldens on every push). Milestone 6 is under way: Linux and Windows builds and release packages, then longer sessions into more of the game before a faithful release.
- What no session reaches yet is in [COVERAGE.md](docs/COVERAGE.md): spells, most SCD events and traps, most object uses and conversation built-ins. The port may stop or misbehave there.

## Documents

- [BUILDING.md](docs/BUILDING.md): requirements, building and running the port, the make targets, testing, the gate, CI and releases, working on one file, and the tools.
- [PORT.md](docs/PORT.md): the port's design, its milestones and their results.
- [FINDINGS.md](docs/FINDINGS.md): likely bugs in the original, game rules, engine findings, dead code and open questions.
- [UW1-UW2-DIFFERENCES.md](../docs/UW1-UW2-DIFFERENCES.md): where UW1's and UW2's rules differ, routine by routine, and what UnderworldGodot does for each.
- [docs/subsystems/](docs/subsystems/): one page per subsystem on its architecture, data, rules and open questions.
- [docs/behaviour/](../docs/behaviour/README.md): what the game does, as rules with exact numbers, for game programmers and modders who don't read the C: NPC AI, schedules, combat, magic, conversations, player upkeep, and traps and triggers, both games.
- [CONTRIBUTING.md](docs/CONTRIBUTING.md): the rule every change follows, comments, renaming, shared headers, constants and struct fields.
- [MATCHING.md](docs/MATCHING.md): the compiler switches, the assembler, and what Turbo C's output reveals about the original source.
- [LINKING.md](docs/LINKING.md): the exact link, the segment classes, what is taken from your EXE, and the modding build.
- [LAYOUT.md](docs/LAYOUT.md): every address written as a number, and what still depends on the original layout.
- [MAP.md](docs/MAP.md): the map, the target tables, `symbols.tsv`, `matched.txt` and the source file names.
- [COVERAGE.md](docs/COVERAGE.md): what the port's tests reach.
- [map/crosswalk.tsv](map/crosswalk.tsv): every function by the disassembly listing's name and address (the names UnderworldGodot cites), with the matched source's name and the file and line that define it. `python3 tools/crosswalk.py --find NAME_OR_ADDRESS` looks one up, including an address inside a routine, with or without the segment's paragraph. [uw1/](../uw1/README.md) has the same table for UW1; segment numbers mean different code in the two games.

## Credits

- Looking Glass Technologies and Origin Systems made Ultima Underworld II.
- John Miles' Audio Interface Library 2 source, which its author released free for any use (in effect public domain), is what the port's sound drivers are written from.
- [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3) emulates the FM chips and [munt's libmt32emu](https://github.com/munt/munt) the Roland MT-32, both under the LGPL 2.1 or later; [SDL3](https://libsdl.org) (zlib licence) is the base of the platform layer.
- [emu2](https://github.com/dmsc/emu2), [DOSBox-X](https://dosbox-x.com) and [js-dos](https://js-dos.com), through [dos-mcp](https://www.npmjs.com/package/dos-mcp), run the Borland toolchain and the DOS side of the tests.
- The original names come from the FM Towns release of UW2, through the symbol list and the IDA work in [UWReverseEngineering](https://github.com/hankmorgan/UWReverseEngineering). Some file and function names come from System Shock's source release.
- The method and tools are generalised in [Exhume](https://github.com/abedegno/Exhume), a toolkit and set of Claude Code skills for byte-matching decompilation of old DOS games; this repository is its first case study.

[THIRD-PARTY-NOTICES](THIRD-PARTY-NOTICES) lists every third-party component with its licence.

## Licence and legal notice

This project's own work (the tools, the port's code, the documentation and the tests) is under the [MIT licence](LICENSE), copyright 2026 abedegno.

The decompiled game code is derived from Ultima Underworld II, whose copyright belongs to its owners (Electronic Arts). The MIT licence covers only this project's original work and grants no right in the game. No game data is included in the repository or in any release package; to play or to build the DOS EXE you need your own copy of the game, such as the GOG release. [NOTICE](NOTICE) says which files are which, and covers the two other pieces of code in `src/`: Borland's overlay manager and Miles' AIL API.
