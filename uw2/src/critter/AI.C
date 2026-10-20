/* target: seg007_17A2 */
/* opts: -mm -1 -G -O -Y -d */
/* Critter movement and AI: the whole of DOS resident segment seg007_17A2, in original
   order.

   What it does in the game: every moving thing in the level is stepped from here.
   move_mobile (called by PLAYMOVE.C's per-frame update while MoveCrits is set and time
   is not stopped) walks the active mobile list, and each object whose time bin has come
   round gets critter_ai (critters) or PATHFIND.C's move_me_joe (missiles and other
   mobile objects). critter_ai moves the critter through the physics engine, plays out
   attack, casting and dying sequences, and otherwise calls critter_mv, which notices
   being hit, chooses a goal and runs it.

   Goals (the goal word's low nibble, the switch in critter_mv): 0 and 7 stand still,
   1 go home, 2 wander (crit_drunkwalk), 3 nothing here (the critter is moved by other
   code; critter_ai never skips it for distance), 4 guard (crit_guard, also the goal
   critter_set_goal remembers in the old goal), 5 attack (crit_offense), 6 flee
   (crit_flee), 8 mill about near home (crit_mill), 9 fight back when cornered
   (crit_defense), 10 talk to the player (crit_talk), 11 flutter at random, 12 hover at
   home (crit_hover), 15 stand frozen while the goal target counts down. Other values
   only set a slow rate.

   Animation sequences (the 6-bit seq in byte 0x15, the 8 actions of CRIT\CR.AN): 0
   standing, 1 walking, 2 combat stance, 3 to 5 the three melee attacks, 6 casting or
   firing a missile (the fourth attack slot), 7 dying.

   A critter's "home" word (OBJ_HOMEX, OBJ_HOMEY) is the tile it stands on now; its home
   in the AI's sense, myxhome and myyhome, is kept in the quality and owner fields
   (set_critter_vars, critter_ai).

   Data owned: atk_charge (the strength of a blow by attack frame), the record of the
   last critter the player hit (crithit, typehit, crithittime, hitx, hity, hitpz, which
   CRITTIME.C saves into the player record), the time bins (curBin, lastbin), seqptr and
   seq_len for the current critter's animation, and lastcombattime. The per-critter
   scratch globals (meptr, mycst, myxpos ... tdistsqr) are PATHFIND.C's.

   Neighbours: PATHFIND.C supplies movement towards a square (crit_head_for_loc), line of
   sight and the physics handlers; COMBAT.C's critter_attack resolves a blow;
   MISSILE.C's critter_fire and SPELLS.C's cast fire missiles and spells; CRITTIME.C
   changes goals from outside; the conversation code is entered through TalkTo.

   Function and global names are the originals from the FM Towns symbol table where it
   has them.
   Name: inferred (the job of System Shock's AI.C: critter goals and AI, critter_ai,
   crit_attack). */

#include <dos.h>
#include <stdlib.h>
#include "combat.h"
#include "conv.h"
#include "critter.h"
#include "event.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

int16 lastXeye, lastYeye;               /* DS:2294, this file's _BSS (see below) */
/* match: MISSILE.C's missile_try ties with that file's statics missile_src and missile_arc
   (key 957), which a header cannot declare first, so it is declared only here. */
extern int16 missile_try;
int32 lastcombattime;                   /* DS:2280, this file's _BSS (see below) */

/* The charge of a critter's blow, by attack frame (the 4-bit attack frame in the word at
   0x0F, which crit_attack counts up while the critter stands in reach and critter_ai
   passes to critter_attack): 50 for an unwound blow up to 255 after 15 frames. */
/* name: static in FM Towns, so the name is provisional. */
/* match: a far variable, so its own segment (SEG0075_FAR, 609E:0000, segment table entry
   69, right after PATHFIND's four): only this file uses it. */
struct AtkCharge {
    unsigned char charge;
    char b1;
};
static struct AtkCharge far atk_charge[16] = {
    { 50, 0 }, { 60, 0 }, { 70, 0 }, { 80, 0 }, { 90, 0 }, { 100, 0 }, { 110, 0 }, { 120, 0 },
    { 130, 0 }, { 140, 0 }, { 155, 0 }, { 170, 0 }, { 185, 0 }, { 205, 0 }, { 230, 0 }, { 255, 0 } };

/* match: uninitialised data, DS:222C..2299. This block is one module's _BSS (Turbo C lays
   a module's _BSS out by name), and variables only seg006 uses (DS:222E, 2236, 2238,
   2240, 2244, 225A, 2272, 227E) sit between the ones below, so it belongs to seg006 or to
   this file and cannot be split. Until seg006 is matched it is declared here as extern;
   the names are the FM Towns ones. */

/* match: this file's _BSS, DS:2280..2299, laid out by name (tools/bssorder.py):
   lastcombattime 84, crithittime 139, hitx and hity 520, hitpz 552, curBin 555, victim
   574, seq_len 603, seqptr 659, lastXeye and lastYeye 820, seq_lframe 859 (Turbo C puts
   anything wider than a byte on an even offset, so DS:228F and DS:2299 are padding). */
/* The critter type last damaged (victim), and where (hitx, hity, hitpz) and when
   (crithittime, in game_clock units) the player last hit a critter. lastXeye and
   lastYeye are the tile the 3D view was last drawn from (VIEW3D.C). */
/* name: hitpz (DS:228A) is static in FM Towns (_seq_lframe+1 there, read by critter_mv_
   for set_loc beside _hitx and _hity), so it has no original name: provisional, chosen
   for its key. FM Towns' _hitz is another variable, seg024's combat height (DS:24CE). */
uint32 crithittime;
unsigned char hitx, hity;
static unsigned char hitpz;
signed char curBin;
struct Creature near *victim;
unsigned char seq_len;
unsigned char seq_lframe;

/* An animation sequence, 64 bytes, in the critter animation pages. */
struct Seq {
    char pad0[7];
    unsigned char len;                  /* 0x07 */
    char pad8[0x40 - 8];
};
struct Seq far *seqptr;



/* Initialised data, DS:C4: the time bin of the last pass over the mobile objects, and the
   critter the player last hit (its mobile index) and its race (struct Creature's race;
   0xFF for none). */
signed char lastbin = 0;
unsigned char crithit = 0;
unsigned char typehit = 0xFF;

/* Switch the current critter to animation sequence seq from its first frame, or if it is
   already in that sequence step to the next frame (always if force, else half the time). */
void far change_or_inc_seq(int seq, char force)
{
    if (OBJ_SEQ(meptr) != seq) {
        SET_SEQ(meptr, seq);
        SET_FRAME(meptr, 0);
    } else if (force || rand() % 2 == 0)
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % seq_len);
}

/* Face the current critter in one of the 8 directions: the 0-255 heading byte, the
   3-bit coarse heading and a zero fine heading. */
void far set_htx(int heading)
{
    meptr->heading = heading << 5;
    SET_HEADING(meptr, heading);
    SET_FINEHEAD(meptr, 0);
}

/* Goal 2, wandering, and the fallback when no path is found. A hostile critter turns to
   guarding half the time. Otherwise it switches between standing (seq 0) and walking
   (seq 1) at random, the creature's lazy value making it more likely to stand; a walker
   that bumped into something turns a quarter turn left or right, otherwise drifts up to
   45 degrees off its heading and steers round the player (crit_avoid_player). Fliers
   also pick a new pitch. Ends with check_out_player, which turns it to face a nearby
   player. */
