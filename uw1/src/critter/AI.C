/* target: seg007_1798 */
/* opts: -mm -1 -G -O -Y -d */
/* Critter movement and AI: the whole of UW1's DOS resident segment seg007_1798 (UW2's
   seg007_17A2), in original order.

   What it does in the game: every moving thing in the level is stepped from here.
   move_mobile (the per-frame update) walks the active mobile list, and each object
   whose time bin has come round gets critter_ai (critters) or PATHFIND.C's move_me_joe
   (missiles and other mobile objects). critter_ai moves the critter through the physics
   engine, plays out attack, casting and dying sequences, and otherwise calls critter_mv,
   which notices being hit, chooses a goal and runs it.

   Goals (the goal word's low nibble, the switch in critter_mv): 0 and 7 stand still,
   1 go home, 2 wander (crit_drunkwalk), 3 keep close to the target (seg007_1798_3E4;
   critter_ai never skips it for distance), 4 guard (crit_guard, also the goal
   critter_set_goal remembers in the old goal), 5 attack (crit_offense), 6 flee
   (crit_flee), 8 mill about near home (crit_mill), 9 fight back when cornered
   (crit_defense), 10 talk to the player (crit_talk), 11 flutter at random, 12 hover at
   home (crit_hover). Other values only set a slow rate.

   Animation (the 6-bit seq in byte 0x15, four frames to a sequence, the frame counted
   modulo 4): 0x20 standing, 0x2C walking, 0 combat stance, 1 to 3 the three melee
   attacks (striking on frame 4), 5 firing a missile, 0xD casting, 7 backing away, 0xC
   dying (last frame 3).

   UW1 against UW2: no change_or_inc_seq, set_htx or set_cur_seq_len (the animation steps
   are written out, with fixed sequence numbers and four frames); a new goal 3 routine
   (seg007_1798_3E4) and no goal 15; crit_talk does not refuse hostile critters and does
   not remap the critter pages; Tybal's guards (race 0x13) on level 7 neither cast nor
   stand off while his orb is whole; acceptable_danger is 0 for conversation 0x16 on
   level 6; crit_die plays the death cry by the current critter (mycst), not the victim;
   critter_ai's melee strike comes on frame 4 and resets to the combat stance; no victim,
   seqptr or seq_len globals. The player record's layout is UW1's (struct Player, player.h).

   A critter's "home" word (OBJ_HOMEX, OBJ_HOMEY) is the tile it stands on now; its home
   in the AI's sense, myxhome and myyhome, is kept in the quality and owner fields
   (set_critter_vars, critter_ai).

   Data owned: atk_charge (the strength of a blow by attack frame), the record of the
   last critter the player hit (crithit, typehit, crithittime, hitx, hity, hitpz, which
   CRITTIME.C saves into the player record), the time bins (curBin, lastbin), lastXeye
   and lastYeye, and lastcombattime. The per-critter scratch globals (meptr, mycst,
   myxpos ... tdistsqr) are PATHFIND.C's.

   UW1 has no symbol-bearing build: function and global names are UW2's (the FM Towns
   originals) where the routine is the same, else the listing's.
   Name: UW2Decomp's (the job of System Shock's AI.C: critter goals and AI, critter_ai,
   crit_attack).

   Entry points: move_mobile, each frame from PLAYMOVE.C's move_physics; damage_critter, from
   DAMAGE.C for every blow, missile and spell that hurts a critter or the player;
   set_critter_vars and critter_set_goal, for CRITTIME.C. Neighbours: PATHFIND.C (the
   per-critter globals, crit_head_for_loc, the collision handlers, move_me_joe), MOTION.C and
   OBJPHYS.C (do_physics, get_phys_data, set_phys_data), COMBAT.C (critter_attack,
   critter_fire), SPELLS.C (cast), CONVERSE.C (TalkTo). */

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
#ifndef __TURBOC__
#define COOP_PLAYER(index) port_shared_player_index(index)
#define COOP_OTHER(index) (!port_shared_player_index(index))
#else
#define COOP_PLAYER(index) index == 1
#define COOP_OTHER(index) index != 1
#endif

/* Declared in each file that uses it, its own way (no header). */
extern struct Spell far spells[53];


int16 lastXeye, lastYeye;               /* DS:248E, this file's _BSS (see below) */
/* match: MISSILE.C's missile_try ties with that file's statics missile_src and missile_arc
   (key 957), which a header cannot declare first, so it is declared only here. */
extern int16 missile_try;
int32 lastcombattime;                   /* DS:2482, this file's _BSS (see below) */

/* The charge of a critter's blow, by attack frame (the 4-bit attack frame in the word at
   0x0F, which crit_attack counts up while the critter stands in reach and critter_ai
   passes to critter_attack): 50 for an unwound blow up to 255 after 15 frames. */
/* name: static in FM Towns, so the name is provisional. */
/* match: a far variable, so its own segment (in UW1 5619:0000, the listing's seg060):
   only this file uses it. */
