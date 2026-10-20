/* target: seg029_29EE */
/* opts: -mm -1 -G -O -d */
/* Object physics: bouncing, missile hits, collisions between objects, copying an object
   to and from the physics record, moving objects between the static and mobile lists,
   and settling a placed object into its tile. The whole of DOS resident segment
   seg029_29EE, in original order. Seeded from UW2Decomp's src/motion/OBJPHYS.C (UW2's
   seg030_2BB7): the same functions in the same order, less UW2's do_filanium. Function
   and global names are UW2's (the FM Towns originals); UW1 has no symbols of its own.

   An object in the map is either static (an 8-byte record, objdata and above, no motion
   state) or mobile (the longer records of critdata, below objdata: creatures and anything
   in motion). get_phys_data and set_phys_data convert between a mobile or static object
   and a struct Phys (motion.h) for MOTION.C's do_physics: critter/AI.C and PATHFIND.C
   call them around each critter's or moving object's step. When a moving object comes to
   rest set_phys_data turns it back into a static object (mob_to_static) and settles it
   with obj_deal; a static object given speed becomes mobile (static_to_mob, mob_init).
   The caller passes the object's tile in the globals XP and YP (critter.h).

   A mobile non-creature keeps its exact position in fields a creature uses for its AI:
   goal_word and attitude_word hold x and y in 1/256 tiles and b0F z in 1/8 units. A
   creature's or static object's position is only known to 1/8 tile, so get_phys_data
   fills the low bits at random.

   do_objhit is MOTION.C's response to touching an object: it uses usable objects and
   triggers (UseObj, UseTrigger), lets a missile strike (UseObj calls missile_newhit for a
   missile used on what it hit), and pushes the other object (bounce_obj). obj_deal
   settles an object dropped or placed in a tile: it destroys it in a wall, in another
   object or in water, may burn it in lava, and sets it moving if it lands on nothing.

   UW1 differences, all from the bytes: no satellites (missile_newhit) and no fireball or
   sphere exception in do_objhit, which marks creatures too and takes any major class 6
   object as a trigger; set_phys_data checks no pressure plates and no height change,
   damages on impacts over 0x100 without scaling by kind, stores the hit points twice and
   sets gravity only for acc -4; mob_to_static and obj_deal make no filanium check, obj_deal
   no splash and no ice case, and its random speed and pitch come after a bounce as well;
   mob_to_static keeps nothing on level 9, and on level 8 an object of fate 10 (talismans,
   by the listing's name) landing in lava within 6 tiles of the map's centre while the
   player lives is destroyed in an explosion, counting down player->talismans (when
   player->talisman_ok allows it; otherwise it sets bit 3 of player->dreams). Some flags and returns are read as signed
   chars (cbw), marked where they are.

   name: descriptive, UW2Decomp's (its map/filenames.tsv: object physics, bounce_obj,
   static_to_mob). */

#include <stdlib.h>
#include "combat.h"
#include "critter.h"
#include "event.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* This file's _BSS, DS:2760..2766 (UW2 DS:25BC..25C2). */
/* name: no FM Towns names: FM Towns keeps them as statics, so they are static, with
   UW2Decomp's provisional names. */
/* match: the names' keys (tools/bssorder.py) lay them out as UW1 has them: objhit_used
   87, objhit_tilex and objhit_tiley 151, objhit_myx and objhit_myy 183, deal_bounced
   and deal_blocked 908. */
static unsigned char objhit_used;           /* DS:2760 */
static unsigned char objhit_tilex, objhit_tiley;    /* DS:2761, the other object's tile */
static unsigned char objhit_myx, objhit_myy; /* DS:2763, the moving object's tile */
static unsigned char deal_bounced;          /* DS:2765 */
static unsigned char deal_blocked;          /* DS:2766 */

/* CP (the moving object) pushes other: other gets CP's heading, speed 0xBC and a
   vertical velocity scaled by the ratio of the masses (at most twice CP's). An object of
   no mass is stopped instead. Returns 4, "bounce", to do_objhit. */