void far crit_drunkwalk(void)
{
    unsigned char head STACK_JUNK(0);   /* read before it is set when the critter is not
                                           sequence 1 and its laziness does not turn it: see
                                           FINDINGS.md */
    unsigned char seq;
    unsigned char r;
    struct Tile far *tile;
    int pitch;
    int range;

    seq = 1;
    if (OBJ_B15_7(meptr)) {
        freepaths |= 1 << OBJ_PATH(meptr);
        SET_B15_7(meptr, 0);
    }
    tile = Map_GetAddr(myxpos, myypos);
    if (!control) {
        SET_RATE(meptr, 1);
        return;
    }
    if (OBJ_ATTITUDE(meptr) == 0 && rand() % 2) {
        crit_guard();
        return;
    }
    if (mycst->flier) {
        pitch = 0xE;
        range = 3;
        if (myzpos <= 0xE) {
            if (myzpos < tile->height + 2)
                pitch = 0x10;
            else
                range = 5;
        }
        SET_PITCH(meptr, pitch + rand() % range);
    }
    if (OBJ_SEQ(meptr) == 0) {
        r = rand() % 16;
        if (mycst->lazy <= r || OBJ_FRAME(meptr) != seq_lframe)
            seq = 0xFF;
    } else {
        r = rand() % 16;
        if (mycst->lazy < r && OBJ_FRAME(meptr) == seq_lframe)
            seq = 0;
    }
    if (OBJ_SEQ(meptr) != seq && seq != 0xFF) {
        SET_SEQ(meptr, seq);
        set_cur_seq_len();
        if (OBJ_FRAME(meptr) > seq_lframe)
            SET_FRAME(meptr, seq_lframe);
    }
    if (OBJ_SEQ(meptr) == 1) {
        if (failed && !aligned) {
            head = (meptr->heading + (rand() % 2 * 2 - 1) * 0x40 + 0x100) % 0x100;
            meptr->heading = head;
            SET_HEADING(meptr, head >> 5);
            SET_FINEHEAD(meptr, head);
            SET_SPEED(meptr, 0);
            return;
        }
        r = rand() % 0x40;
        if (r < mycst->lazy + 8)
            head = (meptr->heading + rand() % 0x40 + 0xE0) % 0x100;
        else
            head = meptr->heading;
        if (!aligned)
            head = crit_avoid_player(head, 10);
    } else {
        r = rand() % 0x80;
        if (mycst->lazy > r)
            head = (meptr->heading + rand() % 0x40 + 0xE0) % 0x100;
    }
    meptr->heading = head;
    SET_HEADING(meptr, head >> 5);
    SET_FINEHEAD(meptr, head);
    if (OBJ_SEQ(meptr) == 0) {
        SET_B15_6(meptr, 1);
        SET_SPEED(meptr, 0);
        SET_RATE(meptr, 6);
        if (rand() % 2)
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % seq_len);
    } else {
        SET_B15_6(meptr, 0);
        SET_SPEED(meptr, mycst->speed);
        SET_RATE(meptr, 4);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % seq_len);
    }
    check_out_player();
}

/* Goal 8, milling about: a hostile critter switches to guarding the player (goal 4);
   otherwise it wanders while within its creature's range of home (myxhome, myyhome) and
   heads back home when further. */
void far crit_mill(void)
{
    signed char dx;
    signed char dy;

    if (!control)
        return;
    if (OBJ_ATTITUDE(meptr) == 0 && OBJ_GOAL(meptr) != 4) {
        critter_set_goal(4, 1);
        return;
    }
    dx = myxhome - myxpos;
    dy = myyhome - myypos;
    if (dx * dx + dy * dy > mycst->range * mycst->range)
        crit_head_for_loc(myxhome, myyhome, Map_GetAddr(myxhome, myyhome)->height);
    else
        crit_drunkwalk();
}

/* Goals 0, 4 and 7, and the hostile branch of wandering. A hostile critter (attitude 0)
   watches the player: if it already knows where the player is (b19 bit 0) it attacks
   (goal 5); with probability alert/16 it looks (target_found), attacking if it sees or
   closely hears the player and, half the time, walking towards a sound further off. Then
   it does what its goal says: wander for goal 2, stand for goals 0 and 7, mill about for
   the rest. */
void far crit_guard(void)
{
    unsigned char found;
    unsigned char x;
    unsigned char y;

    if (!control)
        return;
    switch (OBJ_ATTITUDE(meptr)) {
    case 0:
        SET_GTARG(meptr, 1);
        set_up_target();
        if (OBJ_B19_0(meptr)) {
            critter_set_goal(5, 1);
            return;
        }
        if (OBJ_B19_1(meptr)) {
            if (rand() % 16 > mycst->alert)
                SET_B19_1(meptr, 0);
            else
                look_for_target(0);
        }
        if (rand() % 16 < mycst->alert) {
            found = target_found(&x, &y);
            switch (found) {
            case 0:
                SET_B19_0(meptr, 1);
                set_loc(x, y, tzpos);
                critter_set_goal(5, 1);
                return;
            case 2:
                SET_B19_1(meptr, 1);
                if (rand() % 2 == 0) {
                    crit_head_for_loc(x, y, tzpos);
                    return;
                }
                break;
            }
        }
    }
    switch (OBJ_GOAL(meptr)) {
    case 2:
        crit_drunkwalk();
        break;
    case 0:
    case 7:
        SET_RATE(meptr, 6);
        SET_SPEED(meptr, 0);
        change_or_inc_seq(0, 0);
        break;
    default:
        crit_mill();
    }
}

/* Goal 5, attacking the target set up by set_up_target. In melee reach (within 10 fine
   units, squared distance under 0x64, or on the target's tile, and within 4 height steps
   unless a flier) it fights hand to hand (crit_attack). Otherwise a caster tries a
   defensive spell or a spell attack and a critter whose first weapon is a missile weapon
   shoots; a successful ranged attack holds it in the combat stance. A critter that was
   guarding (old goal 4), has wandered more than twice its range from home and is not
   bound to fight (b19 bit 5) gives up and goes back to guarding. Otherwise it closes in
   (crit_offense_find_target). */
void far crit_offense(void)
{
    signed char dx;
    signed char dy;
    unsigned char attacked = 0;
    unsigned char how = 4;
    unsigned dist;
    unsigned homedist;

    if (!control)
        return;
    dist = tdx * tdx + tdy * tdy;
    dx = myxhome - myxpos;
    dy = myyhome - myypos;
    homedist = dx * dx + dy * dy;
    if (OBJ_GTARG(meptr) == 1)
        SET_ATTITUDE(meptr, 0);
    if ((dist < 0x64 || myxpos == txpos && myypos == typos)
        && (abs((signed char)myzpos - tzpos) < 4 || mycst->flier)) {
        crit_attack(dist);
        return;
    }
    if (mycst->caster > 0) {
        if (!maybe_cast_defensive_spell() && mycst->b2D_0)
            attacked = crit_magik_attack();
    } else if (mycst->arms[0].item >> 4 == CLASS_MISSILE)
        attacked = crit_missile_attack();
    if (attacked) {
        if (OBJ_SEQ(meptr) == 6 || OBJ_SEQ(meptr) == 6 || OBJ_SEQ(meptr) == 3)
            return;
        change_or_inc_seq(2, 1);
        SET_RATE(meptr, 4);
        SET_SPEED(meptr, 0);
        return;
    }
    if (dist > 0x100 && OBJ_OLDGOAL(meptr) == 4 && !OBJ_B19_5(meptr)
        && mycst->range * mycst->range * 4 < homedist) {
        SET_B19_0(meptr, 0);
        SET_B19_1(meptr, 0);
        critter_set_goal(4, 0);
        return;
    }
    crit_offense_find_target(txpos, typos, mycst->b2D_0 ? how : 1);
}

/* Melee manoeuvring: face the target, then by the squared fine distance back off
   (under 0x31: three times in four walk directly away, else sidestep), close in (over
   0x51), dodge in a random direction (chance dexterity/64) or stand. In reach (dist up to
   0x64) one time in four starts an attack, choosing sequence 3, 4 or 5 by the creature's
   attack probabilities (attacks[i].prob, percentages); otherwise the attack frame counts
   up, winding up a stronger blow (atk_charge). Fliers pitch towards the target's
   height. Always returns 1. */
