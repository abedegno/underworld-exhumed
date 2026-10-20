/* target: seg008_1B2A */
/* opts: -mm -1 -G -O -Y -d */
/* The player's physics: the terrain the player stands in, the simple step-and-turn
   movement, the per-tick motion setup from the input, applying the result to the player
   object, and moving the player between tiles. The whole of DOS resident segment
   seg008_1B2A, in original order. Seeded from UW2Decomp's src/motion/PHYSICS.C (UW2's
   seg008_1B09). Function and global names are UW2's (the FM Towns originals) where the
   routine is the same one; UW1 has no symbols of its own.

   UW1 against UW2: no ice and no water currents (munge_vectors and their state are
   absent), no change_GrSq, hgt_change or player_newsq (the tile change is written out
   where UW2 calls change_GrSq, and there are no pressure plate or light checks), and
   three functions of its own at the end, before newFPS: BouncePlayer, DoTrapScreenShake
   and QuakeTrap (the listing's names), a quake trap's shake and throw. player_setup has
   no third argument, newFPS scales the speeds out of 10 with no swimming skill, and
   do_player_input has no paralysis test. struct Player is UW1's (player.h).

   The player's physics record is PN (motion.h, owned by MOTION.C) with the handler PT,
   whose special function is player_sqhandler. Each tick move_player calls
   set_player_phys_params (input into PN), MOTION.C's do_physics, and phys_affect_player
   (PN back into the player object, ThePlayer, and fall damage). simple_fizix is the
   other way to move: a fixed step or a 45 degree turn.

   The movement state (newFPS's state, the player's fps): 0 on foot, 1 swimming, 2 on
   lava, 3 (ice in UW2; nothing sets it here), and in the air 4 levitating, 5 flying, 6 slow falling. It sets
   the motion state's low bits and the speeds pFPS, scaled from Run_FPS, Side_FPS and
   Back_FPS. motionbits holds the motion spells: 1 leap (lower gravity when jumping),
   2 slow fall, 4 levitate, 8 water walk (no swimming), 0x10 fly.

   GrSq is the player's tile as an index into mapdata.

   name: UW2Decomp's (its map/filenames.tsv: the player's physics, simple_fizix and
   fizix_update; the job of System Shock's PHYSICS.C).

   Entry points: player_setup (UWEDIT.C's new_player_pos, SPELLS.C, MAINMENU.C);
   set_player_phys_params and phys_affect_player (PLAYMOVE.C's move_player); simple_fizix
   (PLAYMOVE.C's player_simple_move); parse_player_terr, fizix_update and newFPS
   (PLAYDATA.C); QuakeTrap_seg008_DE7 (TRIGGER.C); EtherealVoidSpecialEffects_seg008_150
   (INTERACT.C). Data owned: GrSq, pTurn, pFPS and the speeds, lastTerr, motionbits,
   fiz_update, MaxPlayerAccel. */

#include <stdlib.h>
#include "event.h"
#include "gfx.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* Uninitialised data, DS:2492..24A1 (UW2 DS:229A onwards). saved_dz is the four bytes
   at DS:249C, which nothing in the game references. */
/* match: Turbo C lays _BSS out in an order set by the names, not by declaration. */
/* name: the publics are UW2's FM Towns names; saved_dz is UW2Decomp's name for the
   unreferenced static, chosen there for layout, and it lands where UW1 has it too. */
int16 pTurn;                            /* the turn rate for the current motion state */
int16 GrSq;                             /* the player's tile, as an index into mapdata */
int16 pFPS[3];                          /* forward, side and backward speeds */
static int32 saved_dz;
int16 lastTerr;                         /* the terrain bits parse_player_terr last saw */

/* Initialised data, DS:C8 onwards. Speeds are in PN.speed's units, 0x2F to an object's
   OBJ_SPEED step (OBJPHYS.C). MaxPlayerAccel is the most the speed may change in a tick;
   newFPS lowers it when the player carries more than half the most they can. */
int16 MaxPlayerAccel = 0x60;
int16 Back_FPS = 0xBC;
int16 Side_FPS = 0xEB;
int16 Run_FPS = 0x3AC;
int16 plyMoType = 0;
unsigned char motionbits = 0;
unsigned char fiz_update = 1;

