/* target: seg022_230E */
/* opts: -mm -1 -G -O -d */
/* Combat: where a blow lands, what it hits, whether it hits, the damage it does, the
   sounds of a miss, the player's weapon swing and charge, missiles striking, critters
   attacking and the experience for a kill. The whole of DOS resident segment seg022_230E,
   in original order. Seeded from UW2Decomp's src/combat/COMBAT.C (UW2's seg024_24E9).
   Function and global names are UW2's (the FM Towns originals) where the routine is the
   same one; UW1 has no symbols of its own.

   What it does in the game: one melee blow is a few globals filled in by the attacker's
   side and then do_attack. The player's side is player_attack, called every frame from the
   input code while a swing key or the right mouse button is held: it charges the blow
   (play_pow), works out the weapon (GetPlayerWeapon, DoPlayerWeapon) and on release swings,
   or for a bow or sling hands over to player_fire in MISSILE.C. A critter's side is
   critter_attack, called by the AI. do_attack finds the target in front of the attacker
   (resolve_attack), rolls to hit (frp_check, a skill_check in SKILLCHK.C against the
   defender's Creature defence) and either rolls damage (do_damage, which hands it to
   damage_item in DAMAGE.C) or plays the miss (do_miss). missile_thwack is the same damage
   path for a missile that has struck, and player_killed_a gives the experience for a kill.

   UW1 against UW2: no is_sharp, no poison weapon bonus, no special weapons (specweap and
   its tables), no player_weapon (the wall hit is always effect 7, a blocked blow takes its
   weapon kind from the attacker's Creature record) and no hit_wall or missile_hit flags;
   ddone is a local of do_damage.
   player_attack steps on the weapon frame number itself (UW2 the frame's flag bits),
   check_ammo prints its message from literals, a weapon enchantment adds (effect & 7) + 1
   to damage or to hit, critters poison the player without the armour roll, do_miss stores
   the attacker's item id into fromwho, and a kill makes the dragons nod and plays music 9.
   struct Player differs (player.h has UW1's record).

   Callers: INTERACT.C and PLAYER.C (the player's swing, player_attack, clear_fight_state),
   AI.C (critter_attack, player_killed_a), MISSILE.C, OBJPHYS.C (missile_thwack), SKILLS.C,
   GAMEWRAP.C, WORLDEV.C. It calls COLLIDE.C's ObjectCheck and TerrainCheck to find what a
   blow meets, SKILLCHK.C's skill_check, DAMAGE.C's damage_item, INVDATA.C for the
   player's weapon and armour, EFFECT.C for blood and sparks, and PANELS.C and SOUND.C for
   the feedback.

   Data owned: the attack globals shared by these steps (fromwho attacker, hitobj target,
   towhere and swing, askill attack skill, damage, power, hitloc, hitangle, criti) and the
   player's swing state (pQatt, attackKey, play_pow). Tables: swing_kind, swing_keys and
   hitz_tab; the literal pool holds check_ammo's strings.
   Name: descriptive (melee and missile combat: resolve_attack, player_attack). */

#include <stdlib.h>
#include <string.h>
#include "combat.h"
#include "critter.h"
#include "gfx.h"
#include "inv.h"
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
unsigned char far IsMobElem(struct Object far *obj);

/* Initialised data, DS:236 onwards (DS:234 and 235 are COLCYCLE.C's), then the literal
   pool to DS:267. */
/* name: the statics have no FM Towns names; the names here are descriptive. */
/* By swing (1-9, the ninths of the 3D view from the bottom left): 0 slash, 1 bash, 2 stab,
   indexing a weapon's damage bytes. */
static unsigned char swing_kind[9] = { 2, 2, 2, 0, 0, 0, 1, 1, 1 };
/* By swing / 3: the scan code of the key that holds the swing. */
static unsigned char swing_keys[3] = { 0x34, 0x27, 0x19 };
int16 pQatt = 0;
int16 attackKey = -1;
/* By hit location: the height of the blood splash. [4] is set to -hitz before use. */
static signed char hitz_tab[5] = { 5, 3, 1, 7, 0 };

/* Uninitialised data, DS:2654..2681. */
/* match: Turbo C lays _BSS out in an order set by the names
   (some hash), not by declaration, so the static names were chosen (by compiling
   candidates) to land where the EXE has them: otime, elapsed, swing_state, fire_mode,
   curr_weapon and weapon_data are not original names. */
int16 askill;
int16 wsize;
int16 towhere;
static int32 otime;
static int16 elapsed;
static int16 swing_state;
unsigned char play_pow;
int16 targx;
int16 targy;
int16 hitobj;
signed char cmbModTH[4];
signed char hitz;
int16 damage;
int16 hitloc;
static unsigned char fire_mode;
unsigned char hitangle;
char criti;
int16 fromwho;
static struct Object far *curr_weapon;
static struct Weapon *weapon_data;
unsigned char power;