int far bounce_obj(struct Object far *obj, struct Object far *other)
{
    struct Phys phys;
    struct Phys *pp = &phys;
    int scale;

    if (other != 0) {
        XP = objhit_tilex;
        YP = objhit_tiley;
        get_phys_data(other, pp);
        if (pp->mass != 0) {
            /* 16-bit ints, as Turbo C's (shl ax,6, and imul, each then cwd before the idiv):
               a mover of 512 (51.2 stones) or more gets a scale below 0, which the clamp
               lets through, and the product wraps too, where the host's int kept 32 bits */
            scale = (int16)(CP->mass << 6) / pp->mass;
            if (scale > 0x80)
                scale = 0x80;
            pp->heading = CP->heading;
            pp->speed = 0xEB;
            pp->vel[2] = (int16)(CP->vel[2] * scale) / 0x40;
            XP = objhit_tilex;
            YP = objhit_tiley;
            set_phys_data(other, pp);
        } else if (pp->speed != 0)
            pp->speed = 0;
    }
    return 4;
}

/* proj strikes hit: the missile's damage (Missile[] by its in-class index), scaled by
   the Missile skill and a skill check when the player (object 1) fired it and its
   Missile[] ammo field is -64 (meaning not known), applied with missile_thwack at the
   hit object's tile if the missile was the one used, else at the missile's own. */
void far missile_newhit(struct Object far *proj, struct Object far *hit)
{
    unsigned damage;
    int x;
    int y;
    struct MissileInfo *mi;
    int scale;

    mi = &Missile[OBJ_INCLASS(proj)];
    damage = mi->damage;
    if (proj->last_hit == 1 && mi->ammo == -64) {
        scale = (player->skills[SKILL_MISSILE] << 3) + 0xC0;
        switch (skill_check(player->skills[SKILL_MISSILE], 10)) {
        case -1:
            scale -= 0x80;
            break;
        case 2:
            scale += 0xC0;
            break;
        }
        damage = damage * scale >> 8;
    }
    if (objhit_used) {
        x = objhit_tilex;
        y = objhit_tiley;
    } else {
        x = objhit_myx;
        y = objhit_myy;
    }
    missile_thwack(proj->last_hit, proj, hit, x, y, damage, -mi->ammo);
}

/* The object index (the mover) touches oCollisions[ci] (ci -1: the floor). Each record
   is handled once per move (link bit 0x20). Using: if the other object is usable it is
   used on the mover, if it is a trigger it fires; if it is touchable and the mover is
   usable (a missile, for one), the mover is used on it. Returns 2 to pass through, 4 to
   bounce (bounce_obj), 0x10 if using destroyed the mover (MapObj_X < 0), or a trigger's
   result. Two mobile non-creatures hitting mark each other (b15 bit 7) and do not hit
   again, except fireballs and the resilient sphere. */
int far do_objhit(int ci, int index)
{
    int item;
    struct Object far *obj;
    struct Object far *other;
    char touch;
    int my_item;

    if (ci != -1) {
        if (oCollisions[ci].link.f.low & 0x20)
            return 2;
        oCollisions[ci].link.f.low = oCollisions[ci].link.f.low | 0x20;
    }
    objhit_myx = Ppd.x >> 3;
    objhit_myy = Ppd.y >> 3;
    obj = Obj_IntTMem(index);
    my_item = OBJ_ITEM(obj);
    if (ci == -1) {
        other = 0;
        item = -1;
        touch = 1;
    } else {
        other = Obj_IntTMem(oCollisions[ci].link.f.index);
        item = oCollisions[ci].offset & MAP_MASK;
        objhit_tilex = objhit_myx + item & MAP_MASK;
        item = objhit_tilex - objhit_myx;
        objhit_tiley = objhit_myy + (oCollisions[ci].offset - item) / MAP_SIZE & MAP_MASK;
        item = OBJ_ITEM(other);
        touch = ComObjData[item].touch;
        /* UW1: index tested first; no fireball or sphere exception, and creatures are
           marked too */
        if (index < NUM_MOBILE && oCollisions[ci].link.f.index < NUM_MOBILE) {
            if (my_item >> 6 != MAJOR_CREATURE && (unsigned char)(OBJ_B15_7(obj)))
                return 2;
            SET_B15_7(obj, 1);
        }
    }
    /* match: a compare with no jump after it (an empty if); its direction cannot be
       recovered from the bytes */
    if (item >= NUM_MOBILE)
        ;
    if (item != -1) {
        if (ComObjData[item].usable) {
            MapObj_X = objhit_tilex;
            MapObj_Y = objhit_tiley;
            objhit_used = 0;
            UseObj(obj, other, 0);
        } else if (item >> 6 == MAJOR_TRAP)     /* UW1: any trap or trigger */
            return UseTrigger(obj, 0L, other, 0);
    }
    if (touch) {
        if (ComObjData[my_item].usable) {
            MapObj_X = objhit_myx;
            MapObj_Y = objhit_myy;
            objhit_used = 1;
            UseObj(other, obj, 0);
            if (MapObj_X < 0)
                return 0x10;
        }
        return bounce_obj(obj, other);
    }
    return 2;
}