/* Starts swimming if terr has the water bit: swim_count (the head's dip below the
   surface) and leaving combat mode. Returns 1 if swimming. */
char far water_set(int terr)
{
    char swimming;

    swimming = 0;
    if (terr & FOOT_WATER)
    {
        swimming = 1;
        player->swim_count = 0x60;
        punt_fightmode();
    }
    else
        player->swim_count = 0x10;
    return swimming;
}

/* The terrain byte under the player (MOTION.C's set_resterr) to the movement state,
   when it changes or with force: water (unless Water Walk), lava, or in the air
   levitating, flying or slow falling (see the top of the file). In the air every tick:
   levitating or flying cancels gravity and damps vertical speed by 4/5; otherwise
   gravity is -4, and slow fall holds the fall at -94 and halves the speed. */
void far parse_player_terr(int terr, char force)
{
    char swimming;
    int state;

    if (lastTerr != terr || force)
    {
        swimming = 0;
        state = FPS_WALK;
        lastTerr = terr;
        if (terr & (FOOT_WATER | FOOT_SHORE))
        {
            if ((motionbits & MB_WATER_WALK) == 0)
            {
                swimming = water_set(terr);
                state = FPS_SWIM;
            }
        }
        else if (terr & FOOT_LAVA)
            state = FPS_LAVA;
        else if (terr & FOOT_AIR)
        {
            if (motionbits & MB_LEVITATE)
                state = FPS_LEVITATE;
            else if (motionbits & MB_FLY)
                state = FPS_FLY;
            else if (motionbits & MB_SLOW_FALL)
                state = FPS_SLOW_FALL;
        }
        newFPS(state);
        if (!swimming)
            player->swim_count = 0;
    }
    if (terr & FOOT_AIR)
    {
        if (motionbits & (MB_LEVITATE | MB_FLY))
        {
            PN.acc[2] = 0;
            if (abs(PN.vel[2]) > 10)
                PN.vel[2] = (PN.vel[2] << 2) / 5;
            else
                PN.vel[2] = 0;
        }
        else
        {
            if (PN.acc[2] == 0)
                PN.acc[2] = -4;
            if (motionbits & MB_SLOW_FALL && PN.vel[2] <= -94)
            {
                PN.vel[2] = -94;
                if (PN.speed > 20)
                    PN.speed = PN.speed / 2;
                else
                    PN.speed = 0;
            }
        }
    }
}

/* A flash, a hit to the player's hit points scaled by how many are left, a shake and a
   random compass frame. In UW2 it has no callers; UW1 calls it (the listing's name: the
   Ethereal Void's effects). */
void far EtherealVoidSpecialEffects_seg008_150(void)
{
    int hp;

    fill_FB(0xB5);
    hp = ThePlayer->hp;
    if (hp > 100)
        hp -= rand() % 6;
    else if (hp > 50)
        hp -= rand() % 4;
    else if (hp > 20 && (rand() & 3) < 1)
        hp -= rand() % 3;
    else if (hp > 1 && !(rand() & 7))
        hp--;
    ThePlayer->hp = hp;
    if (rand() % 12)
        set_effect(0x40, rand() % 30 + 15);
    set_screen_frame(2, rand() & 0xF);
}

/* The step-and-turn movement (the step keys and the arrows over the view): turn -1 or 1
   turns 45 degrees; 0 steps half a tile forward but refuses to step into a different
   terrain or off a ledge; 2 takes the same step without those checks (the local is called
   backwards, but the step is forward); -2 steps a quarter tile back. A step is tested with
   can_place and then made directly, without do_physics: the player object moves tile,
   lands at nvokHgt (or starts falling) and move triggers it now overlaps fire. Returns 1
   if the player moved or turned. Only while not falling and moving slower than
   MaxPlayerAccel. */