#ifndef __TURBOC__
/* Only state which survives between player_attack calls belongs to a seat.
   The hit calculation scratch (fromwho, damage, hitobj, etc.) stays on the
   calling stack of the one authoritative world. */
void port_shared_combat_state(int which,int save)
{
    static struct {
        int16 queued,key,elapsed,state,reach;
        int32 time;
        unsigned char pow,fire;
        struct Object *weapon;
        struct Weapon *data;
    } seats[2];
    static int initialized;
    if(which<0){memset(&seats[1],0,sizeof seats[1]);seats[1].key=-1;return;}
#define FIELD(member,global) do{if(save)seats[which].member=global;else global=seats[which].member;}while(0)
    if(!initialized){seats[1].key=-1;initialized=1;}
    FIELD(queued,pQatt);FIELD(key,attackKey);FIELD(elapsed,elapsed);FIELD(state,swing_state);FIELD(time,otime);
    FIELD(pow,play_pow);FIELD(fire,fire_mode);FIELD(weapon,curr_weapon);FIELD(data,weapon_data);
    FIELD(reach,wsize);
#undef FIELD
}
#endif

/* Picks the hit location (0..3) from the defender's bottom and top (dz, dtop) and the
   attack's (az, atop): a blow whose middle is below the defender's bottom gives 2, above its
   top 3, otherwise a weighted random choice favouring 2 or 3 by which half the blow lands in
   and then 0 over 1. The location indexes the Creature armour values and cmbModTH; the
   player's armour slot hit is (hitloc + 1) & 3. missile_thwack adds 4 for a missile. */
int far pickloc(int dz, int dtop, int az, int atop)
{
    int dmid;
    int amid;

    dmid = (dz + dtop) >> 1;
    amid = (az + atop) >> 1;
    if (dz + 1 > amid)
        return 2;
    if (dtop - 1 < amid)
        return 3;
    if (amid < dmid) {
        if (rand() % 2)
            return 2;
    } else if (rand() % 3 == 0)
        return 3;
    if (rand() % 3)
        return 0;
    return 1;
}

/* From the objects ObjectCheck found in the blow's way (oCollisions[c->first ..
   c->first + c->count]), picks the one nearest the attacker, skipping traps and the
   attacker itself; the player's blows also skip allied mobiles unless that one is the only
   candidate left. Sets targx, targy to its map square and returns its oCollisions index, or
   -1. */
int far set_hitobj(struct MotionCalc *c)
{
    int found;
    int last;
    int ax;
    int ay;
    int idx;
    int t;
    int dx;
    int dy;
    int32 dist;
    int32 best;
    struct Object far *obj;
    struct Object far *att;
    int i;

    i = c->first;
    found = -1;
    best = 100000L;
    last = c->first + c->count;
    att = critdata + fromwho;
    ax = (OBJ_HOMEX(att) << 3) + OBJ_FINEX(att);
    ay = (OBJ_HOMEY(att) << 3) + OBJ_FINEY(att);
    for (; i < last; i++) {
        idx = oCollisions[i].link.f.index;
        obj = Obj_IntTMem(idx);
        if (OBJ_MAJOR(obj) == MAJOR_TRAP)
            continue;
        if (idx == fromwho)
            continue;
#ifndef __TURBOC__
        /* Party melee passes through the other avatar, without armor rolls. */
        if (COOP_PLAYER(fromwho) && COOP_PLAYER(idx))
            continue;
#endif
        if (COOP_PLAYER(fromwho) && IsMobElem(obj) && OBJ_ALLY(obj) && (last - 1 != i || best != 100000L))
            continue;
        t = oCollisions[i].offset & MAP_MASK;
        targx = ((c->x >> 3) + t) & MAP_MASK;
        t = targx - (c->x >> 3);
        targy = ((c->y >> 3) + (oCollisions[i].offset - t) / MAP_SIZE) & MAP_MASK;
        dx = ax - ((targx << 3) + OBJ_FINEX(obj));
        dy = ay - ((targy << 3) + OBJ_FINEY(obj));
        dist = dx * dx + dy * dy;
        if (dist < best) {
            best = dist;
            found = i;
        }
    }
    if (found >= 0) {
        t = oCollisions[found].offset & MAP_MASK;
        targx = ((c->x >> 3) + t) & MAP_MASK;
        t = targx - (c->x >> 3);
        targy = ((c->y >> 3) + (oCollisions[found].offset - t) / MAP_SIZE) & MAP_MASK;
    }
    return found;
}

