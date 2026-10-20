# Building

[docs/BUILDING.md](../../docs/BUILDING.md) has what both games share: getting the repository, Exhume, the top-level `make`, continuous integration and releases. This page has UW1's own: its make targets, its port, how to record and replay a UW1 session and check it against its golden reference, and its tests.

To build only the native port, which needs none of the Borland toolchain, DOS or game data, see [Building the port](#building-the-port).

## Exhume

The gate, the links and the replay tools are Exhume's, and the DOS build also uses Exhume's runtime in place:

- `runtime/include/portable.h` holds the macros the shared sources use to build for DOS and for a native port alike (explicit widths, `NULLTRAP`, `STACK_JUNK` and the record and replay hooks). The build stages it beside `src/include` for every compile, so the gate proves that it costs no byte. It includes UW1's `src/include/hookgame.h` first, which gives the original tokens of the hooks: `ORIG_GAME_TIME()` is `(*Time)`, `ORIG_KEY()` is `key()`, and so on.
- `runtime/replay/replay.c` is the record and replay code. Only the replay DOS build compiles it, with UW1's `src/include/rpgame.h`, which says what the hooks read and what the state dumps hold.

`tools/exhume.py` finds the checkout as every tool does ([docs/BUILDING.md](../../docs/BUILDING.md#exhume)): `$EXHUME`, else the submodule `../exhume`, else `.exhume`, else `~/Exhume`. The port's tools (`portbuild.py`, `package.py`, `fuzzasm.py`, `replay.py` and the rest) are Exhume's too, run with this folder's `exhume.toml`; they need Python 3.11 or later.

## Make targets

The Makefile is Exhume's template (`tools/templates/Makefile`); `make help` lists every target.

| Target | What it does |
| --- | --- |
| `make check` | the gate: every source matches and verifies, symbols.tsv rebuilds, the exact link is your `UW.EXE`, and the modding build with no change is the same |
| `make game` | the modding build, `build/MODLINK/out/UW.EXE` |
| `make hooks` | UW1's entry in the repository's git pre-push hook: `git push` runs `make check` in `uw1/` (`make hooks HOOK="make test"` for the port's tests). UW2's entry is separate, so you install only the games you have |
| `make setup-port` | what the port needs to build on this OS, and nothing else ([Building the port](#building-the-port)); `make setup-libs` and `make setup-sound` do one part each |
| `make port` | the port, `build/port/uw1port`; `make port-debug` the UBSan build, `build/port-debug/uw1port`; `make port-check` the compile-only measurement |
| `make port-release`, `make package` | the build the players' packages are made from, and the package for this OS in `build/dist` ([Releases](../../docs/BUILDING.md#releases)) |
| `make icons` | the icon files from their SVG sources ([The icon](#the-icon)) |
| `make test` | the gate, the port, the quick routine fuzzing and every session against its golden ([Testing](#testing)) |
| `make test-full` | the long tier ([Testing](#testing)) |
| `make verify`, `make fuzz`, `make coverage`, `make blackbox` | one part of the tests each (`make blackbox`: the black box, Exhume's `tools/blackboxcheck.py`, which CI also runs on Windows) |
| `make golden` | every session's golden reference made again from DOS (each replayed twice, checked identical) |
| `make golden-check` | every session replayed twice in DOS by the replay DOS build and compared with its committed golden; writes nothing |

Everything built goes under `build/`, which is never committed.

## The native port

The port ([PORT.md](PORT.md)) is a second build of the same C, for a modern host, on Exhume's runtime. It builds on macOS (Apple silicon and Intel), Linux and Windows (MSYS2's CLANG64 environment). The replays and the fuzzing pass on macOS; CI builds it on all three ([Continuous integration](#continuous-integration-and-releases)).

### Building the port

No Turbo C, DOS or game data is needed to build the port, only to run it.

```sh
make setup-port     # Exhume, and the compiler check, SDL3, libmt32emu and Nuked OPL3, per OS
make port           # build/port/uw1port
```

- macOS: the Xcode command line tools (`xcode-select --install`) and Homebrew; `make setup-port` runs `brew install sdl3 mt32emu pkgconf`.
- Linux (Debian, Ubuntu): `make setup-port` prints the `apt-get install` line for clang, pkg-config, cmake, ninja and the X11, Wayland, ALSA and PulseAudio headers when any is missing, then builds SDL3 3.4.16 and libmt32emu 2.8.3 from source into `tools/libs` (Exhume's `tools/setup-libs.sh`; ignored by git). `make port` finds `tools/libs` by itself and links the program to load its libraries from there.
- Windows: install MSYS2 and open its CLANG64 shell; `make setup-port` installs the packages with pacman and builds libmt32emu into `tools/libs`. Then `make PY=python port` links `build/port/uw1port.exe`, with the icon as a resource.

### Running the port

```sh
build/port/uw1port                      # finds the game by itself, or asks
build/port/uw1port --data ~/UWGOG/UW1   # names it
```

The port needs the user's own UW1: a directory with `UW.EXE`, `DATA`, `CRIT`, `CUTS` and `SOUND`, which it reads and never writes. It checks that `UW.EXE` is the GOG release's by its size and CRC-32. Without `--data` it looks at `$UW1PORT_DATA`, the folder it used last, the current directory and its own (and a `UW1` folder in either), on Windows the folders GOG's installers record in the registry, and GOG's install folders on each host and `~/UWGOG`, up to four levels into any folder whose name has "Underworld" or "UW1" in it (Exhume's `runtime/port/sys/gamedir.c`, with UW1's names in `src/port/portgame.h`). GOG's Mac app keeps the game in a CD image, `game.gog`, the Ultima Underworld 1 and 2 CD, where UW1 is the folder `UW` (`PORT_GAME_IMAGE_FOLDER`); when the search finds only the image, the port copies that folder once into `gog-cd/UW1` in its home directory and plays from there. When nothing is found and the port has a window, it asks with a folder picker.

Files the game creates or changes (its scratch files, the saved games, `DATA\UW.CFG`) go to the home directory, `--home DIR`, else `$UW1PORT_HOME`, else `~/.uw1port` (`%APPDATA%\uw1port` on Windows when `HOME` is not set). Its settings file, `uw1port.cfg`, keeps the game folder and the MT-32 ROM folder of the last run with a window. The first such run with no `DATA\UW.CFG` in the home directory writes one for a Sound Blaster with its FM music and digitised speech (`--sound 3,1`), or, when MT-32 ROMs are set, for the MT-32's music and the Sound Blaster's speech (`--sound 6,1`). Runs with `--hidden`, `--record` or `--replay`, which are tests, never show a dialog, write no settings and get no sound card they were not given. `uw1port --help` lists the options.

### Sound

The emulated sound hardware comes from two libraries, neither in the repository: Nuked OPL3 (commit `765ec96`, LGPL-2.1), which `make setup-sound` fetches into `tools/nuked-opl3` and `make port` compiles in (`[[port.vendor]]` in `exhume.toml`), and libmt32emu 2.8.3 (LGPL-2.1-or-later), which `make port` links when pkg-config finds it (`[[port.pkg]]`). `make port` says which it found (`third-party: Nuked OPL3, libmt32emu`); without them it builds and those chips are silent. The MT-32 needs the user's own ROM images: the port finds them by their contents under any names: `--mt32-roms` with a folder or one of the files (or `UW1PORT_MT32_ROMS`), else the remembered setting, else a search of the home folder's `roms/` and `mt32-roms/`, the game folder, the program's folder and DOSBox Staging's MT-32 folders. A CM-32L pair is preferred to a CM-32LN pair, then an MT-32 pair, the newest control ROM first; split halves (`_a`/`_b`, `_l`/`_h`) are joined when both are there, and a path to nothing finds none. A ROM folder dropped on the app or the window is remembered too, and the music moves to the MT-32 from the next start; so it does when ROMs turn up after a first run on which the port chose the Sound Blaster itself (`sound-auto` in the settings), but never after the player's own `--sound` ([docs/BUILDING.md](../../docs/BUILDING.md) lists the folders).

### The icon

The icon is the project's own drawing: a stylised silver ankh on a dark blue stone tile (UW2's design in other colours), not the Ultima logo and nothing from the game. `tools/dist/icon/uw1.svg` is its source, with `uw1-small.svg` for 16 to 32 pixels. `make icons` (Exhume's `tools/icons.py`; it needs puppeteer's Chrome: after the top-level `make setup-exhume`, `npx --prefix exhume puppeteer browsers install chrome`, and `iconutil` on macOS) writes the committed files: `png/uw1-N.png` from 16 to 1024 pixels, `uw1.icns` (the app's), `uw1.ico` (the Windows program's resource), `uw1.png` (Linux) and `src/port/platform/sdl3/icon.h`, the window icon on Linux and Windows (`PLAT_ICON`).

## Testing

The port is tested against DOS in three ways, in two tiers (Exhume's `tools/test.py`):

| Command | What it runs | Time |
| --- | --- | --- |
| `make test` | `make check`; `make port`; the routine fuzzing's quick run; every replay session in the port against its golden | about a minute |
| `make test-full` | `make check`; `make port` and `make port-debug`; every golden made again from DOS, each session replayed twice and checked identical; every session against them in the port and in the UBSan build; the sound drivers against the real ones (`tools/ailcheck.py` on `sound`, `soundfm` and `soundmt`); the fuzzing's deep run; the coverage report | about 15 minutes, nine of them the deep fuzzing |

Both print a summary of their steps with the time each took, and exit 1 if any failed. After `make test-full`, a golden the regenerated runs changed is reported: review it, and commit it with the change that explains it.

### Routine fuzzing

Exhume's `tools/fuzzasm.py` tests single routines on inputs no session gives them. For each target in `tools/fuzz_targets.py` it runs the routine's own bytes from your `UW.EXE` in Unicorn, and the port's code for it (the translation by `tools/asm2c.py`, or a C entry such as `cSqRt`) in Exhume's `tools/fuzzhost.c`, linked from the port's objects in place of `main.c` with `tools/fuzzhost-uw1.c` (the memory regions and the C entries), on the same registers and memory, and compares the registers, the flags the callers read and every byte of memory the routine changed. There are 34 targets: IMATH's sines, square roots and arctangents, near and far, and their four C entries; INSTANCE's matrix products, clip codes and `mxmul`; SPHERE's `sphere_check` and `get_dist`; GRENTRY's frame buffer span writers, fill, dim, light and `setup_frame_buf` (with the copier patch); and EXPAND's image decoders, palette builder and record decoder. `make fuzz` runs 200 cases of each (40 of the heavy ones) in about ten seconds; `make fuzz FUZZ=--deep` fifty times as many with a new seed. Unicorn must be in Exhume's `.venv`.

### Coverage

`make coverage` builds the port with clang's source-based coverage, replays every session in it, runs the fuzzing on the same objects, and writes [COVERAGE.md](COVERAGE.md).

## Recording and replaying a session

A session is a recording of everything the game read from the outside world while someone played it in DOS: each read of the game clock, the keyboard, the mouse and its buttons, the time of day, and the sound hardware's state, in order. Replayed, the game reads the same values in the same order, so it does exactly what it did, however fast the machine is. At checkpoints the replay writes the game state to `STATE.OUT`: the player record, the level, the graphics library's data, Borland's rand seed, C0's null-pointer area and, in a full dump, the palette, the CRT controller and all of video memory. Exhume's docs/port.md, "Verifying a port", is the method, and `runtime/replay/replay.c` has the file formats.

The hooks are the macros `GAME_TIME()`, `KEY()`, `MOUSE()`, `MBUTTONS()`, `WALL_TIME()`, `SRAND()`, `SND_READ()`, `SLAVE_TIMER()` and `CHECKPOINT()` in the C sources (`JOY_READ()` and `JOY_BUTTONS()` exist too, but no UW1 source reads the joystick). Under Turbo C they are the original tokens, so the DOS build is unchanged. `CHECKPOINT(1)` to `CHECKPOINT(5)` are in `src/game/UWEDIT.C`: after the start-up, after the title, after the main menu, when the game screen is drawn and when it has faded in.

All the commands are Exhume's `tools/replay.py`, run with UW1's `exhume.toml`, whose `[replay]` section names the sessions, their steps and their sound configurations:

```sh
R="python3 $HOME/Exhume/tools/replay.py --config exhume.toml"
$R build                                   # the replay DOS build: build/replay/UW.EXE
$R record OUT --session newgame            # record a session in js-dos, from [replay.steps]
$R record OUT w:9000 k:Escape s:menu       # or from steps of your own
$R dos tests/replay/walk.rec OUT           # replay a session in DOS
$R compare OUT1 OUT2 [--same-build]        # compare two runs' dumps, checkpoint by checkpoint
$R show OUT/STATE.OUT PNGDIR               # list a dump, and write each full checkpoint's screen
$R nulls OUT                               # the null-pointer write check of a DOS run
$R log tests/replay/walk.rec               # what a recording holds
$R golden [SESSION ...]                    # make goldens from DOS
$R golden [SESSION ...] --check            # replay in DOS and compare with the committed goldens
```

The replay DOS build is the modding build with every source that uses a hook compiled with `-DREPLAY`, every source with a `NULLTRAP` mark compiled with `-DNULLTRAP`, and `replay.c` linked in as one more resident module (`tools/link.py --mod --add`). The other commands build it when it is out of date; run `make check` first, since the modding link starts from the exact link's snapshot.

Recording runs in js-dos through dos-mcp, which takes the inputs in real time; the steps are Exhume's `tools/replaydos.mjs` steps (`w:MS` wait, `k:KEY` keys, `t:TEXT` typed text, `h:KEY,MS` a key held down, `M` the pointer to the top left corner, `m:DX,DY` a pointer move in the 640 by 400 frame, `c:BUTTON,MS` a click, `s:NAME` a screenshot). F12 ends a recording. Replays run in DOSBox-X when it is installed, else in js-dos.

Each session has its own `DATA\UW.CFG`, `NAME.cfg` beside `NAME.rec`, so no session depends on the configuration in your copy of the game. Every session also starts with no saved games: the saved games already in your game's `SAVE1` to `SAVE4` are left out of the staged copy.

These variables, when set, pass through to the replay DOS build (`replay.c`'s first comment has them all): `UWRPCK=n` a periodic dump every n clock ticks (hex) instead of 400h; `UWRPFULL=lo,hi` full periodic dumps while the clock is in that range; `UWRPHOOK=lo,hi` a full dump at every hook call whose count is in that range; `UWRPTRACE=lo,hi` every hook call in that clock range, with its caller, to `TRACE.OUT`; `UWRPFB=1` the 3D view's frame buffer in every dump. Together they find where two runs part: the checkpoints bracket it, `UWRPCK=1` finds the tick, `UWRPHOOK` the hook call.

### The sessions

The recordings and their goldens are committed under `tests/replay`. The moves in `items` and `talk` use the step keys (Shift+W half a tile, Shift+A and Shift+D 45 degrees), which move by fixed amounts, so that the same steps reach the same place in every recording. They hold inputs and digests only, no game data.

| Session | What it covers |
| --- | --- |
| `newgame` | boot, the title, the main menu, a new character (every default, the name Avatar), sixteen seconds in the game |
| `walk` | the way into the game, then walking, turning, looking up and down, stepping back, sliding and a click in the 3D view |
| `sound` | the way into the game with a Sound Blaster (FM music, SBFM.ADV, and digital speech, SBDIG.ADV), fight mode, swings, walking and turning, and the music played on until the theme changes |
| `soundfm` | the same with the FM music alone (no speech card) |
| `soundmt` | the same on a Roland MT-32 (MT32MPU.ADV at 330h), every wait doubled for the timbre uploads |
| `items` | the sack by the start: picking it up, opening it, the map in it (the automap, with a note), look mode, the statistics panel, a save to slot I and a restore |
| `talk` | the Red Key from the pack in the chest by the start, the locked door west of it unlocked and opened, the walk to Bragit's room in the human encampment, and a conversation with Bragit (whoami 67, the first man to talk to) with three answers. Talk mode clicks over a grid of the view in four directions, since Bragit wanders about his room: the misses say "You cannot talk to that", the first hit starts the conversation, and the rest fall in the conversation screen. Its golden is the largest (11 MB), since every click is a full checkpoint |
| `load` | slot I, the save `items` makes, loaded from the main menu ("Journey Onward") |
| `intro` | the title and the whole introduction, about four and a half minutes, to the main menu |
| `female` | the way in as `newgame`'s, but a female character with the last portrait (Down at the sex question, Down four times at the portraits), then a few seconds in the game. Her body pictures after the first start past 7FFFh in CHRBTNS.GR's block, where the port read them through signed offsets and stopped (issue 8) |
| `savecursor` | the sack by the start held on the cursor, where Ctrl+S and Ctrl+R are refused ("You cannot select options partway through an action") and a click on the icons is ignored; then the sack into the first slot with the right button, a save to slot I and a restore |
| `fallrestore` | the way to Bragit's room as `talk`'s, then west along the corridor to the platform at (9,6), a save to slot I there, a run north off its edge and Ctrl+R while still falling. A restore keeps the player's speed and the first step after it is 40h units long, so MOTION.C's 16-bit multiply wraps in DOS (821 * 64 and 547 * 64), where the port kept 32 bits and landed the player a step further on (issue 10) |
| `tooheavy` | issue 9 in DOS: from the saved game `setup/tidy-heavy` writes (a new character with strength 10, carrying 200 coins, all they can, and 20 more coins at their feet), get mode on the coins and Enter at "Move how many? 1": "That is too heavy for you to pick up", and `player_3dget` loses the split stack's copy (it unlinks it and never frees it). Then a save to slot I and a restore of it, whose check of the object lists says "Problems in object list". Its golden is DOS's, so the port's faithful leak is proven against DOS; with `--enhance tidy-objects` the copy is freed (`tests/replay/enhanced/tidy-objects`) |
| `fallpause` | from the saved game `fallrestore` writes (the platform at (9,6)): Shift+D, then s held a second, which walks north-east off the edge at the walking speed (248 on each axis), and the restore prompt opened and cancelled (Ctrl+R, Escape) four times while the player falls. The first step after each prompt is 40h units long and whole, and zspeed (the fall over the slow axis, times 256) passes 1024, so MOTION.C's `full_move` shifts it out of 16 bits in DOS: the step comes out upwards, which `full_move` takes for a landing (`do_zbounce`) in mid-air, where the port moved the player down |
| `overpay` | from the saved game `setup/trader` writes (a new character on level 2 beside the dwarf Brawnclan, with 26 rubies): talk, trade, the rubies into the player's first trade slot, his first item selected, the player's appraisal and the offer. BARTER.C works the gain out in 16 bits: the rubies are worth 26 * 25 * 40 / 64 = 406, blurred to 564 for the appraisal and 422 for his valuation, and his item 5, so (564 - 5) * 100 and (422 - 5) * 100 pass 7FFFh. DOS says "an excellent deal" and he refuses, where the port said "a terrible deal" and he took them |
| `powerkill` | from the saved game `setup/headless` writes (a new character with attack, barehand and strength raised, on level 6 beside a powerful headless worth 162): fight mode and swings until it dies. COMBAT.C's `player_killed_a` multiplies the experience for a powerful critter in 16 bits: here 919 * 39, which DOS takes as -1855, so the kill takes experience away, where the port gave 2240 |

`load` replays the saved game `items` writes (`[replay] stage_from`); the golden tools take it from `items`' own DOS run. To replay `load` by hand, give it a directory holding that `SAVE1` with `--stage DIR`.

`tooheavy` replays the saved game `setup/tidy-heavy` writes. That is a setup session (`tests/replay/setup/tidy-heavy/`), made by the port from an input script that uses a test's cheats ([docs/BUILDING.md](../../docs/BUILDING.md#cheats-for-tests)): a new character as `newgame` makes one, then `set str 10`, `give 0xA0 200` and `give 0xA0 20 drop`, and a save. Its recording is format 6, which DOS cannot replay, so `replay.py golden` and `verify` both make that saved game by replaying it in the port, and the golden keeps its digests; DOS then judges `tooheavy` from it. The saved game loads in DOS: `tooheavy` was recorded there, in js-dos, from it (`replay.py --stage DIR record OUT --session tooheavy`). Exhume's `tools/enhcheck.py setup record tidy-heavy` records it again from its script, and `setup check` (part of `enhcheck.py all`) checks its cheats ran and that two replays write the same saved game.

`fallpause`, `overpay` and `powerkill` were recorded by the port from input scripts (`tests/replay/NAME.script`, with no cheats, so their recordings are format 4, which DOS replays), and their goldens are DOS's replays of those recordings, so DOS judges them as it judges any other. They could not be recorded in js-dos: it runs the game flat out, a physics step of one unit a frame, at which a walking player hardly moves, and its steps cannot drag the mouse, which taking a stack out of the pack needs. Each was recorded with the change it tests in place, so that DOS replays the inputs down the same path; the port before that change fails it. `overpay` and `powerkill` start from setup sessions, `setup/trader` and `setup/headless`, made as `setup/tidy-heavy` is (above).

`sound` replays in js-dos (`[replay] dos_backend`): in DOSBox-X its replay hangs once the introduction starts its speech on SBDIG.ADV (tried with the Sound Blaster 16 and Pro 2, either CPU core, with the mixer on and off), while js-dos replays it to the end, the same twice. The other sessions replay in DOSBox-X, and their goldens come out the same in js-dos.

### Golden references

A golden is what DOS did with a session: `tests/replay/golden/NAME/golden.json` holds, for each checkpoint, its header and a 64-bit digest of each section as DOS and a port are compared, and a PNG of the screen at each full checkpoint. `make golden` makes them again (each session replayed twice, the two runs required identical, and the null-pointer check passed); `make golden-check` replays every session twice with the current replay DOS build and compares both runs with the committed golden, writing nothing. `make golden-check` needs no port: it tests that the replay build and the sessions still agree with DOS.

Some bytes are the machinery of the graphics library rather than game state, and `exhume.toml` masks them with the reason for each (`[[replay.mask]]`): its two private stacks in seg048 (3963:4DFE..4FA6 and 547E..5546), which interrupts push onto whenever they come, the clock word its retrace wait keeps (0652), and, between DOS and a port, the stack pointer it saves (5588..558B). The far pointers stored in seg048 are compared as the blocks they name (`[replay.segments]`), since DOSBox-X, js-dos and a port load the program at different segments.

### Adding a session

1. Write its steps into `[replay.steps]` and its `DATA\UW.CFG` text into `[replay.cfgs]` in `exhume.toml`.
2. Record it: `$R record OUT --session NAME`. Look at the screenshots it took, then copy `OUT/RECORD.OUT` to `tests/replay/NAME.rec` and `OUT/UW.CFG` to `tests/replay/NAME.cfg`.
3. If it starts from another session's saved game, add it to `[replay] stage_from`. A state no session reaches cheaply can be set up by a setup session with cheats (`tests/replay/setup/NAME/`, above), named here as `setup/NAME`; record the new session in DOS from that saved game with `--stage DIR`.
4. `$R golden NAME`, and commit the recording and its golden together.

A recording is only as good as the replay build that made it: when the replay build changes in a way that changes what the game does (a `STACK_JUNK` mark, say), record the sessions again and make their goldens again.

### What the replays found

- **A stack-junk read in the cutscene player.** `src/gfx/CUTS.C`'s `show_anm` never sets its state's flag b7 (a pause that waits for the speech) before the pause loop reads it, so in DOS a pause waits or not by whatever the stack held. DOSBox-X and js-dos took different paths on the same recording. `STACK_JUNK_SET(st.flags.bit.b7, 0)` gives the replay build and a port the path with no wait; the DOS build is unchanged.
- **The game polls the mouse buttons on a small stack.** Some of UW1's calls to `mbuttons()` run on the 0D4h-byte stack at the start of SYSENTRY.ASM's `_DATA` (DS:2444 in the replay build), just above the C library's near heap variables. A full dump made from that hook overran it, corrupted the near heap, and a later `realloc` wrote into DS:4. `replay.c` now does its deep work (its files, the dumps, the trace, the end) on a stack of its own.

## Continuous integration and releases

One set of workflows serves both games; [docs/BUILDING.md](../../docs/BUILDING.md#continuous-integration) has them, the private bundle (`abedegno/uw1-ci-assets`) and its secrets, the releases (tags `uw1-vX.Y.Z`), signing and notarising. UW1's `sound` session's goldens are made in js-dos (`[replay] dos_backend`), whose headless Chrome CI's Linux tools leave out, so the nightly golden step for `sound` needs it; `accuracy.yml` replays goldens only in the port and does not.

The icon is the project's own drawing: a stylised silver ankh on a dark blue stone tile (UW2's design in other colours), not the Ultima logo and nothing from the game. `tools/dist/icon/uw1.svg` is its source; `make icons` (Exhume's `tools/icons.py`) writes the committed sizes and formats and `src/port/platform/sdl3/icon.h`.