unsigned char far crit_attack(unsigned dist)
{
    signed char dz;
    unsigned char head;
    int seq;
    int speed;
    int i;
    int r;

    head = deltatotheta(tdx, tdy);
    set_htx(head);
    SET_B15_6(meptr, 0);
    seq = 0x20;
    if (dist < 0x31) {
        if (rand() % 4 == 0) {
            head = (head + (rand() % 2 * 2 - 1) * 2 + 8) % 8;
            seq = 2;
            speed = mycst->speed * 2 / 3;
        } else {
            head = (head + 4) % 8;
            seq = 1;
            speed = 2;
        }
    } else if (dist > 0x51) {
        seq = 1;
        speed = 2;
    } else if (rand() % 0x40 < mycst->attr[1]) {
        head = rand() % 8;
        seq = 2;
        speed = 1;
    } else {
        seq = 2;
        speed = 0;
    }
    meptr->heading = head << 5;
    SET_SPEED(meptr, speed);
    if (OBJ_SEQ(meptr) != seq) {
        SET_SEQ(meptr, seq);
        SET_FRAME(meptr, 0);
    } else
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % seq_len);
    if (mycst->flier) {
        dz = OBJ_Z(mytarget) + 0xE - OBJ_Z(meptr);
        if (dz > 1)
            SET_PITCH(meptr, 0x12);
        else if (dz < -1)
            SET_PITCH(meptr, 0xE);
        else
            SET_PITCH(meptr, rand() % 3 + 0xF);
    }
    if (dist <= 0x64) {
        if (rand() % 4 == 0) {
            r = rand() % 100;
            for (i = 0; mycst->attacks[i].prob <= r && i < 2; i++)
                r -= mycst->attacks[i].prob;
            SET_SEQ(meptr, i + 3);
            SET_FRAME(meptr, 0);
        } else if (OBJ_ATKFRAME(meptr) < 0xF)
            SET_ATKFRAME(meptr, OBJ_ATKFRAME(meptr) + 1);
    }
    SET_RATE(meptr, 4);
    return 1;
}

/* Move towards the target's square (x, y). One time in eight, if the destination is out
   of date, the critter checks whether it still perceives the target (target_found) and
   either updates its destination or drops the goal. how is the stand-off distance in
   tiles: crit_offense passes 4 for critters with b2D_0 set, else 1, and the critter only
   heads in while further away than that. */
void far crit_offense_find_target(unsigned char x, unsigned char y, unsigned char how)
{
    unsigned char found;

    if ((OBJ_DESTX(meptr) != x || OBJ_DESTY(meptr) != y) && rand() % 8 == 0) {
        found = target_found(&x, &y);
        if (found == 0 || found == 2 && rand() % 2 == 0)
            set_loc(x, y, tzpos);
        else {
            SET_B19_0(meptr, 0);
            SET_B19_1(meptr, found == 1);
            critter_discard_goal();
        }
    }
    if (x == myxpos && y == myypos && abs((signed char)myzpos - tzpos) < 4)
        return;
    if (how > 1 && how * how < tdistsqr || how * how * 8 * 8 < tdisttsqr
        || how <= 1 && abs((signed char)myzpos - tzpos) >= 4) {
        crit_head_for_loc(x, y, tzpos);
        if (OBJ_B18_6(meptr)) {
            critter_discard_goal();
            SET_B19_1(meptr, 0);
        }
    }
}

/* Cast the creature's third spell (spells[2], 0xFF for none) with chance caster/256,
   unless an anti-magic area covers the critter: start the cast sequence (6) with cast
   slot 3. critter_ai casts it on frame 3. */
unsigned char far maybe_cast_defensive_spell(void)
{
    if (mycst->spells[2] != 0xFF && rand() % 0x100 < mycst->caster
        && !anti_magic_p(myxpos, myypos)) {
        SET_SPEED(meptr, 0);
        SET_SEQ(meptr, 6);
        SET_FRAME(meptr, 0);
        SET_CAST(meptr, 3);
        return 1;
    }
    return 0;
}

/* A spell attack: possible when no anti-magic covers the critter, the target is within
   8 tiles (squared tile distance under 0x40), in line of sight, and the critter has
   turned to face it (look_for_target(1)). Then with chance caster/128 it starts casting
   its first spell (11 times in 16) or its second. Returns 1 if a ranged attack was
   possible, so the caller holds the combat stance. */
unsigned char far crit_magik_attack(void)
{
    int r;

    if (anti_magic_p(myxpos, myypos) == 0 && tdistsqr < 0x40 && !anti_magic_p(myxpos, myypos)
        && line_of_sight(myxpost, myypost, OBJ_Z(meptr) + ComObjData[OBJ_ITEM(meptr)].height,
                         txpost, typost, OBJ_Z(mytarget) + ComObjData[OBJ_ITEM(mytarget)].height)
        && look_for_target(1)) {
        r = rand() % 0x80;
        if (mycst->caster > r) {
            SET_SPEED(meptr, 0);
            SET_SEQ(meptr, 6);
            SET_FRAME(meptr, 0);
            SET_CAST(meptr, rand() % 16 < 0xB ? 1 : 2);
        }
        return 1;
    }
    return 0;
}

/* A missile attack: the target within 4 tiles (squared distance under 0x10), in line of
   sight and faced; then with chance (dexterity+1)/192 the critter starts the firing
   sequence (6), and critter_ai fires on frame 3. Returns 1 if an attack was possible. */
unsigned char far crit_missile_attack(void)
{
    if (tdistsqr < 0x10
        && line_of_sight(myxpost, myypost, OBJ_Z(meptr) + ComObjData[OBJ_ITEM(meptr)].height,
                         txpost, typost, OBJ_Z(mytarget) + ComObjData[OBJ_ITEM(mytarget)].height)
        && look_for_target(1)) {
        if (rand() % 0xC0 <= mycst->attr[1]) {
            SET_SPEED(meptr, 0);
            SET_SEQ(meptr, 6);
            SET_FRAME(meptr, 0);
        }
        return 1;
    }
    return 0;
}

/* Goal 9, cornered: fight in melee when close (squared fine distance under 0x90),
   use spells or missiles from further away, or flee if the critter has neither; within
   2 tiles it just faces the target in its combat stance. */
void far crit_defense(void)
{
    unsigned char head;
    unsigned dist;

    if (!control)
        return;
    dist = tdx * tdx + tdy * tdy;
    if (dist < 0x90 || txpos == myxpos && myypos == typos) {
        crit_attack(dist);
        return;
    }
    if (tdistsqr > 4) {
        if (maybe_cast_defensive_spell())
            return;
        if (mycst->caster > 0)
            crit_magik_attack();
        else if (mycst->arms[0].item >> 4 == CLASS_MISSILE)
            crit_missile_attack();
        else
            crit_flee();
    } else {
        head = deltatotheta(tdx, tdy);
        SET_SPEED(meptr, 0);
        set_htx(head);
        SET_RATE(meptr, 4);
        change_or_inc_seq(2, 1);
    }
}

/* Goal 6, fleeing. Within about 1.7 tiles of the target (squared tile distance 3 or
   less) and near its height, the critter turns to fight (goal 9, setting b19 bit 4 so it
   keeps fighting) with chance 1 in 256 if its nerve (b1C_0, 4 bits) is 8 or more, or
   at once if it is stuck;
   otherwise it backs away at half speed. A stuck critter further away either fights
   back (within 3 tiles) or turns 90 degrees. Otherwise it tries a defensive spell, then
   runs, drifting at random and steering round the player, at its run speed within 8
   tiles and its walking speed beyond. */