/* Fills pp from obj and its ComObjData entry. Mobile objects: heading from the 8-bit
   heading, speed from b13 (in units of 0x2F), vertical velocity from the pitch
   ((pitch - 16) << 6), gravity from b13 bit 7, terrain byte from the stored code. A
   resting mobile non-creature with no_hit clear gets a speed from OBJ_SPEED and the light
   field instead. Static objects: XP, YP give the tile, they have no motion, and hp is
   their quality. impact is cleared. */
void far get_phys_data(struct Object far *obj, struct Phys *pp)
{
    char jitter = 1;
    struct ComObj *co;

    co = &ComObjData[OBJ_ITEM(obj)];
    pp->index = Obj_MemTPtr(obj);
    pp->mass = co->mass;
    pp->light = co->light;
    pp->bounce = co->bounce;
    pp->resist = co->resist;
    pp->b1D = 0;
    pp->heading = OBJ_HEADING(obj) << 13;
    pp->b24 = 0;
    pp->radius = co->radius;
    pp->height = co->height;
    pp->x = OBJ_FINEX(obj);
    pp->y = OBJ_FINEY(obj);
    pp->z = OBJ_Z(obj);
    if (obj < (struct Object far *)objdata) {
        pp->x = pp->x + (OBJ_HOMEX(obj) << 3);
        pp->y = pp->y + (OBJ_HOMEY(obj) << 3);
        pp->heading = obj->heading << 8;
        pp->terrain = 1 << OBJ_TERRAIN(obj);
        pp->vel[2] = (OBJ_PITCH(obj) - 0x10) << 6;
        pp->acc[2] = ((obj->b13 & 0x80) >> 7) * -4;
        pp->hp = obj->hp;
        if (OBJ_MAJOR(obj) != MAJOR_CREATURE) {
            jitter = 0;
            pp->x = obj->goal_word;
            pp->y = obj->attitude_word;
            pp->z = obj->b0F;
        }
        pp->speed = OBJ_SPEED(obj);
        if (OBJ_MAJOR(obj) != MAJOR_CREATURE && (pp->acc[2] | pp->vel[2]) == 0 && !co->no_hit) {
            /* UW1: light read as a signed char (cbw) */
            if ((char)pp->light * 2 + 2 >= pp->speed)
                pp->speed = 0;
            else
                pp->speed = OBJ_SPEED(obj) * (((char)pp->light << 2) + 0x29);
        } else {
            pp->speed *= 0x2F;
            if (OBJ_MAJOR(obj) == MAJOR_CREATURE)
                pp->b24 = 8;
        }
    } else {
        pp->speed = pp->acc[2] = pp->vel[2] = 0;
        pp->hp = obj->qn.f.quality;
        pp->x += XP << 3;
        pp->y += YP << 3;
    }
    if (jitter) {
        pp->x = (pp->x << 5) + (rand() & 0x1F);
        pp->y = (pp->y << 5) + (rand() & 0x1F);
        pp->z = (pp->z << 3) + (rand() & 7);
    }
    pp->impact = 0;
}

/* Writes pp back to obj, whose tile is XP, YP on entry. Moves it between tile lists
   when it changes tile. An impact above 0x100 damages it, with a thud for objects; on lava
   (terrain 4) it takes 1 fire damage (type 8, probably) one time in five. A moving
   static object becomes mobile and a stopped mobile non-creature becomes static and is
   settled (mob_to_static, obj_deal). Returns 1 if obj is still mobile, 0 if it is
   static or was destroyed. */