/* A blow that met a wall: steps along the heading in 1/16-square steps for dist + 1
   steps until the terrain check reports a wall, and puts a short-lived ITEM_DAMAGE animation
   object there (the spark). For the player it also plays the wall hit sound, effect 7. */
void far find_wall_coll(int heading, int dist, struct MotionCalc *c)
{
    int16 fx;
    int16 fy;
    struct Object far *obj;
    int tx;
    int ty;

    curP = c;
    curP->radius = 1;
    curP->index = 0;
    dist++;
    fx = curP->x << 4;
    fy = curP->y << 4;
    do {
        TerrainCheck(0);
        if ((curP->hits0 | curP->hits1) & 0x300) {
            if ((obj = CreateObj(ITEM_DAMAGE, 0)) == 0)
                return;
            SET_FINEX_UNSIGNED(obj, curP->x & 7);
            SET_FINEY(obj, curP->y & 7);
            tx = curP->x >> 3;
            ty = curP->y >> 3;
            SET_Z(obj, curP->z + 8);
            if (COOP_PLAYER(fromwho))
                play_effect(7, curP->x, curP->y, 0);
            if (add_animobj(Obj_MemTPtr(obj), 2, 0, tx, ty) == -1)
                Obj_Free(obj);
            else
                Obj_AddEnd(&Map_GetAddr(tx, ty)->objects, obj);
            return;
        }
        move_along(heading, 0x10, &fx, &fy);
        curP->x = fx >> 4;
        curP->y = fy >> 4;
    } while (--dist > 0);
}

/* Where the attacker's blow goes: a motion probe of radius wsize + 1, at a height set by
   the swing (towhere / 3 picks the low, middle or high third of the attacker; the player's
   pitch shifts it), wsize + 3 fine units ahead along the attacker's heading. Returns 1 with
   hitobj and hitloc set when it meets an object; on a wall it makes the spark
   (find_wall_coll) and returns 0. */
char far resolve_attack(void)
{
    struct Object far *att;
    struct MotionCalc calc;
    register int i;
    register int heading;

    curP = &calc;
    curP->index = fromwho;
    curP->radius = wsize + 1;
    curP->height = (wsize * 2 + 1) * 4;
    att = critdata + fromwho;
    curP->z = OBJ_Z(att) + ComObjData[OBJ_ITEM(att)].height * (towhere / 3) / 3;
    if (COOP_PLAYER(fromwho))
        curP->z += PlayerPitch / 0x200;
    hitz = curP->z + ComObjData[OBJ_ITEM(att)].height / 6;
    curP->x = (OBJ_HOMEX(att) << 3) + OBJ_FINEX(att);
    curP->y = (OBJ_HOMEY(att) << 3) + OBJ_FINEY(att);
    heading = (OBJ_HEADING(att) << 5) + OBJ_FINEHEAD(att);
    move_along(heading, wsize + 3, &curP->x, &curP->y);
    ObjectCheck(0, 1);
    if (curP->found) {
        process_objlist();
        if (curP->count == 0)
            return 0;
        i = set_hitobj(curP);
        if (i < 0)
            return 0;
        hitloc = pickloc(oCollisions[i].bottom, oCollisions[i].top,
                         curP->z, curP->height + curP->z);
        hitobj = oCollisions[i].link.f.index;
        return 1;
    }
    TerrainCheck(0);
    if ((curP->hits0 | curP->hits1) & 0x300) {
        curP->x = (OBJ_HOMEX(att) << 3) + OBJ_FINEX(att);
        curP->y = (OBJ_HOMEY(att) << 3) + OBJ_FINEY(att);
        find_wall_coll(heading, wsize + 3, curP);
    }
    return 0;
}

/* The to-hit roll. Returns 0 for a hit and 1 or 2 for a miss (1 - the skill_check
   result). A non-critter defender is always hit; the player striking a door may wear his
   weapon (a chance of 2 * (door id & 7) in 12). Against a critter: the attack skill plus
   hitangle is checked against the critter's defence, less the player's protection at that
   location (cmbModTH) when the player is the target. A critical
   (result 2) sets criti and multiplies the damage by 1 or 2 at even odds; if the player is
   the one hit, the screen flashes and the armour at that location takes wear. A bad miss
   (result -1) by the player against a critter not marked passive wears his weapon. */