void far crit_flee(void)
{
    unsigned char head;
    unsigned char r;
    unsigned char newh;
    signed char dz;
    int pitch;

    if (!control)
        return;
    head = deltatotheta(tdx, tdy);
    dz = OBJ_Z(mytarget) - OBJ_Z(meptr);
    if (mycst->flier) {
        pitch = 0;
        if (OBJ_Z(meptr) <= 0x6E)
            pitch = 2;
        SET_PITCH(meptr, pitch + rand() % 5 + 0xD);
    }
    if (tdistsqr <= 3 && abs(dz) < 0x10) {
        if (rand() % 0x100 < mycst->b1C_0 >> 3 || failed && !aligned) {
            SET_B19_4(meptr, 1);
            critter_set_goal(9, OBJ_GTARG(meptr));
            return;
        }
        meptr->heading = (head + 4) % 8 << 5;
        SET_HEADING(meptr, head);
        SET_FINEHEAD(meptr, 0);
        change_or_inc_seq(1, 1);
        SET_SPEED(meptr, (mycst->speed + 1) / 2);
        return;
    }
    if (failed && !aligned) {
        if (tdistsqr < 9) {
            if (OBJ_GOAL(meptr) == 9) {
                SET_SPEED(meptr, 0);
                set_htx(head);
                SET_RATE(meptr, 4);
                change_or_inc_seq(2, 1);
                return;
            }
            SET_B19_4(meptr, 1);
            critter_set_goal(9, OBJ_GTARG(meptr));
            return;
        }
        newh = (((meptr->heading >> 5) + (rand() % 2 * 2 - 1) * 2 + 8) % 8 << 5) + rand() % 0x20;
    } else {
        if (maybe_cast_defensive_spell())
            return;
        r = rand() % 0x40;
        if (r < mycst->lazy + 8)
            newh = (meptr->heading + rand() % 0x40 + 0xE0) % 0x100;
        else
            newh = meptr->heading;
        if (!aligned)
            newh = crit_avoid_player(newh, 0x18);
        /* match: the comparison survives, but its if has no body. */
        if (meptr->heading != newh)
            ;
    }
    meptr->heading = newh;
    SET_HEADING(meptr, newh >> 5);
    SET_FINEHEAD(meptr, newh);
    if (tdistsqr < 0x40)
        SET_SPEED(meptr, mycst->run);
    else
        SET_SPEED(meptr, mycst->speed);
    change_or_inc_seq(1, 1);
    SET_RATE(meptr, 4);
}

/* Steer a heading (0-255) away from the player when the player is within how fine
   units (8 to a tile): a heading already more than 90 degrees from the player is kept,
   others are bent by 45 degrees away. Returns the new heading. */
unsigned char far crit_avoid_player(unsigned char heading, int how)
{
    struct Object far *plyr;
    int px;
    int dx;
    int dy;
    unsigned char th;
    unsigned char newh;
    unsigned char diff;
    register int py;
    register unsigned dist;

    plyr = Obj_IntTMem(1);
    px = (OBJ_HOMEX(plyr) << 3) + OBJ_FINEX(plyr);
    py = (OBJ_HOMEY(plyr) << 3) + OBJ_FINEY(plyr);
    dx = px - myxpost;
    dy = py - myypost;
    dist = dx * dx + dy * dy;
    if (how * how > dist) {
        th = (deltatotheta(dx, dy) + 4) % 8 << 5;
        diff = (th + 0x100 - heading) % 0x100;
        if (diff < 0x40 || diff > 0xC0)
            newh = heading;
        else if (diff < 0x60)
            newh = (th + 0xE0) % 0x100;
        else if (diff < 0x80)
            newh = (heading + 0x20) % 0x100;
        else if (diff > 0xA0)
            newh = (th + 0x20) % 0x100;
        else
            newh = (heading + 0xE0) % 0x100;
        return newh;
    }
    return heading;
}

/* Goal 10, wanting to talk. A hostile critter turns to guarding instead. Otherwise it
   stands; if it perceives the player within 2.5 tiles it turns to face them, and within
   1.5 tiles, if the player faces it (the player's heading within 45 degrees of facing
   the critter), it starts the conversation (TalkTo). The conversation maps other EMS
   pages, so the critter pages are mapped back afterwards. */
void far crit_talk(void)
{
    unsigned char head;
    signed char rel;
    unsigned char found;
    unsigned char x;
    unsigned char y;
    unsigned dist;

    if (!control)
        return;
    if (OBJ_ATTITUDE(meptr) == 0 && OBJ_GOAL(meptr) != 4) {
        critter_set_goal(4, 1);
        return;
    }
    SET_GTARG(meptr, 1);
    set_up_target();
    dist = tdx * tdx + tdy * tdy;
    found = target_found(&x, &y);
    SET_RATE(meptr, 6);
    SET_SPEED(meptr, 0);
    change_or_inc_seq(0, 0);
    if (found == 1)
        return;
    if (dist < 0x190) {
        head = deltatotheta(tdx, tdy);
        set_htx(head);
        if (dist < 0x90) {
            rel = OBJ_HEADING(ThePlayer) + 8 - head & 7;
            if (rel >= 3 && rel <= 5) {
                pmouseHandled = 0;
                mouse_freereign();
                TalkTo(meptr);
                map_crit_pages();
                seqptr = MK_FP(EmsBuff + 0xC00, 0);
            }
        }
    }
}

/* A standing critter, or any critter while the player has a weapon drawn, turns to face
   the player and stands when the player is within 1.5 tiles. */
void far check_out_player(void)
{
    unsigned char head;
    unsigned dist;

    if (OBJ_SPEED(meptr) <= 0 || player->drawn) {
        SET_GTARG(meptr, 1);
        set_up_target();
        dist = tdx * tdx + tdy * tdy;
        if (dist < 0x90) {
            head = deltatotheta(tdx, tdy);
            SET_SPEED(meptr, 0);
            SET_RATE(meptr, 6);
            change_or_inc_seq(0, 0);
            SET_HEADING(meptr, head);
            SET_FINEHEAD(meptr, 0);
        }
    }
}

/* Goal 12: go home and stand there. Hostile critters turn to guarding. */
void far crit_hover(void)
{
    signed char dx;
    signed char dy;

    if (!control)
        return;
    dx = myxhome - myxpos;
    dy = myyhome - myypos;
    if (OBJ_ATTITUDE(meptr) == 0 && OBJ_GOAL(meptr) != 4)
        critter_set_goal(4, 1);
    else if (dx != 0 || dy != 0)
        crit_head_for_loc(myxhome, myyhome, Map_GetAddr(myxhome, myyhome)->height);
    else {
        SET_RATE(meptr, 6);
        SET_SPEED(meptr, 0);
        change_or_inc_seq(0, 0);
    }
}

/* Does the current critter perceive its target? Sets *x and *y to the target's tile.
   Hearing range is the critter's hearing times the target creature's noise over 16,
   sight range its sight times the target's visibility over 16 (in tiles). Returns 0
   when the target is within half the hearing range, or within sight range, within 45
   degrees of the way the critter faces and in line of sight (setting b19 bit 0, "knows
   where the target is"); 2 when it is within twice the hearing range (heard faintly);
   1 when not perceived (clearing bit 0). */
unsigned char far target_found(unsigned char *x, unsigned char *y)
{
    signed char dx;
    signed char dy;
    signed char th;
    signed char myh;
    signed char rel;
    int seen;
    register int dist;
    register int heard;

    *x = txpos;
    *y = typos;
    dx = txpos - myxpos;
    dy = typos - myypos;
    dist = dx * dx + dy * dy;
    heard = mycst->hearing * Creature[OBJ_INMAJOR_NOSHIFT(mytarget)].noise / 16
          * (mycst->hearing * Creature[OBJ_INMAJOR_NOSHIFT(mytarget)].noise / 16);
    if (heard / 4 > dist)
        return 0;
    seen = mycst->sight * Creature[OBJ_INMAJOR_NOSHIFT(mytarget)].visibility / 16
         * (mycst->sight * Creature[OBJ_INMAJOR_NOSHIFT(mytarget)].visibility / 16);
    if (dist <= seen) {
        th = deltatotheta(dx, dy);
        myh = OBJ_HEADING(meptr);
        rel = (th - myh + 8) % 8;
        if ((rel == 0 || rel == 1 || rel == 7)
            && line_of_sight(myxpost, myypost, OBJ_Z(meptr) + ComObjData[OBJ_ITEM(meptr)].height,
                             txpost, typost, OBJ_Z(mytarget) + ComObjData[OBJ_ITEM(mytarget)].height)) {
            SET_B19_0(meptr, 1);
            return 0;
        }
    }
    if (heard * 4 > dist)
        return 2;
    SET_B19_0(meptr, 0);
    return 1;
}