unsigned char far set_phys_data(struct Object far *obj, struct Phys *pp)
{
    int xmoved = 0;
    int ymoved = 0;

    if (pp->x >> 8 != XP)
        xmoved = 1;
    if (pp->y >> 8 != YP)
        ymoved = 1;
    if (xmoved || ymoved) {
        struct Tile far *tile;
        union Link far *head;

        tile = Map_GetAddr(XP, YP);
        head = &tile->objects;
        Obj_Rem(head, obj);
        XP = pp->x >> 8;
        YP = pp->y >> 8;
        tile = Map_GetAddr(XP, YP);
        head = &tile->objects;
        Obj_Add(head, obj);
    }
    SET_Z(obj, pp->z >> 3);
    SET_FINEX_UNSIGNED(obj, pp->x >> 5 & 7);
    SET_FINEY(obj, pp->y >> 5 & 7);
    if (obj < (struct Object far *)objdata)
        obj->hp = pp->hp;
    else
        obj->qn.f.quality = pp->hp;
    if (pp->impact > 0x100) {           /* UW1: 0x100, and no scaling by kind */
        int mass;
        int damage;

        damage = pp->impact >> 8;
        mass = ComObjData[OBJ_ITEM(obj)].mass;
        if (OBJ_MAJOR(obj) != MAJOR_CREATURE)
            play_effect_on_mobile(0xF, obj, (mass - 600) / 50);
        damage_item(obj, 0L, XP, YP, damage, 0);
    }
    /* UW1: the hit points are stored again after the impact */
    if (obj < (struct Object far *)objdata)
        obj->hp = pp->hp;
    else
        obj->qn.f.quality = pp->hp;
    if (pp->terrain & FOOT_LAVA && rand() % 5 == 0)
        damage_item(obj, 0L, XP, YP, 1, DMG_FIRE);
    if (OBJ_MAJOR(obj) != MAJOR_CREATURE) {
        if (obj > (struct Object far *)objdata) {
            if (pp->speed | pp->vel[2] | pp->acc[2])
                obj = static_to_mob(obj);
        } else if (!(pp->speed | pp->vel[2] | pp->acc[2])) {
            SET_TERRAIN(obj, res_to_terr[pp->terrain]);
            if ((obj = mob_to_static(obj)) == 0)
                return 0;
            if ((obj = obj_deal(obj, XP, YP, 0)) == 0)
                return 0;
            if (obj < (struct Object far *)objdata)
                update_hack_vecs(pp);
        }
    }
    if (obj < (struct Object far *)objdata) {
        register int v;

        obj->heading = pp->heading >> 8;
        SET_HOMEX(obj, XP);
        SET_HOMEY(obj, YP);
        SET_GRAVITY(obj, pp->acc[2] == -4);      /* UW1: gravity is exactly -4 */
        v = pp->vel[2] / 0x40 + 0x10;
        if (v < 0)
            v = 0;
        else if (v > 0x1F)
            v = 0x1F;
        SET_PITCH(obj, v);
        SET_SPEED(obj, pp->speed / 0x2F);
        SET_TERRAIN(obj, res_to_terr[pp->terrain]);
        if (OBJ_MAJOR(obj) != MAJOR_CREATURE) {
            obj->goal_word = pp->x;
            obj->attitude_word = pp->y;
            obj->b0F = pp->z;
        }
        return 1;
    }
    if (OBJ_MAJOR(obj) == MAJOR_RECT)
        SET_HEADING(obj, pp->heading >> 13);
    return 0;
}

/* Replaces the static object obj, in tile XP, YP, with a new mobile copy and frees the
   static record; returns the mobile object, or 0 if none is free (obj then stays). */
struct Object far * far static_to_mob(struct Object far *obj)
{
    struct Object far *mob;
    struct Tile far *tile;
    union Link far *head;

    tile = Map_GetAddr(XP, YP);
    head = &tile->objects;
    if ((mob = Obj_Alloc(1)) != 0) {
        *(struct StaticObj far *)mob = *(struct StaticObj far *)obj;
        mob_init(mob, XP, YP);
        mob->hp = obj->qn.f.quality;
        if (OBJ_MAJOR(obj) != MAJOR_RECT && ComObjData[OBJ_ITEM(obj)].render != 2)
            mob->whoami = OBJ_HEADING(obj);
        if (OBJ_MAJOR(mob) == MAJOR_ANIMOBJ)
            Change_AnimPtr(mob, obj);
        Obj_Rem(head, obj);
        Obj_Free(obj);
        Obj_Add(head, mob);
    }
    return mob;
}

/* Sets a new mobile object's motion fields: heading, gravity (none for no_hit types),
   home tile x, y, the update bin (curBin + 1), full hit points, and for a non-creature
   its exact position at the middle of its 1/8 tile. */