#ifndef __TURBOC__
#define frp_check shared_frp_check
#endif
int far frp_check(int attacker, int defender)
{
    struct Object far *def;
    struct Creature *cr;
    register int slot;
    register int result;

    def = Obj_IntTMem(defender);
    if (OBJ_MAJOR(def) != MAJOR_CREATURE) {
        if (COOP_PLAYER(attacker) && OBJ_CLASS(def) == CLASS_DOOR
            && (int)(rand() * 12L / 0x8000L) < (def->id & 7) << 1) {
            slot = INV_WEAPON_HAND - player->lefty;
            DamageInventory(slot, rollem(2, 4), 4, 0, 1);
        }
        return 0;
    }
    cr = &Creature[OBJ_INMAJOR(def)];
    if (COOP_PLAYER(hitobj))
        askill -= cmbModTH[hitloc];
    result = skill_check(askill + hitangle, cr->defence);
    criti = 0;
    if (result == 2) {
        criti = 1;
        damage = damage * (((rand() & 0x1F) + 0x30) >> 5);
        if (COOP_PLAYER(defender)) {
            fill_FB(0xB8);
            if ((slot = hitloc + 1 & 3) == 3)
                slot += rand() % 5 == 0;
            else if (slot != 0 && slot <= 2)
                slot = player->lefty + INV_HAND;
            DamageInventory(slot, rollem(2, 4), 4, 1, 1);
        }
        return 0;
    }
    if (result == -1 && COOP_PLAYER(attacker)
        && !Creature[Obj_IntTMem(hitobj)->id & ID_INMAJOR].passive) {
        slot = INV_WEAPON_HAND - player->lefty;
        DamageInventory(slot, rollem(2, 3), 4, 0, 1);
    }
    return 1 - result;
}

#ifndef __TURBOC__
#undef frp_check
int far frp_check(int attacker,int defender)
{
    if(attacker!=defender && port_shared_player_index(attacker) && port_shared_player_index(defender))return 1;
    int old=port_shared_context(defender);
    int result=shared_frp_check(attacker,defender);
    port_shared_context_end(old);return result;
}
#endif

/* Rolls and applies a hit's damage. damage is turned into dice, (damage / 6)d6 plus
   1d(damage % 6), with a minimum of 2; the roll is scaled by power / 128 (the charge) and
   hitangle is added, so blows from the side and behind do more. A critter's armour at the
   hit location is subtracted (a location of 0xFF uses location 0; powerful critters'
   armour counts 5/3); easy mode halves damage to the player. The result, ddone, goes to
   damage_item with the damage type bits in type (DAMAGE.C's check_res lists them; melee
   is 4, physical). The rest is feedback: the hit sound (effect 4 for any attacker),
   the critter's health shown in the view frame when the player is the attacker, blood for
   critters that bleed (twice on a player's critical), and a dust effect otherwise. */
#ifndef __TURBOC__
#define do_damage shared_do_damage
#endif
void far do_damage(int type)
{
    int ddone;
    int quot;
    int rem;
    struct Object far *def;
    char result;
    int fx;
    register int item;
    register int level;

    def = Obj_IntTMem(hitobj);
    item = OBJ_ITEM(def);
    if (damage < 2)
        damage = 2;
    quot = damage / 6;
    rem = damage % 6;
    damage = 0;
    if (quot != 0)
        damage = rollem(quot, 6);
    if (rem != 0)
        damage += rollem(1, rem);
    ddone = damage * power >> 7;
    ddone += hitangle;
    if (COOP_PLAYER(hitobj))
        play_effect_here(3, 0x40, ddone << 2);
    else
        play_effect(4, (targx << 3) + OBJ_FINEX(def), (targy << 3) + OBJ_FINEY(def), ddone << 2);
    if (item >> 6 == MAJOR_CREATURE) {
        int armour;

        if ((armour = Creature[item & ID_INMAJOR].armour[hitloc % 4]) == 0xFF) {
            hitloc = hitloc & 4;
            armour = Creature[item & ID_INMAJOR].armour[0];
        }
        if (COOP_OTHER(hitobj) && OBJ_POWERFUL(def))
            armour = armour * 5 / 3;
        if (armour > ddone)
            ddone = 0;
        else
            ddone -= armour;
    }
    if ((level = ddone / 4) >= 4)
        level = 3;
    if (COOP_PLAYER(hitobj) && player->easy)
        ddone >>= 1;
    result = damage_item(def, Obj_IntTMem(fromwho), targx, targy, ddone, type);
    if (ddone == 0)
        return;
    if (fromwho == -1)
        return;
    if (hitloc >= 4) {
        hitz_tab[4] = -hitz;
        hitloc = 4;
    }
    if ((int)(UsPtr - critdata) == hitobj) {
        set_effect(0x20, level * 5);
        return;
    }
    if (item >> 6 == MAJOR_CREATURE) {
        if (COOP_PLAYER(fromwho)) {
            if (Creature[item & ID_INMAJOR].avghit != 0)
                fx = def->hp * 3 / Creature[item & ID_INMAJOR].avghit;
            else
                fx = 0;
            if (fx >= 3)
                fx = 2;
            set_screen_frame(7, 3 - fx);
        }
        if (Creature[item & ID_INMAJOR].blood) {
            put_effect(def, 0, 1, level, hitz_tab[hitloc], targx, targy);
            if (criti && COOP_PLAYER(fromwho))
                put_effect(def, 0, 1, level, hitz_tab[hitloc] + (rand() & 1) * 5 - 2, targx, targy);
            return;
        }
        result = 0;
    }
    if (result)
        def = 0;
    if (((item & ID_CLASS) >> 4) == CLASS_DOOR || item == ITEM_MOVING_DOOR) {
        /* def is 0 when the blow destroyed the door: then this reads the interrupt vector
           table (docs/PORT.md, "Null pointers") */
        if (OBJ_Z(FARNULLTRAP(def)) > hitz) {
            hitz = OBJ_Z(FARNULLTRAP(def)) + 2;
            put_effect(def, 0xB, 1, level, -hitz, targx, targy);
        } else
            put_effect(def, 0xB, 1, level, -hitz, targx, targy);
    } else
        put_effect(def, 0xB, 1, level, -hitz, targx, targy);
}