char far simple_fizix(int turn)
{
    int dist;
    int16 x;
    int16 y;
    char backwards;                     /* UW1: signed chars (cbw) */
    char flying;
    int sq;
    struct MotionCalc near *oldP;
    struct Object far *obj;
    struct MotionCalc calc;
    register int i;
    register int heading;

    if (PN.acc[2] == 0 && PN.speed < MaxPlayerAccel)
    {
        backwards = 0;
        flying = 0;
        switch (turn)
        {
        case 2:
            backwards = 1;
        case 0:
            dist = 0x80;
            heading = PlayerFacing / 256;
            break;
        case -2:
            dist = 0x40;
            heading = (unsigned)(PlayerFacing + 0x8000) >> 8;
            break;
        default:
            if (PlayerFacing & 0x1FFF)
                PlayerFacing = (PlayerFacing & 0xE000) + ((turn > 0 ? 1 : 0) << 13);
            else
                PlayerFacing = PlayerFacing + (turn << 13);
            SET_HEADING(ThePlayer, PlayerFacing >> 13);
            SET_FINEHEAD(ThePlayer, PlayerFacing >> 8);
            return 1;
        }
        if (motionbits & (MB_LEVITATE | MB_FLY))
            flying = 1;
        x = PN.x;
        y = PN.y;
        move_along(heading, dist, &x, &y);
        if (can_place(ITEM_ADVENTURER, 1, x / 32, y / 32, ThePlayer->pos & POS_Z, backwards | flying, 8))
        {
            if (!backwards && nvokTerr != FOOT_FLOOR && nvokTerr != lastTerr
                && (nvokTerr != FOOT_AIR || !flying))
                return 0;
            {
                PN.x = x;
                PN.y = y;
                /* UW1: the tile change written out (UW2 calls change_GrSq) */
                if ((sq = (PN.x >> 8) + ((PN.y >> 8) << 6)) != GrSq)
                {
                    if (GrSq != -1)
                        Obj_Rem(&(mapdata + GrSq)->objects, ThePlayer);
                    GrSq = sq;
                    Obj_Add(&(mapdata + GrSq)->objects, ThePlayer);
                    SET_HOMEX(ThePlayer, PN.x >> 8);
                    SET_HOMEY(ThePlayer, PN.y >> 8);
                }
                SET_FINEX_UNSIGNED(ThePlayer, (PN.x >> 5) & 7);
                SET_FINEY(ThePlayer, (PN.y >> 5) & 7);
                if (!(backwards || flying) || (PN.z >> 3) - 8 <= nvokHgt)
                {
                    SET_Z(ThePlayer, nvokHgt);
                    PN.z = nvokHgt << 3;
                }
                else if (PN.acc[2] == 0 && !flying)
                    PN.acc[2] = -4;
                parse_player_terr(nvokTerr, 0);
                SET_FRAME(ThePlayer, ((unsigned)GAME_TIME() & 0xFF) >> 6);
                oldP = curP;
                curP = &calc;
                curP->index = 1;
                curP->radius = ComObjData[ITEM_ADVENTURER].radius;
                curP->height = ComObjData[ITEM_ADVENTURER].height;
                curP->x = x / 32;
                curP->y = y / 32;
                curP->z = ThePlayer->pos & POS_Z;
                ObjectCheck(0, 0);
                process_objlist();
                for (i = curP->first; i < curP->first + curP->count; i++)
                {
                    obj = Obj_PtrTMem(&oCollisions[i].link);
                    if (OBJ_ITEM(obj) == ITEM_MOVE_TRIGGER)
                        UseTrigger(ThePlayer, 0L, obj, 0);
                }
                curP = oldP;
                return 1;
            }
        }
    }
    return 0;
}

/* Sets PN up for a tick of rate time units. On the ground the input (do_player_input)
   sets a wanted speed, approached by at most MaxPlayerAccel and capped at the run speed;
   in the air the player can only turn. Also sets PN.flags 0x80 (slide along walls) when
   nothing accelerates the player or when levitating or flying, and PT.ignore 0x1000 (no
   falling) when levitating or flying. UW1 has no ice or currents. */
