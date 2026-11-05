# Mods

Some mods for the original games might want to change bytes of the executable (`UW.EXE`,
`UW2.EXE`), such as the level and square a new game starts on or the 3D models. The ports cannot
take such an executable: they run their own compiled code, and they check that the executable
is the GOG release's before reading its data (`src/port/mem/fardata.c`). So a mod manager gives
the ports the same settings another way. Everything here is port-only and off unless set; with
nothing set, the game is exactly as before.

Other files of a mod need nothing new: a file in the port's home folder is read in place of the
game's own (the home folder is laid over the game folder, Exhume's `runtime/port/platform/files.c`),
so a mod's `DATA\LEV.ARK`, `DATA\STRINGS.PAK` or pictures go in `DATA` in a home folder of its own,
started with `--home`. Its saved games stay there too.

## Start level and square

The level and square a new game starts on. The original sets them in the main menu's code:
`GetLevel(1)`, then `player_setup(0x20, 2, 1)` in UW1 and `player_setup(0x13, 0x30, -1)` in UW2
(`src/ui/MAINMENU.C`); the port asks `port_start_level` and `port_start_square` there
(`src/port/sys/mods.c`).

| Setting | Command line (this run only) | Settings file (`uw1port.cfg` / `uw2port.cfg`) | Values |
|---|---|---|---|
| Start level | `--start-level N` | `start-level=N` | 1 to 9 (UW1), 1 to 80 (UW2) |
| Start square | `--start-square X,Y` | `start-square=X,Y` | 1 to 62 each |

`off` (or nothing) is the game's own. Either can be set without the other; a start level alone
keeps the game's own square, which on another level may well be rock (see below).

- **The level must have a map** in `LEV.ARK`: the game's, or a mod's in the home folder's `DATA`
  (see the top of this page). UW2's has maps for 42 of its 80 levels: 1 to 5, 9 to 18, 25, 26,
  33, 34, 41 to 52, 57 to 59, 65 to 69 and 71 to 73. A level without one stops the start with a
  message.
- **The player is put in the middle of the square**, on its floor, as the original does. With a
  start level or square set, a square the player does not fit on (rock, a diagonal wall) gives
  way to the nearest one that it does within four squares: the squares one away first, then two,
  and so on, each ring from its south-west corner row by row, asking the game's `can_place` as a
  teleport's arrival does (`port_start_fit` in `src/ui/MAINMENU.C`). When there is none the
  player stands on the square given, in the rock, as the original's start would leave them. The
  log says where the game started: `mods: a new game starts on level 10, square (30, 29)`.
- **The level's own rules for arriving** run as when the player walks onto it (`do_level_hacks`):
  UW1's level 7 takes the mana while the orb stands, for example. Nothing else of the story is
  set: the quests, conversations and everything a game changes on its way down are as at the
  start, so a mod that starts deeper sets up what it needs in its own `LEV.ARK`.

## What a build supports

`--features` prints one line for each thing this build plays and exits, for a mod manager:

```
features 1 uw1port
start-square
start-level 1-9
data-overlay
enhance skip-intro,wrap-menu,...
```

`start-level` gives the levels a start level may name, `data-overlay` says that files in the
home folder's `DATA` and the other game folders are read in place of the game's, and `enhance`
lists this game's enhancements and fixes, the names `--enhance` takes.

## Recordings

Both settings change play, so a session played with either says so in its log
(`mods: start level 2; start square (33, 5) (not as DOS)`) and its recording keeps them:
`MODS.TXT` beside `RECORD.OUT` (in the black box's session folder, or in the home folder for
`--record`):

```
start-level 2
start-square 33,5
```

`--replay` reads the `MODS.TXT` beside the recording it is given, and never the settings file or
the command line (giving `--start-level` or `--start-square` with `--replay` is refused), so a
recording replays as it was played. A recording made without mods has no `MODS.TXT` and replays
as before; DOS replays it as before too.