/* Turn towards the target. With how set, turn smoothly (turn_real_fine) and return 1
   once facing it; otherwise step the 3-bit heading one eighth towards it and return 1
   only if it already faced it. */
unsigned char far look_for_target(char how)
{
    signed char dx;
    signed char dy;
    signed char th;
    signed char myh;
    signed char rel;

    dx = txpost - myxpost;
    dy = typost - myypost;
    th = deltatotheta(dx, dy);
    myh = OBJ_HEADING(meptr);
    rel = (th - myh + 8) % 8;
    if (how)
        return turn_real_fine(dx, dy);
    if (rel == 0)
        return 1;
    if (rel <= 4)
        SET_HEADING(meptr, OBJ_HEADING(meptr) + 1);
    else
        SET_HEADING(meptr, OBJ_HEADING(meptr) - 1);
    return 0;
}

/* Limit how far a critter turns in one AI step: its facing to 45 degrees (0x20) either
   side of where it faced before the step, and, when it was and is moving at speed above
   1, its heading of travel likewise; a turn of more than 90 degrees stops it. A critter
   whose physics step changed its heading (aligned) keeps the old heading. */
/* name: IDA's NPC_Move. Named from FM Towns: constrain_movement_ follows
   look_for_target_ there and makes the same limits of the new facing and heading to 0x20
   either side of myoldfacing and myoldheading; critter_mv calls it last, as here. */
void far constrain_movement(void)
{
    unsigned char oldh;
    unsigned char newf;
    register unsigned char diff;

    oldh = meptr->heading;
    newf = (OBJ_HEADING(meptr) << 5) + OBJ_FINEHEAD(meptr);
    diff = (newf + 0x100 - myoldfacing) % 0x100;
    if (diff >= 0x20 && diff <= 0xE0) {
        if (diff < 0x80)
            newf = (myoldfacing + 0x20) % 0x100;
        else
            newf = (myoldfacing + 0xE0) % 0x100;
    }
    SET_HEADING(meptr, newf >> 5);
    SET_FINEHEAD(meptr, newf);
    if (aligned)
        meptr->heading = myoldheading;
    else if (myoldspeed > 1 && OBJ_SPEED(meptr) > 1) {
        diff = (oldh + 0x100 - myoldheading) % 0x100;
        if (diff < 0x20 || diff > 0xE0)
            meptr->heading = oldh;
        else if (diff < 0x40)
            meptr->heading = (myoldheading + 0x20) % 0x100;
        else if (diff > 0xC0)
            meptr->heading = (myoldheading + 0xE0) % 0x100;
        else {
            SET_SPEED(meptr, 0);
            meptr->heading = myoldheading;
        }
    }
}

/* Turn the critter's facing towards the vector (dx, dy) by at most 45 degrees (0x20 of
   256); returns 1 once it faces that way. */
/* name: IDA's TurnTowardsVector. Named from FM Towns: turn_real_fine_ comes next there,
   is called by look_for_target_, and makes the same cSqRt of tdx and tdy, cAtan2 and
   turn of 0x20 at most. */
unsigned char far turn_real_fine(signed char dx, signed char dy)
{
    int32 d2;
    unsigned ang;
    unsigned char cur;
    unsigned char want;
    unsigned char diff;
    unsigned char newf;
    int sy;
    int32 ly;
    int32 lx;
    unsigned char done = 0;
    register unsigned dist;
    register int sx;

    cur = (OBJ_HEADING(meptr) << 5) + OBJ_FINEHEAD(meptr);
    d2 = tdx * tdx + tdy * tdy;
    dist = cSqRt(d2);
    ly = dx;
    lx = dy;
    if (dist == 0)
        return 1;
    if (lx == dist)
        sy = 0x7FFF;
    else if (-lx == dist)
        sy = 0x8000;
    else
        sy = (lx << 15) / dist;
    if (ly == dist)
        sx = 0x7FFF;
    else if (-ly == dist)
        sx = 0x8000;
    else
        sx = (ly << 15) / dist;
    ang = cAtan2(sy, sx);
    want = 0x40 - (ang >> 8) & 0xFF;
    diff = want - cur;
    if (diff < 0x20 || diff > 0xE0) {
        newf = want;
        done = 1;
    } else if (diff < 0x80)
        newf = (cur + 0x20) % 0x100;
    else
        newf = (cur + 0xE0) % 0x100;
    SET_HEADING(meptr, newf >> 5);
    SET_FINEHEAD(meptr, newf);
    return done;
}

/* The vertical aim for a critter's spell or missile, put in missile_try before the
   shot: the height difference to the target over the distance, times 4, clamped to
   -15..15. With flag set and a speed, it adds dist * 3 / speed (lobbing a slow missile
   higher at range). critter_ai passes speed 0x1E and no flag for spells, the missile
   type and flag 1 for missiles. */
signed char far compute_trz_or_try_rather(unsigned speed, unsigned char flag)
{
    int trz;
    register int dist;
    register int dz;

    set_up_target();
    dz = OBJ_Z(mytarget) - OBJ_Z(meptr);
    dist = cSqRt(tdisttsqr);
    if (dist == 0)
        return dz > 0 ? 0xF : 0xF1;
    trz = (dz << 2) / dist;
    if (trz > 0xF)
        trz = 0xF;
    if (trz < -0xF)
        trz = -0xF;
    if (!flag || speed == 0)
        return trz;
    trz += dist * 3 / speed;
    return trz;
}

/* Load the per-critter globals (meptr, myid, mycst, positions, home, old heading and
   speed, height) for obj, and choose its physics record and handler by how it moves:
   CN2/CT2 fliers, CN4/CT4 swimmers, CN1/CT1 walkers. Used by code outside the AI loop
   (CRITTIME.C) before it works on a critter. */
void far set_critter_vars(struct Object far *obj)
{
    meptr = obj;
    myid = Obj_MemTPtr(meptr);
    mycst = &Creature[OBJ_INMAJOR(meptr)];
    myxpos = OBJ_HOMEX(meptr);
    myypos = OBJ_HOMEY(meptr);
    myzpos = OBJ_Z(meptr) >> 3;
    myxpost = (myxpos << 3) + OBJ_FINEX(meptr);
    myypost = (myypos << 3) + OBJ_FINEY(meptr);
    myxhome = meptr->qn.f.quality;
    myyhome = meptr->ol.f.owner;
    myoldheading = meptr->heading;
    myoldfacing = (OBJ_HEADING(meptr) << 5) + OBJ_FINEHEAD(meptr);
    myoldspeed = meptr->b13 & 0x7F;
    myheight = ComObjData[OBJ_ITEM(meptr)].height;
    if (mycst->flier) {
        pn_act = &CN2;
        tp_act = &CT2;
    } else if (mycst->swims) {
        pn_act = &CN4;
        tp_act = &CT4;
    } else {
        pn_act = &CN1;
        tp_act = &CT1;
    }
}

/* seq_len and seq_lframe: the frame count of the current critter's sequence, read from
   CR.AN in the EMS page (seqptr; 8 sequences of 64 bytes per animation page, the page
   from grs_3dinf). */
void far set_cur_seq_len(void)
{
    register int page;

    page = grs_3dinf[OBJ_INMAJOR_NOSHIFT(meptr)].page;
    seq_len = seqptr[(page << 3) + OBJ_SEQ(meptr)].len;
    seq_lframe = seq_len - 1;
}

