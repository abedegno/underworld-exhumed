# What the decompilation found

This page collects what matching UW2 byte for byte revealed about the game: likely bugs in the shipped program, the game rules the code implements, how the engine works, code nothing calls, and the questions still open. The detail lives in the [subsystem notes](subsystems/), [LAYOUT.md](LAYOUT.md), [MATCHING.md](MATCHING.md) and the comments in `src/`; each entry here links to it.

Everything here is read from the matched sources, which compile to the same bytes as the shipped `UW2.EXE`, so a statement about what the code does is a statement about what DOS UW2 does. Two other witnesses are used where they help: the FM Towns build of UW2 (`tools/fmt.py`, the same program compiled for a 32-bit machine, with Looking Glass's own names), and the game data of the GOG release. Statements about intent or about the effect in the game are inferences, and say so.

Bugs are labelled by how sure we are that the code is wrong (what the code does is always read from the code):

- **confirmed**: a second piece of evidence shows the code is wrong, such as the FM Towns build doing it differently or the game data reaching the faulty path;
- **likely**: the code leaves little doubt that it was not meant, but nothing outside the code shows it;
- **possible**: it looks wrong, but there is a plausible intent the code fulfils.

"Both builds" means the FM Towns build has the same code, so the mistake, if it is one, is in the original source and not in the DOS port.

## 1. Likely bugs in the original game

Each entry was re-read against the source before it was written here. The section ends with the candidates that did not hold up.

### Relk's skull says nothing

- **What happens:** looking at a skull or bones prints whose remains they are, by the owner field. The code has a case that prints "Relk." for owner 0x3D, but the condition in front of it lets through only owners below 0x3C and owner 0x3F, so the case never runs. Had it run, it would also have printed a creature name after "Relk.", because the next test has an `else` and this one does not.
- **Where:** `BonesLook` in [obj/LOOK.C](../src/obj/LOOK.C).
- **Evidence:** code reading; both builds. The game data reaches the dead case: the shipped `LEV.ARK` has a skull (item 0xC2) with owner 0x3D on level 17, in the object list of tile (32, 42), as static object 0x225. It is the only skull or bones object with that owner.
- **Confidence:** confirmed.
- **Effect:** looking at Relk's skull gives only the plain description; "Relk." is never printed.
- **For a port:** matching DOS means printing nothing extra; printing "Relk." is what the code was evidently written to do.

### A critter placed in a north-west diagonal tile goes to the wrong corner

- **What happens:** `set_gridx_and_y_based_on_tile_type` picks the fine position where a moved critter stands in a tile: the middle of an open tile, the open corner of a diagonal. Type 5 (`TILE_DIAG_NW`) gets (6, 1), the same as type 2 (`TILE_DIAG_SE`), where its open corner is (1, 6). The other two diagonals get their own corners.
- **Where:** [critter/CRITTIME.C](../src/critter/CRITTIME.C); used by `teleport_critter`, by critters going home while the player sleeps, and by the wandering monster placement in the same file.
- **Evidence:** code reading; both builds (the FM Towns jump table sends case 5 to case 2's code).
- **Confidence:** likely.
- **Effect:** the critter is aimed at the closed half of the tile. `can_place` probably refuses it there, so the move fails; not checked in the game.
- **For a port:** (1, 6) is the consistent value; DOS uses (6, 1).

### Inserting a schedule row bumps the wrong level's index

- **What happens:** each schedule keeps, for each level, the time reached and the index of the next row to run. When rows are inserted, every level whose clock is past the new row should have its own next index moved on. `Sched_InsertLong` instead moves the current level's index once for each such level, and leaves the other levels' indexes alone.
- **Where:** `Sched_InsertLong` in [event/SCHEDULE.C](../src/event/SCHEDULE.C). Its only caller is `do_migrations`, which adds queued NPC migrations to block 0, the day schedule.
- **Evidence:** code reading; both builds. `Sched_Delete`, in the same file, does the correct per-clock fix-up.
- **Confidence:** likely.
- **Effect:** after a migration, the current level can skip day-schedule rows, and another level can run a row a second time when the player returns to it. The day clock resets a level's index to 0 when it wraps, which limits the damage to the rest of that day. Not observed in the game.
- **For a port:** fixing it changes which schedule rows run; matching DOS needs the slip.

### gronkify_slay tests the wrong byte

- **What happens:** schedule event 3 kills NPCs. For the NPC the player is talking to, a repeating row should instead count up `xclock[XC_CHANGED]` when byte 6 of the schedule work area is 15. In `SCHEDULE.C`'s layout, byte 6 is the event code of the first queued migration row, not the block number, and event codes run from 1 to 11.
- **Where:** `gronkify_slay` in [event/SCDEVENT.C](../src/event/SCDEVENT.C); the layout is `struct SCDWork` in [include/event.h](../src/include/event.h).
- **Evidence:** code reading; both builds (FM Towns also reads `Workspace+6`, and keeps the block number at +104h).
- **Confidence:** likely.
- **Effect:** the count-up probably never happens; the NPC is spared either way. Whether any plot step waits for it is not known.
- **For a port:** a consideration only; what the test was meant to see is unknown (block 15 is a guess).

### Saving more than 44 notes on one level corrupts them

- **What happens:** a level's automap notes (`ATM_Strings`, 100 records of 0x36 bytes) sit inside `stdat`, at offset 468Eh. Their `LEV.ARK` blocks are flagged compress-on-write (flags 5 in all 80 note blocks), so `SaveTheWords` sends them through `put_arc` to `ac_shrink_disk`, which puts the LZSS work area at the start of `stdat`. Before it reads its input, the compressor sets `rson[4097..4352]` and `dad[0..4095]` to NIL, which covers offsets 5029h to 7229h of `stdat`, and its first `InsertNode` calls write `rson[4060..4078]` at 4FDFh to 5003h. That is note 44 (from its byte 9) onwards. The compressor then reads the notes from the same memory.
- **Where:** `SaveTheWords` in [ui/AUTOMAP.C](../src/ui/AUTOMAP.C), `put_arc` in [sys/ARC.C](../src/sys/ARC.C), `ac_setup_lzw` in [sys/ACLZW.C](../src/sys/ACLZW.C), `CompressLZW_disk` in [sys/LZSS.C](../src/sys/LZSS.C) and `struct LzwWork` in [include/file.h](../src/include/file.h), the buffer in [sys/FARDATA.ASM](../src/sys/FARDATA.ASM).
- **Evidence:** code reading, with the block flags read from the shipped `LEV.ARK`. Found while re-checking the `stdat` overlap candidates below. The FM Towns build was not checked (its 32-bit work area may be laid out differently).
- **Confidence:** likely.
- **Effect:** with 45 or more notes on a level, the 45th note is partly overwritten and the later ones are saved as NIL words (00 10). With fewer notes the input ends before 951h and nothing is lost. Not tried in the game.
- **For a port:** a port that keeps the notes in their own memory loses the bug without trying; matching DOS would need the overlap on purpose.

### A cutscene "data" opcode uses an uninitialised file number

- **What happens:** cutscene opcode 8, `data(file, ext)`, names the next animation file. For file 996 it picks one of cutscenes 034 to 037 (octal) at random; for any other value DOS never sets `file`, so the three digits of the name come from whatever the stack slot held. The FM Towns build has `else file = code[0]`.
- **Where:** `cutsop_data` in [gfx/CUTS.C](../src/gfx/CUTS.C).
- **Evidence:** the DOS code (IDA's listing reads `[bp-2]` uninitialised on that path) against FM Towns' `cutsop_data_`. The shipped scripts use other values: `CS000.N00` has `data(0, 10)`, `data(0, 8)` and `data(0, 11)`, and `CS040.N00` has `data(40, 1)` twice.
- **Confidence:** confirmed (DOS differs from FM Towns).
- **Effect:** unknown. The intro (cutscene 0) plays correctly in DOS, so the stack slot evidently holds a harmless value there; whether cutscene 040 loads the right file has not been checked.
- **For a port:** FM Towns' version is the intended one; matching DOS exactly would mean reproducing the stack's contents.

### A chained bitfield assignment sets stray cutscene flags

- **What happens:** `reset_inf_values_eof` clears three flag bits with `b0 = b1 = b6 = 0`. Turbo C 1.01 compiles a three-deep chain of bitfield stores by OR-ing an uninitialised DX, shifted left by one, into the middle field's byte (`shl dx,1; or [si+5Bh],dl`), so any of bits 1 to 7 of the flags byte can be set. The function then clears bit 0, sets bits 2 and 4, and clears bit 7, so bits 1, 3, 5 and 6 (a key arrived, keep playing the cutscene, speech available, speech playing) can be left set.
- **Where:** [gfx/CUTS.C](../src/gfx/CUTS.C); the compiler behaviour is described in [MATCHING.md](MATCHING.md).
- **Evidence:** the DOS bytes. It is the compiler's code generation, so the source was right and the program is not.
- **Confidence:** confirmed.
- **Effect:** unknown. Several opcodes clear bit 1 again before it is read; what the other bits do at the start of the next file was not traced.
- **For a port:** the source's intent (clear the three bits) is clear.

### mm3x9_bpsi and mm3x9t saturate the wrong product

- **What happens:** these routines multiply a row by a 3 x 3 matrix and saturate each of the three products on overflow. The fix-up for the third product reads and writes CX, the second product's register, instead of DI.
- **Where:** [3d/INSTANCE.ASM](../src/3d/INSTANCE.ASM).
- **Evidence:** the DOS code against FM Towns' `mm3x9_bpsi`, which saturates DI.
- **Confidence:** confirmed.
- **Effect:** when the third product overflows, the second result is overwritten and the third is left wrapped. Overflow of a 1.15 matrix product is probably rare; no visible effect is known.
- **For a port:** a port with wider arithmetic does not overflow there at all.

### do_hull culls with the previous handler's DH

- **What happens:** the model opcode `do_hull` (84h) decides whether a model is wholly outside one frustum plane by ANDing its points' clip codes, but it ANDs words, not bytes: BP starts as DX, whose low byte is the first point's codes and whose high byte is whatever DH the previous opcode handler left, and each further point's word adds its shade byte (+7 of the point record) to the high byte. The `je` tests the 16-bit result, so a model is also dropped (the `do_eof` path) when DH and all its later points' shade bytes share a set bit.
- **Where:** [3d/INTERP.ASM](../src/3d/INTERP.ASM), `do_hull`.
- **Evidence:** the code; point shade bytes are only set by the Gouraud opcodes (`do_setshade`, `do_uwshade`) and otherwise keep what an earlier model left there.
- **Confidence:** likely (the code is unambiguous; whether the condition arises in play is not known).
- **Effect:** a model may vanish for a frame although it is in view. Not seen in the replays so far.
- **For a port:** the faithful port keeps the registers between handlers as the CPU did (docs/PORT.md, "The 3D renderer"), so it drops the same models; a fix tests the low byte only.

### sphere_check's depth loses its low bit to a compare

- **What happens:** for a negative zoom (24E6h), `sphere_check` divides the object's depth, as a 15-bit fraction, by the zoom: `sar dx,1` puts the depth's low bit in the carry for the `rcr ax,1` that follows, but a `cmp dx,cx` comes between them and replaces the carry with its borrow, which is always set on that path. So bit 15 of the dividend is always 1, and the quotient can come out one more than intended.
- **Where:** [3d/SPHERE.ASM](../src/3d/SPHERE.ASM), `sphere_check` (both copies of the tail, at L0364 and L0403).
- **Evidence:** the code.
- **Confidence:** confirmed (whether a negative zoom is ever set is not known).
- **Effect:** the distance `do_obj` stores at 25FAh is off by at most one unit; no visible effect is known.
- **For a port:** reproduced by the faithful port; a fix keeps the carry from the `sar`.

### do_fetchmap reads tmcolor from the wrong segment

- **What happens:** after mapping a texture, `do_fetchmap` sets `tmcolor` (FD58:2696) from the texture's first byte, but it loads DS from SI (the bitmap slot's address) where it should use the texture's segment, so it reads a byte at segment C8xxh.
- **Where:** [3d/PGCACHE.ASM](../src/3d/PGCACHE.ASM).
- **Evidence:** the DOS code against FM Towns, which reads the texture.
- **Confidence:** confirmed.
- **Effect:** none: nothing in the DOS code reads FD58:2696 (searched in every source; its neighbours at 2692h and 2694h are read).
- **For a port:** nothing to do.

### do_bcompact_map maps with a leftover AX

- **What happens:** the opcode `do_bcompact_map` (the mirrored compact floor mapper) jumps into the shared code with AX unset, so its texture block is whatever the previous handler left in AX; FM Towns loads `bmap_blk_ptr` first.
- **Where:** [3d/TMAPOPS.ASM](../src/3d/TMAPOPS.ASM).
- **Evidence:** the DOS code against FM Towns.
- **Confidence:** likely.
- **Effect:** probably none: the C never emits the opcode. The 3D models' bytecode has not been searched for it.
- **For a port:** nothing to do unless a model uses it.

### The pit fighters' rescue from lava mixes units

- **What happens:** `maybe_rescue_guy_from_fire` works in fine positions (8 to a tile) but passes them to `Map_GetAddr` and `teleport_critter`, which take tiles. Near the arena (around tile 0x16, 0x16) the fine coordinates are above 63, so `Map_GetAddr` returns a null pointer and the lava test reads low memory.
- **Where:** [critter/CRITTIME.C](../src/critter/CRITTIME.C).
- **Evidence:** code reading; both builds.
- **Confidence:** likely.
- **Effect:** the rescue probably never works as meant: a pit fighter on lava is not moved, or is moved by chance. Not checked in the game.
- **For a port:** fixing it rescues fighters that DOS leaves in the lava.

### Grass never turns into mushrooms in the dying courtyard

- **What happens:** `kill_plants` (one object in three, at each stage of the castle plot) wilts plants and turns wilted plants into debris, grass or mushrooms. Its grass case chooses a mushroom or debris one time in 15 each, then returns before changing the object.
- **Where:** `kill_plants` in [event/WORLDEV.C](../src/event/WORLDEV.C).
- **Evidence:** code reading; both builds.
- **Confidence:** possible (a `break` was probably meant, but the courtyard gets mushrooms another way from stage 13).
- **Effect:** grass in the courtyard never changes.
- **For a port:** a consideration only.

### Weapon enchantments "of Accuracy" and "of Damage" do each other's job

- **What happens:** a weapon enchantment (class 12) with effect 0 to 3 adds damage (2e + 1: +1, +3, +5, +7), and one with effect 4 to 7 adds to the attack skill (2e - 7: +1, +3, +5, +7). The names the game shows for those effects are the other way round: string block 6 entries 0x1C0 to 0x1C3 are "Minor/Major/Great/Unsurpassed Accuracy" and 0x1C4 to 0x1C7 "Minor/Major/Great/Unsurpassed Damage", and `do_of` names a weapon's enchantment by 0x1C0 + effect. So "of Accuracy" makes the weapon hit harder and "of Damage" makes it hit more often. UW1 has the same names in the same order, and there they are right: effects 0 to 7 add to hit, 8 to 15 add damage.
- **Where:** `DoPlayerWeapon` in [combat/COMBAT.C](../src/combat/COMBAT.C) (the effects), `do_of` in [obj/LOOK.C](../src/obj/LOOK.C) (the names); `decode_obj_spell` in [obj/OBJUSE.C](../src/obj/OBJUSE.C) gives both the same effect number.
- **Evidence:** code reading and the shipped `STRINGS.PAK` (block 6); not measured in play.
- **Confidence:** certain that the names and the effects disagree; whether the code or the strings were meant to change is not known.
- **Effect:** players who choose a weapon by its enchantment's name get the other bonus. Players have long reported the two as swapped, though usually for UW1, where they are not.
- **For a port:** matching DOS keeps both; a fix belongs in the strings, so that the game's arithmetic is unchanged.

### Item toughness on an item in a high slot goes to one location twice

- **What happens:** for an item enchantment of class 12 with the 8 bit set (toughness) on an item in a slot above 4, `player_affected_by` loops over hit locations 0 and 1 but adds the toughness to `armour[slots[0]]` each time, so location 0 gets it twice and location 1 not at all. The to-hit protection in the same loop uses `slots[i]`.
- **Where:** `player_affected_by` in [game/PLAYDATA.C](../src/game/PLAYDATA.C).
- **Evidence:** code reading; both games (UW1's FINDINGS.md has the same entry).
- **Confidence:** likely (the neighbouring line indexes by `i`).
- **Effect:** such an item protects one hit location twice over and leaves the other bare. Which items carry such enchantments was not checked.
- **For a port:** matching DOS means the doubled location.

### Mending's light-source exclusion compares the wrong field

- **What happens:** `mendable` refuses lights in MAJOR_MISC minor class 1 by testing `OBJ_CLASS(obj) != 0x90 && OBJ_CLASS(obj) != 0x94`, but `OBJ_CLASS` is major * 4 + minor (9 here), so both tests always pass. 0x90 and 0x94 are the first unlit and the first lit light item ids, so the test was probably meant to limit Mending to the wands in that class.
- **Where:** [combat/SPELLS2.C](../src/combat/SPELLS2.C).
- **Evidence:** code reading; both builds.
- **Confidence:** possible.
- **Effect:** Mending accepts lanterns, torches, candles and light spheres. What it then does to them was not traced.
- **For a port:** a consideration only.

### Object-cast Mass Paralyze is Repel Undead

- **What happens:** spell 64, Mass Paralyze by the order of the names in string block 6, is class 6 (area) minor 5, which selects `area_spells[4]`, `sp_repel_undead`, with target mode 0 (critters). `area_spells` has no area paralysis handler, and `sp_repel_undead` does nothing unless the player is the caster.
- **Where:** `spells[]` and `area_spells[]` in [combat/SPELLS.C](../src/combat/SPELLS.C).
- **Evidence:** code reading; both builds have the same table entry (30 18 63 05) and the same `area_spells`.
- **Confidence:** possible (either the string order is off by one there, or an object carrying Mass Paralyze really repels undead).
- **Effect:** unknown; whether any object in the game carries spell 64 has not been checked.
- **For a port:** a consideration only.

### Floor heights 14 and 15 convert to z 0

- **What happens:** `hgt_val`, which turns a tile's 4-bit floor height into z, has 0 for heights 14 and 15 (between 0x340 for 13 and 0x400 for 16).
- **Where:** [map/MAP.C](../src/map/MAP.C); read by `grdb_elem`'s face visibility test in [3d/GRIDDB.C](../src/3d/GRIDDB.C) and by `player_setup` in [motion/PHYSICS.C](../src/motion/PHYSICS.C).
- **Evidence:** code reading; both builds have the same table. The shipped `LEV.ARK` has 8 open tiles at these heights: two at height 14 on level 25 and six at height 15 on level 65 (in the Ethereal Void).
- **Confidence:** possible.
- **Effect:** probably none visible. The floor of such a tile always counts as facing the eye, which is right whenever the eye is above it; a slope rising from height 13 gets a wrong back-face test; a player placed on such a tile by `player_setup` starts at z 0. None of this was checked in the game.
- **For a port:** 0x380 and 0x3C0 are the evident values.

### "contains" ignores case only without substitution

- **What happens:** the conversation built-in "contains" lowercases its two source strings in place but searches the `@`-substituted copies; when either string had a substitution, its copy keeps its case.
- **Where:** `StringContains_ovr095_BDB` and `convert_string` in [conv/BABL.C](../src/conv/BABL.C).
- **Evidence:** code reading (`convert_string` returns its argument when there is no `@`).
- **Confidence:** possible.
- **Effect:** a keyword test on substituted text is case-sensitive; no script was checked for one.
- **For a port:** a consideration only.

### Smashing a door reads the interrupt vector table

- **What happens:** at the end of a melee hit, `def` is set to 0 when `damage_item` reports the target destroyed. For a door the code then tests `OBJ_Z(def)` against the hit height, so a blow that breaks a door reads the `pos` word of a null far pointer: 0000:0002, the segment half of the int 0 vector, which `init_world` points at `int0_trap`. Its low seven bits become the height of the debris effect when they are above the hit height.
- **Where:** the end of the melee damage function in [combat/COMBAT.C](../src/combat/COMBAT.C), marked `FARNULLTRAP`.
- **Evidence:** code reading, and clang's static analyzer (docs/PORT.md, "Null pointers"). `damage_item` returns `remove_object`'s result for an object it destroys ([combat/DAMAGE.C](../src/combat/DAMAGE.C)). Not checked in the game.
- **Confidence:** likely.
- **Effect:** the height of the splinters from a smashed door depends on the segment DOS loaded the game at, so it can differ between machines. Nothing else follows from it.
- **For a port:** a faithful port reads the vector table it models; using the door's height from before the blow is the evident fix.

### A bare-handed blow reads the interrupt vector table

- **What happens:** `DoPlayerWeapon` sets up every blow the player strikes, and its first statement tests whether the weapon is the jewelled dagger with `OBJ_ITEM(weap)`, before it tests `weap` for 0. Bare-handed, `GetPlayerWeapon` has returned no weapon, so the test reads the `id` word of a null far pointer: 0000:0000, the offset half of the int 0 vector, which `init_world` points at `int0_trap` (offset 0019h). Item 19h is not the dagger (0Ah), so the flag comes out right.
- **Where:** `DoPlayerWeapon` in [combat/COMBAT.C](../src/combat/COMBAT.C), now marked `FARNULLTRAP`.
- **Evidence:** found by the `sound` replay session, the first to strike a blow: the port stopped on the null read, and the replay DOS build's `NULLTRAP.LOG` lists `combat.c:625` four times, once for each swing. The rest of the function tests `weap` before using it.
- **Confidence:** confirmed.
- **Effect:** none: the vector's offset is the same on every machine, and it is not the dagger's id.
- **For a port:** a faithful port reads the vector table it models; testing `weap` first is the evident fix.

### Looking at an empty inventory slot reads the interrupt vector table

- **What happens:** `inv_look` (look mode on the inventory) calls `checkTrap(ThePlayer, newPlObj, ...)` before it has picked the object, with `newPlObj` still 0, and only then looks the object up. `checkTrap` reads the null object's quantity bit, its contents link (0000:0006, the segment of the int 1 vector), its class and its contents list, through the interrupt vector table.
- **Where:** `checkTrap` in [obj/OBJUSE.C](../src/obj/OBJUSE.C), now marked `FARNULLTRAP` at its four reads; its caller `inv_look` in [ui/INTERACT.C](../src/ui/INTERACT.C).
- **Evidence:** found by a replay session (look mode on the inventory panel): the port stopped on the null read; the replay DOS build's `NULLTRAP.LOG` lists `objuse.c` each time.
- **Confidence:** confirmed.
- **Effect:** depends on the machine. Under DOSBox int 1's segment is 0070h, a link to object 1 (the player), so the game walks the player's object list looking for a trap and finds none; with another DOS or BIOS the link is another object, and a trap in its list could go off. The port's vector table stand-in holds DOSBox's ints 1 to 7 (docs/PORT.md, "Null pointers").
- **For a port:** a faithful port reads the vector table it models; passing the object `inv_look` picks, or returning on a null object, is the evident fix.

### A scheduled trigger reads the interrupt vector table

- **What happens:** schedule event 5, `ev_trigger`, sets off each trigger of minor class 0xC on a square with `UseTrigger(0L, 0L, trigger, 0xC)`, with no one setting it off. `UseTrigger` then asks who set it off before it runs the trap chain: `OBJ_ITEM(who)` and twice `OBJ_MAJOR(who)` read the `id` word of a null far pointer, 0000:0000, the offset half of the int 0 vector (0019h, which `init_world` puts there). Item 19h is neither the adventurer nor a creature, so the trigger goes off when it has `ID_FLAG9`, and every class 0xC trigger in LEV.ARK has it. A door the chain opens (a DOOR trap of quality 1) calls `OpenDoor` and `checkTrap` with the same null, and its own trigger reads the vector again.
- **Where:** `UseTrigger` in [event/TRIGGER.C](../src/event/TRIGGER.C), marked `FARNULLTRAP` at the three reads; its caller `ev_trigger` in [event/SCDEVENT.C](../src/event/SCDEVENT.C). With no one setting the chain off, `CharacterThatTriggeredTrap` and `TriggeringButton` are 0 too; no trap kind a class 0xC trigger reaches in the shipped data reads them, but PROXIMITY, hacks 3, 4, 41, 42 and 62 would, and are marked; TELEPORT, DAMAGE and hack 30 pass the null pointer on to `do_teleport`, `whack_thing` and `arena_opponent_runs`, which are not.
- **Evidence:** the 1.2.0-rc4 port crashed at `UseTrigger` called from `ev_trigger` right after the first conversation with Lord British, which sets quest 109 and so enables block 15's row 25, `ev_trigger` on level 1 square (30, 44): trigger 232, whose chain is hack 36, `move_folks_around`. The same trigger fires from block 0 every six steps of the day clock while the player is on level 1. A walk over every class 0xC trigger in LEV.ARK and every event 5 row in SCD.ARK found the three reads above as the only null reads their chains reach.
- **Confidence:** confirmed (the reads); the trap chains are from the data.
- **Effect:** none: the vector's offset is the same on every machine, and with it the trigger always goes off, as it was evidently meant to.
- **For a port:** a faithful port reads the vector table it models; skipping the checks for a null `who` is the evident fix.

### The castle schedule reads below its tables for whoami 0x81

- **What happens:** `move_folks_around` (trap hack 36) runs `maybe_go_hang_out` for whoami 0x81 to 0x8F, but `where_shall_we_hang_out` indexes its tables of each person's own spot by `whoami - 0x82`, so for 0x81 it reads `xs[-1]` and `ys[-1]`. In DOS's frame `ys` lies just below `xs`, so `xs[-1]` is `ys[13]`, 22h; `ys[-1]` is the high byte of the SI the function saved, `gronk_whoami`'s `arg`, which `move_folks_around` passes as 0. Whoever has whoami 0x81 is sent to square (22h, 0) whenever the stage of the day picks its own spot.
- **Where:** `where_shall_we_hang_out` in [critter/CRITTIME.C](../src/critter/CRITTIME.C), the two reads marked `FRAME_INDEX` (portable.h); the loop in `move_folks_around`, [event/WORLDEV.C](../src/event/WORLDEV.C).
- **Evidence:** the `lb` replay: after the first conversation with Lord British fires the schedule, DOS left quality 22h in object 248 and the port 0; the frame (`sub sp,28h`, `xs` at bp-1Ah, `ys` at bp-28h) and `gronk_whoami`'s `mov si,[bp+0Ah]` give the two values.
- **Confidence:** confirmed.
- **Effect:** the same on every machine, since the values come from the frame and a constant argument.
- **For a port:** a faithful port gives DOS's two values; starting the loop at 0x82, or giving 0x81 a place of its own, is the evident fix.

### Opening a carried container with no bag open reads the interrupt vector table

- **What happens:** `FindSlot` looks for an object in the inventory, and for each slot holding a container that is not the open bag it searches the container; the test compares the slot with `OpenBag->obj` without checking `OpenBag`, which is 0 while no bag is open. So opening a container from the backpack (`UseCont` calls `FindSlot`) reads `obj`, the word at offset 8 of a null far pointer: 0000:0008, the offset half of the int 2 vector, 00F4h under DOSBox, a link to object 3. A slot holding object 3 would be skipped. The function that puts an object into a container (BAGS.C, after `AddTogether`) reads the same field the same way when the container is not the open bag.
- **Where:** `FindSlot` in [inv/INVDATA.C](../src/inv/INVDATA.C) and the container function in [inv/BAGS.C](../src/inv/BAGS.C), marked `FARNULLREC` (portable.h): `struct Bag` holds pointers, so on the host the field is not at DOS's offset, and the port reads the vector table by the struct's DOS layout.
- **Evidence:** 1.2.0-rc7 crashed (a read at address 10h, the host's offset of `obj`) opening a bag picked up in the Avatar's room; in a replay of the `items` session, with the sack carried and no bag open, `FindSlot` crashes the unmarked port at 10h and reads 0000:0008 in the marked one.
- **Confidence:** confirmed.
- **Effect:** depends on the machine: the int 2 vector differs between DOS set-ups; under DOSBox only an inventory slot holding object 3 is affected.
- **For a port:** a faithful port reads the vector table it models; testing `OpenBag` first is the evident fix.

### More reads the original makes through null pointers and past its tables

An audit of the whole game's C for the three ways DOS forgives what a modern host does not (null pointers, reads beyond a table, the host's struct layouts), after three of them crashed the port in play. Each site is marked so that the port reads what DOS read (portable.h); none changes the DOS bytes.

- **A missed blow on an unarmoured spot** ([combat/COMBAT.C](../src/combat/COMBAT.C), `do_miss`): `OBJ_ITEM(armour)` is read before `armour` is tested, so a critter's melee miss where the player wears nothing reads 0000:0000 (item 19h); the value is not used. Common in any early fight. `FARNULLTRAP`.
- **The Pits' fire rescue** ([critter/CRITTIME.C](../src/critter/CRITTIME.C), `maybe_rescue_guy_from_fire`): the lava rescue already listed above reads `Map_GetAddr(...)->floor` through the null square (floor 0). `FARNULLTRAP`.
- **Looking at nothing in the inventory** ([ui/INTERACT.C](../src/ui/INTERACT.C), `inv_look`): with no object under the pointer (the open bag's own picture, or a look released over an empty slot) the major class, item and heading are read through the null object (item 19h), and the lore bits are written into the vector table's int 0 segment word unless bit 2 is set there. `FARNULLTRAP`.
- **Use on the weapon hand with it empty** ([inv/BAGS.C](../src/inv/BAGS.C), `DoSpecialActions`): the item reads as 19h, a bow, so DOS toggles fight mode. `FARNULLTRAP`.
- **Mode buttons** ([ui/GAMESCR.C](../src/ui/GAMESCR.C), `init_gamedisp`; [ui/INTERACT.C](../src/ui/INTERACT.C), `deal_with_icons`): `button_to_mode[RightButtonThing + 5]` reads 6 to 10, which is `mode_to_button` (WRAPPER.C has `mode_to_button[RightButtonThing - 1]`, evidently what was meant), and F2's mode 0 reads `mode_to_button[-1]`, `button_to_mode[5]`. `TABLE_NEXT`, `TABLE_PREV`.
- **A swing in the left two ninths of the view** ([combat/COMBAT.C](../src/combat/COMBAT.C), `player_attack`): `swing_keys[swing / 3 - 1]` with swing 1 or 2 reads `swing_kind[8]`, 1. `TABLE_PREV`.
- **Long descriptions** ([obj/LOOK.C](../src/obj/LOOK.C), `LookAt`): an identified, enchanted, owned item's description runs to 85 bytes in `text[80]`; in DOS the end lands on locals already used. The host's array is longer (`FRAME_LEN`).
- **Disarming a special effects trap** ([game/SKILLS.C](../src/game/SKILLS.C), `RemoveTrap`): "special effects trap" and its 0 are 21 bytes in `name[20]`; in DOS the 0 clears the low byte of `trig`, which `delete_trap` then uses, so a successful disarm of such a trap deletes through a wrong pointer. The host's array is longer (`FRAME_LEN`); the port does not reproduce the stray pointer.
- **Evidence:** the audits (clang's AST over the game's C; AddressSanitizer and `-fsanitize=array-bounds` builds over the ten sessions, where the mode buttons made `items` and `talk` differ from DOS under ASan's layout; clang's host struct layouts), with the DOS frames and data read from the matched objects.
- **Confidence:** confirmed (the code); the effects as stated.
- **For a port:** testing the pointer or the index is the evident fix in each case.

### Escape at a conversation's typed answer reads DS:0

- **What happens:** when a conversation asks the player to type an answer, CONVERSE.C calls `wdialog` with no initial text (a null pointer). If the player presses Escape, `wdialog` copies the initial text into the answer anyway, so the answer becomes the string at DS:0. In UW2.EXE DS:0 holds the tail of an overlay stub (docs/PORT.md, "Null pointers"): the answer is `'` and byte 06h while ovr167 is not in the overlay buffer, and byte 06h followed by the overlay's segment bytes while it is.
- **Where:** `wdialog` in [ui/SCROLLIO.C](../src/ui/SCROLLIO.C), marked `NULLTRAP`; its caller in [conv/CONVERSE.C](../src/conv/CONVERSE.C).
- **Evidence:** code reading, and clang's static analyzer. The other two callers pass real text. Not checked in the game.
- **Confidence:** likely.
- **Effect:** probably none visible: a short string of control bytes is no keyword, so the script takes its default answer. What the script does with it was not traced.
- **For a port:** a faithful port gives the bytes at DS:0; an empty answer is the evident fix.

### The attack check reads an uninitialised flag

- **What happens:** every frame `player_attack(0)` sets `held` from the attack key only when there is one (`attackKey > 0`); otherwise `held` is read before it is set, in `held = held || (mouse_getbut(&charge) & 2)`. When the stack slot happens to hold a nonzero byte the right button is not polled at all and the attack is treated as held.
- **Where:** `player_attack` in [combat/COMBAT.C](../src/combat/COMBAT.C), now declared with `STACK_JUNK(0)`.
- **Evidence:** clang (`-Wsometimes-uninitialized`), and the replay harness: two DOS replays of one recording went different ways in the game by one call of `mbuttons`, and the trace (`UWRPTRACE`) put the extra call in `mouse_btns` between `check_physics` and `display_scr`, which is this one. The slot's contents depend on the calls before and on the timer interrupts, which push onto the game's stack whenever they come, so they differ from run to run.
- **Confidence:** confirmed.
- **Effect:** while a weapon is charged with the mouse, the swing can be held or released by stack junk rather than by the button; with no attack in progress nothing follows from it but the extra or missing mouse poll.
- **For a port:** a faithful port cannot reproduce stack junk; the replay build and the port both start `held` at 0, which is what DOS does when the slot holds zero.

### Other locals read before they are set

clang finds six more locals that some path reads before setting (`-Wsometimes-uninitialized`, `-Wuninitialized`), besides the cutscene file number above. One has been reached: `head` in [critter/AI.C](../src/critter/AI.C)'s `crit_drunkwalk`, which a wandering critter that is not in sequence 1 and whose laziness does not turn it takes as its new heading, so it snaps to whatever the stack held; the `walk` replay session reaches it (a critter in the first room), and the port, whose stack holds something else, went another way. It now has `STACK_JUNK(0)` (0 in the replay build and the port; DOS's own build still reads the stack). What the slot holds in DOS depends on the calls before and on the timer interrupts that push onto the same stack, so there is no one DOS value to read from the build; 0 is the value with which DOS takes the path it takes when the stack is clear.

The others are not reached by any of the eight recorded sessions, which cover the automap and a note, the inventory, a conversation, combat, saving and loading. Two checks show it. Branch coverage of the port over all eight (`tools/portbuild.py --coverage`) never takes the path on which each is read unset: `curx` in [ui/AUTOMAP.C](../src/ui/AUTOMAP.C)'s `ManageDungeonMap` is unset only when all 100 map notes are used (the items session writes one, so `curx` is set before it is read), `charges` in [obj/OBJUSE.C](../src/obj/OBJUSE.C) only when the object holds no cast-on-use spell (no session uses a charged object, so the function never runs), `count` in [ui/PANELS.C](../src/ui/PANELS.C) only when `DATA\weap.dat` cannot be opened (it is then passed to `gronk_gr` unset), and `file` in CUTS.C's `cutsop_data` (above) only for a value other than 996. And a port built with every uninitialised local filled with a pattern (`-ftrivial-auto-var-init=pattern`) replays all eight sessions with dumps identical to DOS's, so no other read-before-set local the sessions reach (clang's fifty "may be" reports included) changes the game's state. Each of the four still needs `STACK_JUNK` and a decision when a session reaches it. (`size` in [conv/BABL.C](../src/conv/BABL.C), on the list before, is the `READ_PAIR` case below and is set by the read.)

### Finding room near a spot: the clearing pass stops early, and the visited table is half cleared

- **What happens:** `find_good_x_and_y` searches outwards from a square, breadth first, for one where an object fits (a teleport's arrival, a critter or object put down). Two slips:
  - With `clear` set it deletes the objects in its way. After `Obj_Punt` removes one, the loop steps on through that object's own link (`link = &o->qn.link`), which the removal has just cleared, so the walk ends at the first object it deletes: at most one object per square is cleared on each visit.
  - `visited` is `uint16 visited[9]`, 18 bytes, but `memset(visited, 0, 9)` clears only the first 9, so the bits for the box's later columns start as whatever the stack held, and a square there may count as tried already. The box is 10 columns wide (`xmax = xmin + 9`), so a square in its last column sets `visited[9]`, one entry past the array, in the stack variable after it.
- **Where:** `find_good_x_and_y` in [event/WORLDEV.C](../src/event/WORLDEV.C).
- **Evidence:** code reading; both games (UW1Decomp `event/WORLDEV.C` has the same code). Not checked in the game.
- **Confidence:** likely.
- **Effect:** a search can give up, or pick a farther square, where a free one was nearer; and clearing a crowded square takes several visits. Which squares are skipped depends on the stack's leftover contents.
- **For a port:** `visited[9]` is a write past the array on the host's stack, not into DOS's neighbouring variable, so a port must give the array room for it (or model DOS's stack layout) to stay memory-safe; matching DOS's skipped squares also means modelling the uncleared bytes.

### A long step of the bin clock leaves a distant critter due for ever

- **What happens:** `move_mobile` advances the critter update clock by the frames `check_physics` counted, and updates each active mobile until `timetodo` says it is no longer due. `critter_ai` moves a critter that is far from both the player and the eye on by 8 bins (`(bin + 8) % 16`) each time it runs. When one step advances the clock by 9 bins or more (mod 16), a bin and the bin 8 after it can both be due (with `curBin` 1 and `lastbin` 8: bin 0 by the first test, bin 8 by the wrap), so such a critter alternates between them and `move_mobile` never returns.
- **Where:** `move_mobile`, `timetodo` and `critter_ai` in [critter/AI.C](../src/critter/AI.C); the step comes from `check_physics` in [motion/PLAYMOVE.C](../src/motion/PLAYMOVE.C).
- **Evidence:** a UW2 player's recording from the port, replayed in DOS's replay build and in the port: both hang in this loop at the same point, identical at every checkpoint before it. The step there was 25; earlier steps of 6, 15 and 23 in the same recording did no harm (their remainder mod 16 was below 9). UW1 has the same code.
- **Confidence:** confirmed for that input; unreachable in practice in DOS, whose clock moves a tick or so between `check_physics`' reads (a step of 5 at most).
- **Effect:** in DOS, none known. In a port whose game runs on a thread the host can stop for a second between those reads, the game freezes for good.
- **For a port:** cap the step at 8, which changes nothing DOS can reach (this port does, in `move_mobile`, and so does UW1's).

### A broken object's debris keeps its spell

- **What happens:** `remove_object` turns a destroyed object into debris by changing only its item number (`SET_ITEM`); the rest of the object stays, its enchantment included. An object that holds its spell itself, an is_quant object with the enchanted bit (an enchanted scroll or potion), so leaves debris that holds the same spell: `decode_obj_spell` finds it there as it did on the object, and `UseObj` ends every use with `checkSpell`, so using the debris casts it. Nothing spends it: `UseBook` and `UseFood` would have used the scroll or potion up, but debris is neither, and `useNSpellCharges` spends only a spell object's charges. So the debris casts the spell each time it is used, as often as the delay between casts from objects (0x2FD ticks) allows. Unlike UW1, a weapon has a branch of its own: it breaks into the broken dagger or its skill's broken weapon and keeps its contents, so a spell object among them stays on the broken weapon too. Other objects with contents have them freed (`Obj_FreeChain`).
- **Where:** `remove_object` in [combat/DAMAGE.C](../src/combat/DAMAGE.C); `UseObj`, `checkSpell` and `decode_obj_spell` in [obj/OBJUSE.C](../src/obj/OBJUSE.C); `UseBook` and `UseFood` in [obj/USEITEMS.C](../src/obj/USEITEMS.C).
- **Evidence:** code reading; both games. In UW1 the shipped levels reach it and the port shows it in play ([UW1's FINDINGS](../../uw1/docs/FINDINGS.md#a-broken-objects-debris-keeps-its-spell)); UW2's levels have not been searched for such objects, nor has it been played here.
- **Confidence:** likely.
- **Effect:** a player who breaks an enchanted scroll or potion gets debris that casts its spell for nothing, again and again, where the scroll or potion would have given it once.
- **For a port:** matching DOS keeps it; the optional enhancement `plain-debris` takes the spell off the debris, a broken weapon's spell object included ([ENHANCEMENTS.md](../../docs/ENHANCEMENTS.md)).

### Smaller slips with no known effect

- `scroll_print3` ([ui/SCROLL.C](../src/ui/SCROLL.C)) tests the last character of its text for a newline as `text[strlen(text) - 1]`, so for the empty string `scroll_wrap` passes it reads the byte before it: DS:098C, `scroll_esc`, only ever 0 or 1, so nothing happens in DOS. On a port the byte before the literal is the compiler's choice; one Linux build had a newline there and crashed writing to it, so the port skips the test for an empty string.
- `SetOffTrap` ([event/TRIGGER.C](../src/event/TRIGGER.C)) has no return statement. Its callers use the result, which is `UseTrap`'s, still in AX because the stores after the call do not touch AX. It works by accident. Three more work the same way: `check_inv_quality` ([conv/CONVERSE.C](../src/conv/CONVERSE.C)) leaves the quality in AX, `do_migrations` ([event/SCHEDULE.C](../src/event/SCHEDULE.C)) `Sched_Save`'s result, and `readlp` ([gfx/CUTS.C](../src/gfx/CUTS.C)) the byte count read (all four now end with `AX_RESULT` or `AX_LAST`, read from the code).
- BABL.C reads four bytes into `int block` in three places, and writes four from it in one, relying on Turbo C having put `size` right after `block` on the stack: each `bglobals.dat` record is the two words. It works in DOS by layout. The port gives the host the same two words through `READ_PAIR` and `WRITE_PAIR` (portable.h); without them the port wrote a 612-byte `BGLOBALS.DAT` where DOS writes 9928, which the items session's save comparison showed.
- `scroll_print` ([ui/SCROLL.C](../src/ui/SCROLL.C)) copies 49 bytes into a 47-byte local array and uses the 49th byte, which is another local (`sentinel`), as the copy's terminator; it works by Turbo C's frame layout (the port's stack protector stopped on it in a conversation). The port declares the array 49 bytes long and makes `sentinel` its last byte (`FRAME_LEN`, `FRAME_TAIL`).
- `mouse_getbut` ([ui/MOUSE.C](../src/ui/MOUSE.C)) stores its argument, a pointer, in `last_button` and returns it, where the buttons were evidently meant (it compiles to the original bytes as written). Its callers' loops (`mouse_dragged`) and `mouse_release`'s choice of button then see an address, not the buttons. Not checked in the game.
- GRLIBI.ASM's `shift_left_1` (for shadowed text) jumps through a table indexed by the bit buffer's length modulo 53; entries 0 and 53 point at 0 and at the byte before the shifting code, so a buffer of 53 bytes, or a multiple, would send it astray. The text rasteriser loops for ever on a glyph of width 0. Neither happens with the game's fonts as far as the replays reach.
- GRLIBL.ASM's `vcopy` transfer (`_581B`) always copies a last partial group with the right edge's mask after the whole groups, because its `je` tests flags from before the `rep movs`; for a rectangle ending at a group boundary that is one group (four pixels) beyond its right edge. The right-to-left copy (`_586D`) takes its edge mask by the pixel count instead of an x. Probably harmless where the game uses them; not checked.
- SPRITE.ASM accepts sprite numbers up to 40h (one past the last record, which addresses the redraw lists), and its overlap scan, after it queues an overlapping sprite, carries on from the start of the list it is scanning because it reused BX; some overlapping sprites may be queued late or twice. No effect seen.
- `seg004_0849_CB` ([3d/EXPAND.ASM](../src/3d/EXPAND.ASM)) compares DH with 0FFh and then overwrites the flags, so an unshaded 8-bit image would index past `lightabs`; probably no caller asks for one.
- `convert_string` ([conv/BABL.C](../src/conv/BABL.C)) does not check `bab_malloc`'s result. With the conversation heap full it would write the substituted text through a null far pointer over the interrupt vector table, and `add_to` would pass the null buffer to `bab_realloc`, which reads below it. The heap is sized so that this probably never happens.
- `get_pals` ([gfx/LOADGR.C](../src/gfx/LOADGR.C)) reads and writes through `PalStore`, which nothing sets, so it would use DS:0, an overlay stub entry; but only `.CR` art files reach it, and nothing loads one.
- In the FM music drivers (YAMAHA.INC, so the slips are Miles's): `update_priority` indexes `alt_voice` by the slot it steals from rather than by that slot's voice, when a four-operator OPL3 timbre takes two two-operator voices; and `release_sustain` releases each sustained note by its transposed note number (`S_note`) where `note_off` matches the key number (`S_keynum`), which differ only on the rhythm channel. Neither is reached: `UW.OPL` has no four-operator timbres, and the game never holds the sustain pedal on channel 10. The port's drivers do the same (Exhume's runtime/port/sound/yamaha.c).
- Three slips are in code nothing calls (see [Dead code](#dead-code)): `get_dist` ([3d/SPHERE.ASM](../src/3d/SPHERE.ASM)) adds the y term's sign correction to CX instead of SI; `seg020_1` ([gfx/PLANECPY.ASM](../src/gfx/PLANECPY.ASM)) puts the last column in the wrong byte for blocks starting at x & 3 of 2 or 3; `seg009_2CC` ([gfx/GRSPIC.C](../src/gfx/GRSPIC.C)) loses its result in the video memory branch.

### Candidates that did not hold up

- **`ATM_Strings` inside the area `clear_fbuf` wipes** (from [sys.md](subsystems/sys.md#open-questions)): not a bug. `clear_fbuf` clears `stdat` up to offset 69D4h at the start of every 3D frame, over the notes. But the notes are only held there between `GetTheWords` and `SaveTheWords`, which run on the map screen and inside `update_map_scraps`, where no 3D frame is drawn. The buffer is shared in time by design. Re-reading this found the compression problem above instead.
- **ACLZW's work area running past `stdat`**: not a bug in itself. `ac_setup_lzw` uses FFFFh bytes from `stdat`, through `ATM_Strings` and into `cmpbuf1_start` and `cmpbuf2_start` (the image decoders' scratch, [3d/EXPAND.ASM](../src/3d/EXPAND.ASM)), which follow it in the same order in both builds. Archive I/O and image decoding do not run at the same time. The only harm found is to the notes, above.
- **`do_fetchmap`'s `tmcolor`** stays in the list above, but with no effect, since nothing reads the value.

## 2. Game rules recovered from the code

Each rule links to the note that has the detail and the function names.

### Skill checks and experience

| Rule | Value | Detail |
|---|---|---|
| Skill check | `value - target + rand() % 31`: over 28 a critical (2), over 15 a success (1), over 2 a failure (0), else a bad failure (-1) | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Experience gain | halved (rounded at random), and halved again plus one above level 2 * world + 2 | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Levels | at 500 times 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192; experience is kept in tenths and capped at 0x7FFF0 | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Skill points | one per 1500 experience; a point raises a skill by 1 to 3 depending on the governing attribute, never above twice the attribute or 30 | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Derived values | hit points 30 + level * strength / 5; mana (mana skill + 1) * intelligence / 8; carrying capacity strength * 13 + 300 | [game.md](subsystems/game.md#rules-found-in-the-code) |
| Kill experience | 4 * exp + 2d(exp) from the creature record, 1.5 to 3 times that for a powerful critter | [combat.md](subsystems/combat.md#melee) |
| Exploration | newly mapped tiles are counted; after the frame, count * (level / 8 + 1) / 10 experience, while the character's level is 1 to 15 | [3d.md](subsystems/3d.md#automap-and-experience) |
| Death | a ninth of the experience; outside Britannia the player wakes by the blackrock gem, in Britannia the game ends, and a player killed by a castle guard on level 1 goes to jail | [game.md](subsystems/game.md#rules-found-in-the-code) |

### Melee

- To hit: `skill_check(attack skill + hit angle, defence)`, the hit angle 0 face to face up to 4 from behind; a critical multiplies the damage by 1 or 2. A bad miss can wear the player's weapon. ([combat.md](subsystems/combat.md#melee))
- Damage: `(d / 6)d6 + 1d(d % 6)`, scaled by the charged power / 128, plus the hit angle, less the armour at the hit location (5/3 of it for a powerful critter), and halved against the player on easy.
- The player's attack skill is Attack / 2 + the weapon skill + Valor + dexterity / 7 (+7 on easy); damage is the weapon's for the swing kind + strength / 9, or bare handed 2/5 of the skill + strength / 6 + 4. Weapon enchantments add damage (2e + 1, effects 0 to 3, named "Accuracy") or accuracy (2e - 7, effects 4 to 7, named "Damage"; see [the swapped names](#weapon-enchantments-of-accuracy-and-of-damage-do-each-others-job)); higher effects are special weapons.
- Armour enchantments (effects 0 to 7 "Protection", 8 to 15 "Toughness", each (e & 7) + 1): Protection is subtracted from an attacker's attack skill when the player is hit at that location, so blows miss more often; Toughness is added to the player's armour at that location, so blows that land do less damage. Neither changes how fast the item wears: the item-damage path (`DamageInventory`, `damage_item`) never reads an enchantment.
- Critters perceive by hearing and sight ranges scaled by the target's noise and visibility; in reach they strike one time in four; casters cast within 8 tiles with chance caster / 128; archers shoot within 4 tiles; they flee by the damage taken and their nerve. ([critters.md](subsystems/critters.md#rules-found-in-the-code))

### Damage types and resistances

| Bit | Type | Notes |
|---|---|---|
| 0x03 | magic | resisted by chance, `(resist & 3)` in 3 |
| 0x04 | physical | melee |
| 0x08 | fire | double against a cold-resistant target |
| 0x10 | poison | |
| 0x20 | cold | double against a fire-resistant target that does not resist cold |
| 0x40 | missiles | |
| 0x80 | undead | a resist bit only, used to tell the undead |

Any other resisted bit cancels the damage. Objects shift damage right by their toughness (class 3 is indestructible), doors wear down without breaking, and destroyed objects become debris by their kind. ([combat.md](subsystems/combat.md#damage))

### Casting

| Rule | Value |
|---|---|
| Circle | spell index / 8 + 1; 69 spells, the last five cast only by objects |
| Requirements | level at least 2 * circle - 1, mana at least 3 * circle; in Britannia only circles 1 to 3 |
| Roll | `skill_check(Casting, 3 * circle)`: 0 fails, -1 backfires for (circle / 2)d8, never below 3 hit points |
| Cooldown | (2 * circle - level) * 4 + 128 ticks; spells cast from objects once per 0x2FD ticks |
| Cost | paid at once, except missile and targeted spells, which pay on release and waive it if nothing is hit |
| Anti-magic | a tile's no-magic bit stops every spell |

World rules: mana cannot be restored by magic in the Scintillus Academy (world 5) outside parts of its first and eighth levels; Map Area, Locate and Roaming Sight fail in the Ethereal Void (world 8), where Tremor drops the floor's things instead of boulders. Every spell class, Repel Undead's budget, Charm's '+' test, Enchantment's tiers, Gate Travel's moonstones and the lines of power are in [combat.md](subsystems/combat.md#casting).

### Time and clocks

- `game_clock` counts 1/256 seconds, driven by a 256 Hz AIL timer. ([sound.md](subsystems/sound.md#the-pieces), [game.md](subsystems/game.md#rules-found-in-the-code))
- `duration_check` runs every 20 seconds of game time: spells and lights run down every call, poison every 3rd, hunger, sobering, wandering monsters and the critters' yearly check every 30th, and every 60th (20 minutes) the day clock moves one of its 72 steps and the day's schedule runs.
- Sleep lasts 7 to 10 hours in two parts; healing comes by fatigue and comfort; the dreams follow the castle plot (stages 4, 6, 10 and 14) or are chosen at random. ([game.md](subsystems/game.md#rules-found-in-the-code))
- A conversation setting X clock 0 moves the game clock 20 minutes a step. ([conversations.md](subsystems/conversations.md#built-ins))

### Schedules and triggers

- `SCD.ARK` has 16 schedules, one per X clock; each has rows sorted by time and a clock per level, and a level the player is away from catches up on his return. The row events are change goal, teleport (only where unseen), kill, set a quest bit, fire scheduled triggers, special cases, set attitude, set or test variables, and remove. ([events.md](subsystems/events.md#schedules))
- Traps, schedules and conversations share one numbering of variables: 0 to 0xFF the player's, 0x100 to 0x17F quest bits, 0x180 to 0x18F quest bytes, 0x190 to 0x19F the X clocks. ([events.md](subsystems/events.md#numbered-variables))
- A trigger runs its trap chain when the action's mode matches its entry in `Triggers[]`; id bits say who may set it off; one without `ID_FLAG10` is used up. Timer triggers fire every z + 1 ticks while the player is within 8 squares. ([events.md](subsystems/events.md#triggers-and-traps))
- Quests 0 to 127 are bits and 128 to 143 bytes; a conversation that sets a bit quest to a value other than 0 or 1 spills into its neighbours. ([conversations.md](subsystems/conversations.md#built-ins))

### Lights

- Light levels 0 to 7 come from `SHADES.DAT` and set the shading and the view's reach (vision radius 3 to 7); level 5 swaps in `MONO.DAT`'s grey maps. ([map.md](subsystems/map.md#light))
- A light burns only in the shoulder and hand slots and only as a single item; anywhere else it goes out. A used-up light (quality 1 or less) will not light; oil adds 0x20 quality. ([inventory.md](subsystems/inventory.md#rules-found-in-the-code), [objects.md](subsystems/objects.md#using-objects))
- Crossing onto a tile whose light flag differs recomputes the light level. ([map.md](subsystems/map.md#light))

### Ownership and races

- Taking an owned object angers up to 20 critters of the owning race within 7 tiles who can see it, each by one step; the object then belongs to nobody. Damaging an owned object also counts against the player. ([critters.md](subsystems/critters.md#rules-found-in-the-code), [combat.md](subsystems/combat.md#damage))
- Hitting a critter that is not a loner turns its race hostile within hearing for 0x200 clock units. While the player sleeps, each race's attitude drifts by the net count of its friendly and hostile members. ([critters.md](subsystems/critters.md#rules-found-in-the-code))
- A dead critter's loot belongs to its race; taking an owned book (0x138) is a crime against the humans. ([objects.md](subsystems/objects.md#loot), [ui.md](subsystems/ui.md#the-3d-view-and-the-panels))

### Inventory, objects and the map

- Reach: within a squared distance of 0x90 fine units and from 12 below to 24 above the player (doubled with a pole), along a line that neither climbs nor crosses into other terrain. ([ui.md](subsystems/ui.md#the-3d-view-and-the-panels))
- Stacking, container masks and capacities, wear, and the weight display (tenths of a stone, by inference) are in [inventory.md](subsystems/inventory.md#rules-found-in-the-code).
- Locks: `skill_check(Picklock, 3 * difficulty)`; difficulty 0xE needs a skill of 0x20, and 0xF cannot be picked. Food values, `CMB.DAT`'s five combinations and the rune traps are in [objects.md](subsystems/objects.md#using-objects).
- Bartering: an NPC's greed, patience and valuation error are seeded by its object index; an item is worth value * quantity * quality / 64; demands weigh level, readiness to fight, health and Charisma. ([conversations.md](subsystems/conversations.md#bartering))
- Map scraps: a scrap's map is kept as the automap of level 0x47 + its quality; reading it copies the sections it covers and gives a little experience for each new tile. ([ui.md](subsystems/ui.md#the-automap))
- Repair, fishing, the blackrock gem's facets, jail and the castle's day are in [events.md](subsystems/events.md#world-events).

## 3. Engine findings

- **Screen and frame buffer.** The game runs the VGA in mode X, and every routine reaches a row through a table built from the bottom up, so y counts up from the bottom of the screen. The 3D view is drawn into a linear frame buffer in `stdat`, also bottom-up, and copied to the screen afterwards. ([gfx.md](subsystems/gfx.md#the-screen), [3d.md](subsystems/3d.md#orientation))
- **The renderer.** Each frame the C turns the map into a bytecode program (the vision grid, then one tile at a time, farthest first), and seg004 runs it as threaded code: every handler jumps to the next through a table holding the same opcodes in the same order as FM Towns. Points are transformed as they are defined, with the eye position patched into the code, and a level view swaps in cheaper handlers. Floors and walls get exact perspective at the ends of each row or column and linear mapping between. Divide overflows are caught by redirecting int 0. A pick frame redraws the scene in identifying colours to find what is under the cursor. ([3d.md](subsystems/3d.md))
- **Sprite scaler.** Sprites are decoded into a buffer and drawn by a scaler generated as code for each sprite's width. ([3d.md](subsystems/3d.md#sprites-and-critters), [gfx.md](subsystems/gfx.md))
- **Registers between opcode handlers.** The model interpreter's handlers jump into each other with no central loop, and some read a register the previous handler left: `do_hull` reads DH (above), `do_bcompact_map` AX. A port that runs the renderer has to keep the registers from handler to handler as the CPU did, which is one reason the port translates seg004 instruction by instruction ([PORT.md](PORT.md#the-3d-renderer)).
- **Divide faults as control flow.** The renderer installs one of nine int 0 handlers before a divide that may overflow (by writing its offset at FD71:05A5): some saturate the quotient and resume after the `idiv`, which only works for the instruction length each assumes; SMOOTH.ASM's halves the dividend and divides again; PROJPOLY.ASM's decodes the faulting instruction and flags the polygon; and three drop the interrupt frame and jump into a clipping path. ([PORT.md](PORT.md#the-3d-renderer))
- **The record decoder restores its patch with the other encoding.** EXPAND.ASM's run-length decoder turns the first byte of L01A6 into a `ret` (C3h) for the recursive calls a repeat-count record makes, and writes 29h back afterwards, the first byte of `sub ax,ax` encoded as `29 C0`; the code as assembled has `2B C0`, the same instruction in its other encoding. So after the first image with a repeat-count record, the code segment holds a byte the EXE does not, and the program behaves the same. Found by the routine fuzzing (`tools/fuzzasm.py`), which compares every byte a routine changes; the port keeps the patch as a flag and does not write the byte. ([3d/EXPAND.ASM](../src/3d/EXPAND.ASM), [PORT.md](PORT.md#milestone-6a-results))
- **Critter page cache.** Critter animations are paged from `CRIT\CRnn.0p` into an EMS cache of 32 KB slots, loaded when a frame needs them, aged every frame and evicted least recently used. ([3d.md](subsystems/3d.md#sprites-and-critters), [critters.md](subsystems/critters.md#critter-art))
- **Keyboard.** The game's own int 9 handler stores raw scan codes in a 64-byte ring and never chains to the BIOS, so the BIOS sees no keys while the game runs. ([sys.md](subsystems/sys.md#input-and-time))
- **Debug keys.** `init_debug` ([sys/DEBUG.C](../src/sys/DEBUG.C)) runs at start-up in the shipped game and binds Ctrl+J to the joystick calibration and Alt with key code 83h to raising the COM1 interrupt in software ([sys/COM1INT.C](../src/sys/COM1INT.C)).
- **Mushroom lighting.** `random_light` fills the light maps with 4 KB from segment 0 (the interrupt vectors and the BIOS data area); FM Towns uses the start of its own data segment. ([map.md](subsystems/map.md#light))
- **Memory.** UW2 needs EMS 4.0. The four physical pages normally hold a critter page pair, an art page and a texture page, and `get_workspace` lends the whole frame as a 64 KB scratch area. `stdat` is shared in time by the frame buffer, the archive tables, LZSS, the cutscene player and the automap notes. ([sys.md](subsystems/sys.md#memory))
- **Overlays.** Most of the C runs as Borland VROOMM overlays from a 300h-paragraph buffer, kept in EMS through `_OvrInitEms`. ([sys.md](subsystems/sys.md#the-overlay-manager))
- **Sound.** Miles' AIL 2 owns int 8 and runs up to 16 timers; UW2 uses two, the 256 Hz game clock and a 16 Hz effects timer, and the music driver adds its own 120 Hz service. ([sound.md](subsystems/sound.md))
- **The sound drivers.** The `.ADV` files are John Miles' AIL 2.14 drivers, but not all as released. Assembling 2.14's public-domain sources with TASM 2.0 and comparing with the GOG files byte for byte shows: the digital drivers DD01, DD02 and DD03 are 2.14's SBDIG, SBPDIG and PASDIG exactly; the music drivers are built from an older XMIDI shell (before 1.10 of 14 October 1992, whose "new time signature function" counts beats and bars differently) and, for the FM cards (DM02, DM03, DM04, DM06, DM07), with YAMAHA.INC's `OSI_ALE` switch on, which includes Origin's time-variant effects (TVFX, `ALE.INC`, never released: 0530h..0ADAh of DM03.ADV). Every timbre in bank 1 of `UW.AD`, the MIDI sound effects, is a TVFX timbre: command lists that change the pitch, levels, feedback, multipliers and wave select of a voice 60 times a second. DM03 assembles from 2.14's YAMAHA.INC with a stand-in for ALE.INC byte for byte outside that range and the XMIDI differences. The API, `AIL.ASM`, is AIL 2.11 (its revision word 0D3h is 2.14's `current_rev` of 211) assembled without DIGPAK support: 2.14's AIL.ASM with `DIGPAK` false is 10 bytes longer, in the timer interrupt's guard against re-entry and in `AIL_register_timer`, which 2.12 changed. ([PORT.md](PORT.md#sound), [sound.md](subsystems/sound.md))
- **The Sound Blaster's digital driver has no volume.** SBDIG's `set_volume` only turns the DSP's speaker on, whatever the volume and pan, so with speech card 1 every digitised effect plays at full volume in the centre, however far away its source; only the Sound Blaster Pro and Pro Audio Spectrum drivers apply `sound_move`'s volume and pan (four bits a side on the Pro). ([PORT.md](PORT.md#sound))
- **UW2's introduction has no speech; only UW1's does.** The cutscene player's `say` opcode (`cutsop_say`, [gfx/CUTS.C](../src/gfx/CUTS.C)) plays `SOUND\BSP<voc>.VOC` when there is a speech card, and shows the line as a subtitle when there is none, when the sample fails, or when `voc` is 999 (998 is silence with a speech card). Every `say` in the introduction's scripts, `CUTS\CS000.N00` (17) and `CS002.N00` (8), has `voc` 999, so the introduction is subtitles only with any card: the port, like DOS, plays no sample there, and nothing is missing. UW1's introduction is spoken. UW2's eight speech files (`BSP00`, `BSP02` to `BSP07`, `BSP12`) belong to other cutscenes: `CS004` to `CS007` say 5, 6, 7 and 12, and `CS030` to `CS033` say 0, 2, 3 and 4 (read from the scripts with the opcode lengths of CUTS.C's `cuts_dispatch`; which cutscenes those are has not been checked in the game). Nobody need chase the introduction's silence.
- **Toolchain.** Turbo C++ 1.01, medium model, `-mm -1 -G -O -Y -d` (the switches vary by file, and each file records its own); Turbo Assembler 2.0 for the assembly, with seg004 in 386 code and the overlay manager showing MASM 5.1's encodings. ([MATCHING.md](MATCHING.md))
- **Link.** TLINK linked the program as `uwedit.exe` on 12 May 1993, with `C0.ASM` changed in three places. seg003, seg004 and seg021 were libraries of 14, 14 and 17 assembly modules, recovered from TLINK's padding and the relocation order. The relinked EXE is byte-identical. The code segments were in three classes: seg000 to seg002 in one that ends in `CODE`, seg003 and seg004 in one that does not end in upper-case `CODE` (which is why their code flags in the overlay segment table are 0), and everything else in `CODE`. ([LINKING.md](LINKING.md#segment-classes))
- **Shared code.** The FM Towns build was linked from the same object list in the same order and kept 3237 original names; System Shock's source release shares file names and functions with UW2 (`interp.asm`, `DAMAGE.C`, `GAMEWRAP.C` and others); `LZSS.C` is Okumura's 1989 code almost line for line; `AIL.ASM` is Miles' AIL 2.11 (see "The sound drivers" above). ([MAP.md](MAP.md#source-file-names))
- **Determinism.** Everything DOS UW2 reads from outside the program goes through a handful of places: the game clock, `key`, `mouse`, `mbuttons`, the joystick, `time()` and the seeds given to `srand`; with a sound card also the sound hardware's state (a sequence's or a digital buffer's status, a locked channel, a timbre's status), which the card changes behind the game's back, and the 16 Hz effects timer, game code that AIL calls from the timer interrupt (docs/PORT.md, "Sound": `SND_READ` and `SLAVE_TIMER`). Recorded there and replayed (docs/PORT.md, "The differential test"), a session runs the same way in DOS every time, to the byte in the player record, the level, the graphics library's data and all 256 KB of video memory, except where code reads a local before setting it (the attack check above), since the timer interrupts write onto the game's stack. Only machinery differs between runs: the graphics library's private stacks and the tick count its retrace wait keeps.
- **Layout.** Every address the assembly writes as a number, and the four DGROUP addresses that had to become names, are audited in [LAYOUT.md](LAYOUT.md).

## 4. Dead code and open questions

### Dead code

Routines nothing in the EXE calls, by the sources and the IDA listing:

- Graphics: POLYFILL.ASM's wall mapper (FM Towns' `wmap_bitmap`) and the routine after `L0582`; [LPFDELTA.ASM](../src/gfx/LPFDELTA.ASM), a run/skip/dump decoder; [PLANECPY.ASM](../src/gfx/PLANECPY.ASM); GRCORE.ASM's `seg003_0272_43F5` and the far string compare and copy at `5220`; GRDISP.ASM's `5363` and `536B`; GRSPIC.C's `seg009_2CC`; CUTS.C's `writeCutsValue_ovr108_2EAC` and `bufferPointer`. ([gfx.md](subsystems/gfx.md#open-questions))
- 3D: SPHERE.ASM's `get_dist`; INTERP.ASM's `seg004_0849_DB0`, `DD2`, `E11` and `F08`; SMOOTH.ASM's `smooth_over`; VIEW3D.C's `seg032_2E9B_18B` and `seg032_2E9B_195`. ([3d.md](subsystems/3d.md#open-questions))
- System: STUBS.C (seven stubs), STUBS2.C and EMPTY.C; EMS.C's three single-page routines; UTIL.C's `seg041_35D7_E9`; BIOSKEY.ASM's two thunks; CRITERR.ASM's `_diskerr` entry; TICKS.ASM's clock increment at +714; the joystick calibration routines `_375`, `_380` and `_38F` (no caller traced); SYSENTRY.ASM's far entries after `_809`. ([sys.md](subsystems/sys.md#open-questions))
- Sound: SOUND.C's `seg016_1E73_19DE`, `seg016_1E73_1BC3`, `toggle_music`, `fade_music`, `stop_speech`, `init_voc_playback` and `voc_stub`; AIL.ASM's `AIL_set_timer_divisor` and `AIL_release_driver_handle`. ([sound.md](subsystems/sound.md#open-questions))
- UI: BROWSE.C's seven hooks (empty in both builds); MOUSE.C's `flush_keys`; MAINMENU.C's `UnknownAutomapLoop_ovr147_A56`; INTERACT.C's `seg026_2716_F8A` and `seg026_2716_F98`; SCROLL.C's `seg043_3619_669`, which draws a framed box; AUTOMAP.C's `CornerShade_ovr094_9D2`. ([ui.md](subsystems/ui.md#open-questions))
- Game code: MAP.C's `OverwriteAllTiles_ovr128_37` and TEXTMAPS.C's `ovr140_4F` (probably leftovers of the level editor); OBJECTS.C's `UNREFERENCED_seg029_2A8E_B04`; EFFECT.C's `add_timer_obj`; INVDATA.C's `FindEmptySlot`; CRPAGES.C's `ovr117_2FD`; MOTION.C's `seg031_2CFA_A3F`; PHYSICS.C's `unreferenced_seg008_1B09_160` (a flash, a hit and a shake); BABL.C's `bab_nothing_ovr095_2296`; TRIGSAVE.C's `ovr162_0` (no caller found in the sources).
- Dead data: `npc_wit` in [conv/BARTER.C](../src/conv/BARTER.C) is computed and never read; `sound_b292` in [sound/SOUND.C](../src/sound/SOUND.C) is written and never read; LZSS's `flag0` is tested to no effect; `tmcolor` (above) is written and never read; opcode 0DAh of the model interpreter points at a data word in SMOOTH.ASM (`do_uwsetup`, which in FM Towns is the overflow handler itself), so no program can use it.
- Empty but called: `make_stew` (both builds); `seg021_22FD_C10` ([sys/SYSLIBP.ASM](../src/sys/SYSLIBP.ASM)), which `render_3d` calls on its own stack every frame; the exit hook at FD71:192h.

### Open questions

Merged from the notes, without repeating the open parts of the bugs above.

Answered while writing this page:

- **AIL's version, its version word 0D3h and offset 14h of the driver description** ([sound.md](subsystems/sound.md#open-questions)): AIL 2.11 without DIGPAK (above); 0D3h is 2.14's `current_rev`, 211, which a driver's `min_API_version` must not exceed; offset 14h is the description table's `service_rate`, the driver's periodic service in hertz (120 for the XMIDI drivers, -1 for the digital ones).
- **Levels 65 to 72** ([3d.md](subsystems/3d.md#open-questions)): `GRIDDB.C` tests `(PlayerLevel - 1) / 8 == 8`, so these are world 8, the Ethereal Void, drawn with no ceiling and no distance lighting. Why level 68 is excepted is still open.
- **What reads `tmcolor`** ([3d.md](subsystems/3d.md#open-questions)): nothing.
- **Whether floor heights 14 and 15 occur** ([map.md](subsystems/map.md#open-questions)): they do, on levels 25 and 65.
- **`BonesLook`'s "Relk." case** ([objects.md](subsystems/objects.md#open-questions)): the data has the skull it was written for.

Still open:

- Rendering: what `do_set_gmap_ctxt`, `do_defdelta` and the frame header's model variables 4, 8 and 9 do exactly; whether the words `set_cyb` overwrites are the texture masks; the geometry behind `do_door`'s drawing order; why `seg032_2E9B_195` toggles `SpecShadeMode`; whether the 3D models' bytecode uses `do_bcompact_map`. ([3d.md](subsystems/3d.md#open-questions))
- Graphics: the pens of VIDMODE.ASM's `_2FD6` and `_303B` and the third word of each screen-mode record; what `_2977`'s split-screen virtual screen serves besides the cutscenes. ([gfx.md](subsystems/gfx.md#open-questions))
- Cutscenes: whether cutscene 040 loads the right file in DOS, given `cutsop_data` (above). ([gfx.md](subsystems/gfx.md#cutscenes))
- Fields named by offset only: many `struct Creature` fields and critter flag bits (`bA_1`, `b0F`, `b2D_0`, `b19`) and the summoning exclusions 0x7B and 0x7C ([critters.md](subsystems/critters.md#open-questions), [combat.md](subsystems/combat.md#open-questions)); `struct Player`'s `b3C`, `b60_11` (set by Armageddon) and `b62_5` ([game.md](subsystems/game.md#open-questions)); `Phys.b1D`, `MotionParams.f21` and `Handler.w6` ([motion.md](subsystems/motion.md#open-questions)); the id bits `ID_FLAG9`, `ID_FLAG10` and `ID_FLAG11` ([objects.md](subsystems/objects.md#open-questions)); `struct Scroll`'s fields ([ui.md](subsystems/ui.md#open-questions)).
- Movement states: `player_sleep`'s motion states 1, 2 and 8 (probably swimming and falling), and bit 0x10 of `motion_state`, which is tested with swimming but never set by `newFPS`; the collision state bits have no original names. ([game.md](subsystems/game.md#open-questions), [motion.md](subsystems/motion.md#open-questions))
- Race numbering: whether the owner field and the `Creature` race number share a numbering, which the names given to races 0xB, 0x15, 0x17 (the liche), 0x1C and 6 assume. ([combat.md](subsystems/combat.md#open-questions), [events.md](subsystems/events.md#open-questions))
- Combat: which of `do_miss`'s effects 7 and 8 is which sound; what the missile `ammo` value -64 marks in `missile_newhit`. ([combat.md](subsystems/combat.md#open-questions), [motion.md](subsystems/motion.md#open-questions))
- Critters: what goal 11's fluttering is used for. ([critters.md](subsystems/critters.md#open-questions))
- Plot and places: what the player sees of the hack traps known only by their FM Towns names (`skup_ductosnore`, `do_qbert`); `do_filanium`'s link to the djinn quest (string 0x14C); which callers of `player_setup` drop the player from near the ceiling. ([events.md](subsystems/events.md#open-questions), [motion.md](subsystems/motion.md#open-questions))
- Time: whether `Sched_WrapTime` in PLAYTIME.C and `pass_time`'s `Sched_SetAllClocks` ever disagree about the day clock. ([game.md](subsystems/game.md#open-questions))
- Conversations: the order in which the scripts' source wrote a built-in's arguments. ([conversations.md](subsystems/conversations.md#open-questions))
- Objects and inventory: which container has which `Containers[]` mask; whether the food table's values are nourishment; what a book link of 0x100 or more meant (`make_stew`); the unit of weight; what using each kind of object from a slot does. ([inventory.md](subsystems/inventory.md#open-questions), [objects.md](subsystems/objects.md#open-questions))
- Map: why level 5 is the grey `MONO.DAT` level and what sets it; `Txm_Load` opening only the save directory's archive; `Map_Save`'s question, string 0x96. ([map.md](subsystems/map.md#open-questions))
- Automap: what the 0x20 and 0x30 kinds of a `PlayersMap` byte mean; what `player_newsq` takes from `PlayersMap` into `player->automap`. ([ui.md](subsystems/ui.md#open-questions), [motion.md](subsystems/motion.md#open-questions))
- Screen furniture: whether the gargoyle eyes' sequence reflects anything in the game. ([ui.md](subsystems/ui.md#open-questions))
- Sound: why effects 0x5A and 0x5B play as MIDI only with speech card 1; the unit behind the loop count of effects of length 5000 and more; the song titles of themes other than 2 to 6. ([sound.md](subsystems/sound.md#open-questions))
- System: who writes FD71:0120, which the keyboard layout choice and the joystick set-up read. ([sys.md](subsystems/sys.md#open-questions))
- Link: how the overlay manager sizes its buffer, and how far an overlay's code can grow ([LAYOUT.md](LAYOUT.md#limits-that-still-apply)); the owners of eight small unreferenced DGROUP gaps ([LINKING.md](LINKING.md#what-still-comes-from-your-exe)).