void far set_player_phys_params(register int rate)
{
    int16 speed;
    register int d;

    speed = 0;
    if (PN.acc[2] == 0)
    {
        do_player_input(PlayerInput, rate, &speed);
        if (PN.acc[2] == 0)
        {
            d = speed - PN.speed;
            if (abs(d) > MaxPlayerAccel)
                d = (d > 0 ? 1 : -1) * MaxPlayerAccel;
            PN.speed += d;
            if (PN.speed > pFPS[0])
                PN.speed = pFPS[0];
            else if (PN.speed < 0)
                PN.speed = 0;
        }
    }
    if (PN.acc[2] != 0)
        PlayerFacing += rate * PlayerTurn * (TurnInpRate / 4) / 4;
    PN.time = rate;
    PN.bounce = 5;
    PN.flags = 0;
    if (!(PN.acc[0] | PN.acc[1] | PN.acc[2]))
        PN.flags = 0x80;
    if (PN.speed == 0)
        PlayerHeading = PlayerFacing;
    PN.heading = PlayerHeading;
    PT.ignore = 0;
    if (motionbits & (MB_LEVITATE | MB_FLY))
    {
        PT.ignore = 0x1000;
        PN.flags = 0x80;
    }
    PN.impact = 0;
}

/* Puts the player in the middle of tile x, y at the floor's height (higher on a tile
   with tile_walls bit 0x20). Resets PN and PT, links ThePlayer into the tile and works
   out the terrain and movement state. UW1: no third argument (UW2 can start the player
   near the ceiling, falling), and the tile change is written out. */
void far player_setup(register int x, register int y)
{
    struct MotionCalc calc;

    if (GrSq >= 0)
        Obj_Rem(&(mapdata + GrSq)->objects, ThePlayer);
    PT.special = (unsigned char (far *)(uint16 *))player_sqhandler;
    PT.mask = 0x1100;
    PT.ignore = 0;
    PN.speed = 0;
    PN.acc[0] = PN.acc[1] = PN.acc[2] = 0;
    PN.vel[0] = PN.vel[1] = PN.vel[2] = 0;
    PN.x = (x << 8) + 0x80;
    PN.y = (y << 8) + 0x80;
    PN.b24 = 8;
#ifndef __TURBOC__
    PN.index = port_shared_on() ? Obj_MemTPtr(ThePlayer) : 1;
#else
    PN.index = 1;
#endif
    GrSq = x + (y << 6);
    PN.z = hgt_val[mapdata[GrSq].height];
    if (tile_walls[mapdata[GrSq].type] & 0x20)
        PN.z += 0x20;
    SET_Z(ThePlayer, PN.z >> 3);
    SET_HOMEX(ThePlayer, x);
    SET_HOMEY(ThePlayer, y);
    SET_FINEX_UNSIGNED(ThePlayer, 3);
    SET_FINEY(ThePlayer, 3);
    SET_SEQ(ThePlayer, 0x2C);
    ThePlayer->qn.f.next = ThePlayer->qn.f.quality = 0;
    curP = &calc;
    curP->index = Obj_MemTPtr(ThePlayer);
    curP->radius = ComObjData[ITEM_ADVENTURER].radius;
    curP->height = ComObjData[ITEM_ADVENTURER].height;
    curP->x = (x << 3) + 3;
    curP->y = (y << 3) + 3;
    curP->z = PN.z >> 3;
    TerrainCheck(PN.b24);
    PN.terrain = set_resterr(curP->hits0 | curP->hits1);
    parse_player_terr(PN.terrain, 0);
    playerMod[0] = 0;
    parse_effect();
    fiz_update = 1;
    Obj_Add(&(mapdata + GrSq)->objects, ThePlayer);
}

/* After do_physics: copies PN to the player object (tile, fine position, height,
   animation frame), stops the player after a wall hit, turns the facing towards the
   heading by 0x400 a tick while sliding along a wall, and turns PN.impact into damage:
   impact >> 8, doubled in a fall, cut by an Acrobat skill check, dealt above 3, with a
   thud above 1. */