/* One AI step for meptr. A critter more than 10 tiles from both the view and the player
   is skipped for half a cycle (its bin moves on 8), unless its goal is 3. Otherwise its
   motion is run through the physics engine (do_crit_phys), with lava (collision bit
   0x20, a footprint corner on lava) ignored by creatures whose ComObjData resist has bit
   8 (probably fire resistance). Then by
   sequence: a dying critter at its last frame is removed, its inventory generated and
   dropped and its corpse built (returns 0, so move_mobile revisits the slot); an attack
   sequence strikes on frame 3 (critter_attack with a random swing kind 0-8, the charge
   from atk_charge and the creature's b0F as poison) and starts combat music if the
   player is the target; the casting sequence casts or fires on frame 3; any other
   sequence goes to critter_mv. Finally the bin advances by the rate. */
unsigned char far critter_ai(void)
{
    struct Object far *plyr;
    signed char px;
    signed char py;
    int eyedist;
    unsigned char oldh;
    register int type;
    register int pdist;

    myid = Obj_MemTPtr(meptr);
    mycst = &Creature[OBJ_INMAJOR(meptr)];
    myxpos = OBJ_HOMEX(meptr);
    myypos = OBJ_HOMEY(meptr);
    plyr = Obj_IntTMem(1);
    px = OBJ_HOMEX(plyr);
    py = OBJ_HOMEY(plyr);
    eyedist = ((signed char)myxpos - lastXeye) * ((signed char)myxpos - lastXeye)
            + ((signed char)myypos - lastYeye) * ((signed char)myypos - lastYeye);
    pdist = ((signed char)myxpos - px) * ((signed char)myxpos - px)
          + ((signed char)myypos - py) * ((signed char)myypos - py);
    if (eyedist > 0x64 && pdist > 0x64 && OBJ_GOAL(meptr) != 3) {
        SET_BIN(meptr, (OBJ_BIN(meptr) + 8) % 16);
        return 1;
    }
    if (mycst->flier) {
        pn_act = &CN2;
        tp_act = &CT2;
    } else if (mycst->swims) {
        pn_act = &CN4;
        tp_act = &CT4;
    } else {
        pn_act = &CN1;
        tp_act = &CT1;
    }
    if (ComObjData[OBJ_ITEM(meptr)].resist & 8) {
        tp_act->mask &= ~0x20;
        tp_act->w6 &= ~0x20;
        tp_act->ignore |= 0x20;
    }
    failed = 0;
    control = 1;
    hitwall = 0;
    aligned = 0;
    didhitobj = 0;
    hitadoor = 0;
    dontchangedz = 0;
    if (OBJ_SEQ(meptr) != 1 && OBJ_SEQ(meptr) != 0 && OBJ_B15_7(meptr)) {
        freepaths |= 1 << OBJ_PATH(meptr);
        SET_B15_7(meptr, 0);
    }
    if (!OBJ_B15_6(meptr) || OBJ_SPEED(meptr) != 0 || OBJ_PITCH(meptr) != 0x10) {
        get_phys_data(meptr, pn_act);
        oldh = meptr->heading;
        crit_terr = get_terrain(meptr);
        do_crit_phys(pn_act, tp_act);
        XP = OBJ_HOMEX(meptr);
        YP = OBJ_HOMEY(meptr);
        set_phys_data(meptr, pn_act);
        if (meptr->heading != oldh)
            aligned = 1;
    }
    if (ComObjData[OBJ_ITEM(meptr)].resist & 8) {
        tp_act->mask |= 0x20;
        tp_act->w6 |= 0x20;
        tp_act->ignore &= ~0x20;
    }
    myxpos = OBJ_HOMEX(meptr);
    myypos = OBJ_HOMEY(meptr);
    myzpos = OBJ_Z(meptr) >> 3;
    myxpost = (myxpos << 3) + OBJ_FINEX(meptr);
    myypost = (myypos << 3) + OBJ_FINEY(meptr);
    myxhome = meptr->qn.f.quality;
    myyhome = meptr->ol.f.owner;
    myoldheading = meptr->heading;
    myoldfacing = (OBJ_HEADING(meptr) << 5) + OBJ_FINEHEAD(meptr);
    myoldspeed = meptr->b13 & 0x7F;
    myheight = ComObjData[OBJ_ITEM(meptr)].height;
    set_cur_seq_len();
    if (OBJ_GOAL(meptr) == 0xB || OBJ_GOAL(meptr) == 3)
        critter_mv();
    else if (OBJ_SEQ(meptr) == 7) {
        if (OBJ_FRAME(meptr) == seq_lframe) {
            death_check(meptr, 1);
            XP = OBJ_HOMEX(meptr);
            YP = OBJ_HOMEY(meptr);
            if (Obj_Rem(&Map_GetAddr(XP, YP)->objects, meptr)) {
                generate_inventory(meptr);
                build_corpse(meptr, mycst->corpse, mycst->remains);
                drop_some_objects(meptr);
                Obj_Free(meptr);
                return 0;
            }
        } else
            SET_FRAME(meptr, OBJ_FRAME(meptr) + 1);
    } else if (OBJ_SEQ(meptr) >= 3 && OBJ_SEQ(meptr) <= 5) {
        if (OBJ_FRAME(meptr) == 0 && OBJ_GTARG(meptr) == 1) {
            if (get_current_music() < MUSIC_FOE_HURT || get_current_music() > MUSIC_DANGER)
                set_new_music(MUSIC_COMBAT);
            lastcombattime = GAME_TIME();
        }
        if (OBJ_FRAME(meptr) == 3)
            critter_attack(meptr, rand() % 9, atk_charge[OBJ_ATKFRAME(meptr)].charge, OBJ_SEQ(meptr) - 3,
                           mycst->b0F);
        if (OBJ_FRAME(meptr) == seq_lframe) {
            SET_SEQ(meptr, 2);
            SET_FRAME(meptr, 0);
            SET_ATKFRAME(meptr, 0);
        } else
            SET_FRAME(meptr, OBJ_FRAME(meptr) + 1);
    } else if (OBJ_SEQ(meptr) == 6) {
        if (OBJ_FRAME(meptr) == 3) {
            if (OBJ_CAST(meptr)) {
                missile_try = compute_trz_or_try_rather(0x1E, 0);
                cast(mycst->spells[OBJ_CAST(meptr) - 1], meptr, 0L);
            } else {
                type = mycst->arms[0].item & ID_INCLASS;
                missile_try = compute_trz_or_try_rather(Missile[type].type, 1);
                critter_fire(meptr, type, Missile[type].type);
            }
        }
        if (OBJ_FRAME(meptr) == seq_lframe) {
            SET_SEQ(meptr, 2);
            SET_FRAME(meptr, 0);
            SET_CAST(meptr, 0);
            SET_ATKFRAME(meptr, 0);
        } else
            SET_FRAME(meptr, OBJ_FRAME(meptr) + 1);
    } else
        critter_mv();
    SET_BIN(meptr, (OBJ_BIN(meptr) + OBJ_RATE(meptr)) % 16);
    return 1;
}

/* Decide and run the current critter's goal. Walking critters play a footstep sound on
   odd frames (by the creature's sound kind). Unless the creature ignores fights
   (bA_1), it joins a fight when, within 0x200 game_clock units of the player hitting a
   critter of its own race (or, for the player's allies, any critter) and within its
   hearing of that place, it turns hostile and attacks. When it has been hit itself
   (last_hit: 1 the player, or another critter if allies are involved) it attacks the
   attacker, or flees if should_i_flee says so, or fights on if it was cornered before.
   Then the goal switch (see the file header) and constrain_movement. */