void far mob_init(struct Object far *obj, int x, int y)
{
    obj->heading = OBJ_HEADING(obj) << 5;
    SET_FINEHEAD(obj, 0);
    SET_PITCH(obj, 0x10);
    SET_GRAVITY(obj, ComObjData[OBJ_ITEM(obj)].no_hit ? 0 : 1);
    SET_HOMEX(obj, x);
    SET_HOMEY(obj, y);
    SET_BIN(obj, curBin + 1);
    SET_RATE(obj, 2);
    SET_SPEED(obj, 0);
    SET_INVIS(obj, 0);
    obj->hp = 0x3F;
    SET_TERRAIN(obj, 0);
    if (OBJ_MAJOR(obj) != MAJOR_CREATURE) {
        obj->goal_word = (x << 8) + (OBJ_FINEX(obj) << 5) + 0xF;
        obj->attitude_word = (y << 8) + (OBJ_FINEY(obj) << 5) + 0xF;
        obj->b0F = OBJ_Z(obj) << 3;
        obj->last_hit = 0;
    }
}

/* Replaces the mobile object obj, in tile XP, YP, with a new static copy and releases
   the mobile record. Its type's fate (ComObjData) may destroy it instead: 1 to 8 is the
   chance in 8 that it is culled, if Obj_Elem_Fate also allows it, and 9 runs mts_doanim
   on it; landing in water (terrain code 1) splashes and counts as fate 8. An object of
   fate 10 landing in lava at the centre of level 8 is destroyed (see the file header),
   and nothing is kept on level 9. Light sources 4-6 become 0-2 (probably lit to unlit).
   Returns the static object, or 0. */
struct Object far * far mob_to_static(struct Object far *obj)
{
    struct Object far *st;
    struct Tile far *tile;
    union Link far *head;
    char keep = 1;
    char who;
    unsigned fate;

    fate = ComObjData[OBJ_ITEM(obj)].fate;
    if (OBJ_TERRAIN(obj) == 1) {
        fate = 8;
        put_effect(obj, 6, 3, 0, 0, XP, YP);
#ifndef __TURBOC__
    } else if (OBJ_TERRAIN(obj) == 2 && fate == 10 && PlayerLevel == 8
               && abs(XP - 32) + abs(YP - 32) < 6
               && (port_shared_on() ? port_shared_party_alive() : ThePlayer->hp != 0)) {
#else
    } else if (OBJ_TERRAIN(obj) == 2 && fate == 10 && PlayerLevel == 8
               && abs(XP - 32) + abs(YP - 32) < 6 && ThePlayer->hp != 0) {
#endif
        if (player->talisman_ok) {
            register int i;

            fate = 8;
            keep = 0;
            if (--player->talismans == 0) {
                game_sprint(0x116);
                editchng(0x400);
            } else {
                SET_ITEM(obj, ITEM_EXPLOSION_1C2);
                for (i = 8; player->talismans <= i; i--) {
                    SET_Z(obj, OBJ_Z(obj) + (rand() & 7) + 4);
                    fireball_effect(obj, XP + rand() % 3 - 1, YP + rand() % 3 - 1);
                }
            }
        } else
            player->dreams |= 8;
    }
    if (fate > 0 && fate <= 8 && (rand() & 7) < fate && Obj_Elem_Fate(10, obj))
        keep = 0;
    if (PlayerLevel == LEVEL_VOID)
        keep = 0;
    tile = Map_GetAddr(XP, YP);
    head = &tile->objects;
    if (keep && (st = Obj_Alloc(0)) != 0) {
        *(struct StaticObj far *)st = *(struct StaticObj far *)obj;
        obj->ol.f.link = 0;
        if (OBJ_MAJOR(st) == MAJOR_ANIMOBJ)
            Change_AnimPtr(st, obj);
        else if (OBJ_CLASS(st) == CLASS_LIGHT && OBJ_INCLASS(st) >= 4 && OBJ_INCLASS(st) <= 6)
            SET_INCLASS(st, OBJ_INCLASS(st) - 4);
        st->qn.f.quality = obj->hp;
        if (OBJ_MAJOR(st) != MAJOR_RECT && OBJ_MAJOR(st) != MAJOR_TRAP && ComObjData[OBJ_ITEM(st)].render != 2)
            SET_HEADING(st, obj->whoami);
    } else
        st = 0;
    if (fate == 9) {
        if (OBJ_MAJOR(obj) == MAJOR_CREATURE)
            who = 0;
        else
            who = obj->last_hit;
    }
    Obj_Punt(head, obj, 1);
    if (st != 0)
        Obj_Add(head, st);              /* UW1: no pressure plate check */
    if (fate == 9 && !(char)mts_doanim(st, XP, YP, who)) {     /* UW1: signed (cbw) */
        st = Obj_Punt(head, st, 0);
        return st;
    }
    return st;
}