void far phys_affect_player(void)
{
    int dmg;
    int vol;
    register int16 h;
    register int sq;

    /* UW1: the tile change written out (UW2 calls change_GrSq and player_newsq) */
    if ((sq = (PN.x >> 8) + ((PN.y >> 8) << 6)) != GrSq)
    {
        if (GrSq != -1)
            Obj_Rem(&(mapdata + GrSq)->objects, ThePlayer);
        GrSq = sq;
        Obj_Add(&(mapdata + GrSq)->objects, ThePlayer);
        SET_HOMEX(ThePlayer, PN.x >> 8);
        SET_HOMEY(ThePlayer, PN.y >> 8);
    }
    SET_FINEX_UNSIGNED(ThePlayer, (PN.x >> 5) & 7);
    SET_FINEY(ThePlayer, (PN.y >> 5) & 7);
    SET_Z(ThePlayer, PN.z >> 3);
    SET_FRAME(ThePlayer, ((unsigned)GAME_TIME() & 0xFF) >> 6);
    if (PN.impact && PN.heading == PlayerHeading)
        PN.speed = 0;
    if (PN.heading != PlayerHeading)
    {
        PlayerHeading = PN.heading;
        h = PN.heading - (plyMoType << 14);
#ifndef __TURBOC__
        if ((PN.flags & 0x80) && !ENHANCED(ENH_FREE_HEADING))  /* port only: --enhance free-heading */
#else
        if (PN.flags & 0x80)
#endif
        {
            if (abs((int16)(PlayerFacing - h)) < 0x400)
                PlayerFacing = h;
            else if ((uint16)(PlayerFacing - h) < 0x7FFF)
                PlayerFacing -= 0x400;
            else
                PlayerFacing += 0x400;
        }
    }
    SET_HEADING(ThePlayer, PlayerFacing >> 13);
    SET_FINEHEAD(ThePlayer, PlayerFacing >> 8);
    if (PN.impact)
    {
        if (PN.bounce > 0)
        {
            dmg = PN.impact >> 8;
            if (PN.vel[2] != 0)
                dmg <<= 1;
            if (skill_check(player->skills[SKILL_ACROBAT], dmg << 1) > 0)
                dmg = dmg * (30 - player->skills[SKILL_ACROBAT]) / 30;
            if (dmg > 3)
                damage_item(ThePlayer, 0L, 0, 0, dmg, 0);
            if (dmg > 1 || (PN.terrain & FOOT_AIR))
            {
                vol = (dmg << 2) - 60;
                play_effect_here(0xF, 0x40, vol);
            }
        }
        PN.impact = 0;
    }
    parse_player_terr(PN.terrain, 0);
    fiz_update = 0;
}

/* PT's special function (called by MOTION.C's check_positions with the collision
   state): a player walking slowly (under 3/10 of the run speed) off a ledge is stopped
   at the edge instead (returns 1, so the step is undone). UW1: swimming or not. */
/* name: UW2's; the listing makes no procedure of it (it is the tail of the target
   table's phys_affect_player row), and player_setup stores its address in PT. */
char far player_sqhandler(uint16 *w)
{
    if ((*w & 0x1000) && PN.vel[2] == 0 && PN.speed * 10 < pFPS[0] * 3)
    {
        PN.vel[0] = PN.vel[1] = 0;
        return 1;
    }
    return 0;
}

/* PlayerInput to a heading and wanted speed: 0 stop, 1 forward with the mouse or key
   rates (ForwInpRate, TurnInpRate), 8 back, 9 and 10 sideways, 6 a running jump
   (starts at half the run speed, then as 7), 7 a jump (vertical speed 0x263, less above
   z 0x280 and again above 0x2C0, with gravity -4, or -2 with Leap), 12 and 13 up and
   down while levitating or flying. UW1 has no paralysis test. */
