# Building

This page covers what both games share: getting the repository, Exhume, the top-level `make`, continuous integration and releases. Each game's own page has the rest: [uw2/docs/BUILDING.md](../uw2/docs/BUILDING.md) (UW2's tools, the DOS the toolchain runs in, its sessions, the gate in detail) and [uw1/docs/BUILDING.md](../uw1/docs/BUILDING.md) (UW1's sessions, its golden references, its fuzz targets).

To build only a native port, which needs neither the Borland toolchain, nor DOS, nor game data, see [The ports](#the-ports).

## Getting the repository

```sh
git clone --recursive https://github.com/abedegno/underworld-exhumed.git
cd underworld-exhumed
make setup          # Exhume and its Python tools, then each game's setup
```

`--recursive` fetches Exhume, a submodule at `exhume/`. Without it, `make setup` (or `make setup-exhume`) fetches it. The repository holds no game data and no Borland software. You need:

- **Your own copies of the games.** UW1's `UW.EXE` at `~/UWGOG/UW1/UW.EXE` (or set `UW1_EXE`), and UW2's `UW2.EXE` at `~/UWGOG/UW2/UW2.EXE` (or set `UW2_EXE`). The GOG releases work.
- **For the byte-matching builds:** the Turbo C++ 1.01 disk images (four 720K images, which Borland released free of charge) and the Turbo Assembler 2.0 disk image. `make setup-toolchain TC_DISKS=DIR TASM_DISKS=DIR` unpacks them into `uw2/TC` and `uw2/TASM` (ignored by git) and checks both are the expected builds; both games' builds use them from there (`uw1/exhume.toml`, or set `EXHUME_TC` and `EXHUME_TASM`), so UW1 alone can be set up without UW2. `make -C uw2 setup TC_DISKS=DIR TASM_DISKS=DIR` does the same as part of UW2's setup.
- **Tools:** Node 20 or later, Python 3.11 or later, mtools and 7z (`brew install mtools p7zip` on macOS, `apt install mtools p7zip-full` on Ubuntu).

## Exhume

Both games are built and tested with [Exhume](https://github.com/abedegno/Exhume), the decompilation toolkit and porting runtime both were made with. It is a git submodule at `exhume/`, so the repository records the exact Exhume commit both games are proved with: one commit for both. The ports' generic half (the portability and platform layers, the emulated hardware, the sound library, the x86 machine, record and replay) is Exhume's `runtime/`, compiled in place, and the gate, the replays, the fuzzing and the packaging are Exhume's tools, run with each game's `exhume.toml`.

Every tool finds Exhume as `$EXHUME`, else the submodule `../exhume` seen from a game folder, else `.exhume` there, else `~/Exhume`. `$EXHUME` lets you build against another checkout while you work on Exhume itself.

To take a newer Exhume: `git -C exhume pull`, run `make test-full`, and commit the submodule with the change that needs it. Dependabot proposes Exhume updates as pull requests, which CI checks. A change to Exhume itself is made and proved in Exhume first (its `examples/uw2/prove-port.sh`).

## Make

The top-level Makefile runs a target in both games, or in one with `GAME=uw1` or `GAME=uw2`. Each game folder has its own Makefile with every target (`make -C uw2 help`), and works the same run from the top or from inside the folder.

| Target | What it does |
| --- | --- |
| `make setup` | Exhume and its Python environment (`exhume/.venv`, with iced-x86 and Unicorn), then each game's setup |
| `make check` | each game's gate: every source matches and verifies, `symbols.tsv` rebuilds, the exact link is the original EXE, and the modding build with no change is the same |
| `make test` | the gate, the port, the quick routine fuzzing, every recorded session in the port against its DOS golden, and the enhancements (Exhume's `tools/enhcheck.py all`: the options and settings, presentation ones keeping DOS's game state, and the port-made baselines under `tests/replay/enhanced/`; [ENHANCEMENTS.md](ENHANCEMENTS.md)), and the player's movement in live runs (Exhume's `tools/movecheck.py`: sidestepping both ways and walking backwards at five headings go the way the key says, the 3D frames 8 or more ticks apart; issue 6), and the black box (Exhume's `tools/blackboxcheck.py`: each player's session recorded to a folder of its own under `recordings/`, the newest five kept) |
| `make test-full` | the long tier: goldens made again from DOS, the UBSan build, the sound drivers against the real ones, the deep fuzzing, the coverage report |
| `make port` | each game's native port: `uw1/build/port/uw1port` and `uw2/build/port/uw2port` |
| `make port-release`, `make package` | the players' builds and packages, into each game's `build/dist` |
| `make repocheck` | the whole repository: no game data or Borland software is tracked, every script compiles, every Markdown link resolves, anchors included, and nothing links into the old UW1Decomp or UW2Decomp repositories' files |

Everything built goes under each game's `build/`, which is never committed.

## The ports

No Turbo C, DOS or game data is needed to build a port, only to run it:

```sh
make setup-port     # per game: the compiler check, SDL3, libmt32emu and Nuked OPL3 for this OS
make port           # both games; GAME=uw2 for one
uw2/build/port/uw2port                      # finds the game by itself, or asks
uw1/build/port/uw1port --data ~/UWGOG/UW1   # or names it
```

- **macOS:** the Xcode command line tools and Homebrew.
- **Linux (Debian, Ubuntu):** `make setup-port` prints the `apt-get install` line it needs, then builds SDL3 and libmt32emu into each game's `tools/libs`.
- **Windows:** MSYS2's CLANG64 shell, then `make PY=python port`.

Each game's BUILDING.md has its options, its sound cards and where it keeps saves and recordings. [PORT.md](PORT.md) is how the ports work.

The ports also build to WebAssembly for a web page, and a site with them can be deployed: see [WEB.md](WEB.md).

## Input scripts

The ports take `--input-script FILE`, for tests: keys and mouse events at set times, fed in as a player's would be (Exhume's `runtime/port/sys/inscript.c`), so a hidden run can record a session with no one at the keyboard. One event a line, at a time in milliseconds of the game's PIT clock (18.2 Hz, so about 55 ms apart at best) from the start; `#` starts a comment:

```
3000 key enter         press and release (names: a-z, 0-9, f1-f12, up, down, left, right,
4500 down w            enter, space, esc, tab, shift, ctrl, alt, home, end, pgup, pgdn ...)
6000 up w
7000 move 160 100      the pointer to x, y of the 320 by 200 screen
7200 click 160 100 left
8000 key grave         the ` key (also named: . and ;)
8500 look 200 -40      relative motion, as a captured mouse gives (mouse-look): right and up
```

A bad line stops the run, naming it. (Related test switches: `UW1PORT_TEXTURE_PROBE` makes UW1's texture mappers draw each face in a colour of its own, for Exhume's `tools/enhcheck.py coverage`; `UWnPORT_HOLE_PROBE=N` clears the 3D view to colour N before each frame and reports the pixels still N after it on stderr, and `UWnPORT_NO_BACK_PASS` leaves out `wide-pitch`'s drawing behind the player, for `tools/enhcheck.py holes`; `UWnPORT_POS_LOG` prints the player's position and facing at the end of the run, with how many 3D frames came under 8 ticks apart, for `tools/movecheck.py`.) The enhancements' sessions are made this way (`tests/replay/enhanced/*/session.script`; Exhume's `tools/enhcheck.py baseline record NAME`). Two recordings of one script differ in timing, since the events arrive between frames while the game reads its input on its own thread; a recording's replay is what is exact.

### Cheats, for tests

A test sometimes needs a game state that scripted play cannot reach cheaply: a character carrying nearly all they can, say, or one on a deep level. An input script can set one up with `cheat` lines, then save the game, and the real test starts from that saved game:

```
44000 cheat set str 10                 strength 10; the maxima follow (UW1 carries str * 20, UW2 str * 13 + 300)
44100 cheat give 0xA0 200              200 coins into the pack (or at the feet when it is full or too heavy)
44200 cheat give 0xA0 20 drop          20 coins at the player's feet
44300 cheat goto 2 30 30               level 2, square (30, 30)
```

| Command | What it does |
| --- | --- |
| `goto LEVEL X Y` | moves the player as a teleport trap does (`do_teleport`, then the main loop's `new_player_pos`: `ChangeLevel` for another level, then `player_setup`). LEVEL is the game's number (UW1 1 to 9, UW2 1 to 80), X and Y a square, 1 to 62. On the same level a square with no free place near it is refused; on another level, which is not loaded until the move, the game kills a player it cannot place, as it does after any teleport |
| `give ITEM [QTY [QUALITY]] [drop]` | makes an object with the game's allocator (`CreateObj`): ITEM the game's item id (`src/include/items.h`), decimal or `0x` hex, one a player can carry (0x000 to 0x03F, 0x080 to 0x13F); QTY for a stack, 1 to 511; QUALITY 0 to 63 (the game's default is 40). It goes into the first empty backpack slot on show when the player can carry it (`AddToInventory`, `EncumCheck`), else, or with `drop`, at the player's feet (`put_at`, as a conversation puts things there) |
| `set FIELD VALUE` | `hp` (1 to the maximum), `mana` (0 to the maximum), `str`, `dex`, `int` (1 to 99) or a skill by name (`attack`, `defense`, `barehand`, `sword`, `axe`, `mace`, `missile`, `mana`... `swimming`; 0 to 30). An attribute or skill is followed by the game's `player_compute`, as a level gained is: the maximum hit points and mana and the weight the player can carry |

Each goes through the game's own routines, so the state it leaves is one the game could hold itself, and a game saved after it loads in DOS (checked: UW1's `setup/tidy-heavy` save, and saves after `goto`, `set` and `give` in both games, each loaded in the replay DOS build). The commands are the game's (`src/port/sys/cheats.c`); Exhume's `runtime/port/sys/cheat.c` reads, gates, records and replays them. A cheat's time only lets it go: it runs on the game's thread at the main loop's next pass in which the game is on its 3D view with nothing on the cursor and no move pending (`port_cheat_point`, `src/game/MAINLOOP.C`, port only), so a cheat waits out a fade, a level change or a menu.

Cheats are a test's alone. A script with a `cheat` line is refused unless the environment has `EXHUME_TEST_CHEATS=1`, and the web build refuses it always, so that no script passed to a player's build can change its game. Each cheat logs `cheat: TEXT (not as DOS)`. A recording made with cheats in its script is format 6 (Exhume's `runtime/replay/replay.c`): format 5's enhancements chunk, always there, and each cheat where it ran, by the number of the main loop's pass, so its replay runs them again at the same passes and is as exact as any other. Its replay also needs `EXHUME_TEST_CHEATS=1`, which Exhume's `tools/replay.py` sets for a format 6 recording, and DOS never replays one.

A session that uses cheats is a setup session, `tests/replay/setup/NAME/` (`session.script`, `session.rec`, `session.cfg`), made by the port from its script with Exhume's `tools/enhcheck.py setup record NAME` and checked by `enhcheck.py setup check` (its cheats ran, and two replays write the same saved game). Another session names it in `stage_from` (`[replay] stage_from`, or an enhanced session's `stage_from` file) as `setup/NAME`: the saved game it writes is then made by the port's replay of it, in `replay.py golden` and `verify` alike, and the golden keeps that saved game's digests. So a session recorded in DOS can start from it, and DOS still judges what that session does. UW1's `tooheavy` is one ([BUILDING.md](../uw1/docs/BUILDING.md#the-sessions)).

## Continuous integration

One set of workflows in `.github/` serves both games. They are rendered from Exhume's CI templates with this repository's values in `ci.toml` (`python3 exhume/tools/citemplates.py --vars ci.toml .`), so they are never edited by hand. Each job runs once per game, in that game's folder. A change under `uw1/` runs only UW1's jobs, and likewise `uw2/`; a change to `exhume`, `docs/`, `.github/` or `ci.toml` runs both.

| Workflow | When | What it runs | Needs the private bundle |
| --- | --- | --- | --- |
| `port.yml` | every push and pull request, forks included | each port built, and its compile-only check, on Ubuntu 24.04, macOS and Windows (MSYS2 CLANG64) | no |
| `repocheck.yml` | every push and pull request | `make repocheck` | no |
| `accuracy.yml` | pushes to `main`, pull requests from branches of this repository, and by hand | per game: the gate and `make test` on Ubuntu 24.04; every session against its golden in the port built on Windows and on macOS; the players' Windows zip, unpacked, started with only Windows's own folders on the PATH and replaying every session, on Windows Server 2022 (Exhume's `tools/pkgcheck.py`); and on all three, the MT-32 checks below | yes |
| `nightly.yml` | 03:17 UTC daily, and by hand | per game: `make test-full` (UW1's with dos-mcp's Chrome, for its js-dos-only `sound` golden); and `make test` against Exhume's latest `master` instead of the pinned submodule (`exhume-master`), so a change in Exhume that would break a game shows within a day. A regenerated golden that differs from the committed one fails the run, and the artifact holds the list, the changed screens and the text diff of each `golden.json` (`golden-diff.txt`, printed in the log too) | yes |
| `release.yml` | a tag `uw1-v*` or `uw2-v*`, weekly on Mondays, and by hand | that game's packages for macOS, Linux and Windows, each then tested as below; for a tag push, once every test passes, a draft release (a weekly or hand-made run never makes one, even on a tag) | the tests do (Apple's secrets sign and notarise the macOS app) |
| `site.yml` | a release published (not a prerelease), and by hand | the web build deployed ([WEB.md](WEB.md), "Deploying"): each game's latest published final release built from its own tag, checked, and pushed to the `gh-pages` branch of `abedegno/underworld-exhumed-web` with `WEB_DEPLOY_KEY`; by hand a dry run unless `push` is ticked (`skip_replays` leaves out `make webcheck`) | yes, both, for the game files and the ROMs |

### The MT-32 checks

With the bundle's ROMs (`MT32_ROMS`), on Linux, macOS and Windows, after the sessions:

- **How the port finds ROMs** (Exhume's `tools/romcheck.py`): each source in turn (`--mt32-roms` with a folder or a file, the environment variable, the remembered setting with a folder or a file, and every searched folder on that system), their order (a remembered folder that has gone falls through to the search), a CM-32L pair preferred to an MT-32 pair, and split halves from the bundle's `mt32-split/` joined into a pair, and the wrong cases (half a pair, wrong files, halves without their partners, a mistyped path, which finds none rather than its parent folder's, none); and in a player's runs, the music card: the MT-32 once ROMs turn up or a folder is given as a bare argument (dropped on the program's icon), never after the player's own `--sound`; each in a fresh home.
- **The MT-32's sound** (Exhume's `tools/audiocheck.py`): `soundmt` replayed with the ROMs, its whole audio written as a WAV, checked not silent, and its SHA-256 compared with `uwN/tests/replay/audio/soundmt.PLATFORM.sha256`. Floating point rounds differently between systems, so each platform has its own digest (`--update` writes one).

### Testing the release packages

Each game's release workflow tests the packages it has just built before it makes a draft: the Windows zip on Windows Server 2022 and 2025, the app on macOS 14 and 15 (Apple Silicon) and macOS 15 on Intel, and the tarball and the AppImage in clean Ubuntu 22.04, Ubuntu 24.04, Debian 12 and Fedora 41 containers. Each package is unpacked and started as a player gets it (`tools/pkgcheck.py`), replays every session against the goldens, and plays `soundmt`'s MT-32 audio against its digest; on a tag, Gatekeeper must accept the notarised app. The same runs weekly with no draft, so a change in GitHub's runners or a dependency shows between releases. These tests found a crash that only one Linux build had (UW2's FINDINGS, "Smaller slips").

A pull request from a fork gets `port.yml` and `repocheck.yml` only: GitHub gives it no secrets, and the jobs that need them skip themselves rather than fail.

### The private bundles

The gates and the replays need each game and the Borland toolchain, which cannot be published. They come from two private repositories, each with its Actions disabled and holding one age-encrypted file: `abedegno/uw1-ci-assets` (`uw1-ci-assets.tar.gz.age`) and `abedegno/uw2-ci-assets` (`uw2-ci-assets.tar.gz.age`). Each is a tar.gz of `game/UWn` (the owner's GOG copy), `tc/Disk01.img` to `Disk04.img` (Turbo C++ 1.01), `tasm/Disk01.img` (TASM 2.0), `MANIFEST.txt` and `SHA256SUMS`. This repository's secrets open them:

| Secret or variable | What it is |
| --- | --- |
| `UW1_ASSETS_DEPLOY_KEY`, `UW2_ASSETS_DEPLOY_KEY` | the private half of an ed25519 deploy key that can only read that bundle repository |
| `UW1_ASSETS_AGE_KEY`, `UW2_ASSETS_AGE_KEY` | the age secret key; the owner keeps it in the macOS Keychain, services `uw1-ci-assets-age` and `uw2-ci-assets-age` |
| `UW1_CI_ASSETS` (a variable) | `true` turns on UW1's bundle jobs, and `site.yml` |
| `WEB_DEPLOY_KEY` | the private half of an ed25519 deploy key with write access to the site repository `abedegno/underworld-exhumed-web` and nothing else (`site.yml`) |

The local actions `.github/actions/uw1-assets` and `uw2-assets` clone the bundle with the deploy key, written to `$RUNNER_TEMP` with mode 600 and deleted once the clone is done. Each game's `tools/ci-assets.sh` then decrypts it into `$RUNNER_TEMP`, with the age key on age's standard input, checks every file against `SHA256SUMS`, and prints only a count.

What keeps the data private:

- Nothing decrypted, or built from it, is cached.
- A failed run uploads only the difference pictures and the logs.
- No step lists the decrypted tree.
- The last step of every job deletes it.
- No workflow uses `pull_request_target`.
- A deploy key can read one bundle and nothing else, and a bundle is useless without its age key.

To make a new bundle (new game files, say):

1. Put `game/UWn`, `tc/` and `tasm/` and a `MANIFEST.txt` in an empty directory.
2. In it, run `find game tc tasm MANIFEST.txt -type f | sort | xargs shasum -a 256 > SHA256SUMS`.
3. Then run `COPYFILE_DISABLE=1 tar --no-xattrs -czf - game tc tasm MANIFEST.txt SHA256SUMS | age -r "$(security find-generic-password -s uwn-ci-assets-age -w | age-keygen -y)" -o uwn-ci-assets.tar.gz.age`.
4. Commit that one file to the bundle repository in place of the old one.

A new deploy key: `ssh-keygen -t ed25519 -N '' -f deploy`, add `deploy.pub` read-only to the bundle repository, `gh secret set UWn_ASSETS_DEPLOY_KEY < deploy`, then remove the old key and delete both files.

A new deploy key for the site: the same, with `deploy.pub` added to `abedegno/underworld-exhumed-web` with write access, and `gh secret set WEB_DEPLOY_KEY < deploy`. `site.yml` writes it to `$RUNNER_TEMP` with mode 600 only for a push, uses it through `GIT_SSH_COMMAND` with GitHub's host key alone, and deletes it, with the decrypted bundles and the games' trees, in its last step, which always runs.

## Releases

A tag names the game and the version: `uw2-v1.2.1`, `uw1-v1.0.0-rc2`. `release.yml` builds that game's packages from the sources alone (no game data, no Borland toolchain) and attaches them to a draft release titled with the game. The owner checks the draft and publishes it by hand. UW2's releases before the merge are tagged `v1.2.0` and `v1.2.0-rc1` to `rc9`, and keep those tags.

| Package | Built on | What is in it |
| --- | --- | --- |
| `UWn-V-macos.zip` | macOS 15 | `UW1.app` or `UW2.app`, universal (arm64 and x86_64, macOS 11 on), with SDL3, libmt32emu and Nuked OPL3 inside; signed with the Developer ID and notarised |
| `UWn-V-linux-x86_64.tar.gz` | Ubuntu 22.04, so glibc 2.35 or later | a launcher, the program and its libraries, and a `.desktop` entry and icon |
| `UWn-V-linux-x86_64.AppImage` | Ubuntu 22.04 | the same as one executable file |
| `UWn-V-windows-x86_64.zip` | Windows, MSYS2 CLANG64 | `uw1port.exe` or `uw2port.exe`, with its icon, and every DLL it loads that is not Windows's own |
| `third-party-sources.tar.gz` | Ubuntu | the source of the LGPL libraries at the versions built |

Each package holds `README.txt`, the licence and notice files, and the libraries' licence texts. Nuked OPL3 and libmt32emu are separate shared libraries a player can replace, which is how the LGPL is met (each game's THIRD-PARTY-NOTICES: [uw1](../uw1/THIRD-PARTY-NOTICES), [uw2](../uw2/THIRD-PARTY-NOTICES)). Each game's `CHANGELOG.md` ([uw1](../uw1/CHANGELOG.md), [uw2](../uw2/CHANGELOG.md)) gives a draft its notes: the section for the tag's version, or for a candidate (`uw2-v1.2.1-rc1`) its version's "(unreleased)" section; the draft is titled with the game and the version.

### Signing and notarising the macOS apps

With these secrets set, the macOS job signs each app with the owner's Developer ID and, on a tag, notarises it. Without `CSC_LINK` the app is signed ad hoc and the job still passes.

| Secret | What it is |
| --- | --- |
| `CSC_LINK` | the Developer ID Application certificate and its private key, as a `.p12`, base64 encoded |
| `CSC_KEY_PASSWORD` | the `.p12`'s password |
| `APPLE_API_KEY` | an App Store Connect API key's `.p8` file, its contents |
| `APPLE_API_KEY_ID` | that key's ID |
| `APPLE_API_ISSUER` | its issuer ID |
| `APPLE_TEAM_ID` | the team ID; the signing identity must be that team's |

The job imports the `.p12` into a keychain of its own and signs from the inside out with the hardened runtime and a secure timestamp. `xcrun notarytool submit --wait` notarises the zip, and `xcrun stapler staple` attaches the ticket. A final step deletes the keychain and the keys. Nothing secret is printed.
