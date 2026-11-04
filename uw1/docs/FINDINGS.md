# What the decompilation found

This page collects what matching UW1 byte for byte revealed about the game: likely bugs in the shipped program, the game rules the code implements, how the engine works, code nothing calls, and the questions still open. The detail lives in the [subsystem notes](subsystems/), [NOTES.md](NOTES.md), [LAYOUT.md](LAYOUT.md), [LINKING.md](LINKING.md) and the comments in `src/`; each entry here links to it.

Everything here is read from the matched sources, which compile to the same bytes as the shipped `UW.EXE`, so a statement about what the code does is a statement about what DOS UW1 does. UW1 has no build with symbols to compare against, as UW2 has in its FM Towns release. Two other witnesses are used where they help: the matched source of UW2 ([UW2Decomp](https://github.com/abedegno/UW2Decomp)), the same engine a year later, and the game data of the GOG release, where it reaches the code in question. Statements about intent or about the effect in the game are inferences, and say so.

Bugs are labelled by how sure we are that the code is wrong (what the code does is always read from the code):

- **confirmed**: a second piece of evidence shows the code is wrong, such as UW2's source doing the same thing differently or the game data reaching the faulty path;
- **likely**: the code leaves little doubt that it was not meant, but nothing outside the code shows it;
- **possible**: it looks wrong, but there is a plausible intent the code fulfils.

"Both games" means UW2's matched source has the same code, so the mistake, if it is one, was carried into UW2. "UW1 only" means UW2's code differs or UW2 has no such code; where UW2 does the same job correctly, that counts as the second witness.

## 1. Likely bugs in the original game

Each entry was re-read against the source before it was written here, and each "both games" was checked against UW2Decomp's `src`. The section ends with the candidates that did not hold up.

### Valuable gems skip the treasure chance and always come singly

- **What happens:** `generate_treasure` gives a critter its loot. It keeps the treasure type's value in a `char` and stretches it (`value * 8 - 68` from 12 up). From 25 up the result does not fit and goes negative: the ruby (value 25) becomes -124, the large blue gem (30) -84 and the sapphire (40) -4. With a negative value the test that makes valuable treasure rare (`(prob << 2) < value`) is false, the dice count `((prob << 2) / value) << 1` is 0 or negative, and `rollem`, given no sides, returns its dice count, 4, so the quantity is 4 / 4 = 1.
- **Where:** `generate_treasure` in [obj/TREASURE.C](../src/obj/TREASURE.C); `rollem` in [sys/UTIL.C](../src/sys/UTIL.C).
- **Evidence:** code reading; both games. The game data reaches the overflow: the shipped `COMOBJ.DAT` gives items 0xA2, 0xA5 and 0xA6 the values 25, 30 and 40, and the type roll can reach them on every level.
- **Confidence:** confirmed.
- **Effect:** when a critter's loot roll picks a ruby, a large blue gem or a sapphire, it gets exactly one, every time, where the rule makes the cheaper red gem (52 after stretching) and small blue gem (92) rarer. Not checked in the game.
- **For a port:** matching DOS means keeping the 8-bit arithmetic.

### The FANLO and NO mantras print the wrong messages

- **What happens:** at a shrine, the mantra FANLO places the key of truth and then prints string 0x1E of block 1, "None of your skills improved.", the message a group mantra prints when it raised nothing. The mantra NO prints string 0x1F, which is empty. The next two strings of the block, 0x20 ("The Key of Truth is needed. Use it well.") and 0x21 ("You hear a chime in the distance."), are printed by nothing in the C.
- **Where:** `mantra_advance` in [game/SKILLS.C](../src/game/SKILLS.C); the strings are in the shipped `STRINGS.PAK`.
- **Evidence:** code reading against the shipped strings; UW1 only (UW2 has no mantras). Both cases are two below a message written for them that nothing else uses.
- **Confidence:** confirmed (the code is read; that 0x20 and 0x21 were meant is an inference from their words and from the shared offset of two).
- **Effect:** the player gets the key with a message saying nothing improved, and NO prints an empty line.
- **For a port:** matching DOS means the shipped strings 0x1E and 0x1F; 0x20 and 0x21 are evidently what was meant.

### A dying critter's cry comes from the wrong critter

- **What happens:** `crit_die` decides whether to play the death cry (effect 6) by `mycst->death`, and plays it at `myxpost`, `myypost`: the creature record and position of the critter the AI processed last, not of the critter that is dying.
- **Where:** `crit_die` in [critter/AI.C](../src/critter/AI.C).
- **Evidence:** code reading; UW1 only. UW2's `crit_die` takes the death kind from the critter last damaged (`victim`) and the position from the dying object.
- **Confidence:** confirmed.
- **Effect:** when the player kills a critter, its cry is decided by, and placed at, whichever critter moved last, so a cry can be missing, or heard from elsewhere. Not checked in the game.
- **For a port:** matching DOS means reading the stale `mycst` and position.

### The player's blocked blows never sound as metal

- **What happens:** `do_miss` plays the sound of a blow that was blocked: effect 7 when weapon kind and armour kind are both 1, else effect 8. UW1 first overwrites the global `fromwho`, the attacker's object index, with the attacker's item id, and then tests `fromwho == 1` for the player. Item 1 is never an attacker, so the player's blows take their weapon kind from `Creature[0x7F & 0x3F]`, the adventurer's own creature record.
- **Where:** `do_miss` in [combat/COMBAT.C](../src/combat/COMBAT.C).
- **Evidence:** code reading; UW1 only. UW2 keeps the item id in a local and tests the object index, giving the player the weapon kind of what he wields. The game data reaches it: in the shipped `OBJECTS.DAT` the adventurer's record has weapon kind 0.
- **Confidence:** confirmed.
- **Effect:** every blocked blow by the player plays effect 8, never 7 (which sound is which is inferred: [combat.md](subsystems/combat.md#open-questions)). `fromwho` also holds an item id after the call until the next blow sets it; whether anything reads it in between was not traced.
- **For a port:** matching DOS means the item id and effect 8.

### The script heap never frees anything

- **What happens:** `bab_malloc` ends each block of the conversation heap with a tag holding the block's data address (the header plus 8). `bab_free` steps back to the header, then compares the tag with the header's address, so the test never passes and nothing is freed. The code behind the test is broken too: it adds sizes to struct pointers, scaling them by 8, and its loop does not advance after a merge. It never runs.
- **Where:** `bab_malloc` and `bab_free` in [conv/BABL.C](../src/conv/BABL.C); also noted in [NOTES.md](NOTES.md#from-wave-3-b-conversation-and-events) and [conversations.md](subsystems/conversations.md).
- **Evidence:** code reading; UW1 only. UW2's `bab_free` checks the tag against the data pointer (`tag_check`) before it steps back, and its merge loop advances.
- **Confidence:** confirmed.
- **Effect:** the substituted texts of every menu and every string a script builds stay allocated until the conversation ends; `load_script` starts the heap afresh for each conversation. The heap is 0xFFFF bytes, so only a very long conversation could run out (callers then stop the game with `pfatal_code(4)`). Not checked in the game.
- **For a port:** freeing changes nothing a player sees unless the heap runs out; a port with a larger heap loses the risk.

### Flameproof fliers leave lava marked in the shared handlers

- **What happens:** for a creature whose `ComObjData` resist has bit 8 (flameproof), `critter_ai` clears 0x20 (a footprint corner on lava) from its collision handler's `mask` and `w6` and sets it in `ignore` for its step, then afterwards sets 0x20 in `mask` and `w6` and clears it from `ignore`. The handlers are shared by all critters of a kind, and `init_ai` gave the fliers' `CT2` neither bit and the swimmers' `CT4` no `w6` bit. So after the first flameproof flier or swimmer has moved, those handlers keep 0x20 for the rest of the session (`init_ai` runs once, at start-up).
- **Where:** `critter_ai` in [critter/AI.C](../src/critter/AI.C); `init_ai` in [critter/PATHFIND.C](../src/critter/PATHFIND.C).
- **Evidence:** code reading; both games (UW2's `init_ai` sets the same masks). The game data reaches it: the shipped `OBJECTS.DAT` makes creature 0x7B a flier and `COMOBJ.DAT` makes it flameproof, and `LEV.ARK` places one on level 7, at (54, 10).
- **Confidence:** confirmed (that the restore was meant to undo the change exactly is an inference, but it can only be wrong for these two handlers).
- **Effect:** for every flier afterwards, `flood_path` counts a lava floor as danger (`w6` is its cost mask) and `crit_hndlr_fly` is called for lava corners. Probably small: fliers avoid paths over lava that they crossed before. Not checked in the game.
- **For a port:** matching DOS means keeping the leak.

### Removing a trap's triggers stops at the first one in each list

- **What happens:** `delete_trap` searches the whole map for the triggers that point at a trap, and `kill_triggers` walks each tile's list. When it finds one it calls `Obj_Rem`, which sets the trigger's next link to 0, frees it, and then takes the next object from that same link. So the walk of that list ends at the first trigger it removes, and any trigger for the same trap later in the list, or in a container after it, is left behind with a link to a freed object.
- **Where:** `kill_triggers` and `delete_trap` in [event/TRIGGER.C](../src/event/TRIGGER.C); `Obj_Rem` in [obj/OBJECTS.C](../src/obj/OBJECTS.C).
- **Evidence:** code reading; UW1 only. UW2's `kill_triggers` reads the next link before it removes the trigger. The shipped `LEV.ARK` has no tile list with two triggers for one trap, so the data does not reach the faulty path.
- **Confidence:** confirmed (the code), with no effect in the shipped levels.
- **Effect:** none as shipped. This also answers the question in [events.md](subsystems/events.md#open-questions): the write after the free is harmless, but the walk does not go on from the freed trigger as that note supposes.
- **For a port:** reading the next link first is the evident fix; it changes nothing with the shipped levels.

### A blocked blow's sound uses the attacker's armour

- **What happens:** in the same `do_miss`, for a critter target the armour kind is read as `Creature[fromwho & ID_INMAJOR].armour_kind`, and `fromwho` (in UW1 by then the attacker's item id) is the attacker, not the target, `hitobj`. For the player as target the function reads the player's own armour, which shows the target's was meant.
- **Where:** `do_miss` in [combat/COMBAT.C](../src/combat/COMBAT.C).
- **Evidence:** code reading; both games (UW2 reads it through `attitem`, the attacker's item).
- **Confidence:** likely.
- **Effect:** a blocked blow between critters sounds by the attacker's armour. Not checked in the game.
- **For a port:** matching DOS means using the attacker.

### set_targz never prefers the object under the centre

- **What happens:** when a mover stands still, `set_targz` picks the highest touchable object below it to rest on, with a test meant to prefer one under the mover's centre (link bit 0x10): `MP.hit == -1 || !(oCollisions[MP.hit].link.f.low & 0x10) || !(!oCollisions[i].link.f.low & 0x10)`. In the last term `!` applies before `&`: `!x` is 0 or 1, `& 0x10` makes it 0, and the outer `!` makes the term always true. So the whole test always passes.
- **Where:** `set_targz` in [motion/MOTION.C](../src/motion/MOTION.C).
- **Evidence:** code reading; the source compiles to the shipped bytes as written. Both games.
- **Confidence:** likely (operator precedence; the parentheses show the intent).
- **Effect:** with several objects underfoot, the highest touchable one wins even when the current choice is under the centre and it is not. Probably rarely visible; not checked in the game.
- **For a port:** keep the expression to match DOS.

### Conversation quests 15 to 31 do not work

- **What happens:** the conversation built-ins `set_quest` and `get_quest` make a quest's bit as `1 << quest` in a 16-bit int and then widen it to the 32-bit quest word. For quests 16 to 31 the shift leaves 0, so they can be neither set nor read. Quest 15 gives 0x8000, which widens with its sign to 0xFFFF8000, so setting it sets bits 15 to 31, clearing it clears them, and reading it reports any of them.
- **Where:** `set_quest` and `get_quest` in [conv/BABLHACK.C](../src/conv/BABLHACK.C).
- **Evidence:** code reading; UW1 only (UW2 keeps its quests in an array of bytes). The trap code builds the same bits with `1L << d`, correctly ([event/TRIGGER.C](../src/event/TRIGGER.C)). Every call of the two built-ins in the shipped `CNV.ARK` passes a constant: quests 0 to 11 and 32 (a quest byte), nothing from 12 to 31.
- **Confidence:** likely.
- **Effect:** none with the shipped conversations.
- **For a port:** matching DOS means the 16-bit shift; it matters only for new scripts.

### find_barter_total cannot search by class

- **What happens:** the built-in tests `wanted < 1000 ? ids[i] == wanted : (ids[i] >> 4) == wanted - 1000`, a class search for item numbers from 1000, but the whole loop is inside `if (wanted < 1000)`, so a class search finds nothing and returns 0.
- **Where:** `find_barter_total` in [conv/CONVERSE.C](../src/conv/CONVERSE.C).
- **Evidence:** code reading; both games. The shipped `CNV.ARK` calls it twice, for items 0x129 and 0xB6, never for a class (the related `find_barter` is asked for class 1011 once, and its class search works).
- **Confidence:** likely.
- **Effect:** none with the shipped conversations.
- **For a port:** a consideration only.

### Item toughness on an item in a high slot goes to one location twice

- **What happens:** for an item enchantment of class 12 with the 8 bit set (toughness) on an item in a slot above 4, `player_affected_by` loops over hit locations 0 and 1 but adds the toughness to `armour[slots[0]]` each time, so location 0 gets it twice and location 1 not at all. The to-hit protection in the same loop uses `slots[i]`.
- **Where:** `player_affected_by` in [game/PLAYDATA.C](../src/game/PLAYDATA.C).
- **Evidence:** code reading; both games.
- **Confidence:** likely (the neighbouring line indexes by `i`).
- **Effect:** such an item protects one hit location twice over and leaves the other bare. Which items carry such enchantments was not checked.
- **For a port:** matching DOS means the doubled location.

### The dragons' idle tail swish asks for a flask redraw instead

- **What happens:** every 64 ticks `update_screen` may start a flask bubbling and a dragon's tail swishing. For the bubbles it sets the flask's bit, `1 << (r & 1)`. For the tail it sets `swishing[r & 1]` and then `slow_adjust |= 1 << (r + 4 & 1)`. In C `+` binds tighter than `&`, so the shift is `(r + 4) & 1`, again 0 or 1: the bit of a flask (elements 0 and 1), not of a dragon (4 and 5).
- **Where:** `update_screen` and `adjust_dragons` in [ui/PANELS.C](../src/ui/PANELS.C).
- **Evidence:** the bytes (`add cl,4; and cl,1; shl ax,cl`). The bubble line just above uses the element's own bit, and `adjust_dragons`, the only routine that draws a swish, keeps its bit set while a swish is pending, so `(r & 1) + 4` is evidently what was meant. UW1 only (UW2 has no dragons).
- **Confidence:** likely.
- **Effect:** the flask's adjust runs and finds nothing to do. The swish stays pending until something else sets the dragon's bit, which `set_screen_frame(SCR_DRAGON, n)` does when a dragon animation starts; the tail then swishes with it. So the dragons probably never swish their tails on their own. Not checked in the game.
- **For a port:** matching DOS means keeping the wrong bit; idle tail swishing is the evident intent.

### A level gained in Tybal's lair after the orb is gone does not raise the mana

- **What happens:** while Tybal's orb stands, arriving on level 7 keeps the maximum mana aside in `saved_mana` and sets the mana to 0, and leaving gives it back. `player_compute` writes a new maximum into `saved_mana` whenever the player is on level 7, without asking whether the orb still stands. Once it is destroyed nothing reads `saved_mana` there any more, so a level or a mana skill gained on level 7 after that leaves the maximum mana as it was until the next `player_compute` on another level.
- **Where:** `player_compute` in [game/SKILLS.C](../src/game/SKILLS.C); `do_level_hacks` in [game/GAMEWRAP.C](../src/game/GAMEWRAP.C); the orb in [obj/USEITEMS.C](../src/obj/USEITEMS.C).
- **Evidence:** code reading; UW1 only.
- **Confidence:** likely.
- **Effect:** the maximum mana lags behind for the rest of the stay on level 7. Not checked in the game.
- **For a port:** a consideration only.

### A critter placed in a north-west diagonal tile goes to the wrong corner

- **What happens:** `set_gridx_and_y_based_on_tile_type` picks the fine position where a moved critter stands in a tile: the middle of an open tile, the open corner of a diagonal. `TILE_DIAG_NW` gets (6, 1), the same as `TILE_DIAG_SE`, where its open corner is (1, 6).
- **Where:** [critter/CRITTIME.C](../src/critter/CRITTIME.C); used by critters going home while the player sleeps and by `wander_that_monster`.
- **Evidence:** code reading; both games (UW2's findings list it).
- **Confidence:** likely.
- **Effect:** the critter is aimed at the closed half of the tile. `can_place` probably refuses it there, so the move fails; not checked in the game.
- **For a port:** (1, 6) is the consistent value; DOS uses (6, 1).

### get_dist adds the y term's sign correction to the wrong register

- **What happens:** `get_dist` takes the absolute values of three 32-bit coordinates by `cwd; xor low,dx; xor high,dx; neg dx; add low,dx; adc high,0`. For the y term the `xor` goes to SI but the `add` to CX, which holds the term already chosen, so a negative y comes out one too small and CX one too large.
- **Where:** `get_dist` in [3d/SPHERE.ASM](../src/3d/SPHERE.ASM); also in [NOTES.md](NOTES.md#seg004-the-3d-renderer).
- **Evidence:** code reading. The code is in both games, but only UW1 calls it: UW1's object headers `seg004_15AC` (opcode 1Eh) and `seg004_17AB` do, and `render_3d` calls `seg004_17AB` for each record of a list before the database runs ([3d/INSTANCE.ASM](../src/3d/INSTANCE.ASM)). Whether that list is ever filled in play was not checked.
- **Confidence:** likely.
- **Effect:** a distance estimate off by a unit or two for objects on one side of the eye; what uses the estimate was not traced, and no visible effect is known.
- **For a port:** matching DOS means keeping the off-by-one.

### do_compact_map stores the address of the mapper word as the mapper

- **What happens:** model opcode 0D2h stores the constant 0B00Ah, the address of the g-map context's routine word, as the texture mapper's offset (`mov word ptr ds:[0B006h],0B00Ah`). The other handlers that use the context load the word itself (`mov bp,word ptr ds:[0B00Ah]`), as UW2's `do_compact_map` does.
- **Where:** `do_compact_map` in [3d/TMAPOPS.ASM](../src/3d/TMAPOPS.ASM).
- **Evidence:** code reading; UW1 only (UW2's handler differs).
- **Confidence:** likely (as a slip).
- **Effect:** a program using opcode 0D2h would call seg003 at offset 0B00Ah, inside the code. The C never emits 0D2h; whether any 3D model's bytecode does was not checked.
- **For a port:** nothing to do unless a model uses it.

### A conversation's minute is shorter than the game's

- **What happens:** `setup_converse_data` gives scripts `game_time` and `game_mins` as `game_clock / 0x3BC4`, and `game_days` as `game_clock / 0x1502E80` (0x3BC4 * 1440). Sleep moves the clock by 0xE1000 an hour, 0x3C00 a minute.
- **Where:** `setup_converse_data` in [conv/CONVVARS.C](../src/conv/CONVVARS.C); `CONV_MINUTE` and `CONV_DAY` in [include/conv.h](../src/include/conv.h); the hour in [game/SKILLS.C](../src/game/SKILLS.C).
- **Evidence:** code reading; both games.
- **Confidence:** possible (0x3BC4 may be deliberate, and sleep is the only other place that counts in hours).
- **Effect:** a conversation's day is 86,400 ticks (about 5.6 game minutes) shorter than a sleeper's, so after many days a script's day count runs ahead.
- **For a port:** a consideration only.

### An object casting a class 13 or 14 spell is tested at a home it does not have

- **What happens:** `do_spell` tests an object caster (at or above `objdata`, a static object) for anti-magic at `inanmMapX`, `inanmMapY` only for spell classes up to 11. For classes 13 (special effects) and 14 (cutscenes) it falls to the critter test, `anti_magic_p(OBJ_HOMEX(who), OBJ_HOMEY(who))`. The home word is at offset 0x16 of a mobile record; a static object's record is 8 bytes, so this reads the following records.
- **Where:** `do_spell` in [combat/SPELLS.C](../src/combat/SPELLS.C); the object casters come through `inanimate_spell` in [event/WORLDEV.C](../src/event/WORLDEV.C).
- **Evidence:** code reading; both games (UW2 has the same test, without UW1's level 9 refusal). Whether any trap or object in the shipped data casts a class 13 or 14 spell was not checked.
- **Confidence:** possible.
- **Effect:** such a spell may be refused, or allowed, by the anti-magic bit of an unrelated tile; on level 9 it is always refused.
- **For a port:** a consideration only.

### EMS 4.0 is used without being asked for

- **What happens:** `seg012_B` checks for the EMM driver, its status and the page counts, but not its version. UW1 still names its handle with function 5301h and maps four pages at once with function 5000h (`seg012_BB`), both EMS 4.0 calls; `get_workspace` maps the whole page frame that way.
- **Where:** `seg012_B` and `seg012_BB` in [sys/EMS.C](../src/sys/EMS.C); `get_workspace` in [sys/TMPALLOC.C](../src/sys/TMPALLOC.C).
- **Evidence:** code reading; UW1 only. UW2's equivalent asks for the version (function 46h) and requires 4.0.
- **Confidence:** possible (EMS 3.2 drivers were rare by 1992, and UW2's check may be a clearer message rather than a fix).
- **Effect:** with an EMS 3.2 driver the game starts, and the first `get_workspace` stops it with error C003 (`ERR_EMS | 3`). Not tried.
- **For a port:** nothing to do.

### Floor heights 14 and 15 convert to z 0

- **What happens:** `hgt_val`, which turns a tile's 4-bit floor height into z, has 0 for heights 14 and 15 (between 0x340 for 13 and 0x400 for 16).
- **Where:** [map/MAP.C](../src/map/MAP.C); read by the face visibility test of [3d/GRIDDB.C](../src/3d/GRIDDB.C) and by `player_setup` in [motion/PHYSICS.C](../src/motion/PHYSICS.C).
- **Evidence:** code reading; both games (UW2's findings list it). None of UW1's nine shipped levels has an open tile at height 14 or 15, nor a slope at height 13 that would read entry 14.
- **Confidence:** possible.
- **Effect:** none with the shipped levels.
- **For a port:** 0x380 and 0x3C0 are the evident values.

### A full-screen picture that cannot be read counts as shown

- **What happens:** `LoadBitMap_ovr141_0` returns 0 only when it had no buffer. When `bltfromdrive` cannot read the file it skips the drawing and still returns 1. The one caller that tests the result is the start of play: `if (!LoadBitMap_ovr141_0(-1, "DATA\\main.byt")) pfatal_code(ERR_READ | 0xB);`.
- **Where:** [gfx/SHOWPIC.C](../src/gfx/SHOWPIC.C); the caller in [game/UWEDIT.C](../src/game/UWEDIT.C).
- **Evidence:** code reading; UW1 only (UW2 loads the screen from an archive by another routine).
- **Confidence:** possible (the result may have been meant as "had memory").
- **Effect:** with `DATA\MAIN.BYT` missing or short, the game does not stop with error B00B but starts with whatever the buffer held as the screen frame. Not tried.
- **For a port:** matching DOS means ignoring a failed read here.

### Camera type 2 writes its view one word off what type 4 reads

- **What happens:** camera type 2 (`seg004_E02`) builds a view description at seg051:00C8h, type 4, as the eye, the word 1, the view matrix and the zoom 7ED2h, and makes it current. Camera type 4 (`seg004_CF4`) reads a description as the eye, the zoom, a flag and the matrix, so it would take the zoom from the 1 and the matrix one word late.
- **Where:** [3d/CAMERA.ASM](../src/3d/CAMERA.ASM); first noted in [NOTES.md](NOTES.md#seg004-the-3d-renderer).
- **Evidence:** code reading; UW1 only (UW2 has only `head_view`).
- **Confidence:** possible, and unreachable: the current view description (seg051:104h) is 0 in the EXE, which selects `head_view` (type 0), and only seg004's own far entries, which nothing in the EXE calls, would change it (see [Dead code](#dead-code)).
- **Effect:** none.
- **For a port:** nothing to do.

### A broken object's debris keeps its spell

- **What happens:** `remove_object` turns a destroyed object into a pile of debris by changing only its item number (`SET_ITEM`); the rest of the object stays, its enchantment included. An object that holds its spell itself, an is_quant object with the enchanted bit (an enchanted scroll or potion), so leaves debris that holds the same spell: `decode_obj_spell` finds it there as it did on the object, and `UseObj` ends every use with `checkSpell`, so using the debris casts it. Nothing spends it. `UseBook` and `UseFood` would have used the scroll or potion up, but debris is neither, and `useNSpellCharges` spends only a spell object's charges. So the debris casts the spell each time it is used, as often as the delay between casts from objects (0x2FD ticks) allows. An object that keeps its spell as a spell object among its contents loses it, since `remove_object` frees those contents (`Obj_FreeChain`).
- **Where:** `remove_object` in [combat/DAMAGE.C](../src/combat/DAMAGE.C); `UseObj`, `checkSpell`, `decode_obj_spell`, `UseBook` and `UseFood` in [obj/USEITEMS.C](../src/obj/USEITEMS.C).
- **Evidence:** code reading; both games. The game data reaches it: the shipped levels hold potions that carry their own spell from level 1 down and scrolls from level 2 down (is_quant, enchanted, bit 11 set, so `checkSpell` casts them). Seen in the port, which plays as DOS: the `plain-debris` enhanced session's recording, replayed without the enhancement, casts Light when the debris of a thrown scroll of Light is used.
- **Confidence:** confirmed.
- **Effect:** a player who breaks an enchanted scroll or potion, by throwing it at a wall until it breaks, say, gets debris that casts its spell for nothing, again and again, where the scroll or potion would have given it once.
- **For a port:** matching DOS keeps it; the optional enhancement `plain-debris` takes the spell off the debris ([ENHANCEMENTS.md](../../docs/ENHANCEMENTS.md)).

### Smaller slips with no known effect

- **UW.CFG's card numbers are stored as words into bytes.** `seg014_1DC5_1D0D` reads each card number with `%d` straight into a byte global, so `sscanf` writes two bytes. The music card's high byte lands on `speech_card`, which the next line then reads; the speech card's high byte lands on the low byte of `music_driver` (the next in `_DATA`, by the matched data), turning its initial -1 into 0xFF00. `init_sounds` sets `music_driver` whenever there is a music card, and with none, music and effects are off before it is used. UW1 only: UW2 reads into `int` locals ([sound/SOUND.C](../src/sound/SOUND.C)).
- **`mem_set` leaves the last byte of an odd count.** MODEX.ASM's far `mem_set` stores `count / 2` words and nothing more; UW2's stores the odd byte. Its only callers (BABL.C, 0x1000 bytes; PATHFIND.C, 0x5000 bytes) pass even counts. UW1 only ([gfx/MODEX.ASM](../src/gfx/MODEX.ASM)).
- **The rune bag would take a 25th rune.** `add_rune` refuses `rune > NUM_RUNES` rather than `>=`, so item 0x100 (a key) would set bit 7 of `runebag[3]`, past the bag's three bytes; but the rune bag's rule in `ItemFitsSlot` lets only runestones (0xE8 to 0xFF) through. Both games ([combat/RUNES.C](../src/combat/RUNES.C), [inv/INVPANEL.C](../src/inv/INVPANEL.C)).
- **`player_affected_by` reads past `slots`.** Its loop tests `slots[i] != -1` before `i < 2`, so with both slots in use it reads `slots[2]`, a stack word, before stopping. The value does not change the outcome. Both games ([game/PLAYDATA.C](../src/game/PLAYDATA.C)).
- **New objects used unchecked.** `generate_treasure`, `generate_food`, `generate_weapons` and `generate_equipment` write to the object `CreateObj` returns without testing it (both games, [obj/TREASURE.C](../src/obj/TREASURE.C)); so do `player_is_dead` with the bones and `plant_seed` with the tree ([game/SKILLS.C](../src/game/SKILLS.C)). `CreateObj` returns 0 only when a free list is empty after `Obj_Alloc` has culled, so each would need an object store that culling cannot relieve; the write would then go through a null far pointer into the interrupt vector table.
- **The path search box reaches one past the map.** `flood_path` clamps the low ends of its search box to 1 but the high ends to `MAP_SIZE` (64), so a search from square 63 could step to 64, where `Map_GetAddr` returns a null pointer. No shipped level has an open tile on its edge. Both games ([critter/PATHFIND.C](../src/critter/PATHFIND.C)).
- **`swap_ws_out` tests the wrong thing for failure.** It ignores `ovr113_2A2`'s -1 and tests the pages for 0xFF, which `ovr113_2A2` never stores. `mem_setup` gives at least four critter page pairs, so it cannot fail. UW1 only ([critter/CRPAGES.C](../src/critter/CRPAGES.C)).
- **A texture block of the wrong size is used anyway.** `Txm_Load` notes a block that is not 0x7A bytes ("bad tmap ids size" through the empty `dprintf`) but still copies its 0x80-byte buffer; a block longer than that would overrun it on the stack. The shipped blocks are all 0x7A bytes, and `GAMEWRAP.C` ignores the result. UW1 only ([map/TEXTMAPS.C](../src/map/TEXTMAPS.C)).
- **Archive blocks.** `get_arc` and `put_arc` refuse a block only when `arc->count < blk`, so `blk == count` reads the offset table one past its end (both games: UW2 tests `blk > count`). `put_arc` also reads `offtab[blk]` before the test, returns 1 after rewriting the archive through `_arc.tmp` without looking at the copies, the rename or the reopen (UW2 checks the table write at least), and builds a name ending in `_` that it never uses. The callers' block numbers are in range ([sys/ARC.C](../src/sys/ARC.C)).
- **A failed page frame query leaves the EMS pages allocated.** If function 41h fails after 43h allocated the pages, `seg012_B` returns 0 with the handle set; `init_mem` stops the game, and `free_mem` frees the handle only for a page count above 0, so the pages stay allocated after the game exits. UW2's `free_mem` frees whenever there is a handle ([sys/EMS.C](../src/sys/EMS.C), [sys/TMPALLOC.C](../src/sys/TMPALLOC.C)).
- **The object list checker forgets a duplicate.** `count_list`, a debugging aid, sets `bad` when it meets an index twice, but then assigns the result of checking a container's contents to the same flag, so a duplicate earlier in the list is forgotten when the contents after it are clean. It affects only the "Problems in object list" report. Both games ([obj/OBJECTS.C](../src/obj/OBJECTS.C)).
- **`scroll_print3` reads the byte before an empty string.** It tests `text[strlen(text) - 1]` for a newline ([ui/SCROLLIO.C](../src/ui/SCROLLIO.C)), as UW2's does. The only empty string it is given is `scroll_wrap`'s `"\n"`, which the first call empties, and the byte before that literal is `[MORE]`'s terminating 0, so nothing happens in DOS. The port keeps that 0 with `PERSISTENT_STR`, so, unlike UW2's (where a Linux build crashed), it needs no guard; no caller passes any other empty string.
- **A long step of the bin clock leaves a distant critter due for ever.** `move_mobile` ([critter/AI.C](../src/critter/AI.C#L1550)) has UW2's loop, described in [UW2's FINDINGS](../../uw2/docs/FINDINGS.md#a-long-step-of-the-bin-clock-leaves-a-distant-critter-due-for-ever): unreachable in DOS, but a port paused for a second can freeze for good. The port caps the step at 8, as UW2's does.
- **Slips in code nothing calls** (see [Dead code](#dead-code)): GRDB.C's `gr_getlab` tests an unsigned stack pointer for being negative and `gr_freelab` bounds its push at 0x5F where the stack has 32 entries (both games, [3d/GRDB.C](../src/3d/GRDB.C)); GRSPIC.C's `seg009_2C6` passes the addresses of its own parameters in its video memory branch, so its result would be lost ([gfx/GRSPIC.C](../src/gfx/GRSPIC.C)).

### Finding room near a spot: the clearing pass stops early, and the visited table is half cleared

- **What happens:** `find_good_x_and_y` searches outwards from a square, breadth first, for one where an object fits (a teleport's arrival, a critter or object put down). Two slips:
  - With `clear` set it deletes the objects in its way. After `Obj_Punt` removes one, the loop steps on through that object's own link (`link = &o->qn.link`), which the removal has just cleared, so the walk ends at the first object it deletes: at most one object per square is cleared on each visit.
  - `visited` is `uint16 visited[9]`, 18 bytes, but `memset(visited, 0, 9)` clears only the first 9, so the bits for the box's later columns start as whatever the stack held, and a square there may count as tried already. The box is 10 columns wide (`xmax = xmin + 9`), so a square in its last column sets `visited[9]`, one entry past the array, in the stack variable after it.
- **Where:** `find_good_x_and_y` in [event/WORLDEV.C](../src/event/WORLDEV.C).
- **Evidence:** code reading; both games (UW2Decomp `event/WORLDEV.C` has the same code). Not checked in the game.
- **Confidence:** likely.
- **Effect:** a search can give up, or pick a farther square, where a free one was nearer; and clearing a crowded square takes several visits. Which squares are skipped depends on the stack's leftover contents.
- **For a port:** `visited[9]` is a write past the array on the host's stack, not into DOS's neighbouring variable, so a port must give the array room for it (or model DOS's stack layout) to stay memory-safe; matching DOS's skipped squares also means modelling the uncleared bytes.

### Candidates that did not hold up

- **A dropped lit taper stays lit** (from [combat-findings.md](subsystems/combat-findings.md)): `ReturnObject` puts out a dropped light only for minor classes 4 to 6, and the lit taper is 7. But `mob_to_static` ([motion/OBJPHYS.C](../src/motion/OBJPHYS.C)), the other way a light comes to rest, stops at 6 too, in both games, and the lit taper (0x97) is the Taper of Sacrifice. Two places agreeing look deliberate; not a bug as far as the code shows.
- **More than 191 objects in a pick frame reuse pick colours** (from [3d-findings.md](subsystems/3d-findings.md)): `do_obj` wraps `PickUp` back to 1 at `PICK_WALL` on purpose, as UW2 does at its own limit. A limit, not a slip.
- **The music buffer is not checked against the file's length** (from [sound-findings.md](subsystems/sound-findings.md)): `midi_buf` is 6300 bytes and the largest theme shipped is 6248 (`UW02.XMI`). A buffer sized for the data; it limits a modder, not the game.
- **A door bashed for `rand() % 0` damage** (from [critter-findings.md](subsystems/critter-findings.md)): `try_to_open_door` divides by the creature's first attack damage. In the shipped `OBJECTS.DAT` that is 0 for the mongbat (0x51), which flies and so never reports a door, and for creatures 0x4F, 0x7D and 0x7E, which the shipped `LEV.ARK` places only on level 9, which has no doors.
- **`generate_weapons` gives ammunition a count without setting `ID_ISQUANT`** (from [objects-findings.md](subsystems/objects-findings.md)): `CreateObj` already sets it for stackable items.
- **`kill_triggers` works because a freed record keeps its links** (from [events-findings.md](subsystems/events-findings.md), an engine finding there): it does not go on from the freed trigger, because `Obj_Rem` has cleared the link; see the entry above.
- **The chance test for valuable treasure** and **the NO mantra printing nothing** were listed as possible; re-reading them against the data made them part of the two confirmed entries above.

## 2. Game rules recovered from the code

Each rule links to the note or source that has the detail and the function names.

### Skill checks, experience and the character

| Rule | Value | Detail |
|---|---|---|
| Skill check | `value - target + rand() % 31`: over 28 a critical (2), over 15 a success (1), over 2 a failure (0), else a bad failure (-1) | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Experience gain | ignored once experience passes 0x17700; halved plus one when the character's level is above twice the dungeon level plus two | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Skill points | one per 3000 of total experience, and one per level gained | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Derived values | hit points 30 + level * strength / 5; mana (mana skill + 1) * intelligence / 8; carrying capacity strength * 20 tenths of a stone | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Exploration | newly seen tiles are counted, and `pipeexp * PlayerLevel / 10` experience given while the character's level is 1 to 15 and the map level is not 9 | [3d.md](subsystems/3d.md#rules-found-in-the-code) |
| Death | an eighth of the experience; while talismans remain the player comes back at a planted silver tree, otherwise the game ends | [game.md](subsystems/game.md#rules-found-in-the-code) |

### Mantras

- A skill's own mantra spends a point on two raises of that skill. INSAHN gives the cup of wonder's whereabouts, FANLO the key of truth (once), and SUMM RA, MU AHM and OM CAH spend a point on raises of up to three combat skills, two magic skills (mana favoured below 8) or four of the others. ([game.md](subsystems/game.md#rules-found-in-the-code))

### Casting

| Rule | Value |
|---|---|
| Circle | rune spell index / 6 + 1; 48 rune spells, and five more reached only from objects (`cast`) |
| Requirements | (character level + 1) / 2 at least the circle; mana at least 3 * circle |
| Roll | `skill_check(Casting + 5, 2 * circle)`: 0 fails, -1 backfires at strength circle / 2 |
| Cooldown | (2 * circle - level) * 4 + 0x40 ticks; a spell cast by using an object once per 0x2FD ticks |
| Cost | paid at once, except missile spells, which pay when released |
| Where | an anti-magic tile stops every spell; on level 9 nothing a critter or the player casts works |

Smite Undead hits only a critter with resist bit 0x80; Summon Monster picks a creature number from lvl to 2 * lvl - 1 (lvl the Casting skill, or a critter caster's dungeon level * 4, at least 2) and rerolls swimmers, creatures with no hit points or with `Creature` bit `bA_1`, and items 0x7B and 0x7C. ([combat.md](subsystems/combat.md#casting), [combat/RUNES.C](../src/combat/RUNES.C))

### Melee and damage

- To hit: `skill_check(attack skill + hit angle, defence)`, the angle 0 face to face up to 4 from behind. Damage: `(d / 6)d6 + 1d(d % 6)`, scaled by the charged power / 128, less the armour at the hit location, halved against the player on easy. A weapon enchantment adds (effect & 7) + 1 to hit (effects 0 to 7, named "Accuracy") or to damage (8 to 15, "Damage"): in UW1 the names are right, though players often report them as swapped (in UW2 they are; its FINDINGS.md). ([combat.md](subsystems/combat.md#melee))
- Armour enchantments (effects 0 to 7 "Protection", 8 to 15 "Toughness", each (e & 7) + 1): Protection is subtracted from an attacker's attack skill when the player is hit at that location, so blows miss more often; Toughness is added to the player's armour at that location, so blows that land do less damage. Neither changes how fast the item wears: the item-damage path (`DamageInventory`, `damage_item`) never reads an enchantment.
- Damage types are a bit set (magic 0x03, physical 4, fire 8, poison 0x10, cold 0x20, missiles 0x40, undead 0x80 as a resist bit only); UW1 has no fire against cold doubling. Objects shift damage right by their quality class. ([combat.md](subsystems/combat.md#damage))
- A kill gives 4 * exp + 2d(exp) from the creature record, 1.5 to 3 times that for a powerful critter. ([combat.md](subsystems/combat.md#melee))

### Critters

- For 0x200 game clock units after the player hits a critter that is not a loner, every critter of the same race within hearing of the spot turns hostile. ([critter/AI.C](../src/critter/AI.C))
- Taking an owned object angers its owners who see it; race 0x0D stops caring once quest 32, the Knight of the Crux, reaches 3. ([critter/CRITTIME.C](../src/critter/CRITTIME.C))
- Critters that are not hostile never open a door they bump into. ([critter/PATHFIND.C](../src/critter/PATHFIND.C))
- Tybal's guards (race 0x13) on level 7 close in and never cast while his orb is whole. ([critter.md](subsystems/critter.md))

### Traps and world events

- A create-object trap makes its copy with chance (63 - quality) in 63, not when another created object is within four squares; the wandering monsters are such traps whose template is a mobile, run while the player sleeps and on one regeneration tick in four. ([event/TRIGGER.C](../src/event/TRIGGER.C), [events.md](subsystems/events.md#triggers-and-traps))
- The emerald puzzle: an emerald at each corner of the 9 by 9 square around the trap makes a Vas runestone beside it and destroys the four emeralds. ([event/WORLDEV.C](../src/event/WORLDEV.C))
- While the player sleeps, open doors close by themselves with chance 3 in 10 each. ([events.md](subsystems/events.md))
- A trap is deleted with every trigger that points at it; its flags count them (but see the `kill_triggers` entry above). ([event/TRIGGER.C](../src/event/TRIGGER.C))

### Places

- Tybal's lair (level 7): the player has no mana while the orb stands; the maximum is kept aside and a quarter of it given back on leaving. Level 9, the Ethereal Void: no automap, no sleep, nothing dropped is kept, no silver tree, and the frame is drawn unlit. ([game.md](subsystems/game.md#rules-found-in-the-code), [map.md](subsystems/map.md#special-levels), [3d.md](subsystems/3d.md#rules-found-in-the-code))
- A talisman thrown into the lava within 6 tiles of the centre of level 8 is destroyed while `talisman_ok` is set; when the last one goes the moongate opens. Without `talisman_ok`, bit 3 of the dreams word is set instead, which brings dream 3. ([motion/OBJPHYS.C](../src/motion/OBJPHYS.C), [game/SKILLS.C](../src/game/SKILLS.C))
- The silver tree takes root only on an open square whose floor texture is in one of four ranges. ([game.md](subsystems/game.md#rules-found-in-the-code))

### Movement

- Fall damage is `PN.impact >> 8`, doubled while falling, cut by an Acrobat check, dealt only above 3. A jump starts at vertical speed 0x263. Objects that land in water are destroyed; in lava they survive only a fire resistance check. ([motion.md](subsystems/motion.md#the-player-physicsc-and-playmovec), [motion/PHYSICS.C](../src/motion/PHYSICS.C))

### Inventory, objects and time

- Stacks merge while their total stays below 999; the merged quality is the average. A light burns only in the shoulder and hand slots. The carried weight is in tenths of a stone. ([inventory.md](subsystems/inventory.md), [inv/INVPANEL.C](../src/inv/INVPANEL.C))
- When a free list runs out, `Obj_Alloc` culls objects at random by their fate from tiles more than 7 squares from the player; objects with id bit 13 are never culled. ([objects.md](subsystems/objects.md#the-object-store))
- Sleep lasts 7 to 10 hours, with a chance of a dream; Garamon's dreams come in order, then at random, until he is buried. ([game.md](subsystems/game.md#rules-found-in-the-code))

## 3. Engine findings

- **The 3D renderer.** seg004 is 9 modules where UW2 has 14, and every one assembles as 186 code (`.186`) where UW2's need `.386`. UW1 keeps its own 16-bit textured-polygon clip and projection, and calls seg003's wall mappers through a far pointer: WALLMAP.ASM's for floor-style polygons (UW1 only) and POLYFILL.ASM's for wall-style ones, so both are in use. ([3d.md](subsystems/3d.md), [gfx.md](subsystems/gfx.md), [NOTES.md](NOTES.md#seg004-the-3d-renderer))
- **One camera.** `create_matrix` dispatches on the current view description's type through six camera routines and can blend from one view to the next (UW1 only), but nothing outside seg004 changes the description from type 0, so the game always uses `head_view`, the player's eyes. The other five types, the blend and the far entries look left over from a camera system the shipped game does not use. ([3d/CAMERA.ASM](../src/3d/CAMERA.ASM))
- **Self-modifying code.** `render_3d` patches the screen centre into immediates in SMOOTH.ASM, INTERP.ASM and NOCLIP.ASM; `self_modify` patches the eye and shift into the point loaders; `check_flat` copies one of two `mxmul` bodies over `mxmul`. A port has to do these with variables. ([3d/INSTANCE.ASM](../src/3d/INSTANCE.ASM))
- **The pick test reads a byte as a word.** `pick_3d` compares the colour under the pointer with `*(int16 *)&PickUp`, a byte and the byte after it. That byte (DS:311F) is `_BSS` padding that stays 0, so the test works as meant; a port that moves `PickUp` must keep it a byte test. ([ui/INTERACT.C](../src/ui/INTERACT.C))
- **A breakpoint on every shifted key.** KEYQUEUE.ASM runs `int 2` (the NMI vector) whenever a key goes down with Shift held; UW2 removed it. Under a normal BIOS the NMI handler returns, so nothing visible happens. Other debugging aids are left in too: `int 2` and `int 3` in the renderer's unused opcode slots and error paths, an `int 2` in POLYFILL.ASM and GRDISP.ASM, `render_3d` raising `int 2` after a frame when its word at 2860h is above 68h, and Alt+F4, bound in every mode by `init_debug`, raising COM1's interrupt (`ovr127_0`). ([sys/KEYQUEUE.ASM](../src/sys/KEYQUEUE.ASM), [sys/DEBUG.C](../src/sys/DEBUG.C), [sys/OVR127.C](../src/sys/OVR127.C))
- **The joystick is probed but never read.** SYSINIT.ASM's start-up runs JOYPORT.ASM's axis search, but no C calls SYSENTRY.ASM's joystick readers, and UW1 has no joystick code of its own. ([sys/SYSENTRY.ASM](../src/sys/SYSENTRY.ASM))
- **PLAYER.DAT's cipher.** `xorread` and `xorwrite` XOR each byte with a key that starts at the file's seed plus 3 and grows by 3 a byte, the sequence restarting every 0x50 bytes; UW2 chains each byte to the one before. ([sys/MISCUTIL.C](../src/sys/MISCUTIL.C))
- **A function with no return statement.** `get_arc` returns the count read through AX (`AX_RESULT`): the original source fell off the end and relied on the register. ([sys/ARC.C](../src/sys/ARC.C))
- **Sound.** UW1's AIL is an earlier release than UW2's (version word 0CAh, UW2 0D3h). UW1 has no digitised sound effects: every effect is a MIDI note on one of four channels locked from the music driver, and on the PC speaker only six effects sound. The speech borrows EMS pages from the critter art cache while a cutscene plays. ([sound.md](subsystems/sound.md), [sound/AIL.ASM](../src/sound/AIL.ASM))
- **Cutscenes.** UW1's cutscene player decodes its run/skip/dump deltas with LPFDELTA.ASM, which UW2 keeps but never calls; UW1 has 16 cutscene opcodes. The presents screens use palettes 5 and 6 (`pres1.byt`, `pres2.byt`). ([gfx/CUTS.C](../src/gfx/CUTS.C), [game/UWEDIT.C](../src/game/UWEDIT.C))
- **Memory.** `init_mem` asks for a random 54 to 68 EMS pages and needs at least 30; UW1 then allocates a conventional workspace of four 16 KB pages, which `swap_ws_out` and `unswap_ws` swap through EMS. ([sys/TMPALLOC.C](../src/sys/TMPALLOC.C), [sys.md](subsystems/sys.md#memory))
- **Traps and triggers.** UW1 has 17 trap types (0x180 to 0x190) and 7 trigger types (0x1A0 to 0x1A6). The ward trap and the tell trap share one case of `UseTrap`, which hurts whoever set them off. The variable traps work on the player record: z 0 the 32 quest bits, else game variable z of 64, kept to six bits. ([events.md](subsystems/events.md#triggers-and-traps))
- **Conversations.** UW1 binds 42 built-ins; UW2's `babl_hack`, `give_all_stuff`, `set_sequence`, `transform_talker`, `x_clock`, `x_exp`, `teleport` and `switch_pic` are not in UW1. ([conversations.md](subsystems/conversations.md))
- **The level block.** UW1's level block is 0x7C08 bytes and ends at the magic word; the animation overlays and the texture map are blocks of their own. `TERRAIN.DAT` keeps a floor's class in bits 4 and 5 (0x10 water, 0x20 lava), where UW2 moved it to bits 6 and 7. ([map.md](subsystems/map.md#the-level-block))
- **Containers.** UW1's container masks are the rune bag, the quiver, the map case and the bowl; `ItemFitsSlot` still knows UW2's numbering, and a mask it has no case for refuses everything. ([inventory.md](subsystems/inventory.md#what-fits-where))
- **Toolchain and link.** The same Turbo C++ 1.01 and Turbo Assembler 2.0 as UW2, with per-file switches; the graphics library is 18 modules where UW2 has 14, and the overlay manager is Borland's OVERLAY.LIB, unchanged. The relinked EXE is identical to `UW.EXE`. ([LINKING.md](LINKING.md), [NOTES.md](NOTES.md#seg003-the-graphics-library))

## 4. Dead code and open questions

### Dead code

Routines nothing in the EXE calls, by the sources and the listing:

- 3D: NOCLIP.ASM's whole second handler set for opcodes 78h to 9Ch (`seg004_5040` only ever copies the normal set back, because the `xor` that clears the flag at 286Eh leaves ZF set, and nothing sets the flag); CAMERA.ASM's camera types 2, 4, 6, 8 and 0Ah, the view blend and the far entries `seg004_A10`, `A1E`, `A32` and `11DD` to `1245`; VIEW3D.C's `seg031_189` and `seg031_193`, probably debugging toggles. ([3d.md](subsystems/3d.md#open-questions))
- Graphics: QUADFIT.ASM; MAPDATA.ASM's words, written by POLYFILL and never read; PLANECPY.ASM; VSTATS.ASM's `valloc_stats` (UW1 only); GRSPIC.C's `seg009_2C6`. ([gfx.md](subsystems/gfx.md#open-questions))
- Conversations: BABL.C's text-mode built-ins (`ovr093_9F8`, `A7D`, `ADB`, `BE7`, `E4B`, `E61`: print, ask, menu, fmenu, say and respond on the console), which nothing binds; GRDB.C's `grdb_size`, `gr_getpre`, `gr_getlab` and `gr_freelab`; `unbound` looks the current built-in up and does nothing with it. ([conversations.md](subsystems/conversations.md))
- Critters and objects: `creature_save_ovr101_17`; `crit_hndlr_obj`, whose handler's mask is 0; EFFECT.C's `seg044_368F_392` and `CreateAnimoForSrcObject_seg044_368F_CE3` (UW2's, kept static) and its four bytes `unknown[4]`; INVDATA.C's `FindEmptySlot`; INVPANEL.C's empty `EndInventory`, as in UW2.
- Map and motion: MAP.C's `OverwriteAllTiles_ovr128_37` and TEXTMAPS.C's `ovr131_1DD`, probably editor leftovers; MOTION.C's empty `seg031_2CFA_A3F`; `IgnoringInput`, `oldPlayerInput` and `dseg_5c99_761` in PLAYMOVE.C and `saved_dz` in PHYSICS.C, never read; the ice movement state, which `parse_player_terr` never picks. ([map.md](subsystems/map.md#open-questions), [motion.md](subsystems/motion.md))
- Game and UI: PLAYDATA.C's empty `MaybePlayerDayLoadrelated_ovr142_0` and UWEDIT.C's `ovr112_389`; OVR096.C's seven browse hooks (UW2's BROWSE.C byte for byte), OVR137.C's one empty function, INTERACT.C's `seg026_2716_F8A` and `seg026_2716_F98`; OPTIONS.C's `options_mouse_click` increments x and never reads it again. ([game.md](subsystems/game.md#open-questions), [ui.md](subsystems/ui.md))
- Sound: `sound_b292`, set and never read; a whole-file reader and the effects toggle in SOUND.C. ([sound/SOUND.C](../src/sound/SOUND.C))
- System: OVR090.C and OVR152.C (empty hooks and two that switch to screen mode 16); JOYPORT.ASM's readers and SYSENTRY.ASM's `_7CD` and `_809`; TICKS.ASM's far routine at +714; MISCUTIL.C's `dbg_break`; DEBUG.C's `dprintf` is empty. ([sys.md](subsystems/sys.md#open-questions))
- Empty but called: SYSLIBP.ASM's `_C10`, which `render_3d` calls each frame on the graphics library's stack.

### Open questions

Merged from the notes, without repeating the open parts of the bugs above.

Answered while writing this page:

- **Which quests the conversations use**: 0 to 11 and the quest byte 32, all as constants (the quest entry above).
- **Whether floor heights 14 and 15 occur in UW1**: they do not, on any of the nine levels.
- **Whether a flameproof flier exists**: creature 0x7B, on level 7.
- **Whether `kill_triggers`' write after the free is harmless** ([events.md](subsystems/events.md#open-questions)): the write is, but the walk stops there (above).

Still open:

- Rendering: what UW1's own model opcodes (1Eh, 20h, 24h, 28h, 30h, 52h, 6Ah) are used for; whether the list `render_3d` walks for `seg004_17AB` is ever filled; whether the hallucination effect's words are texture masks; whether any model uses opcode 0D2h. ([3d.md](subsystems/3d.md#open-questions))
- Graphics: what QUADFIT, MAPDATA and PLANECPY served; the plane-by-plane copies in VIDMODE.ASM and the third word of each screen-mode record. ([gfx.md](subsystems/gfx.md#open-questions))
- Combat: which of `do_miss`'s effects 7 and 8 is which sound; what area spell mode 0xC0 was meant to select apart from 0x80. ([combat.md](subsystems/combat.md#open-questions))
- Critters: which races 0x0D and 0x13 are; which critters have goal 11; the unnamed `Creature` fields. ([critter.md](subsystems/critter.md#open-questions))
- Events: why the tell trap acts as a ward trap. ([events.md](subsystems/events.md#open-questions))
- Conversations: the meaning of the import variable types 0x126 to 0x12B; what ran the text-mode built-ins. ([conversations.md](subsystems/conversations.md#open-questions))
- Player record and motion: the bytes with no known meaning (`b3C`, `bB1`, `bCA`) and game variable 0x1A's start at 0x35; bit 0x10 of `motion_state`; `Phys.b1D`, `MotionParams.f21` and `Handler.w6`; when `EtherealVoidSpecialEffects_seg008_150` runs. ([game.md](subsystems/game.md#open-questions), [motion.md](subsystems/motion.md#open-questions))
- Objects and inventory: the unnamed mobile fields; what missile ammunition value 0xC0 marks; why `InvRemoveObject` redraws display position 19. ([objects.md](subsystems/objects.md#open-questions), [inventory.md](subsystems/inventory.md#open-questions))
- Map: what `ovr131_1DD` drew. ([map.md](subsystems/map.md#open-questions))
- Sound: why theme 11 is treated like the victory theme though nothing asks for it; what the cup of wonder's tune plays. ([sound.md](subsystems/sound.md#open-questions))
- Screen and system: the kinds 2 and 3 of a `PlayersMap` byte; what the gargoyle eyes reflect; screen modes 8 and 0x10; what the development build did with Alt+F4's COM1 interrupt and `dprintf`; why start-up still probes the joystick; whose data the IMATH tables are. ([ui.md](subsystems/ui.md#open-questions), [sys.md](subsystems/sys.md#open-questions))
