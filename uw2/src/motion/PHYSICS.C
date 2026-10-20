/* target: seg008_1B09 */
/* opts: -mm -1 -G -O -Y -d */
/* The player's physics: the terrain the player stands in, the simple step-and-turn
   movement, the per-tick motion setup from the input and the floor, applying the result
   to the player object, and moving the player between tiles. The whole of DOS resident
   segment seg008_1B09, in original order. Function and global names are the originals
   from the FM Towns symbol table where it has them.

   The player's physics record is PN (motion.h, owned by MOTION.C) with the handler PT,
   whose special function is player_sqhandler. Each tick PLAYMOVE.C's move_player calls
   set_player_phys_params (input, ice and currents into PN), MOTION.C's do_physics, and
   phys_affect_player (PN back into the player object, ThePlayer, and fall damage).
   simple_fizix is the other way to move: a fixed step or a 45 degree turn, for the
   step keys and the arrows over the view.

   The movement state (newFPS's state, player->fps): 0 on foot, 1 swimming, 2 on lava,
   3 on ice, and in the air 4 levitating, 5 flying, 6 slow falling. It sets
   player->motion_state's low bits (1 swimming, 2 lava, 4 ice, 8 levitating or flying;
   the high bits 0x20, 0x40 and 0x80 are PLAYMOVE.C's screen shakes) and the speeds
   pFPS, scaled from Run_FPS, Side_FPS and Back_FPS. motionbits holds the motion spells,
   bit minor - 1 for spell class 1 (game/PLAYDATA.C), which the Guide lists as 1 Leap,
   2 Slow Fall, 3 Levitate, 4 Water Walk, 5 Fly, 6 Bouncing: so 1 leap (lower gravity
   when jumping), 2 slow fall, 4 levitate, 8 water walk (no swimming), 0x10 fly, 0x20
   bouncing (MOTION.C's do_zbounce).

   GrSq is the player's tile as an index into mapdata; change_GrSq moves the player
   object between tile lists, runs pressure plates and updates the light.

   name: inferred (map/filenames.tsv: the player's physics, simple_fizix and fizix_update;
   the job of System Shock's PHYSICS.C, though that file drives the EDMS physics library
   and shares no code with this one). */

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

/* match: declared here, not in map.h: LIGHTING.C defines set_light(signed char), and this
   file's callers push an int (cbw). */
void far set_light(int level);

/* Uninitialised data, DS:229A onwards. saved_dz is the four bytes at DS:22B0, which
   nothing in the game references. */
/* match: Turbo C lays _BSS out in an order set by the names, not by declaration. */
/* name: the publics are FM Towns names; the statics have none there (FM Towns keeps them
   as unnamed statics), so old_dz, lasth, lasts and saved_dz were chosen from names
   compiled as probes because they land where the EXE has them. */
int16 oldh;                             /* the heading before this tick's ice or current */
int16 olds;                             /* the speed before it */
unsigned char frictionless;             /* standing on ice */
static int16 old_dz;                    /* PN.dz when the motion was set up */
int16 pTurn;                            /* the turn rate for the current motion state */
int16 GrSq;                             /* the player's tile, as an index into mapdata */
int16 pFPS[3];                          /* forward, side and backward speeds */
static int16 lasth;                     /* the heading and speed a current set, or -1 */
static int16 lasts;
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
/* The kind of slope or current carrying the player, -1 for none. No FM Towns name. */
static int16 slide = -1;

/* Starts swimming if terr has the water bit: swim_count (the head's dip below the
   surface, PLAYMOVE.C's parse_effect) and leaving combat mode. Returns 1 if swimming. */