void far critter_mv(void)
{
    unsigned char targeted = 0;
    unsigned char snd;

    didmove = 0;
    SET_B18_5(meptr, 0);
    SET_B15_6(meptr, 0);
    if (OBJ_GOAL(meptr) == 0xB || OBJ_GOAL(meptr) == 0xF)
        goto do_goal;
    if (OBJ_SEQ(meptr) == 1 && (OBJ_FRAME(meptr) & 1) == 1) {
        snd = 0xFF;
        switch (mycst->sound) {
            case 1:
                if (OBJ_FRAME(meptr) == 1)
                    snd = 0x5A;
                else if (OBJ_FRAME(meptr) == 3)
                    snd = 0x5B;
                break;
            case 6:
                if (OBJ_FRAME(meptr) == 1)
                    snd = 0x2F;
                else if (OBJ_FRAME(meptr) == 3)
                    snd = 0x30;
                break;
            case 7:
                if (OBJ_FRAME(meptr) == 1)
                    snd = 0x1D;
                else if (OBJ_FRAME(meptr) == 3)
                    snd = 0x1D;
                break;
            case 3:
                snd = 0x27;
                break;
            case 2:
                snd = 0x17;
                break;
            case 5:
                snd = 0xD;
                break;
            case 4:
                snd = 0xE;
                break;
            default:
                snd = 0xFF;
            }
            if (snd != 0xFF)
                play_effect(snd, myxpost, myypost, 0);
        }
    if (mycst->bA_1)
        goto do_goal;
    if ((!OBJ_ALLY(meptr) && crithit != myid && mycst->race == typehit && !OBJ_LONER(meptr) || OBJ_ALLY(meptr))
        && crithittime + 0x200 > player->game_clock
        && abs(myxpos - hitx) + abs(myypos - hity) < mycst->hearing) {
        SET_ATTITUDE(meptr, 0);
        SET_B19_0(meptr, 1);
        if (OBJ_GOAL(meptr) != 9 && OBJ_GOAL(meptr) != 6) {
            critter_set_goal(5, OBJ_ALLY(meptr) ? crithit : 1);
            set_loc(hitx, hity, hitpz);
        }
    }
    if (meptr->last_hit > 0
        && (meptr->last_hit == 1 && !OBJ_ALLY(meptr) || OBJ_ALLY(meptr)
            || OBJ_ALLY(Obj_IntTMem(meptr->last_hit)))) {
        if (meptr->last_hit != OBJ_GTARG(meptr))
            SET_GTARG(meptr, meptr->last_hit);
        if (!set_up_target())
            goto do_goal;
        targeted = 1;
        if (meptr->last_hit == 1) {
            SET_ATTITUDE(meptr, 0);
            set_loc(OBJ_HOMEX(ThePlayer), OBJ_HOMEY(ThePlayer), OBJ_Z(ThePlayer) >> 3);
            SET_B19_0(meptr, 1);
        }
        if (tdistsqr > 2 && (!mycst->b2D_0 || anti_magic_p(myxpos, myypos))) {
            SET_B19_5(meptr, 1);
            critter_set_goal(5, meptr->last_hit);
        } else if (OBJ_B19_5(meptr))
            critter_set_goal(5, meptr->last_hit);
        else if (!OBJ_B19_4(meptr) && should_i_flee(mycst->avghit, meptr->hp, mycst->b1C_0, OBJ_DAMAGE(meptr)))
            critter_set_goal(6, meptr->last_hit);
        else if (OBJ_B19_4(meptr)) {
            SET_B19_4(meptr, 1);
            critter_set_goal(9, meptr->last_hit);
        } else
            critter_set_goal(5, meptr->last_hit);
        meptr->last_hit = 0;
        meptr->b11 = 0;
    }
do_goal:
    switch (OBJ_GOAL(meptr)) {
    case 0:
    case 7:
        crit_guard();
        break;
    case 1:
        crit_head_for_loc(myxhome, myyhome, Map_GetAddr(myxhome, myyhome)->height);
        break;
    case 2:
        crit_drunkwalk();
        break;
    case 4:
        crit_guard();
        break;
    case 5:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            crit_offense();
        break;
    case 6:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            crit_flee();
        break;
    case 8:
        crit_mill();
        break;
    case 9:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            crit_defense();
        break;
    case 10:
        crit_talk();
        break;
    case 11:
        SET_RATE(meptr, 4);
        SET_SPEED(meptr, rand() % 2);
        meptr->heading = rand() % 0x100;
        SET_PITCH(meptr, rand() % 3 + 0xF);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % seq_len);
        SET_B15_6(meptr, 1);
        break;
    case 15:
        if (control) {
            SET_SPEED(meptr, 0);
            SET_PITCH(meptr, 0x10);
        }
        SET_RATE(meptr, 7);
        if (OBJ_GTARG(meptr) > 0)
            SET_GTARG(meptr, OBJ_GTARG(meptr) - 1);
        else
            critter_discard_goal();
        break;
    case 3:
        break;
    case 12:
        crit_hover();
        break;
    default:
        SET_RATE(meptr, 7);
    }
    constrain_movement();
}

/* Load the target globals (mytarget, its tile and fine position, tdx, tdy and the
   squared tile and fine distances) from the critter's goal target, a mobile index
   (1 is the player). Returns 0 if the target is dead. */
unsigned char far set_up_target(void)
{
    mytarget = Obj_IntTMem(OBJ_GTARG(meptr));
#ifndef __TURBOC__
    mytarget = (struct Object *)port_shared_target(meptr, mytarget);
    if (port_shared_avatar(mytarget)) SET_GTARG(meptr, Obj_MemTPtr(mytarget));
#endif
    if (mytarget->hp <= 0)
        return 0;
    txpos = OBJ_HOMEX(mytarget);
    typos = OBJ_HOMEY(mytarget);
    tzpos = OBJ_Z(mytarget) >> 3;
    txpost = (txpos << 3) + OBJ_FINEX(mytarget);
    typost = (typos << 3) + OBJ_FINEY(mytarget);
    tdx = txpost - myxpost;
    tdy = typost - myypost;
    tdistsqr = (txpos - myxpos) * (txpos - myxpos) + (typos - myypos) * (typos - myypos);
    tdisttsqr = (txpost - myxpost) * (txpost - myxpost) + (typost - myypost) * (typost - myypost);
    return 1;
}

/* Whether a hurt critter flees. Never above three quarters of its average hit points
   (maxhp), and never below an eighth (a nearly dead critter fights on). It flees if the
   damage taken since it last decided (OBJ_DAMAGE) exceeds half maxhp. Otherwise it flees
   when hp * 16 / maxhp + rand() % 4 is at most 15 - nerve, nerve being the creature's
   b1C_0. */
/* name: IDA's MaybeShouldNPCWithdraw. Named from FM Towns: should_i_flee_ is at the same
   place between set_up_target_ and acceptable_danger_, and makes the same tests of the
   hit points against three quarters, an eighth and half of the maximum, then the
   rand() % 4 roll against 15 less the third argument. */
unsigned char far should_i_flee(unsigned char maxhp, unsigned char hp, unsigned char nerve,
                                unsigned char damage)
{
    register unsigned r;

    if (maxhp * 3 >> 2 < hp)
        return 0;
    if (maxhp >> 3 > hp)
        return 0;
    if (maxhp >> 1 < damage)
        return 1;
    if (maxhp == 0)
        return 0;
    r = (hp << 4) / maxhp + rand() % 4;
    if (0xF - nerve < r)
        return 0;
    return 1;
}

/* How much danger a path may cross for this critter (flood_path's range, compared with
   the cost hyp_move adds for drops and bad terrain): 0 for a critter that is not
   hostile, else hp * 4 / avghit plus nerve / 4. */
/* name: IDA's GetCritterRange. Named from FM Towns: acceptable_danger_ is next there and
   has the same body (attitude, the critter type's hit points, bit 13 of the item id, then
   hp * 4 / max hp plus a quarter of the low nibble at +0x1C). PATHFIND.C's
   crit_head_for_loc passes it to flood_path. */
