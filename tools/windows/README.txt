UNDERWORLD EXHUMED - SHARED-WORLD CO-OP (WINDOWS)

GETTING STARTED
1. Extract the complete ZIP on both PCs. Keep Host.exe, Client.exe and bin together.
   Both players must use this rebuilt package; older presence binaries are incompatible.
2. Run Host.exe, choose the game and click Start host. Create a character or load
   an adventure on the HOST. Give the other player the LAN IP shown in the launcher.
3. Run Client.exe, choose the same game, enter that host IP and click Join game.
   Use the same port on both PCs (default 27860). In UW1 the client creates its own
   character on first joining, then later joins restore the saved companion.
   Both play in the host's adventure.
4. Move and interact with the usual game controls. Both avatars are visible in the
   dungeon, and actions affect the same doors, enemies, items and quest progress.

The host needs the original GOG game files. Leave Game folder blank to find or choose
them, or browse to them. The client receives its view and sound from the host and
sends keyboard/mouse input. No compiler, MSYS2 installation or game data is bundled.
The launchers use Windows' .NET Framework 4.x; the game binaries require 64-bit Windows.

Allow the host game through Windows Firewall on the private network if prompted.
This version is intended for a trusted LAN. It has no internet lobby or relay.
For two instances on one PC, use 127.0.0.1 in Client.exe.

UW1 CAMPAIGN RULES
- Each player has its own character, inventory, equipment, attributes, skills,
  experience, health, mana, runes, spells, hunger and fatigue.
- Quests, enemies, unique items, doors and the world stay shared. Pass equipment
  by dropping it into the world for the other player to pick up.
- The host saves/loads the campaign and both characters. Keep every save slot's
  coop2.dat with its PLAYER.DAT and world files. A campaign has one companion slot.
- Either player can start level travel; the party travels together. Saved companion
  positions are reused when valid. Rejoining the same campaign preserves its inventory.
- Conversations belong to their initiator and can pause the other player. Networking
  continues. Campaign ending scenes are shared. Friendly fire is disabled.
- With a living teammate, a fallen character revives nearby with quarter health plus
  one and loses an eighth of experience. Its inventory stays intact. When both fall,
  the native silver-tree/death and restore rules apply.
- Existing solo/older party saves create a new companion. Importing arbitrary second
  solo characters or transferring characters between campaigns is not implemented.

UW2 still uses a shared party character/inventory. The independent-character work
and campaign checks target Ultima Underworld 1.

This is an experimental campaign playtest build. UW1 checks cover separate movement,
shared doors/audio, character isolation, bags/stacks/cursor items, save/load/restart,
revival, guest travel, all nine level archives and return, and Tybal/orb mana rules.
Campaign scenarios also exercise original Garamon dialogue and burial, Tybal/orb
world changes, guest kill rewards, automatic guest conversations, moving floors,
the flute/key-mantra/emerald puzzles, original combination rules, all eight talisman
sacrifices with a mid-ritual save/load, Slasher banishment and the shared ending.
The scenarios supply prerequisites; they do not cover every talisman acquisition route.
A complete start-to-finish campaign, every quest/puzzle/combat interaction, and UW2's
in-game behaviour still need broader playtesting. Use regular campaign save slots.

FILES AND LOGS
Launcher choices: %APPDATA%\underworld-exhumed-coop\host.ini and client.ini
UW1 host saves:    %APPDATA%\underworld-exhumed-coop\uw1\host
UW2 host saves:    %APPDATA%\underworld-exhumed-coop\uw2\host
Client settings/logs use the corresponding uw1\client or uw2\client folder.
launcher-*.log records startup and connection messages. Existing single-player
save folders and the original game data are untouched.

Advanced: --coop-host PORT --coop-shared starts a shared world; --coop-join IPV4:PORT
--coop-shared joins it. Without --coop-shared the older position/health-only presence
mode is available. Record/replay is not supported for a shared-world session.

Project: https://github.com/smhoward/underworld-exhumed-coop
Licences and notices: bin\uw1 and bin\uw2. Replaceable sound DLLs and their sources
and licence texts are included in the package.
