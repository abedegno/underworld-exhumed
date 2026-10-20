# Changelog: Ultima Underworld II port (uw2port)

For players. Each release's notes are drafted from here. Releases are tagged `uw2-vX.Y.Z` (before the move to Underworld Exhumed, `vX.Y.Z`).

## 1.2.3 (14 October 2026)

- **Fixed:** creating a female character crashed the game on choosing any portrait but the first (UW1's issue 8, which UW2 shares). DOS draws her; the port now does too.
- **Fixed:** a map note with no end in a damaged or edited save could crash the game when the map was opened. The port now cuts such a note short, as the UW1 port does for its issue 7.
- **Fixed:** the recording of a session the port crashed in could be cut short on Windows, so that it did not replay to the crash: the crash handler did not see a fault on the game's thread, where the game runs. This was found from UW1's issue 8, whose crash recordings from Windows were cut short; the UW2 port had the same fault and gets the same fix. The handler is now installed on the game's thread too, so the recording is written out whole.
- **Changed:** a session's recording folder now holds only what a replay of it needs from the home folder: the saved games and the `DATA` folder (`UW.CFG`, and any file of yours there that the port reads in place of the game's own, such as a translated `STRINGS.PAK`). Before, it copied everything else there too: a game copied out of GOG's CD image (`gog-cd`), MT-32 ROMs, or a game folder you put there, none of which belongs in a bug report. Links to folders or files elsewhere are not followed.
- **Fixed:** once the game had been running for a few minutes, a cutscene's colour cycling turned on every pass instead of at its own pace. It now keeps DOS's pace however long the game has run.
- **Fixed:** a strings file with a block larger than 32 KB (only modified or translated files have one; the GOG release's do not) gave garbled text. The port now reads it as DOS does.
- **Fixed:** restoring a game while running fast or falling could put you down a little further on than DOS does (UW1's issue 10, which UW2 shares in its code). The original works out the first step after a restore in 16 bits, and the result wraps; the port worked it out in 32 bits. It now does as DOS does.
- **Fixed:** offering a trader far more than their goods are worth could be refused in DOS, with your own appraisal calling it "an excellent deal", where the port accepted it and called it "a terrible deal" (found in UW1, whose code UW2 shares here). The original works out the deal in 16 bits, so a large enough gap wraps round; the port worked it out in 32 bits. It now does as DOS does.
- **Fixed:** a fall at walking pace, paused partway (by opening and closing the restore prompt, say), could stop for a moment in mid-air in DOS, as if landing, where the port carried on down (found in UW1, whose code UW2 shares here). The port now does as DOS does.
- **Fixed:** five more places where the original works in 16 bits and the port worked in 32 now do as DOS does (found in UW1, whose code UW2 shares here): how hard a heavy creature knocks an object it falls on, the distance from a critter to a target 23 tiles or more away, the shading of an object about 23 tiles off, a value that conversations save for an NPC with 128 hit points or more, and the acceleration of a player with strength 30 carrying nearly all they can.
- **New:** optional enhancement restore-at-rest (UW1's issue 10, which UW2 shares in its code; `--enhance restore-at-rest`). A restored game starts at rest where it was saved, instead of keeping the speed and any fall of the game you restored from. A game saved in mid-air still falls, from rest.

## 1.2.2 (10 October 2026)

- **Fixed:** each play session's recording is now kept in its own folder under `recordings/` in the port's home folder, the newest five, as the documentation says. Before, there was one recording a day (in a `RECORDIN` folder), overwritten by each later session that day. An old `RECORDIN` folder there is no longer used and can be large: it is safe to delete.

## 1.2.1 (9 October 2026)

- **New:** optional enhancements, all off by default (`--enhance list`; docs/ENHANCEMENTS.md). The first is `skip-intro`, which starts at the main menu. A session played with one says so in its log and recording.
- **New:** more optional enhancements: wrap-menu, fast-panels, free-heading, subtitles, skill-messages (`--enhance list`).
- **New:** optional enhancements perspective (floors and ceilings stay straight when you look up or down) and full-sprites (`--enhance list`).
- **New:** optional enhancements wide-pitch (look three times as far up and down, with no gaps in the floor or ceiling), mouse-look (the ` key: the mouse turns the view, a crosshair at the centre; `look-speed=` in the settings file) and invert-look (`--enhance list`).
- **New:** optional enhancements modern-keys (UltimaHacks' keys: WASD, Space to attack, Q and E to look and use, Z for the map, R and F for the panels) and rune-keys (Ctrl+Alt and a letter puts a rune on the shelf) (`--enhance list`).
- **New:** the port finds your MT-32 or CM-32L ROMs by itself: in a `roms` folder in its home folder, beside the game or the program, or where DOSBox keeps MT-32 ROMs. Any file names work (they are recognised by their contents), split halves are joined, and `--mt32-roms` takes a folder or one of the files. Or drop the ROM folder onto the app, the program or its window, and the music plays on the MT-32 from the next start; it moves there by itself, too, when ROMs turn up after a first run that had none.
- **New:** a settings screen: F11 (Cmd+, on macOS) opens it over the game, with the sound cards, volume, mouse, window, enhancements and the game folder, and the first run starts with it (docs/SETTINGS.md).
- **Improved:** the program no longer keeps a processor core busy while the game waits for its clock: much lower CPU use, quieter fans and longer battery life, with the game playing exactly as before.
- **Fixed:** the game could freeze for good, most often on some Windows PCs, after the program was paused by the system for a second or more. A creature update loop in the original game never ended after such a pause; the port now caps the step, which changes nothing the DOS game can do.
- **Fixed:** high CPU and GPU use, and a mouse pointer that stalled in other programs, when the graphics driver ignores vsync or the window is in the background. The port now never draws faster than the display refreshes, and draws nothing while minimised or hidden.
- **Changed:** the log says which renderer and refresh rate the port uses and whether vsync held, to help with bug reports.
- **Fixed:** the Linux build could crash in its first conversation, when the game printed an empty line (the original game reads the byte before an empty string; the port now skips that test for one).
- **Checked:** every release's packages are now tested before release on nine systems (Windows Server 2022 and 2025, macOS 14, 15 and 15 on Intel, Ubuntu 22.04 and 24.04, Debian 12, Fedora 41), replaying the recorded sessions from the package itself, with the MT-32 music checked too.

## 1.2.0 (4 October 2026)

- **Fixed:** going up some stairs could hang the game.
- **Fixed:** the floor could turn red after Lord British's cutscene.
- **Fixed:** walking backwards along corridors could get stuck.
- **Fixed:** crash recordings of long sessions were far larger than needed; they are now small.