void far do_player_input(int input, int rate, register int16 *speed)
{
    register int16 h;

    h = PlayerFacing;
    switch (input)
    {
    case PIN_FORWARD:
        PlayerFacing += rate * PlayerTurn * (TurnInpRate / 4) / 4;
        PlayerHeading = h = PlayerFacing;
        *speed = pFPS[0] * (ForwInpRate >> 2) / 32;
        plyMoType = 0;
        break;
    case PIN_RIGHT:
        h += 0x4000;
        *speed = pFPS[1];
        plyMoType = 1;
        break;
    case PIN_LEFT:
        h -= 0x4000;
        *speed = pFPS[1];
        plyMoType = -1;
        break;
    case PIN_BACK:
        h -= 0x8000;
        *speed = pFPS[2];
        plyMoType = -2;
        break;
    case PIN_RUN_JUMP:
        if (PN.vel[2] != 0 || PN.acc[2] != 0 || PN.speed != 0)
            break;
        PlayerHeading = h = PlayerFacing;
        PN.speed = *speed = pFPS[0] / 2;
        plyMoType = 0;
    case PIN_JUMP:
        PN.vel[2] = 0x263;
        if (PN.z > 0x280)
        {
            PN.vel[2] = PN.vel[2] * 5 / 6;
            if (PN.z > 0x2C0)
                PN.vel[2] = (PN.vel[2] << 1) / 3;
        }
        if (motionbits & MB_LEAP)
            PN.acc[2] = -2;
        else
            PN.acc[2] = -4;
        return;
    case PIN_UP:
        plyMoType = 0;
        PN.vel[2] = 0x8D;
        PN.acc[2] = 0;
        break;
    case PIN_DOWN:
        plyMoType = 0;
        PN.vel[2] = -0x8D;
        PN.acc[2] = 0;
        break;
    case PIN_NONE:
        *speed = 0;
        return;
    }
    PlayerHeading = h;
}

/* name: UW2's (FM Towns phys_bounce_up_): for the player, pitch 0x8D unless jumping,
   dz 0. */
void far phys_bounce_up(struct Object far *obj)
{
    if (obj == ThePlayer)
    {
        if ((PN.terrain & FOOT_AIR) == 0)
            PN.vel[2] = 0x8D;
        PN.acc[2] = 0;
    }
}

/* Recomputes the movement state from the current terrain, after the motion spells
   change. */
void far fizix_update(void)
{
    parse_player_terr(PN.terrain, 1);
    fiz_update = 1;
}

/* Throws the player up by intensity (vertical speed intensity * 0x2F / 4, gravity -2
   unless already falling at -4) and halves the horizontal speed. No UW2 counterpart;
   the listing's name. */
void far BouncePlayer_seg008_D9E(int intensity)
{
    if (PN.acc[2] != -4)
        PN.acc[2] = -2;
    PN.vel[2] = intensity * 0x2F / 4;
    PN.vel[0] /= 2;
    PN.vel[1] /= 2;
}

/* A screen shake (set_effect 0x40, 30); the argument is not used. No UW2 counterpart;
   the listing's name. */
void far DoTrapScreenShake_seg008_DD6(int intensity)
{
    set_effect(0x40, 0x1E);
}

/* A quake: bit 0 of type shakes the screen, bit 1 throws the player up by intensity. No
   UW2 counterpart; the listing's name (a trap calls it). */
void far QuakeTrap_seg008_DE7(int type, int intensity)
{
    if (type & 1)
        DoTrapScreenShake_seg008_DD6(intensity);
    if (type & 2)
        BouncePlayer_seg008_D9E(intensity);
}

/* Sets the movement state (-1 keeps the player's fps) and from it the speeds, out of 10
   of the full ones (ratios), and the turn rate likewise on the ground. UW1: no swimming
   skill in the swimming speed. */
void far newFPS(char state)
{
    unsigned char ratios[7] = { 10, 3, 5, 10, 1, 7, 2 };
    unsigned char trans[7] = { 0, MS_SWIM, MS_LAVA, MS_ICE, MS_FLOAT, MS_FLOAT, 0 };

    if (state == -1)
        state = player->fps;
    else
    {
        player->motion_state = (player->motion_state & 0xE0) + trans[state];
        player->fps = state;
    }
    pFPS[0] = Run_FPS * ratios[state] / 10;
    pFPS[1] = Side_FPS * ratios[state] / 10;
    pFPS[2] = Back_FPS * ratios[state] / 10;
    if (state < 4)
        pTurn = PlayerTurn * ratios[state] / 10;
    else
        pTurn = PlayerTurn;
    if (player->max_weight && player->weight * 2 > player->max_weight)
        MaxPlayerAccel = 0x60 - player->weight * 0x60 / (player->max_weight * 2);
    else
        MaxPlayerAccel = 0x60;
}