struct AtkCharge {
    unsigned char charge;
    char b1;
};
static struct AtkCharge far atk_charge[16] = {
    { 50, 0 }, { 60, 0 }, { 70, 0 }, { 80, 0 }, { 90, 0 }, { 100, 0 }, { 110, 0 }, { 120, 0 },
    { 130, 0 }, { 140, 0 }, { 155, 0 }, { 170, 0 }, { 185, 0 }, { 205, 0 }, { 230, 0 }, { 255, 0 } };

/* match: this file's _BSS, in UW1 DS:2482..2491 (UW2 DS:2280..2299), laid out by name
   (tools/bssorder.py): lastcombattime 84, crithittime 139, hitx and hity 520, hitpz 552,
   curBin 555, lastXeye and lastYeye 820. UW1 has no victim, seqptr, seq_len or
   seq_lframe. */
/* Where (hitx, hity, hitpz) and when (crithittime, in game_clock units) the player last
   hit a critter. lastXeye and lastYeye are the tile the 3D view was last drawn from
   (VIEW3D.C). */
/* name: hitpz (DS:248C) is static in FM Towns (read by critter_mv_ for set_loc beside
   _hitx and _hity), so it has no original name: provisional, chosen for its key. */
uint32 crithittime;
unsigned char hitx, hity;
static unsigned char hitpz;
signed char curBin;

/* Initialised data, DS:C4: the time bin of the last pass over the mobile objects, and the
   critter the player last hit (its mobile index) and its race (struct Creature's race;
   0xFF for none). */
signed char lastbin = 0;
unsigned char crithit = 0;
unsigned char typehit = 0xFF;

/* Goal 2, wandering, and the fallback when no path is found. A hostile critter turns to
   guarding half the time. Otherwise it switches between standing (0x20) and walking
   (0x2C) at random, at the end of a four-frame cycle, the creature's lazy value making it more likely to stand; a walker
   that bumped into something turns a quarter turn left or right, otherwise drifts up to
   45 degrees off its heading and steers round the player (crit_avoid_player). Fliers
   also pick a new pitch. Ends with check_out_player, which turns it to face a nearby
   player. */