#ifndef __TURBOC__
#undef do_damage
void far do_damage(int type)
{
    int old=port_shared_context(hitobj);
    shared_do_damage(type);port_shared_context_end(old);
}
#endif

/* The sound of a miss. hit 0 is a swing at nothing (effect 10). Otherwise the blow was
   blocked: effect 7 for weapon kind 1 against armour kind 1 (metal), else effect 8. The
   weapon kind is the attacker's Creature weapon_kind, 1 for item id 1 or a non-mobile;
   for the player as target the armour is the item in the slot for the hit location, and
   leather counts as not metal. Always returns 0. */
char far do_miss(int hit)
{
    struct Object far *armour;
    unsigned char slot;
    char weapon;
    char victim;
    char fx;
    int item;

    if (hit == 0)
        play_effect_on_mobile(10, Obj_IntTMem(fromwho), 0);
    else {
        /* UW1: the attacker's item id goes into fromwho itself (UW2 a local), so the
           tests below compare an item id: fromwho == 1 is item 1 (the battle axe), never
           the player; the player's blocked blows take their weapon kind from his Creature
           record (0x3F), not from what he wields as in UW2 */
        fromwho = OBJ_ITEM(Obj_IntTMem(fromwho));
        if (fromwho == 1)
            weapon = 1;
        else if (fromwho < NUM_MOBILE)
            weapon = Creature[fromwho & ID_INMAJOR].weapon_kind;
        else
            weapon = 1;
        if (COOP_PLAYER(hitobj)) {
            armour = AskInventory(slot = hitloc + 1 & 3);
            item = OBJ_ITEM(FARNULLTRAP(armour));   /* read before the test: 19h for none */
            if (armour == 0)
                victim = 0;
            else if (item == ITEM_LEATHER_VEST || item == ITEM_LEATHER_LEGGINGS
                     || item == ITEM_LEATHER_GLOVES || item == ITEM_LEATHER_BOOTS
                     || item == ITEM_LEATHER_CAP)
                victim = 0;
            else
                victim = 1;
        } else if (hitobj < NUM_MOBILE)
            /* the attacker's armour kind, not the defender's (UW2's do_miss does the same
               through its attitem): probably meant hitobj's */
            victim = Creature[fromwho & ID_INMAJOR].armour_kind;
        else
            victim = 0;
        if (weapon == 1 && victim == 1)
            fx = 7;
        else
            fx = 8;
        play_effect_on_mobile(fx, Obj_IntTMem(hitobj), 0);
    }
    return 0;
}

/* hitangle: how far round from the defender's front the attacker stands, in eighths of
   a turn, 0 (face to face) to 4 (from behind). It is added to the attack skill and to the
   damage. */
void far compute_hitangle(void)
{
    hitangle = OBJ_HEADING(Obj_IntTMem(hitobj)) + 0xC - OBJ_HEADING(Obj_IntTMem(fromwho)) & 7;
    if (hitangle > 4)
        hitangle = 8 - hitangle;
}

/* One melee blow from fromwho, after the caller has set wsize, towhere, power, damage and
   askill. Critters do not hit their own side. A miss still calls damage_item with 0 damage
   (so the target notices it, inferred). Returns 1 for a hit. */
