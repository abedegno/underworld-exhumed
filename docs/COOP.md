# Co-op development

This fork is adding real-time co-op to Ultima Underworld 1 and 2. The first milestone is
**two-player network presence** in the native ports: host/join, live partner position and
health, a status strip, and a partner cross on the automap. It is experimental and off by
default.

**This is not yet a shared adventure.** Each process still runs its own world. Enemies,
doors, items, combat, quests, conversations, random numbers, clocks and saves remain
independent. There is no partner model in the 3D view, collision with a partner, trading,
friendly fire or shared progression. Starting from matching saves does not prevent the
worlds from diverging.

## Build and try it

Use this fork's native binaries, built from the same branch on both computers. Existing
upstream release binaries do not contain these flags. The normal port requirements in
[BUILDING.md](BUILDING.md) apply; each player needs their own original game data.

```sh
git clone --recursive https://github.com/smhoward/underworld-exhumed-coop.git
cd underworld-exhumed-coop
git switch feature/coop-presence
make GAME=uw1 setup-port
make GAME=uw1 port
```

For UW1, start a host and a guest on the same computer with separate save directories:

```sh
# Terminal 1
uw1/build/port/uw1port --data /path/to/UW1 --home ./host-uw1 --coop-host 27860

# Terminal 2
uw1/build/port/uw1port --data /path/to/UW1 --home ./guest-uw1 --coop-join 127.0.0.1:27860
```

For UW2, build with `GAME=uw2` and use:

```sh
uw2/build/port/uw2port --data /path/to/UW2 --home ./host-uw2 --coop-host 27860
uw2/build/port/uw2port --data /path/to/UW2 --home ./guest-uw2 --coop-join 127.0.0.1:27860
```

On Windows, the executables end in `.exe`. For two computers on a LAN, replace
`127.0.0.1` with the host's numeric IPv4 address. The host listens on all IPv4 interfaces
on the chosen TCP port; allow that port in the host's firewall. There is no matchmaking,
relay, automatic NAT traversal, hostname lookup, IPv6 or authentication in this milestone.
This direct connection is intended for a trusted LAN. Browser builds do not support it.

Choose or load characters in both games. The status strip at the top shows the partner's
level, tile coordinates and health. Open the automap: a bright cross marks the partner
when their level matches the map you are viewing, including while they move with your
map open. The overlay does not reveal unexplored map tiles. UW2's full level ID is sent,
so equal floor numbers in different worlds do not share a marker. The marker disappears
on disconnect, inactivity or an inactive player snapshot. Guest connections retry after
one second; a silent connection times out after ten seconds. Only one guest is accepted.

The transport is pumped by the game's main loop. Start menus, some modal dialogs and the
port's settings screen pause that loop; a long pause can time out the session. It reconnects
when both loops resume. No background thread continues to simulate the world during a menu.
`--record`, `--replay` and `--shot-at-flip` are rejected in presence mode. The ordinary crash
input recorder still records local single-player inputs; it cannot reproduce a network
session or its overlay.

## Implementation

- `coop/protocol.c` serialises a fixed 32-byte frame in network byte order. It transmits
  explicit fields, never C structs, pointers or raw save bytes. Version and game IDs must
  match; reserved fields, map bounds, health, message ordering and state sequences are checked.
- `coop/session.c` provides a nonblocking TCP host/guest transport for Linux, macOS and
  Windows. A HELLO/WELCOME handshake precedes state updates at most every 50 ms. Reads are
  bounded per tick; at most one outgoing frame is pending in application memory. Partial
  reads/writes, connection failures, timeouts and reconnects are handled. TCP is suitable
  for this first low-volume presence milestone; movement prediction/UDP can be evaluated
  with the later shared-world simulation.
- `coop/port.c` captures the local physics/player fields on the game thread. The platform
  scanout receives a mutex-protected copy and draws onto the presentation pixels only.
  It changes no emulated VGA bytes, palette, game object list, level data or saves.
- Each game's `src/port/sys/coop*.c` binds the shared implementation. The only shared-game
  hooks are the main-loop tick and the map-level notification, guarded by
  `#ifndef __TURBOC__`. The DOS preprocessor excludes them. Co-op is a deliberate opt-in
  extension to this fork; preservation of single-player behavior remains the default.
- UW1's small builder adapter and UW2's builder add Winsock when targeting Windows.
  The pinned Exhume submodule is unchanged. Use `make port`, `make port-debug` or
  `make port-release` so the UW1 adapter is used.

## Tests

No game files, SDL library or Borland tools are needed for the networking tests:

```sh
python3 tools/test-coop.py
# With the Exhume submodule checked out, also exercise each game adapter:
make test-coop
# On an ordinary Linux development machine, AddressSanitizer and UBSan:
python3 tools/test-coop.py --adapter --sanitize
```

The tests exercise byte order, malformed frames, wrong games/versions, coordinate/health
validation, fragmentation, sequence rejection, party capacity, disconnects, timeouts,
joining before the host starts, host restarts, reconnects, both games' local-state capture,
unchanged solo scanout and the live map marker's lifecycle. CI runs these on Linux and
macOS; the existing native/web build workflows also watch changes to `coop/`.

Asset-backed DOS matching and recorded-session verification still need the private game
and Borland inputs described in [BUILDING.md](BUILDING.md). Headless adapter tests are not
a substitute for a two-instance in-game playtest.

## Next milestones

1. **Host-owned world and session bootstrap.** Assign persistent player IDs, define a
   shared-world snapshot and restore/join boundary, and choose how saves store two avatars.
   The host alone runs NPC AI, randomness, triggers and authoritative object changes.
2. **Second avatar in the world.** Render a remote actor without letting the legacy NPC AI
   own it; add movement intents, host collision checks, prediction and reconciliation.
   Separate local camera/UI state from per-player character/physics state.
3. **Shared interactions and combat.** Route object-use/pickup/drop/door/attack requests to
   the host, give objects stable IDs, order events, and resolve simultaneous interactions.
4. **Progression and transitions.** Synchronise quests and world clocks; define how
   conversations, sleep, death, saving/loading and world/level changes work for a party.
5. **Long sessions and internet transport.** Verify both complete games with two players,
   then add a lobby/relay, stronger session identity and broader network support.

These steps require changing the engines' single-player assumptions. Sending a second
player's position alone cannot make the world authoritative or keep its state consistent.