void far crit_drunkwalk(void)
{
    unsigned char head;
    unsigned char r;
    struct Tile far *tile;

    if (OBJ_B15_7(meptr)) {
        freepaths |= 1 << OBJ_PATH(meptr);
        SET_B15_7(meptr, 0);
    }
    tile = Map_GetAddr(myxpos, myypos);
    if (!(char)control) {
        SET_RATE(meptr, 1);
        return;
    }
    if (OBJ_ATTITUDE(meptr) == ATT_HOSTILE && rand() % 2) {
        crit_guard();
        return;
    }
    if (mycst->flier) {
        if (myzpos > 0xE)
            SET_PITCH(meptr, rand() % 3 + 0xE);
        else if (myzpos < tile->height + 2)
            SET_PITCH(meptr, rand() % 3 + 0x10);
        else
            SET_PITCH(meptr, rand() % 5 + 0xE);
    }
    if (OBJ_SEQ(meptr) == SEQ_STAND) {
        r = rand() % 16;
        if (mycst->lazy > r && OBJ_FRAME(meptr) == 3)
            SET_SEQ(meptr, SEQ_WALK);
    } else {
        r = rand() % 16;
        if (mycst->lazy < r && OBJ_FRAME(meptr) == 3)
            SET_SEQ(meptr, SEQ_STAND);
        else
            SET_SEQ(meptr, SEQ_WALK);
    }
    if (OBJ_SEQ(meptr) == SEQ_WALK) {
        if (failed && !(char)aligned) {
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
        if (!(char)aligned)
            head = crit_avoid_player(head, 10);
        meptr->heading = head;
        SET_HEADING(meptr, head >> 5);
        SET_FINEHEAD(meptr, head);
    } else {
        r = rand() % 0x80;
        if (mycst->lazy > r) {
            head = (meptr->heading + rand() % 0x40 + 0xE0) % 0x100;
            meptr->heading = head;
            SET_HEADING(meptr, head >> 5);
            SET_FINEHEAD(meptr, head);
        }
    }
    if (OBJ_SEQ(meptr) == SEQ_STAND) {
        SET_B15_6(meptr, 1);
        SET_SPEED(meptr, 0);
        SET_RATE(meptr, 6);
        if (rand() % 2)
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
    } else {
        SET_B15_6(meptr, 0);
        SET_SPEED(meptr, mycst->speed);
        SET_RATE(meptr, 4);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
    }
    check_out_player();
}

/* UW1: a goal UW2 lacks (see critter_mv). Within about 1.4 tiles of the target the
   critter faces it and plays its first attack animation (SEQ_ATTACK1, without a blow:
   critter_ai sends goal 3 straight to critter_mv); further than 8 tiles away it jumps to the
   target's side, a quarter of the way back along the line from the target to itself, onto
   the middle of that tile at floor height; in between it heads for the target's tile. */
/* name: the listing's; UW2 has no such function. */
void far seg007_1798_3E4(void)
{
    signed char x;
    signed char y;
    struct Tile far *to;
    struct Tile far *from;
    register int dist;

    if (tdistsqr <= 2) {
        if (!(char)control)
            return;
        SET_SEQ(meptr, SEQ_ATTACK1);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
        SET_SPEED(meptr, 0);
        SET_HEADING(meptr, deltatotheta(tdx, tdy));
        SET_RATE(meptr, 4);
    } else if (tdistsqr > 0x40) {
        dist = cSqRt(tdistsqr);
        x = txpos + ((signed char)myxpos - (signed char)txpos) * 4 / dist;
        y = typos + ((signed char)myypos - (signed char)typos) * 4 / dist;
        from = Map_GetAddr(myxpos, myypos);
        to = Map_GetAddr(x, y);
        Obj_Rem(&from->objects, meptr);
        Obj_Add(&to->objects, meptr);
        SET_HOMEX(meptr, x);
        SET_HOMEY(meptr, y);
        SET_FINEX(meptr, 4);
        SET_FINEY(meptr, 4);
        SET_Z(meptr, to->height << 3);
    } else {
        if (!(char)control)
            return;
        crit_head_for_loc(txpos, typos, tzpos);
    }
}

/* Goal 8, milling about: a hostile critter switches to guarding the player (goal 4);
   otherwise it wanders while within its creature's range of home (myxhome, myyhome) and
   heads back home when further. */
void far crit_mill(void)
{
    signed char dx;
    signed char dy;

    if (!(char)control)
        return;
    if (OBJ_ATTITUDE(meptr) == ATT_HOSTILE && OBJ_GOAL(meptr) != GOAL_GUARD) {
        critter_set_goal(GOAL_GUARD, 1);
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

    if (!(char)control)
        return;
    switch (OBJ_ATTITUDE(meptr)) {
    case ATT_HOSTILE:
        SET_GTARG(meptr, 1);
        set_up_target();
        if (OBJ_B19_0(meptr)) {
            critter_set_goal(GOAL_ATTACK, 1);
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
            case ATT_HOSTILE:
                SET_B19_0(meptr, 1);
                set_loc(x, y, tzpos);
                critter_set_goal(GOAL_ATTACK, 1);
                return;
            case ATT_MELLOW:
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
    case GOAL_WANDER:
        crit_drunkwalk();
        break;
    case GOAL_STAND:
    case GOAL_STAND_7:
        SET_RATE(meptr, 6);
        SET_SPEED(meptr, 0);
        SET_SEQ(meptr, SEQ_STAND);
        if (rand() % 2)
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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

    if (!(char)control)
        return;
    /* UW1: in Tybal's lair (level 7) while the orb is whole, his guards (race 0x13) close
       right in */
    if (PlayerLevel == LEVEL_TYBAL && !player->orb && mycst->race == 0x13)
        how = 1;
    dist = tdx * tdx + tdy * tdy;
    dx = myxhome - myxpos;
    dy = myyhome - myypos;
    homedist = dx * dx + dy * dy;
    if (COOP_PLAYER(OBJ_GTARG(meptr)))
        SET_ATTITUDE(meptr, ATT_HOSTILE);
    if ((dist < 0x64 || myxpos == txpos && myypos == typos)
        && (abs((signed char)myzpos - tzpos) < 4 || mycst->flier))
        crit_attack(dist);  /* UW1: then on to the ranged attack's test */
    else if (mycst->caster > 0) {
        if (!(char)maybe_cast_defensive_spell() && mycst->b2D_0)
            attacked = crit_magik_attack();
    } else if (mycst->arms[0].item >> 4 == CLASS_MISSILE)
        attacked = crit_missile_attack();
    if (attacked) {
        if (OBJ_SEQ(meptr) != SEQ_FIRE && OBJ_SEQ(meptr) != SEQ_CAST && OBJ_SEQ(meptr) != SEQ_ATTACK1) {
            SET_SEQ(meptr, SEQ_COMBAT);
            SET_RATE(meptr, 4);
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
            SET_SPEED(meptr, 0);
        }
        return;
    }
    if (dist > 0x100 && OBJ_OLDGOAL(meptr) == GOAL_GUARD && !OBJ_B19_5(meptr)
        && mycst->range * mycst->range * 4 < homedist) {
        SET_B19_0(meptr, 0);
        SET_B19_1(meptr, 0);
        critter_set_goal(GOAL_GUARD, 0);
        return;
    }
    crit_offense_find_target(txpos, typos, mycst->b2D_0 ? how : 1);
}

/* Melee manoeuvring: face the target, then by the squared fine distance back off
   (under 0x31: three times in four walk directly away, else sidestep), close in (over
   0x51), dodge in a random direction (chance dexterity/64) or stand. In reach (dist up to
   0x64) one time in four starts an attack, choosing sequence 1, 2 or 3 by the creature's
   attack probabilities (attacks[i].prob, percentages); otherwise the attack frame counts
   up, winding up a stronger blow (atk_charge). Fliers pitch towards the target's
   height. Always returns 1. */
unsigned char far crit_attack(register unsigned dist)
{
    signed char dz;
    unsigned char head;
    int i;
    register int r;

    head = deltatotheta(tdx, tdy);
    SET_HEADING(meptr, head);
    SET_FINEHEAD(meptr, 0);
    meptr->heading = head << 5;
    SET_B15_6(meptr, 0);
    if (dist < 0x31) {
        if (rand() % 4 == 0) {
            head = (head + (rand() % 2 * 2 - 1) * 2 + 8) % 8;
            SET_SEQ(meptr, SEQ_COMBAT);
            meptr->heading = head << 5;
            SET_SPEED(meptr, mycst->speed * 2 / 3);
        } else {
            head = (head + 4) % 8;
            SET_SEQ(meptr, SEQ_BACK_OFF);
            meptr->heading = head << 5;
            SET_SPEED(meptr, 2);
        }
    } else if (dist > 0x51) {
        SET_SEQ(meptr, SEQ_WALK);
        meptr->heading = head << 5;
        SET_SPEED(meptr, 2);
    } else if (rand() % 0x40 < mycst->attr[ATTR_DEX]) {
        head = rand() % 8;
        SET_SEQ(meptr, SEQ_COMBAT);
        meptr->heading = head << 5;
        SET_SPEED(meptr, 1);
    } else {
        SET_SEQ(meptr, SEQ_COMBAT);
        meptr->heading = head << 5;
        SET_SPEED(meptr, 0);
    }
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
            SET_SEQ(meptr, i + 1);
            SET_RATE(meptr, 4);
            SET_FRAME(meptr, 0);
            return 1;
        } else if (OBJ_ATKFRAME(meptr) < 0xF)
            SET_ATKFRAME(meptr, OBJ_ATKFRAME(meptr) + 1);
    }
    SET_RATE(meptr, 4);
    SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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
        switch (found) {
        case 0:
            set_loc(x, y, tzpos);
            break;
        case 1:
            SET_B19_0(meptr, 0);
            SET_B19_1(meptr, 0);
            critter_discard_goal();
            return;
        case 2:
            if (rand() % 2 == 0) {
                SET_B19_0(meptr, 0);
                SET_B19_1(meptr, 1);
                critter_discard_goal();
                return;
            }
            set_loc(x, y, tzpos);
            break;
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
   unless an anti-magic area covers the critter: start the cast sequence (0xD) with cast
   slot 3. critter_ai casts it on frame 4. UW1: not for Tybal's guards in his lair while
   the orb is whole. */
char far maybe_cast_defensive_spell(void)     /* UW1: char (callers cbw) */
{
    if (mycst->spells[2] != 0xFF && rand() % 0x100 < mycst->caster
        && !(char)anti_magic_p(myxpos, myypos)
        && (PlayerLevel != LEVEL_TYBAL || player->orb || mycst->race != 0x13)) {
        SET_SPEED(meptr, 0);
        SET_SEQ(meptr, SEQ_CAST);
        SET_CAST(meptr, 3);
        SET_FRAME(meptr, 0);
        return 1;
    }
    return 0;
}

/* A spell attack: possible when no anti-magic covers the critter, the target is within
   8 tiles (squared tile distance under 0x40), in line of sight, and the critter has
   turned to face it (look_for_target(1)). Then with chance caster/128 it starts casting
   its first spell (11 times in 16) or its second. Returns 1 if a ranged attack was
   possible, so the caller holds the combat stance. UW1: Tybal's guards do not cast in
   his lair while the orb is whole. */
unsigned char far crit_magik_attack(void)
{
    register int r;

    if (anti_magic_p(myxpos, myypos)
        || PlayerLevel == LEVEL_TYBAL && !player->orb && mycst->race == 0x13)
        return 0;
    if (tdistsqr < 0x40 && !(char)anti_magic_p(myxpos, myypos)
        && line_of_sight(myxpost, myypost, OBJ_Z(meptr) + ComObjData[OBJ_ITEM(meptr)].height,
                         txpost, typost, OBJ_Z(mytarget) + ComObjData[OBJ_ITEM(mytarget)].height)
        && look_for_target(1)) {
        r = rand() % 0x80;
        if (mycst->caster > r) {
            SET_SPEED(meptr, 0);
            SET_SEQ(meptr, SEQ_CAST);
            SET_CAST(meptr, rand() % 16 < 0xB ? 1 : 2);
            SET_FRAME(meptr, 0);
        }
        return 1;
    }
    return 0;
}

/* A missile attack: the target within 4 tiles (squared distance under 0x10), in line of
   sight and faced; then with chance (dexterity+1)/192 the critter starts the firing
   sequence (5), and critter_ai fires on frame 4. Returns 1 if an attack was possible. */
unsigned char far crit_missile_attack(void)
{
    if (tdistsqr < 0x10
        && line_of_sight(myxpost, myypost, OBJ_Z(meptr) + ComObjData[OBJ_ITEM(meptr)].height,
                         txpost, typost, OBJ_Z(mytarget) + ComObjData[OBJ_ITEM(mytarget)].height)
        && look_for_target(1)) {
        if (rand() % 0xC0 <= mycst->attr[ATTR_DEX]) {
            SET_SPEED(meptr, 0);
            SET_SEQ(meptr, SEQ_FIRE);
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

    if (!(char)control)
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
        SET_SEQ(meptr, SEQ_COMBAT);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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

    if (!(char)control)
        return;
    head = deltatotheta(tdx, tdy);
    dz = OBJ_Z(mytarget) - OBJ_Z(meptr);
    if (mycst->flier) {
        if (OBJ_Z(meptr) > 0x6E)
            SET_PITCH(meptr, rand() % 5 + 0xD);
        else
            SET_PITCH(meptr, rand() % 5 + 0xF);
    }
    if (tdistsqr <= 3 && abs(dz) < 0x10) {
        if (rand() % 0x100 < mycst->b1C_0 >> 3 || failed && !(char)aligned) {
            SET_B19_4(meptr, 1);
            critter_set_goal(GOAL_CORNERED, OBJ_GTARG(meptr));
            return;
        }
        meptr->heading = (head + 4) % 8 << 5;
        SET_HEADING(meptr, head);
        SET_FINEHEAD(meptr, 0);
        SET_SEQ(meptr, SEQ_BACK_OFF);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
        SET_SPEED(meptr, (mycst->speed + 1) / 2);
        return;
    }
    if (failed && !(char)aligned) {
        if (tdistsqr < 9) {
            if (OBJ_GOAL(meptr) == GOAL_CORNERED) {
                SET_SPEED(meptr, 0);
                set_htx(head);
                SET_RATE(meptr, 4);
                SET_SEQ(meptr, SEQ_COMBAT);
                SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
                return;
            }
            SET_B19_4(meptr, 1);
            critter_set_goal(GOAL_CORNERED, OBJ_GTARG(meptr));
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
        if (!(char)aligned)
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
    SET_SEQ(meptr, SEQ_WALK);
    SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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
#ifndef __TURBOC__
    plyr = (struct Object *)port_shared_target(meptr,plyr);
#endif
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

/* Goal 10, wanting to talk. The critter stands; if it perceives the player within 2.5
   tiles it turns to face them, and within 1.5 tiles, if the player faces it (the
   player's heading within 45 degrees of facing the critter), it starts the conversation
   (TalkTo). UW1: no turn to guarding for a hostile critter, and no remapping of the
   critter pages after the conversation. */
void far crit_talk(void)
{
    unsigned char head;
    signed char rel;
    unsigned char found;
    unsigned char x;
    unsigned char y;
    unsigned dist;

    if (!(char)control)
        return;
    SET_GTARG(meptr, 1);
    set_up_target();
    dist = tdx * tdx + tdy * tdy;
    found = target_found(&x, &y);
    if (COOP_OTHER(found) && dist < 0x190) {
        head = deltatotheta(tdx, tdy);
        SET_SPEED(meptr, 0);
        SET_SEQ(meptr, SEQ_STAND);
        SET_RATE(meptr, 6);
        if (rand() % 2)
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
        set_htx(head);
        if (dist < 0x90) {
#ifndef __TURBOC__
            /* The target can be the companion; use its facing and character for
               the conversation, including item grants and experience. */
            rel = OBJ_HEADING(mytarget) + 8 - head & 7;
#else
            rel = OBJ_HEADING(ThePlayer) + 8 - head & 7;
#endif
            if (rel >= 3 && rel <= 5) {
#ifndef __TURBOC__
                int previous = port_shared_context(Obj_MemTPtr(mytarget));
#endif
                pmouseHandled = 0;
                mouse_freereign();
                TalkTo(meptr);
#ifndef __TURBOC__
                port_shared_context_end(previous);
#endif
            }
        }
    } else {
        SET_RATE(meptr, 6);
        SET_SPEED(meptr, 0);
        SET_SEQ(meptr, SEQ_STAND);
        if (rand() % 2)
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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
            SET_SEQ(meptr, SEQ_STAND);
            SET_RATE(meptr, 6);
            if (rand() % 2)
                SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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

    if (!(char)control)
        return;
    dx = myxhome - myxpos;
    dy = myyhome - myypos;
    if (OBJ_ATTITUDE(meptr) == ATT_HOSTILE && OBJ_GOAL(meptr) != GOAL_GUARD)
        critter_set_goal(GOAL_GUARD, 1);
    else if (dx != 0 || dy != 0)
        crit_head_for_loc(myxhome, myyhome, Map_GetAddr(myxhome, myyhome)->height);
    else {
        SET_RATE(meptr, 6);
        SET_SPEED(meptr, 0);
        SET_SEQ(meptr, SEQ_STAND);
        if (rand() % 2)
            SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
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
signed char far compute_trz_or_try_rather(unsigned speed, char flag)  /* UW1: char flag */
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
    myoldspeed = OBJ_SPEED(meptr);
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

/* One AI step for meptr. A critter more than 10 tiles from both the view and the player
   is skipped for half a cycle (its bin moves on 8), unless its goal is 3. Otherwise its
   motion is run through the physics engine (do_crit_phys), with lava (collision bit
   0x20, a footprint corner on lava) ignored by creatures whose ComObjData resist has 8
   set (flameproof: PLAYDATA.C's damage_protection_flags give the Flameproof spell 8).
   The handler is changed for the step and changed back by setting the bits, so a
   flier's or swimmer's handler (CT2, CT4), which init_ai left without 0x20 in mask and
   w6, keeps 0x20 there after a flameproof flier or swimmer has moved. Then by
   sequence: a dying critter at its last frame (3) is removed, its inventory generated and
   dropped and its corpse built (returns 0, so move_mobile revisits the slot); an attack
   sequence strikes on frame 4 (critter_attack with a random swing kind 0-8, the charge
   from atk_charge and the creature's b0F as poison) and starts combat music if the
   player is the target; the casting and firing sequences cast or fire on frame 4; each
   then returns to the combat stance (0). Any other sequence goes to critter_mv. Finally
   the bin advances by the rate. */
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
#ifndef __TURBOC__
    plyr = (struct Object *)port_shared_target(meptr,plyr);
#endif
    px = OBJ_HOMEX(plyr);
    py = OBJ_HOMEY(plyr);
    eyedist = ((signed char)myxpos - lastXeye) * ((signed char)myxpos - lastXeye)
            + ((signed char)myypos - lastYeye) * ((signed char)myypos - lastYeye);
    pdist = ((signed char)myxpos - px) * ((signed char)myxpos - px)
          + ((signed char)myypos - py) * ((signed char)myypos - py);
    if (eyedist > 0x64 && pdist > 0x64 && OBJ_GOAL(meptr) != GOAL_FOLLOW) {
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
    if (OBJ_SEQ(meptr) != SEQ_WALK && OBJ_SEQ(meptr) != SEQ_STAND && OBJ_B15_7(meptr)) {
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
    myoldspeed = OBJ_SPEED(meptr);
    myheight = ComObjData[OBJ_ITEM(meptr)].height;
    if (OBJ_GOAL(meptr) == GOAL_FLUTTER || OBJ_GOAL(meptr) == GOAL_FOLLOW)
        critter_mv();
    else if (OBJ_SEQ(meptr) == SEQ_DYING) {
        if (OBJ_FRAME(meptr) == 3) {
#ifndef __TURBOC__
            port_shared_death_check(meptr, 1);
#else
            death_check(meptr, 1);
#endif
            XP = OBJ_HOMEX(meptr);
            YP = OBJ_HOMEY(meptr);
            Obj_Rem(&Map_GetAddr(XP, YP)->objects, meptr);
            generate_inventory(meptr);
            build_corpse(meptr, mycst->corpse, mycst->remains);
            drop_some_objects(meptr);
            Obj_Free(meptr);
            return 0;
        } else
            SET_FRAME(meptr, OBJ_FRAME(meptr) + 1);
    } else if (OBJ_SEQ(meptr) >= SEQ_ATTACK1 && OBJ_SEQ(meptr) <= SEQ_ATTACK3) {
        if (OBJ_FRAME(meptr) == 0 && COOP_PLAYER(OBJ_GTARG(meptr))) {
            if (get_current_music() < MUSIC_FOE_HURT || get_current_music() > MUSIC_DANGER)
                set_new_music(MUSIC_COMBAT);
            lastcombattime = GAME_TIME();
        }
        if (OBJ_FRAME(meptr) == 4) {
            critter_attack(meptr, rand() % 9, atk_charge[OBJ_ATKFRAME(meptr)].charge, OBJ_SEQ(meptr) - 1,
                           mycst->b0F);
            SET_SEQ(meptr, SEQ_COMBAT);
            SET_FRAME(meptr, 0);
            SET_ATKFRAME(meptr, 0);
        } else
            SET_FRAME(meptr, OBJ_FRAME(meptr) + 1);
    } else if (OBJ_SEQ(meptr) == SEQ_CAST && OBJ_CAST(meptr)) {
        if (OBJ_FRAME(meptr) == 4) {
            missile_try = compute_trz_or_try_rather(0x1E, 0);
            cast(mycst->spells[OBJ_CAST(meptr) - 1], meptr, 0L);
            SET_SEQ(meptr, SEQ_COMBAT);
            SET_FRAME(meptr, 0);
            SET_CAST(meptr, 0);
        } else
            SET_FRAME(meptr, OBJ_FRAME(meptr) + 1);
    } else if (OBJ_SEQ(meptr) == SEQ_FIRE) {
        if (OBJ_FRAME(meptr) == 4) {
            type = mycst->arms[0].item & ID_INCLASS;
            missile_try = compute_trz_or_try_rather(Missile[type].type, 1);
            critter_fire(meptr, type, Missile[type].type);
            SET_SEQ(meptr, SEQ_COMBAT);
            SET_FRAME(meptr, 0);
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
    char targeted = 0;                  /* UW1: signed (cbw) */

    didmove = 0;
    SET_B18_5(meptr, 0);
    SET_B15_6(meptr, 0);
    if (OBJ_GOAL(meptr) == GOAL_FLUTTER)
        goto do_goal;
    if (OBJ_SEQ(meptr) == SEQ_WALK && (OBJ_FRAME(meptr) & 1) == 1) {
        switch (mycst->sound) {
        case 1:
            if (OBJ_FRAME(meptr) == 1)
                play_effect(1, myxpost, myypost, 0);
            else if (OBJ_FRAME(meptr) == 3)
                play_effect(2, myxpost, myypost, 0);
            break;
        case 3:
            play_effect(5, myxpost, myypost, 0);
            break;
        case 2:
            play_effect(0x17, myxpost, myypost, 0);
            break;
        case 5:
            play_effect(0xD, myxpost, myypost, 0);
            break;
        case 4:
            play_effect(0xE, myxpost, myypost, 0);
            break;
        }
    }
    if (mycst->bA_1)
        goto do_goal;
    if ((!OBJ_ALLY(meptr) && crithit != myid && mycst->race == typehit && !OBJ_LONER(meptr) || OBJ_ALLY(meptr))
        && crithittime + 0x200 > player->game_clock
        && abs(myxpos - hitx) + abs(myypos - hity) < mycst->hearing) {
        SET_ATTITUDE(meptr, ATT_HOSTILE);
        SET_B19_0(meptr, 1);
        if (OBJ_GOAL(meptr) != GOAL_CORNERED && OBJ_GOAL(meptr) != GOAL_FLEE) {
            critter_set_goal(GOAL_ATTACK, OBJ_ALLY(meptr) ? crithit : 1);
            set_loc(hitx, hity, hitpz);
        }
    }
    if (meptr->last_hit > 0
        && (COOP_PLAYER(meptr->last_hit) && !OBJ_ALLY(meptr) || OBJ_ALLY(meptr)
            || OBJ_ALLY(Obj_IntTMem(meptr->last_hit)))) {
        if (meptr->last_hit != OBJ_GTARG(meptr))
            SET_GTARG(meptr, meptr->last_hit);
        if (!set_up_target())
            goto do_goal;
        targeted = 1;
        if (COOP_PLAYER(meptr->last_hit)) {
            SET_ATTITUDE(meptr, ATT_HOSTILE);
            set_loc(OBJ_HOMEX(ThePlayer), OBJ_HOMEY(ThePlayer), OBJ_Z(ThePlayer) >> 3);
            SET_B19_0(meptr, 1);
        }
        if (tdistsqr > 2 && (!mycst->b2D_0 || anti_magic_p(myxpos, myypos))) {
            SET_B19_5(meptr, 1);
            critter_set_goal(GOAL_ATTACK, meptr->last_hit);
        } else if (OBJ_B19_5(meptr))
            critter_set_goal(GOAL_ATTACK, meptr->last_hit);
        else if (!OBJ_B19_4(meptr) && should_i_flee(mycst->avghit, meptr->hp, mycst->b1C_0, OBJ_DAMAGE(meptr)))
            critter_set_goal(GOAL_FLEE, meptr->last_hit);
        else if (OBJ_B19_4(meptr)) {
            SET_B19_4(meptr, 1);
            critter_set_goal(GOAL_CORNERED, meptr->last_hit);
        } else
            critter_set_goal(GOAL_ATTACK, meptr->last_hit);
        meptr->last_hit = 0;
        meptr->b11 = 0;
    }
do_goal:
    switch (OBJ_GOAL(meptr)) {
    case GOAL_STAND:
    case GOAL_STAND_7:
        crit_guard();
        break;
    case GOAL_GO_HOME:
        crit_head_for_loc(myxhome, myyhome, Map_GetAddr(myxhome, myyhome)->height);
        break;
    case GOAL_WANDER:
        crit_drunkwalk();
        break;
    case GOAL_FOLLOW:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            seg007_1798_3E4();
        break;
    case GOAL_GUARD:
        crit_guard();
        break;
    case GOAL_ATTACK:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            crit_offense();
        break;
    case GOAL_FLEE:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            crit_flee();
        break;
    case GOAL_MILL:
        crit_mill();
        break;
    case GOAL_CORNERED:
        if (!targeted && !set_up_target())
            critter_discard_goal();
        else
            crit_defense();
        break;
    case GOAL_TALK:
        crit_talk();
        break;
    case GOAL_FLUTTER:
        SET_RATE(meptr, 4);
        SET_SPEED(meptr, rand() % 2);
        meptr->heading = rand() % 0x100;
        SET_PITCH(meptr, rand() % 3 + 0xF);
        SET_FRAME(meptr, (OBJ_FRAME(meptr) + 1) % 4);
        SET_B15_6(meptr, 1);
        break;
    case GOAL_HOVER:
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
char far set_up_target(void)
{
    mytarget = Obj_IntTMem(OBJ_GTARG(meptr));
#ifndef __TURBOC__
    mytarget = (struct Object *)port_shared_target(meptr, mytarget);
    /* Some final-area mobiles have no goal target. A second camera can wake
       them before a script assigns one; target zero is not a live avatar. */
    if (port_shared_on() && mytarget == 0) return 0;
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

    if (OBJ_ATTITUDE(meptr) != ATT_HOSTILE || mycst->avghit == 0 || OBJ_DOORDIR(meptr))
        return 0;
    /* UW1: 0 for the critter with conversation 0x16 on level 6 */
    if (PlayerLevel == 6 && meptr->whoami == 0x16)
        return 0;
    d = (meptr->hp << 2) / mycst->avghit + mycst->b1C_0 / 4;
    return d;
}

/* Give the current critter a goal and goal target. A guarding critter (goal 4) remembers
   that in its old goal, so critter_discard_goal can return it to guarding. */
void far critter_set_goal(unsigned char goal, int target)
{
    if (OBJ_GOAL(meptr) == GOAL_GUARD)
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
        SET_OLDGOAL(meptr, GOAL_STAND);
    } else {
        SET_GOAL(meptr, GOAL_WANDER);
        SET_GTARG(meptr, 0);
    }
}

/* Start obj's dying sequence (0xC) unless it has a conversation and death_check (a
   scripted death) refuses. Returns 1 if it is dying. */
unsigned char far go_into_dying_sequence(struct Object far *obj)
{
#ifndef __TURBOC__
    if (obj->whoami == 0 || port_shared_death_check(obj, 0)) {
#else
    if (obj->whoami == 0 || death_check(obj, 0)) {
#endif
        SET_SEQ(obj, SEQ_DYING);
        SET_FRAME(obj, 0);
        SET_RATE(obj, 4);
        obj->hp = 0;
        return 1;
    }
    return 0;
}

/* Kill obj and, if the current critter's (mycst's) death kind is 1, play sound 6 at the
   current critter's position (UW1: not the victim's, as UW2 does). Returns 1 if it
   started dying here, 0 if it was already dying or death_check kept it alive. */
unsigned char far crit_die(struct Object far *obj)
{
    if (OBJ_SEQ(obj) != SEQ_DYING) {
        if (!(char)go_into_dying_sequence(obj))
            return 0;
        switch (mycst->death) {
        case 1:
            play_effect(6, myxpost, myypost, 0);
        }
        return 1;
    }
    return 0;
}

/* Damage a critter (or the player): adds to its damage tally and works out who did it
   (a critter's mobile index; for a missile or other object, that object's last_hit, its
   shooter). A hit by the player on a critter that is not a loner is recorded (race,
   place, time) so the critter's kin join in (critter_mv). At zero hit points it dies
   (crit_die), crediting the player (player_killed_a); returns 1 then. Also picks the
   combat music: MUSIC_FOE_HURT when a critter the player hits is below a quarter of its
   hit points, MUSIC_DANGER when the player is below a quarter of theirs, else
   MUSIC_COMBAT. */
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
    index = OBJ_INCLASS(obj);
    cr = &Creature[(minor << 4) + index];
    SET_DAMAGE(obj, OBJ_DAMAGE(obj) + damage);
    if (from == 0)
        who = 0;
    else if (OBJ_MAJOR(from) != MAJOR_CREATURE)
        who = from->last_hit;
    else {
        m = Obj_MemTPtr(from);
        who = m >= NUM_MOBILE ? 0 : m;
#ifndef __TURBOC__
        /* Preserve the attacking avatar index for individual experience. */
#endif
    }
    if (who)
        obj->last_hit = who;
#ifndef __TURBOC__
    if (port_shared_player_index(who) && !OBJ_LONER(obj)) {
#else
    if (who == 1 && !OBJ_LONER(obj)) {
#endif
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
#ifndef __TURBOC__
            if (port_shared_player_index(who)) port_shared_killed(who,obj);
#else
            if (who == 1)
                player_killed_a(obj);
#endif
            return 1;
        }
    } else {
        obj->hp = obj->hp - damage;
        if (obj == ThePlayer)
            panel_check_hpmp();
    }
    if (who == 1) {
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
void far move_mobile(char delta)        /* UW1: char delta */
{
    unsigned char far *p;
    char ok;                            /* UW1: signed (cbw) */

#ifndef __TURBOC__
    /* Port only, as UW2Decomp's: DOS's clock moves a tick or so between check_physics'
       reads, so delta is at most 5. A host stall of the port's game thread can make it 9 or
       more (mod 16), and then a distant critter's bin and bin + 8 are both due and
       critter_ai, which moves it on by 8, loops here for ever (a UW2 player's freeze; the
       same code in UW1). Unsigned, so a stall long enough to wrap the char is capped too. */
    if ((unsigned char)delta > 8) delta = 8;
#endif
    curBin = lastbin + delta & 0xF;
    meptr = 0;
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