char far do_attack(void)
{
    int result;

#ifndef __TURBOC__
    if(getenv("UW_COOP_BOT_HOME"))fprintf(stderr,"SHARED MELEE actor=%d reach=%d power=%d skill=%d damage=%d\n",fromwho,wsize,power,askill,damage);
#endif
    if (!resolve_attack())
        return do_miss(0);
#ifndef __TURBOC__
    if(getenv("UW_COOP_BOT_HOME"))fprintf(stderr,"SHARED MELEE target=%d\n",hitobj);
#endif
    if (COOP_OTHER(fromwho) && COOP_OTHER(hitobj) && IsMobElem(Obj_IntTMem(hitobj))
        && !(OBJ_ALLY(Obj_IntTMem(hitobj)) ^ OBJ_ALLY(Obj_IntTMem(fromwho))))
        return 0;
    compute_hitangle();
    if ((result = frp_check(fromwho, hitobj)) != 0) {
        damage_item(Obj_IntTMem(hitobj), Obj_IntTMem(fromwho), targx, targy, 0, DMG_PHYSICAL);
        return do_miss(result);
    }
    do_damage(DMG_PHYSICAL);
    return 1;
}

/* Looks for the ammunition of missile weapon class weapon (Missile[].ammo, an offset
   from FIRST_MISSILE) in the inventory. Returns its slot, or -1 after printing 'Sorry, you
   have no <ammunition>.' ("UNNAMED" when get_name has no name). */
int far check_ammo(int weapon)
{
    int16 found;
    struct StaticObj fake;
    char buf[50];
    struct StaticObj *p;
    int ammo;

    ammo = Missile[weapon].ammo;
    if (FindObj(MAJOR_HACK, 1, ammo, 4, &found) == 0) {
        p = &fake;
        SET_ITEM(p, ammo + FIRST_MISSILE);
        scroll_print("Sorry, you have no ");
        if (!get_name(buf, (struct Object far *)p, 0, 1))
            strcpy(buf, "UNNAMED");
        scroll_print(buf);
        scroll_print(".\n");
        return -1;
    }
    return found;
}

/* The weapon in the player's weapon hand (slot 8 - lefty) and its data record: a Missile
   record for a bow or sling (returns 0, or -1 when there is no ammunition), else a
   Weapons record, else Weapons[15], the bare fist. Sets wsize, the weapon's reach, from
   ComObjData radius. Returns 1 for melee. */
int far GetPlayerWeapon(struct Weapon **wd, struct Object far **weap)
{
    register int item;

    *wd = 0;
    *weap = AskInventory(INV_WEAPON_HAND - player->lefty);
    if (*weap != 0) {
        if (((item = OBJ_ITEM(*weap)) >> 4) == CLASS_MISSILE) {
            if (Missile[item & ID_INCLASS].ammo >= 0 && Missile[item & ID_INCLASS].ammo < 0x10) {
                if (check_ammo(item & ID_INCLASS) >= 0) {
                    *wd = (struct Weapon *)&Missile[item & ID_INCLASS];
                    return 0;
                }
                mouse_release(1);
                return -1;
            }
        } else if ((item >> 4) == CLASS_WEAPON) {
            *wd = &Weapons[item & ID_INCLASS];
            wsize = ComObjData[item].radius;
        }
    }
    if (*wd == 0) {
        *wd = &Weapons[ITEM_FIST];
        wsize = ComObjData[ITEM_FIST].radius;
    }
    return 1;
}

/* Sets up the player's blow. Attack skill: half the attack skill plus the weapon's skill
   (fist for anything outside barehand..mace) plus dexterity / 7, plus 7 on easy.
   Damage: the weapon's damage byte for the swing's kind (slash, bash or stab, by swing_kind)
   plus strength / 9; bare handed, 2/5 of the barehand skill plus strength / 6 plus 4
   (strength is attr[0] of the player's Creature record). A weapon enchantment of major
   class 0xC adds (effect & 7) + 1 to damage when bit 3 of the effect is set, else to hit. */
void far DoPlayerWeapon(register struct Weapon *wd, struct Object far *weap, int swing)
{
    int16 major;
    int16 effect;
    char flag;
    register int skill;

    if ((skill = wd->skill) >= SKILL_MISSILE || skill < SKILL_BAREHAND)
        skill = SKILL_BAREHAND;
    askill = (player->skills[SKILL_ATTACK] >> 1) + player->skills[skill];
    askill += player->dexterity / 7;
    if (player->easy)
        askill += 7;
    if (skill == SKILL_BAREHAND)
        damage = player->skills[SKILL_BAREHAND] * 2 / 5 + Creature[ThePlayer->id & ID_INMAJOR].attr[ATTR_STR] / 6 + 4;
    else
        damage = wd->damage[swing_kind[swing - 1]] + Creature[ThePlayer->id & ID_INMAJOR].attr[ATTR_STR] / 9;
#ifndef __TURBOC__
    fromwho = Obj_MemTPtr(ThePlayer);
#else
    fromwho = 1;
#endif
    towhere = swing;
    if (weap != 0) {
        decode_obj_spell(weap, &major, &effect, (unsigned char *)&flag);
        if (!flag && major == 0xC) {
            if (effect & 8)
                damage += (effect & 7) + 1;
            else
                askill += (effect & 7) + 1;
        }
    }
}