/* name: IDA seg030_2BB7_107A. The map pairs it with FM Towns update_hack_vecs_ by size
   and position only; the code agrees (a byte flag picks a random turn of the heading with
   speed 0xBC, or a random speed (rand()+1 & 3) * 0x2F with gravity -4), so the original
   name is used. */
/* After obj_deal set a settled object moving again: it slides off an object it landed
   on the edge of (deal_bounced), or tumbles. */
void far update_hack_vecs(struct Phys *pp)
{
    if (deal_bounced) {
        pp->heading = pp->heading + (rand() & 0x3FFF) - 0x2000;
        pp->speed = 0xBC;
    } else {
        pp->speed = (rand() + 1 & 3) * 0x2F;
        pp->acc[2] = -4;
    }
}

/* Settles obj in tile x, y. Destroyed (Obj_Punt) in a wall, a high floor or another
   object or in water; in lava (class 2) it survives only if its qualclass is 3 or
   check_res lets it. Over a drop it
   becomes mobile and falls: it slides off a solid object it sits on the edge of
   (deal_bounced), and with how it then also gets a small random speed and pitch. Near the
   floor the object check first uses a point footprint, and only if the object would fall
   is it tried again with its full footprint. Returns the object, or 0 if destroyed. */
struct Object far * far obj_deal(struct Object far *obj, int x, int y, char how)
{
    char destroy;
    char retried;                       /* UW1: signed (cbw) */
    char low;
    int oldx;
    int oldy;
    struct MotionCalc calc;
    struct ComObj *co;

    destroy = 0;
    retried = 0;
    low = 0;
    deal_blocked = deal_bounced = 0;
again:
    curP = &calc;
    curP->index = Obj_MemTPtr(obj);
    co = &ComObjData[OBJ_ITEM(obj)];
    if (co->no_hit)
        return obj;
    curP->radius = co->radius;
    curP->height = co->height;
    curP->z = OBJ_Z(obj);
    curP->x = (x << 3) + OBJ_FINEX(obj);
    curP->y = (y << 3) + OBJ_FINEY(obj);
    TerrainCheck(curP->radius);
    if (curP->floor + curP->radius >= curP->z && !retried)
        low = 1;
    else
        low = 0;
    ObjectCheck(low, 1);
    process_objlist();
    MP.hit = 0xFF;
    if (curP->count == 0 && curP->first > 0 && curP->first <= curP->found) {
        for (curP->first--; curP->first >= 0; curP->first--) {
            if (oCollisions[curP->first].top == curP->z) {
                MP.hit = curP->first;
                MP.item = OBJ_ITEM(Obj_PtrTMem(&oCollisions[MP.hit].link));
                if (ComObjData[MP.item].solid == 1) {
                    if (oCollisions[MP.hit].link.f.low & 0x10) {
                        deal_blocked = 1;
                        break;
                    }
                    deal_bounced = 1;
                }
            } else
                break;
        }
    }
    if ((curP->hits0 | curP->hits1) & 0x300 || curP->count)
        destroy = 1;
    else if ((curP->hits0 & 7) == 5)    /* UW1: no splash, and no ice case */
        destroy = 1;
    else if ((curP->hits0 & 7) == 6) {
        if (ComObjData[OBJ_ITEM(obj)].qualclass == 3)
            destroy = 0;
        else
            destroy = check_res(obj, 1, DMG_FIRE);
    } else if (!(curP->hits0 & 8) && deal_blocked == 0) {
        if (low) {
            retried = 1;
            goto again;
        }
        oldx = XP;
        oldy = YP;
        XP = x;
        YP = y;
        obj = static_to_mob(obj);
        XP = oldx;
        YP = oldy;
        if (deal_bounced) {
            SET_SPEED(obj, 3);
            obj->heading = obj->heading + (rand() % 9 << 4) + 0xC0;
        }
        if (how) {                      /* UW1: also after a bounce */
            SET_SPEED(obj, (rand() & 3) + 1);
            SET_PITCH(obj, (rand() & 3) + 0xE);
        }
    }
    if (destroy)
        return Obj_Punt(&Map_GetAddr(x, y)->objects, obj, 0);
    return obj;
}