unsigned char far acceptable_danger(void)
{
    register unsigned char d;

    if (OBJ_ATTITUDE(meptr) != 0 || mycst->avghit == 0 || OBJ_DOORDIR(meptr))
        return 0;
    d = (meptr->hp << 2) / mycst->avghit + mycst->b1C_0 / 4;
    return d;
}

/* Give the current critter a goal and goal target. A guarding critter (goal 4) remembers
   that in its old goal, so critter_discard_goal can return it to guarding. */
void far critter_set_goal(unsigned char goal, int target)
{
    if (OBJ_GOAL(meptr) == 4)
        SET_OLDGOAL(meptr, OBJ_GOAL(meptr));
    SET_GOAL(meptr, goal);
    SET_GTARG(meptr, target);
}

/* Drop the current goal: back to the remembered one (with the player as target), or
   wander. */
void far critter_discard_goal(void)
{
    if (OBJ_OLDGOAL(meptr)) {
        SET_GOAL(meptr, OBJ_OLDGOAL(meptr));
        SET_GTARG(meptr, 1);
        SET_OLDGOAL(meptr, 0);
    } else {
        SET_GOAL(meptr, 2);
        SET_GTARG(meptr, 0);
    }
}

/* Start obj's dying sequence (7) unless it has a conversation and death_check (a
   scripted death) refuses. Returns 1 if it is dying. */
unsigned char far go_into_dying_sequence(struct Object far *obj)
{
    if (obj->whoami == 0 || death_check(obj, 0)) {
        SET_SEQ(obj, 7);
        SET_FRAME(obj, 0);
        SET_RATE(obj, 4);
        obj->hp = 0;
        return 1;
    }
    return 0;
}

/* Kill obj and play its death cry by victim's death kind (1 and others sound 6, 2 0x22,
   3 0x23, 4 0x24, 0 silent). Returns 1 if it started dying here, 0 if it was already
   dying or death_check kept it alive. */
unsigned char far crit_die(struct Object far *obj)
{
    unsigned char snd;

    if (!(OBJ_SEQ(obj) == 7 || !go_into_dying_sequence(obj))) {
        switch (victim->death) {
        case 0:
            return 1;
        case 1:
            snd = 6;
            break;
        case 3:
            snd = 0x23;
            break;
        case 4:
            snd = 0x24;
            break;
        case 2:
            snd = 0x22;
            break;
        default:
            snd = 6;
        }
        play_effect(snd, (OBJ_HOMEX(obj) << 3) + OBJ_FINEX(obj), (OBJ_HOMEY(obj) << 3) + OBJ_FINEY(obj),
                    0);
        return 1;
    }
    return 0;
}

/* Damage a critter (or the player): adds to its damage tally and works out who did it
   (a critter's mobile index; for a missile or other object, that object's last_hit, its
   shooter). A hit by the player on a critter that is not a loner is recorded (race,
   place, time) so the critter's kin join in (critter_mv). At zero hit points it dies
   (crit_die), crediting the player (player_killed_a); returns 1 then. Also picks the
   combat music: 2 when a critter the player hits is below a quarter of its hit points,
   4 when the player is below a quarter of theirs, else 3. */
unsigned char far damage_critter(struct Object far *obj, unsigned char damage,
                                 struct Object far *from)
{
    unsigned char who;
    int minor;
    int index;
    int m;
    register struct Creature near *cr;
    register int ratio;

    minor = OBJ_MINOR(obj);
#ifndef __TURBOC__
    if (port_shared_avatar(obj)) {
        if (!port_shared_friendly_source(from)) { SET_DAMAGE(obj, OBJ_DAMAGE(obj) + damage); obj->hp = obj->hp > damage ? obj->hp - damage : 0; }
        return obj->hp == 0;
    }
#endif
    index = obj->id & ID_INCLASS;
    cr = &Creature[(minor << 4) + index];
    victim = cr;
    SET_DAMAGE(obj, OBJ_DAMAGE(obj) + damage);
    if (from == 0 || from >= (struct Object far *)objdata)
        who = 0;
    else if (OBJ_MAJOR(from) != MAJOR_CREATURE)
        who = from->last_hit;
    else {
        m = Obj_MemTPtr(from);
        who = m >= NUM_MOBILE ? 0 : m;
#ifndef __TURBOC__
        if (port_shared_avatar(from)) who = 1;
#endif
    }
    if (who)
        obj->last_hit = who;
    if (who == 1 && !OBJ_LONER(obj)) {
        typehit = cr->race;
        crithit = Obj_MemTPtr(obj);
        hitx = OBJ_HOMEX(obj);
        hity = OBJ_HOMEY(obj);
        hitpz = OBJ_Z(obj) >> 3;
        crithittime = player->game_clock;
    }
    if (obj->hp <= damage) {
        obj->hp = 0;
        if (crit_die(obj)) {
            if (who == 1)
                player_killed_a(obj);
            return 1;
        }
    } else {
        obj->hp = obj->hp - damage;
        if (obj == ThePlayer)
            panel_check_hpmp();
    }
    if (who == 1 && obj != ThePlayer) {
        ratio = (obj->hp << 6) / (cr->avghit + 1);
        if (ratio < 0x10)
            set_new_music(MUSIC_FOE_HURT);
        else
            set_new_music(MUSIC_COMBAT);
        lastcombattime = GAME_TIME();
    } else if (obj == ThePlayer && who) {
        ratio = (ThePlayer->hp << 6) / (playerdat->avghit + 1);
        if (ratio < 0x10)
            set_new_music(MUSIC_DANGER);
        else
            set_new_music(MUSIC_COMBAT);
        lastcombattime = GAME_TIME();
    }
    return 0;
}

/* Time bins: the 16-step clock curBin runs round, and each mobile object has a bin
   (b0A's low nibble) that its rate advances after each update. An object is due when
   its bin is up to 4 behind curBin, or, when the clock wrapped past 15 since the last
   pass, at or after lastbin. rate is unused. */
/* name: IDA's CheckIfUpdateNeeded. Named from FM Towns: timetodo_ is next there, before
   move_mobile_, which calls it with the same two arguments, and makes the same tests of
   curBin and lastbin. */
unsigned char far timetodo(int bin, int rate)
{
    if (curBin > bin && bin + 4 >= curBin
        || curBin + 16 > bin && lastbin <= bin && lastbin > curBin)
        return 1;
    return 0;
}

/* Advance the bin clock by delta frames and update every active mobile object until it
   is no longer due: critters through critter_ai, everything else through move_me_joe.
   An object that removed itself returns 0, and the loop steps back one so the entry
   that took its place is not skipped. */
void far move_mobile(int delta)
{
    unsigned char far *p;
    unsigned char ok;

#ifndef __TURBOC__
    /* Port only. DOS's clock moves a tick or so between check_physics' reads, so delta is at
       most 5. A port's game thread can be stopped by the host for a second or more between
       them; a step of 9 or more bins (mod 16) can then leave a distant critter's bin and
       bin + 8 both due, and critter_ai, which moves such a critter on by 8, loops here for
       ever (a UW2 player's Windows 10 freeze; their recording hangs DOS's replay build the
       same way). DOS never sees such a step, so capping it changes nothing DOS can do. */
    if (delta > 8) delta = 8;
#endif
    curBin = lastbin + delta & 0xF;
    meptr = 0;
    map_crit_pages();
    seqptr = MK_FP(EmsBuff + 0xC00, 0);
    for (p = ActiveMob; p < LastActiveMob; p++) {
        meptr = &critdata[*p];
        while (timetodo(OBJ_BIN(meptr), OBJ_RATE(meptr))) {
            if (OBJ_MAJOR(meptr) == MAJOR_CREATURE)
                ok = critter_ai();
            else
                ok = move_me_joe();
            if (!ok) {
                p--;
                break;
            }
        }
    }
    if (meptr != 0)
        editchng(2);
    lastbin = curBin;
}