/* Ends a bow or sling shot: resets the swing state, the power bar and the weapon frame,
   and gives the mouse cursor back. */
void far missile_finish(void)
{
    pQatt = -10;
    attackKey = -1;
    set_screen_frame(3, 0);
    GameInputMode -= 4;
    unforce_mouse_cursor(3);
    set_screen_frame(8, 4);
    fire_mode = 0;
}

/* Ends a blow: the weapon picture goes back to the drawn (frame 4) or sheathed (frame 6)
   pose and the power bar to empty. */
void far fin_attack(void)
{
    if (player->drawn)
        set_screen_frame(8, 4);
    else
        set_screen_frame(8, 6);
    set_screen_frame(3, 0);
}

/* Cancels any attack in progress (also leaving the aiming mode of a bow) and clears
   fromwho. */
void far clear_fight_state(void)
{
    if (fire_mode != 0 && swing_state == 0) {
        GameInputMode -= 4;
        unforce_mouse_cursor(3);
    }
    fin_attack();
    pQatt = 0;
    attackKey = -1;
    fromwho = -1;
}

/* The player's melee and missile attack, called each frame with swing, the ninth of the
   3D view clicked (1..9; 0 for none). pQatt < 0 is a swing in
   progress, its value -1 - swing kind. The weapon animation frame (weap_frame: 0..2 the
   wind-up, 3 the hold, 6 the strike) drives it: while the button or key is held the power
   bar (play_pow, up by the weapon's speed every 16 ticks to at most 100) charges; at the
   strike frame the power becomes min_charge plus the charged share of max_charge -
   min_charge and the
   blow is struck. A bow or sling instead switches the cursor to aim and fires on release
   (player_fire). Making a blow sets the player's noise (10 charging, 15 striking), which
   critters hear. */
void far player_attack(int swing)
{
    int16 charge;
    char held STACK_JUNK(0);    /* read before it is set when there is no attack key (as
                                   in UW2) */
    int tx;
    struct Weapon *wd;

    if (attackKey > 0)
        held = key_on[attackKey] != 0;
    held = held || (mouse_getbut(&charge) & 2);
    if (pQatt > 0)
        return;
    if (pQatt < 0) {
        if (weap_frame < 0 && pQatt <= -10) {
            fin_attack();
            pQatt = 0;
            attackKey = -1;
            return;
        }
        if (weap_frame > 6)
            return;
        wd = weapon_data;
        if (weap_frame < 3) {
            if (!held) {
                fin_attack();
                pQatt = 0;
                attackKey = -1;
            }
            return;
        }
        if (weap_frame == 3) {
            if (fire_mode != 0) {
                if (held || swing_state < 0) {
                    if (swing_state >= 0)
                        return;
                    set_screen_frame(3, 9);
                    swing_state = 0;
                    GameInputMode += 4;
                    force_mouse_cursor(ICON_CURSORS + 9);
                    return;
                }
                if (!held) {
                    if (mous_in_3d_p())
                        player_fire(OBJ_INCLASS(curr_weapon));
                    missile_finish();
                }
                return;
            }
            if (held) {
                playerdat->noise = 10;
                if (elapsed < 0) {
                    otime = GAME_TIME();
                    elapsed = 0;
                    return;
                }
                elapsed += (int)(GAME_TIME() - otime);
                otime = GAME_TIME();
                while (elapsed > 0x10) {
                    play_pow += wd->speed;
                    if (play_pow > 100)
                        play_pow = 100;
                    set_screen_frame(3, play_pow / 12 + 1);
                    elapsed -= 0x10;
                }
                return;
            }
            if (pQatt > -5) {
                set_screen_frame(8, -pQatt - 1);
                pQatt = -5;
            }
            return;
        }
        if (weap_frame != 6)
            return;
        if (pQatt <= -10)
            return;
        set_screen_frame(3, 0);
        charge = wd->max_charge - wd->min_charge;
        charge = charge * play_pow / 100;
        play_pow = wd->min_charge + charge;
        playerdat->noise = 15;
        power = play_pow;
        DoPlayerWeapon(wd, curr_weapon, swing_state);
        do_attack();
        pQatt = -10;
        return;
    }
    if (!player->drawn)
        return;
    if (swing == 0)
        return;
    if (weap_frame != -1)
        return;
    swing_state = swing;
    if ((tx = GetPlayerWeapon(&weapon_data, &curr_weapon)) < 0)
        return;
    fire_mode = tx == 0;
    if (fire_mode != 0)
        swing_state = -1;
    pQatt = -1 - swing_kind[swing - 1];
    attackKey = TABLE_PREV(swing_keys, swing / 3 - 1, swing_kind, 9);
    set_screen_frame(8, -pQatt - 1);
    set_screen_frame(3, 1);
    play_pow = 0;
    elapsed = -1;
}