unsigned char far water_set(int terr)
{
    unsigned char swimming;

    swimming = 0;
    if (terr & 2)
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
   when it changes or with force: water (unless Water Walk), lava, ice, or in the air
   levitating, flying or slow falling (see the top of the file). In the air every tick:
   levitating or flying cancels gravity and damps vertical speed by 4/5; otherwise
   gravity is -4, and slow fall holds the fall at -94 and halves the speed. */
void far parse_player_terr(int terr, char force)
{
    unsigned char swimming;
    int state;

    if (lastTerr != terr || force)
    {
        swimming = 0;
        state = 0;
        lastTerr = terr;
        if (terr & 0x22)
        {
            if ((motionbits & 8) == 0)
            {
                swimming = water_set(terr);
                state = 1;
            }
        }
        else if (terr & 4)
            state = 2;
        else if (terr & 8)
            state = 3;
        else if (terr & 0x10)
        {
            if (motionbits & 4)
                state = 4;
            else if (motionbits & 0x10)
                state = 5;
            else if (motionbits & 2)
                state = 6;
        }
        newFPS(state);
        if (!swimming)
            player->swim_count = 0;
    }
    if (terr & 0x10)
    {
        if (motionbits & 0x14)
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
            if (motionbits & 2 && PN.vel[2] <= -94)
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

/* No FM Towns counterpart and no callers: a flash, a hit to the player's hit points
   scaled by how many are left, a shake and a random compass frame. */
void far unreferenced_seg008_1B09_160(void)
{
    int hp;

    fill_FB(0x20);
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

/* The step-and-turn movement (game/PLAYER.C binds it to A, D, S, W, X and the arrows
   over the view): turn -1 or 1 turns 45 degrees; 0 steps half a tile forward but refuses
   to step into a different terrain or off a ledge; 2 takes the same step without those
   checks (the local is called backwards, but the step is forward); -2 steps a quarter
   tile back. A step is tested with can_place and then made directly, without
   do_physics: the player object moves tile, lands at nvokHgt (or starts falling) and
   move triggers it now overlaps fire. Returns 1 if the player moved or turned. Only
   while not falling and moving slower than MaxPlayerAccel. */
char far simple_fizix(int turn)
{
    int heading;
    int16 x;
    int16 y;
    unsigned char backwards;
    unsigned char flying;
    int sq;
    struct MotionCalc near *oldP;
    struct Object far *obj;
    struct MotionCalc calc;
    register int i;
    register int dist;

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
        if (player->motion_state & 4)
            dist /= 16;
        if (motionbits & 0x14)
            flying = 1;
        x = PN.x;
        y = PN.y;
        move_along(heading, dist, &x, &y);
        if (can_place(ITEM_ADVENTURER, 1, x / 32, y / 32, ThePlayer->pos & POS_Z, backwards | flying, 8))
        {
            if (!backwards && nvokTerr != 1 && nvokTerr != lastTerr
                && (nvokTerr != 0x10 || !flying))
                return 0;
            {
                PN.x = x;
                PN.y = y;
                if ((sq = (PN.x >> 8) + ((PN.y >> 8) << 6)) != GrSq)
                {
                    change_GrSq(sq, nvokHgt);
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
                curP->radius = ComObjData[0x7F].radius;
                curP->height = ComObjData[0x7F].height;
                curP->x = x / 32;
                curP->y = y / 32;
                curP->z = ThePlayer->pos & POS_Z;
                ObjectCheck(0, 0);
                process_objlist();
                for (i = curP->first; i < curP->first + curP->count; i++)
                {
                    obj = Obj_PtrTMem(&oCollisions[i].link);
                    if (OBJ_ITEM(obj) == ITEM_MOVE_TRIGGER_1A0)
                        UseTrigger(ThePlayer, 0L, obj, 0);
                }
                curP = oldP;
                return 1;
            }
        }
    }
    return 0;
}

/* name: IDA's ApplyWaterCurrentIceSliding. Named from FM Towns: munge_vectors_ sits
   between simple_fizix_ and set_player_phys_params_ there, takes the same eight arguments
   (kind 2 ice or 3 current, two heading/speed pairs, a strength, two results) and does
   the same sin/cos, blend, square root and atan2. */
/* Combines two motions given as heading and speed into one: type 2 (ice) moves the
   first towards the second by strength / 64 (strength at most 0x40), type 3 (a current
   or a slope) adds them. The result is in *outh and *outs. */
void far munge_vectors(int type, int heading, int speed, int heading2, int speed2,
                       int strength, int16 *outh, register int16 *outs)
{
    int16 x1;
    int16 y1;
    int16 x2;
    int16 y2;
    int dxa;
    int dya;
    int vx;
    int32 lx;
    int32 ly;
    register int vy;

    if (strength <= 0)
    {
        *outs = speed;
        *outh = heading;
        return;
    }
    if (strength > 0x40)
        strength = 0x40;
    cFstSinCos(heading, &x1, &y1);
    cFstSinCos(heading2, &x2, &y2);
    x1 /= 0x800;
    y1 /= 0x800;
    x2 /= 0x800;
    y2 /= 0x800;
    x1 *= speed;
    y1 *= speed;
    x2 *= speed2;
    y2 *= speed2;
    switch (type)
    {
    case 2:
        dxa = x2 - x1;
        dya = y2 - y1;
        vx = x1 + (int)((int32)dxa * strength / 0x40);
        vy = y1 + (int)((int32)dya * strength / 0x40);
        break;
    case 3:
        vx = x2 + x1;
        vy = y2 + y1;
        break;
    }
    lx = vx / 16;
    ly = vy / 16;
    *outs = cSqRt(lx * lx + ly * ly);
    if (*outs <= 1)
        *outs = 0;
    if (*outs != 0)
    {
        lx *= 0x7FFF;
        ly *= 0x7FFF;
        lx = lx / *outs;
        ly = ly / *outs;
        vx = lx;
        vy = ly;
        *outh = cAtan2(vx, vy);
    }
    else
        *outh = heading;
}

/* Sets PN up for a tick of rate time units. On the ground the input (do_player_input)
   sets a wanted speed, approached by at most MaxPlayerAccel and capped at the run speed;
   in the air the player can only turn. On ice the floor texture's terrain bits 3-5 give
   the grip (7 none of ice's effects): the new motion is blended from the old by
   munge_vectors, so the player slides, and an ice slope pushes downhill. Swimming, the
   same bits give a current's direction (0: the last tile's current goes on), added with
   speed 0x8D; lasth and lasts
   keep the player's own heading and speed so phys_affect_player can restore them. Also
   sets PN.flags 0x80 (slide along walls) on foot or flying, and PT.ignore 0x1000 (no
   falling) when levitating or flying. */
void far set_player_phys_params(int rate)
{
    int16 speed;
    int angle;
    int16 newh;
    int16 news;
    int bits;
    register int t;
    register int d;

    speed = 0;
    PN.bounce = 5;
    PN.flags = 0;
    oldh = PN.heading;
    olds = PN.speed;
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
    if (player->motion_state & 4)
    {
        bits = (TxmTerr[mapdata[GrSq].floor] & 0x38) >> 3;
        if (bits != 7)
        {
            frictionless = 1;
            t = mapdata[GrSq].type;
            if (t >= TILE_SLOPE_N && t <= TILE_SLOPE_W)
            {
                slide = t - 6;
                angle = 0x2F;
            }
            PN.bounce = 0xF - bits;
            bits = (bits << 3) + 8 - (olds / 0x2F << 2);
            if (olds < 0x2F)
                bits += 0x10;
            else if (PN.speed > olds)
            {
                if (slide == -1)
                    bits += 8;
                else
                    bits -= 4;
            }
            munge_vectors(2, oldh, olds, PlayerHeading, PN.speed, bits, &newh, &news);
            PlayerHeading = newh;
            PN.speed = news;
            if (olds + MaxPlayerAccel <= PN.speed && !(rand() & 3)
                || olds - MaxPlayerAccel >= PN.speed)
                set_effect(0x80, 4);
        }
        else
        {
            slide = -1;
            frictionless = 0;
        }
    }
    else if (player->motion_state & 1)
    {
        t = slide;
        slide = ((TxmTerr[mapdata[GrSq].floor] & 0x38) >> 3) - 1;
        if (slide == -1)
            slide = t;
        angle = 0x8D;
    }
    else
        slide = -1;
    lasts = -1;
    if (slide != -1)
    {
        t = 0;
        if (slide <= 3)
        {
            if (slide > 1)
                t = 0x4000;
            if (!(slide & 1))
                t = t + 0x8000;
        }
        newh = angle;
        oldh = PlayerHeading;
        olds = PN.speed;
        munge_vectors(3, PlayerHeading, PN.speed, t, newh, 0x20, &oldh, &olds);
        if (angle != 0x2F)
        {
            lasth = PlayerHeading;
            lasts = PN.speed;
        }
        if (olds > pFPS[0])
            olds = pFPS[0];
        PlayerHeading = oldh;
        PN.speed = olds;
    }
    PN.time = rate;
    if (!(PN.acc[0] | PN.acc[1] | PN.acc[2]) && (player->motion_state & 0x84) == 0)
        PN.flags = 0x80;
    else if (PN.acc[2] != 0 && (player->motion_state & 0x84))
        PN.bounce = 5;
    if (PN.speed == 0)
        PlayerHeading = PlayerFacing;
    PN.heading = PlayerHeading;
    PT.ignore = 0;
    if (motionbits & 0x14)
    {
        PT.ignore = 0x1000;
        PN.flags = 0x80;
    }
    PN.impact = 0;
    old_dz = PN.acc[2];
}

/* Puts the player in the middle of tile x, y at the floor's height (higher on a tile
   with tile_walls bit 0x20), or, when how is not -1, near the ceiling and falling. Resets
   PN and PT, links ThePlayer into the tile and works out the terrain and movement state.
   Called from the main menu, start-up, game/SKILLS.C and the spells. */
void far player_setup(int x, int y, int how)
{
    int sq;
    struct MotionCalc calc;

    change_GrSq(-1, -1);
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
    if (how != -1 && 1000 - ComObjData[0x7F].height > PN.z)
    {
        PN.z = 1000 - (ComObjData[0x7F].height << 3);
        PN.acc[2] = -4;
    }
    SET_Z(ThePlayer, PN.z >> 3);
    SET_HOMEX(ThePlayer, x);
    SET_HOMEY(ThePlayer, y);
    SET_FINEX_UNSIGNED(ThePlayer, 3);
    SET_FINEY(ThePlayer, 3);
    SET_SEQ(ThePlayer, 1);
    ThePlayer->qn.f.next = ThePlayer->qn.f.quality = 0;
    curP = &calc;
    curP->index = Obj_MemTPtr(ThePlayer);
    curP->radius = ComObjData[0x7F].radius;
    curP->height = ComObjData[0x7F].height;
    curP->x = (x << 3) + 3;
    curP->y = (y << 3) + 3;
    curP->z = PN.z >> 3;
    TerrainCheck(PN.b24);
    PN.terrain = set_resterr(curP->hits0 | curP->hits1);
    parse_player_terr(PN.terrain, 0);
    playerMod[0] = 0;
    parse_effect();
    fiz_update = 1;
    sq = GrSq;
    GrSq = -1;
    change_GrSq(sq, -1);
}

/* After do_physics: copies PN to the player object (fine position, tile, height,
   animation frame), runs player_newsq on a new tile, restores the player's own heading
   and speed after a current, stops the player after a wall hit, turns the facing towards
   the heading by 0x600 a tick while sliding along a wall, and turns PN.impact into
   damage: impact >> 8, doubled in a fall, none in the ninth world (levels 65 to 72), cut
   by an Acrobat skill check, dealt above 3, with a thud above 1. */
void far phys_affect_player(void)
{
    int sq;
    int dmg;
    int vol;
    register int16 h;
    register int z;

    z = ThePlayer->pos & POS_Z;
    if (lasts != -1)
    {
        PlayerHeading = lasth;
        PN.speed = lasts;
    }
    SET_FINEX_UNSIGNED(ThePlayer, (PN.x >> 5) & 7);
    SET_FINEY(ThePlayer, (PN.y >> 5) & 7);
    SET_FRAME(ThePlayer, ((unsigned)GAME_TIME() & 0xFF) >> 6);
    if ((sq = (PN.x >> 8) + ((PN.y >> 8) << 6)) != GrSq)
    {
        change_GrSq(sq, PN.z >> 3);
        SET_HOMEX(ThePlayer, PN.x >> 8);
        SET_HOMEY(ThePlayer, PN.y >> 8);
        player_newsq(GrSq);
    }
    else if (PN.z >> 3 != z)
        hgt_change(ThePlayer, mapdata + GrSq, PN.z >> 3);
    if (PN.impact && PN.heading == PlayerHeading && PN.acc[2] == 0
        && (player->motion_state & 4) == 0 && lasts == -1)
        PN.speed = 0;
    if (PN.heading != PlayerHeading)
    {
        PlayerHeading = PN.heading;
        h = PN.heading - (plyMoType << 14);
#ifndef __TURBOC__
        if ((PN.flags & 0x80) && slide == -1 && !ENHANCED(ENH_FREE_HEADING))  /* port only: --enhance free-heading */
#else
        if ((PN.flags & 0x80) && slide == -1)
#endif
        {
            if (abs((int16)(PlayerFacing - h)) < 0x600)
                PlayerFacing = h;
            else if ((uint16)(PlayerFacing - h) < 0x7FFF)
                PlayerFacing -= 0x600;
            else
                PlayerFacing += 0x600;
        }
    }
    SET_HEADING(ThePlayer, PlayerFacing >> 13);
    SET_FINEHEAD(ThePlayer, PlayerFacing >> 8);
    if (PN.impact)
    {
        if (PN.bounce > 0)
        {
            dmg = PN.impact >> 8;
            if ((PlayerLevel - 1) / 8 == 8)
                dmg = 0;
            if (PN.vel[2] != 0)
                dmg <<= 1;
            if (skill_check(player->skills[SKILL_ACROBAT], dmg << 1) > 0)
                dmg = dmg * (30 - player->skills[SKILL_ACROBAT]) / 30;
            if (dmg > 3)
                damage_item(ThePlayer, 0L, 0, 0, dmg, 0);
            if (dmg > 1 || (PN.terrain & 0x10))
            {
                vol = (dmg << 2) - 60;
                play_effect_here(0xF, 0x40, vol);
            }
        }
        PN.impact = 0;
    }
    parse_player_terr(PN.terrain, 0);
    if ((player->motion_state & 4) || (player->motion_state & 1) && slide != -1)
        fiz_update = 1;
    else
        fiz_update = 0;
}

/* Entering tile sq: sets player->automap when the tile's PlayersMap low nibble is 1 to
   9 (what that flag and nibble mean is not yet known). */
void far player_newsq(int sq)
{
    unsigned char m;

    m = PlayersMap[0][sq];             /* sq indexes the whole map */
    if (!player->automap && (m & 0xF) < 10 && (m & 0xF))
        player->automap = 1;
}

/* PT's special function (called by MOTION.C's check_positions with the collision
   state): a player walking slowly (under 3/10 of the run speed) off a ledge, not
   swimming or on ice, is stopped at the edge instead (returns 1, so the step is undone). */
char far player_sqhandler(uint16 *w)
{
    if ((*w & 0x1000) && PN.vel[2] == 0 && PN.speed * 10 < pFPS[0] * 3 && !(lastTerr & 0xA))
    {
        PN.vel[0] = PN.vel[1] = 0;
        return 1;
    }
    return 0;
}

/* PlayerInput to a heading and wanted speed: 0 stop, 1 forward with the mouse or key
   rates (ForwInpRate, TurnInpRate), 8 back, 9 and 10 sideways, 6 a running jump
   (starts at half the run speed, then as 7), 7 a jump (vertical speed 0x263, less above
   z 0x280 and again above 0x2C0, with gravity -4, or -2 with Leap), 12 and 13 up and down while
   levitating or flying. Paralysis stops everything. */
void far do_player_input(int input, int rate, register int16 *speed)
{
    register int16 h;

    if (player->paralyzed)
    {
        *speed = 0;
        return;
    }
    h = PlayerFacing;
    switch (input)
    {
    case 1:
        PlayerFacing += rate * PlayerTurn * (TurnInpRate / 4) / 4;
        PlayerHeading = h = PlayerFacing;
        *speed = pFPS[0] * (ForwInpRate >> 2) / 32;
        plyMoType = 0;
        break;
    case 10:
        h += 0x4000;
        *speed = pFPS[1];
        plyMoType = 1;
        break;
    case 9:
        h -= 0x4000;
        *speed = pFPS[1];
        plyMoType = -1;
        break;
    case 8:
        h -= 0x8000;
        *speed = pFPS[2];
        plyMoType = -2;
        break;
    case 6:
        if (PN.vel[2] != 0 || PN.acc[2] != 0 || PN.speed != 0)
            break;
        PlayerHeading = h = PlayerFacing;
        PN.speed = *speed = pFPS[0] / 2;
        plyMoType = 0;
    case 7:
        PN.vel[2] = 0x263;
        if (PN.z > 0x280)
        {
            PN.vel[2] = PN.vel[2] * 5 / 6;
            if (PN.z > 0x2C0)
                PN.vel[2] = (PN.vel[2] << 1) / 3;
        }
        if (motionbits & 1)
            PN.acc[2] = -2;
        else
            PN.acc[2] = -4;
        return;
    case 12:
        plyMoType = 0;
        PN.vel[2] = 0x8D;
        PN.acc[2] = 0;
        break;
    case 13:
        plyMoType = 0;
        PN.vel[2] = -0x8D;
        PN.acc[2] = 0;
        break;
    case 0:
        *speed = 0;
        return;
    }
    PlayerHeading = h;
}

/* name: IDA's StopFalling. Named from FM Towns: phys_bounce_up_ is next there and does
   the same (for the player, pitch 0x8D unless jumping, dz 0). */
void far phys_bounce_up(struct Object far *obj)
{
    if (obj == ThePlayer)
    {
        if ((PN.terrain & 0x10) == 0)
            PN.vel[2] = 0x8D;
        PN.acc[2] = 0;
    }
}

/* Recomputes the movement state from the current terrain, after the motion spells
   change (game/PLAYDATA.C). */
void far fizix_update(void)
{
    parse_player_terr(PN.terrain, 1);
    fiz_update = 1;
}

/* Sets the movement state (-1 keeps player->fps) and from it the speeds, out of 20 of
   the full ones: on foot 20, swimming Swimming / 2 + 4, lava 14, ice 20, levitating 1,
   flying 14, slow falling 4; the turn rate likewise on the ground. */
void far newFPS(char state)
{
    unsigned char ratios[7] = { 20, 6, 14, 20, 1, 14, 4 };
    unsigned char trans[7] = { 0, 1, 2, 4, 8, 8, 0 };
    register int r;

    if (state == -1)
        state = player->fps;
    else
    {
        player->motion_state = (player->motion_state & 0xE0) + trans[state];
        player->fps = state;
    }
    if (state == 1)
        r = player->skills[SKILL_SWIMMING] / 2 + 4;
    else
        r = ratios[state];
    pFPS[0] = Run_FPS * r / 20;
    pFPS[1] = Side_FPS * r / 20;
    pFPS[2] = Back_FPS * r / 20;
    if (state < 4)
        pTurn = PlayerTurn * ratios[state] / 20;
    else
        pTurn = PlayerTurn;
    if (player->max_weight && player->weight * 2 > player->max_weight)
        MaxPlayerAccel = 0x60 - player->weight * 0x60 / (player->max_weight * 2);
    else
        MaxPlayerAccel = 0x60;
}

/* Moves the player object from tile GrSq to tile sq (-1: nowhere) at height z (-1: PN's),
   leaving and entering pressure plates (check_pplate 0xE and 6), and when light_mod is
   set redoes the light level if the tile's light bit differs from the last one. */
void far change_GrSq(int sq, register int z)
{
    int flag;

    if (GrSq >= 0)
    {
        Obj_Rem(&(mapdata + GrSq)->objects, ThePlayer);
        check_pplate(ThePlayer, mapdata + GrSq, PN.z >> 3, 0xE);
    }
    GrSq = sq;
    if (z == -1)
        z = PN.z >> 3;
    else
        SET_Z(ThePlayer, z);
    if (GrSq >= 0)
    {
        Obj_Add(&(mapdata + GrSq)->objects, ThePlayer);
        check_pplate(ThePlayer, mapdata + GrSq, z, 6);
        if (light_mod)
        {
            flag = (mapdata + GrSq)->light & 1;
            if (last_light != flag)
            {
                last_light = flag;
                loc_lght = (last_light ^ light_hi) ? light_mod : -1;
                set_light(light_act > loc_lght ? light_act : loc_lght);
            }
        }
    }
}

/* Changes obj's height in its tile to z, running pressure plates for leaving the old
   height and arriving at the new one. */
void far hgt_change(struct Object far *obj, struct Tile far *tile, int z)
{
    char r = 1;
    register int oldz = obj->pos & POS_Z;

    SET_Z(obj, z);
    r = check_pplate(obj, tile, oldz, 0xF);
    if (r)
        check_pplate(obj, tile, z, 7);
}