/* A missile (or anything thrown) has hit def: fills in the attack globals as for a
   blow, full power and no angle bonus, a hit location from the heights plus 4, and the
   missile's damage dmg, then do_damage with damage type bits type. */
void far missile_thwack(int attacker, struct Object far *missile, struct Object far *def,
                        int x, int y, int dmg, unsigned char type)
{
    targx = x;
    targy = y;
    criti = 0;
    hitangle = 0;
    hitz = OBJ_Z(missile) + ComObjData[OBJ_ITEM(missile)].height / 2;
    power = 0x80;
    fromwho = attacker;
    hitobj = Obj_MemTPtr(def);
    hitloc = pickloc(OBJ_Z(def), OBJ_Z(def) + ComObjData[OBJ_ITEM(def)].height,
                     OBJ_Z(missile), OBJ_Z(missile) + ComObjData[OBJ_ITEM(missile)].height) + 4;
    damage = dmg;
    if (def == ThePlayer)
        play_effect_here(3, 0, 0);
    else if (IsMobElem(def))
        play_effect_on_mobile(4, def, 0);
    do_damage(type);
}

/* A critter's blow. damage and to-hit come from its Creature record's attack type
   (attacks[type].damage plus strength / 5; attacks[type].chance plus half its equip
   value); a powerful critter adds 7..12 to hit and 4..15 damage. charge is the power.
   A hit on the player by a poisonous attack (poison above his present level) poisons him
   at that level unless he resists poison (check_res, type 0x10).
   Returns 1 for a hit. */
char far critter_attack(struct Object far *npc, int swing, unsigned char charge, int type,
                        int poison)
{
    char result;
    struct Creature *cr;

    wsize = 2;
    fromwho = Obj_MemTPtr(npc);
    towhere = swing;
    power = charge;
    cr = &Creature[npc->id & ID_INMAJOR];
    damage = cr->attacks[type].damage;
    damage += Creature[npc->id & ID_INMAJOR].attr[ATTR_STR] / 5;
    askill = cr->attacks[type].chance + (cr->equip >> 1);
    if (OBJ_POWERFUL(npc)) {
        askill += rand() % 6 + 7;
        damage += rand() % 12 + 4;
    }
    result = do_attack();
    #ifndef __TURBOC__
    int old=port_shared_context(hitobj);
#endif
    if (result && COOP_PLAYER(hitobj) && player->poison < poison) {
        if (check_res(ThePlayer, 1, DMG_POISON))
            player->poison = poison;
    }
#ifndef __TURBOC__
    port_shared_context_end(old);
#endif
    return result;
}

/* name: the listing's (UW2's seg024_24E9_1AC7, which has no FM Towns counterpart between
   critter_attack and player_killed_a). It cancels a player blow that is still queued
   (pQatt > 0) and resets the weapon and power displays. */
void far seg022_230E_16F5(void)
{
    if (pQatt > 0) {
        pQatt = 0;
        attackKey = -1;
        set_screen_frame(3, 0);
        set_screen_frame(8, 4);
    }
}

/* Experience for killing a critter: exp * 4 + 2d(exp), from its Creature record, times
   1.5..3 for a powerful one, through player_get_exp (SKILLCHK.C). Also makes the dragons
   of the frame nod (screen frame 4) and starts music theme 9. */
void far player_killed_a(struct Object far *npc)
{
    int exp;

    if (OBJ_MAJOR(npc) == MAJOR_CREATURE) {
        set_screen_frame(4, 2);
        set_new_music(MUSIC_VICTORY);
        exp = Creature[npc->id & ID_INMAJOR].exp;
        exp = exp * 4 + rollem(2, exp);
        /* the product is a 16-bit int (imul, then cwd before the idiv): for a critter worth
           about 120 or more it can wrap, and the kill can then take experience away; the
           host's int kept 32 bits. UW2 has (int32)exp here, a fix DOS UW1 does not have */
        if (OBJ_POWERFUL(npc))
            exp = (int16)(exp * (rand() % 24 + 24)) / 16;
        player_get_exp(exp);
    }
}
