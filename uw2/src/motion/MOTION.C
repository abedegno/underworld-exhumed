/* target: seg031_2CFA */
/* opts: -mm -1 -G -O -Y -d */
/* Motion: stepping a physics record through the tile grid, bouncing off walls, floors and
   objects. The whole of DOS resident segment seg031_2CFA, in original order. Function and
   global names are the originals from the FM Towns symbol table; the source file's own
   name is not known.

   Everything that moves under physics comes through do_physics: the player (PN with the
   handler PT, from PLAYMOVE.C's move_player), walking, flying and swimming critters (CN1,
   CN2 and CN4 with CT1, CT2 and CT4, chosen in critter/AI.C) and thrown or falling objects
   and missiles (CN3 with CT3, critter/PATHFIND.C's move_me_joe). The caller fills a struct
   Phys (motion.h) with a position in 1/256 tiles (z in 1/8 of an object's z unit), a
   heading and speed, a vertical velocity and gravity (acc[2]) and the ticks to run (time),
   and do_physics advances it in place.

   How a move works: space_to_motion turns heading and speed into a velocity and sets up a
   Bresenham walk along the larger of the x and y velocities (struct MotionParams MP), one
   1/8-tile cell per step, with the minor axis and z carried as fractions (0x2000 to a cell
   in x and y, 0x800 in z). grid_move takes one step (flat_move, or full_move when there is
   vertical velocity). After each step check_positions asks COLLIDE.C what the mover now
   overlaps (get_pcoll: floor, walls, slopes and objects, as a word of state bits) and
   reacts: it steps back, stops, calls the handler's special function, bounces off a wall
   (do_2dbounce and rehead) or starts a fall. Reaching the target height in z (a floor, a
   ceiling or the top of an object, chosen by set_targz) goes to do_zbounce. back_to_space
   writes the cell position back into the record.

   The collision state bits (get_pcoll's result, built from struct MotionCalc's hits0 and
   hits1, which COLLIDE.C fills): bits 0-1 the floor's terrain class (0 plain, 1 water,
   2 lava, 3 ice; map.h TERRAIN_*), 4 standing on the floor, 8 << class a footprint corner
   on a floor of that class, 0x80 standing on a solid object, 0x100 a floor higher than
   the mover can step, 0x200 a wall, 0x400 an object in the way, 0x800 a drop below,
   0x1000 nothing underneath (falling), 0x2000 on a slope, 0x4000 and 0x8000 an object
   hit that stops the mover or ends this move (do_objhit's 0x10 and 8). These meanings are
   read from COLLIDE.C's SolvePnt and SolveCenter and from the code here; nothing names
   them.

   The terrain byte CP->terrain (set_resterr) has one bit per kind of footing: 1 plain
   floor, 2 water, 4 lava, 8 ice, 0x10 in the air, and 0x20 for the player standing on
   water with a corner of the footprint on another floor (probably the shore; the player
   code treats it like water). res_to_terr turns it back into the 0-4 code an object
   stores (OBJ_TERRAIN).

   Neighbours: COLLIDE.C (seg028: TerrainCheck, ObjectCheck, process_objlist and the
   oCollisions list behind Ppd's fields), OBJPHYS.C (seg030: do_objhit, and get_phys_data
   and set_phys_data, which convert an object to and from a struct Phys), PHYSICS.C and
   PLAYMOVE.C (the player), critter/AI.C and PATHFIND.C (critters and objects).

   name: descriptive (map/filenames.tsv: stepping a physics record through the grid). */

#include <stdlib.h>
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"

/* This file's _BSS, DS:25C4..26E9 (seg030's ends at 25C3, seg032's starts at 26EA). PN,
   the CNs, PT and the CTs are used only by other files (seg006, seg007, seg008, seg035 and
   overlays). Ppd is the collision record (struct MotionCalc, map.h) of whatever
   do_physics is moving; CP and TP point at the physics record and handler being moved. */
/* match: laid out by name (tools/bssorder.py): Ppd 144, bounce_flag 298, PN 336,
   CN1..CN4 371, hit_obj 568, hit_flag 624, CP 731, targ_ceil 764, terr_type 820, PT 848,
   trycnt 868, TP 884, CT1..CT4 931. */
/* name: FM Towns keeps TP, CP, CN4..CN1, PT, PN and CT4..CT1 together, and the six
   without names as statics after _CT1 (+0xC..+0x11), so those are static here, with
   provisional names chosen for their keys. */
struct MotionCalc Ppd;                      /* DS:25C4 */
static signed char bounce_flag;             /* DS:25DB, _CT1+0xF */
struct Phys PN;                             /* DS:25DC */
struct Phys CN1, CN2, CN3, CN4;             /* DS:2604, 262C, 2654, 267C */
static unsigned char hit_obj;               /* DS:26A4, _CT1+0x10 */
static unsigned char hit_flag;              /* DS:26A5, _CT1+0xD */
struct Phys near *CP;                       /* DS:26A6 */
static unsigned char targ_ceil;             /* DS:26A8, _CT1+0x11 */
static unsigned char terr_type;             /* DS:26A9, _CT1+0xE */
struct Handler PT;                          /* DS:26AA */
static unsigned char trycnt;                /* DS:26B6, _CT1+0xC */
struct Handler near *TP;                    /* DS:26B8 */
struct Handler CT1, CT2, CT3, CT4;          /* DS:26BA, 26C6, 26D2, 26DE */

/* A terrain byte (one bit set) to the 0-4 code an object stores: 1 plain, 2 water,
   4 lava, 8 ice and 0x10 air give 0, 1, 2, 3 and 4. */
unsigned char res_to_terr[18] = { 0, 0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 4 };

/* headings[] are the wall normals do_2dbounce reflects from, indexed by the direction
   ComputeHeading finds (0 to 7, eighths of a turn) or by major * 2 for a plain wall
   across the major axis. */
struct MotionParams MP = {
    0, (int16 *)&Ppd, { 0 }, { 0 }, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    { 0, 0xE000, 0xC000, 0xA000, 0x8000, 0x6000, 0x4000, 0x2000 }
};

/* Moves pp for pp->time ticks under the handler tp. Gives up after 16 steps that
   collided (trycnt) and then zeroes the speed, vertical velocity and gravity, so a mover
   wedged between obstacles stops rather than loops. */
void far do_physics(struct Phys *pp, struct Handler *tp)
{
    CP = pp;
    TP = tp;
    MP.vel = CP->vel;
    terr_type = CP->terrain;
    bounce_flag = 0;
    trycnt = 0;
    if (!space_to_motion(1, 1))
        return;
    while (MP.steps + 1 > MP.done) {
        if (trycnt++ == 0x10)
            goto stuck;
        if (grid_move(1))
            check_positions();
    }
    back_to_space();
    return;
stuck:
    CP->speed = CP->vel[2] = CP->acc[2] = 0;
}

/* name: IDA CopyMotionValsFromInitialToBase25C4: FM Towns space_to_pos_, same position
   and code. */
/* Loads Ppd and MP's cell position from CP: x and y to 1/8 tiles (>> 5) with the
   remainder as a fraction of 0x2000, z to object units (>> 3) with its fraction. */
void far space_to_pos(void)
{
    curP = &Ppd;
    Ppd.heading = CP->heading;
    Ppd.radius = CP->radius;
    Ppd.height = CP->height;
    Ppd.index = CP->index;
    MP.pos[0] = CP->x >> 5;
    MP.pos[1] = CP->y >> 5;
    MP.pos[2] = CP->z >> 3;
    MP.frac[0] = (CP->x & 0x1F) << 8;
    MP.frac[1] = (CP->y & 0x1F) << 8;
    MP.frac[2] = (CP->z & 7) << 8;
}

/* Chooses the height the mover will stop at in z, MP.targz, and what it would land on
   or hit, MP.hit (an oCollisions index, or -1) and MP.item. Rising: the bottom of the
   next object above, or the ceiling (0x80 less the mover's height), or the highest floor
   under the footprint if the mover is below it (targ_ceil then says the target is a
   floor). Level: the floor, or the top of a touchable object it stands on. Falling: the
   highest floor under the footprint or the top of the object just below. MP.f23 is the
   lowest bottom of the objects overhead, the headroom get_pcoll checks. how is unused. */
void far set_targz(char how)
{
    char solid;
    int i;

    targ_ceil = 1;
    process_objlist();
    MP.hit = 0xFF;
    MP.f23 = 0x7F;
    if (CP->vel[2] > 0) {
        if (Ppd.found > 0 && Ppd.first + Ppd.count < Ppd.found && Ppd.first >= 0) {
            MP.targz = oCollisions[Ppd.first + Ppd.count].bottom - CP->height;
            MP.hit = Ppd.first + Ppd.count;
        } else
            MP.targz = 0x80 - CP->height;
        targ_ceil = 0;
        if (MP.pos[2] + CP->radius < Ppd.top) {
            MP.targz = Ppd.top;
            targ_ceil = 1;
        }
    } else if (CP->vel[2] == 0) {
        MP.targz = Ppd.floor;
        MP.targz = MP.pos[2] + CP->b24 >= Ppd.top ? Ppd.top : Ppd.floor;
        solid = Ppd.count == 0 && Ppd.first > 0 && Ppd.first <= Ppd.found;
        for (i = 0; i < Ppd.found; i++) {
            if (ComObjData[OBJ_ITEM(Obj_IntTMem(oCollisions[i].link.f.index))].touch) {
                if (Ppd.first <= i) {
                    if (oCollisions[i].bottom - 1 < MP.f23)
                        MP.f23 = oCollisions[i].bottom - 1;
                    if (Ppd.first + Ppd.count > i && oCollisions[i].top > MP.targz
                        && oCollisions[i].top < 0x80 - CP->height) {
                        MP.hit = i;
                        MP.targz = oCollisions[i].top;
                    }
                } else if (solid && oCollisions[i].top >= MP.targz
                           && (MP.hit == -1 || !(oCollisions[MP.hit].link.f.low & 0x10)
                               || !(!oCollisions[i].link.f.low & 0x10))) {
                    MP.targz = oCollisions[i].top;
                    MP.hit = i;
                }
            }
        }
    } else {
        MP.targz = Ppd.top;
        if (Ppd.first > 0 && Ppd.first <= Ppd.found && oCollisions[Ppd.first - 1].top > MP.targz) {
            MP.targz = oCollisions[Ppd.first - 1].top;
            MP.hit = Ppd.first - 1;
        }
        targ_ceil = 0;
    }
    if (MP.hit != -1)
        MP.item = OBJ_ITEM(Obj_PtrTMem(&oCollisions[MP.hit].link));
}

/* Sets up the step walk for the rest of CP's time: velocity from heading and speed
   (sin and cos are 15-bit fractions) plus acc * time, the major axis, the per-step
   increments, the whole steps and the remainder, and the ticks per step (dt). With
   vertical velocity it also finds the target height and zspeed, the z change per step in
   1/256ths of a cell. pos reloads the position from CP first; check runs the terrain and
   object checks first (always for the player, index 1). Returns 0 when nothing moves. */
unsigned char far space_to_motion(char pos, char check)
{
    int16 s;
    int16 c;
    int32 t;
    int32 a;
    int32 b;

    cSinCos(CP->heading, &s, &c);
    t = s;
    CP->vel[0] = t * CP->speed >> 15;
    t = c;
    CP->vel[1] = t * CP->speed >> 15;
    MP.vel[0] += CP->acc[0] * CP->time;
    MP.vel[1] += CP->acc[1] * CP->time;
    MP.vel[2] += CP->acc[2] * CP->time;
    if ((MP.vel[0] | MP.vel[1] | MP.vel[2]) == 0)
        return 0;
    if (pos)
        space_to_pos();
    if (abs(CP->vel[0]) > abs(CP->vel[1]))
        MP.major = 0;
    else
        MP.major = 1;
    MP.minor = (MP.major + 1) % 2;
    if (MP.vel[MP.major] > 0)
        MP.step[MP.major] = 0x2000;
    else
        MP.step[MP.major] = 0xE000;
    if (MP.vel[MP.major] != 0) {
        MP.step[MP.minor] = MP.step[MP.major] / 0x100 * MP.vel[MP.minor] / MP.vel[MP.major] << 8;
        /* a 16-bit int multiply, as Turbo C's (imul, then cwd): it wraps when the mover is fast
           and the step long, as the first step after a restore is (check_physics caps it at 40h
           units, and a restore keeps the player's speed). On the host it kept 32 bits (UW1's
           issue 10). abs is the low word's too, so abs(-8000h) is -8000h */
        t = (int16)(MP.vel[MP.major] * CP->time);
        MP.steps = (int16)abs(t) >> 13;
        MP.rem = abs(t) & 0x1FFF;
        MP.dt = abs(0x2000 / MP.vel[MP.major]);
    } else {
        MP.step[MP.minor] = 1;
        MP.step[MP.major] = 1;
        t = 0;
        MP.steps = 0;
        MP.rem = 0;
        MP.dt = CP->time;
    }
    MP.done = 0;
    if ((Ppd.index == 1 || MP.vel[2] != 0) && check) {
        TerrainCheck(CP->b24);
        ObjectCheck(0, 0);
    }
    if (MP.vel[2] != 0) {
        if (MP.vel[2] > 0)
            MP.step[2] = 0x800;
        else
            MP.step[2] = 0xF800;
        set_targz(check);
        if (MP.vel[MP.major] != 0) {
            a = MP.vel[2];
            a *= MP.step[MP.major] / 0x2000;
            b = MP.vel[MP.major];
            b *= MP.step[2] / 0x800;
            a *= 0x100;
            a = a / b;
            if (a > 0x7FFF || a < -0x8000L) {
                MP.rem = 0;
                MP.zspeed = abs((int16)((MP.vel[2] >> 1) * CP->time) >> 5);    /* 16-bit, as below */
            } else
                MP.zspeed = a;
        } else
            /* the product is a 16-bit int (imul, then sar ax,5), which wraps once the fall is
               fast and the step long */
            MP.zspeed = abs((int16)((MP.vel[2] >> 1) * CP->time) >> 5);
    } else
        MP.step[2] = 0;
    return 1;
}

/* After a bounce: charges the steps taken against CP->time and sets up the walk again
   from the new velocity, or ends the move when no time is left. */
void far recalc_vecs(char how)
{
    CP->time -= MP.dt * MP.done;
    if (CP->time <= 0 || space_to_motion(0, how) == 0)
        MP.done = MP.steps + 1;
}

void far stop_me(void)
{
    CP->acc[2] = CP->vel[2] = CP->acc[0] = CP->vel[0] = CP->acc[1] = CP->vel[1] = 0;
    CP->speed = 0;
    MP.done = MP.steps + 1;
}

void far back_to_space(void)
{
    CP->x = (MP.pos[0] << 5) + (MP.frac[0] >> 8);
    CP->y = (MP.pos[1] << 5) + (MP.frac[1] >> 8);
    CP->z = (MP.pos[2] << 3) + (MP.frac[2] >> 8);
    if (Ppd.hits0 & 0x2000 && abs(MP.pos[2] - Ppd.floor) <= CP->radius
        && Ppd.index == 1 && CP->vel[2] == 0)
        CP->z = GetSlopeHgt(CP->x, CP->y);
    CP->heading = Ppd.heading;
}

/* One step of the walk in x and y, forward (dir 1) or back (dir -1, undoing the last
   step after a collision). Whole steps move one cell on the major axis; the last step
   moves only the remainder. Returns nonzero when the minor axis crossed into a new cell
   (crossed is passed in from full_move). */
unsigned char far flat_move(int crossed, int dir)
{
    if (MP.steps + (dir == -1) > MP.done) {
        if (dir == 1)
            MP.frac[MP.minor] += MP.step[MP.minor];
        else
            MP.frac[MP.minor] -= MP.step[MP.minor];
        if (MP.step[MP.major] * dir > 0)
            MP.pos[MP.major]++;
        else
            MP.pos[MP.major]--;
        if (MP.frac[MP.minor] & 0xE000) {
            crossed = 1;
            if (MP.frac[MP.minor] > 0)
                MP.pos[MP.minor]++;
            else
                MP.pos[MP.minor]--;
            MP.frac[MP.minor] &= 0x1FFF;
        }
        MP.done += dir;
        return 1;
    }
    if (dir == 1)
        MP.frac[MP.minor] += (MP.step[MP.minor] >> 5) * (MP.rem >> 8);
    else
        MP.frac[MP.minor] -= (MP.step[MP.minor] >> 5) * (MP.rem >> 8);
    if (MP.step[MP.major] * dir > 0)
        MP.frac[MP.major] += MP.rem;
    else
        MP.frac[MP.major] -= MP.rem;
    if (MP.frac[0] & 0xE000) {
        crossed = 1;
        if (MP.frac[0] > 0)
            MP.pos[0]++;
        else
            MP.pos[0]--;
        MP.frac[0] &= 0x1FFF;
    }
    if (MP.frac[1] & 0xE000) {
        crossed = 1;
        if (MP.frac[1] > 0)
            MP.pos[1]++;
        else
            MP.pos[1]--;
        MP.frac[1] &= 0x1FFF;
    }
    MP.done += dir;
    return crossed;
}

/* name: IDA seg031_2CFA_A3F: unreferenced, and FM Towns has nothing between flat_move
   and rehead. */
/* match: its whole body is a compare with no jump, which Turbo C makes from an empty
   if. */
static void seg031_2CFA_A3F(void)
{
    if (Ppd.open == 9)
        ;
}

/* Bounces CP off a wall whose normal is heading: damps the vertical velocity by the
   bounce factor (0 to 15), reflects or turns the heading and scales the speed down,
   adding the lost speed to impact (which set_phys_data and phys_affect_player turn into
   damage). Movers with flags 0x80 (walking, flying and swimming critters, and the player
   on foot and not on ice, or flying) do not bounce: they slide along the wall, placed at
   the cell's corner for a diagonal, or are stopped by a nearly head-on wall (impact
   gains the whole speed).
   flags 0x40 refuses any change. Returns 0 when the mover cannot be redirected. */
unsigned char far rehead(unsigned heading)
{
    register int16 diff;
    int t;
    unsigned char fx;
    unsigned char fy;
    unsigned char tile;

    if (CP->flags & 0x40)
        return 0;
    if (CP->vel[2] != 0) {
        diff = CP->vel[2] / 16;
        diff *= CP->bounce + 1;
        CP->vel[2] = diff;
    }
    diff = heading - Ppd.heading;
    if (diff > 0x4000 || diff < -0x4000) {
        heading += 0x8000;
        diff += 0x8000;
    }
    if (CP->flags & 0x80) {
        if (abs(diff) > 0x3000 && abs(diff) < 0x5000) {
            CP->impact += CP->speed;
            return 0;
        }
        if (Ppd.heading != heading) {
            Ppd.heading = heading;
            if (heading & 0x2000) {
                tile = get_home_tile();
                if (tile >= 2 && tile <= 5) {
                    fy = 0;
                    fx = heading & 0x4000 ? 0 : 1;
                    if ((diff < 0) ^ ((heading & 0x8000) != 0)) {
                        fx ^= 1;
                        fy ^= 1;
                    }
                    MP.frac[0] = fx * 0x1F00;
                    MP.frac[1] = fy * 0x1F00;
                } else {
                    MP.frac[0] = 0x1000;
                    MP.frac[1] = 0x1000;
                }
            }
        } else {
            unsigned char gx, gy;

            gy = 0;
            gx = heading & 0x4000 ? 0 : 1;

            if ((diff > 0) ^ ((heading & 0x8000) != 0)) {
                gx ^= 1;
                gy ^= 1;
            }
            MP.frac[0] = gx * 0x1F00;
            MP.frac[1] = gy * 0x1F00;
        }
    } else if (abs(diff) > 0x3000 && abs(diff) < 0x5000)
        Ppd.heading = heading + diff;
    else {
        t = diff / 15;
        Ppd.heading = heading + t * CP->bounce;
    }
    if ((CP->flags & 0x80) == 0) {
        CP->impact += CP->speed * (15 - CP->bounce) / 15;
        CP->speed = CP->speed * CP->bounce / 15;
    }
    CP->heading = Ppd.heading;
    return 1;
}

/* Steps back from a wall and turns the mover with rehead, using the wall direction
   ComputeHeading found (how) or the major axis. bounce_flag stops a second bounce on the
   very next step. Ends the move if the mover cannot be turned. */
void far do_2dbounce(char how)
{
    register int h;

    if (bounce_flag > 0)
        grid_move(-1);
    else {
        if (how) {
            ComputeHeading();
            h = Ppd.slope;
            if (h == 9)
                h = MP.major * 2;
        } else
            h = MP.major * 2;
        grid_move(-1);
        if (rehead(MP.headings[h])) {
            recalc_vecs(1);
            bounce_flag = 2;
            return;
        }
    }
    MP.done = MP.steps + 1;
}

/* name: IDA MaybeReflection: FM Towns bounce_that_guy_, same position and code. */
/* Throws a mover that landed on something it cannot rest on (a non-solid object, or
   not over the floor) up and sideways: vertical velocity 0xBC, gravity, a minimum speed
   and, one time in four, a random turn of up to 0x3000 either way. */
void far bounce_that_guy(void)
{
    CP->vel[2] = 0xBC;
    CP->acc[2] = -4;
    if (CP->speed < 0xEB)
        CP->speed = 0xEB;
    CP->terrain = 0x10;
    if (!(rand() & 3)) {
        CP->heading -= 0x3000;
        CP->heading += rand() % 0x6000;
    }
    hit_obj = 1;
}

/* The mover reached MP.targz. Uses up the time the partial step took, then either
   lands it on the floor (stop, with a thud sound chosen by mass), hands an object hit
   to do_objhit, or bounces: reverses and damps the vertical velocity by the bounce
   factor (fully elastic for the player with the Bouncing spell, motionbits 0x20), and
   once it is slow enough settles it, setting the terrain byte from what it stands on.
   An object that do_objhit let through is dropped from oCollisions. */
void far do_zbounce(void)
{
    int dz;
    register int r;
    register int mass;

    hit_obj = 0;
    mass = ComObjData[OBJ_ITEM(Obj_IntTMem(Ppd.index))].mass;
    if (MP.zspeed > 4) {
        /* 16-bit ints, as Turbo C's (imul, then shl ax,4 and cwd before the idiv): both the
           product and the shift wrap where the host's int kept 32 bits */
        dz = (int16)(abs(MP.pos[2] - MP.targz) * MP.dt);
        dz = (int16)(dz << 4) / (MP.zspeed / 4);
        CP->time -= dz;
    } else
        CP->time = 0;
    MP.pos[2] = MP.targz;
    MP.frac[2] = 0;
    if (MP.hit == -1 && (3 & Ppd.hits0) == 1 && Ppd.floor + Ppd.radius >= MP.pos[2]
        && CP->vel[2] <= 0) {
        int fx;

        stop_me();
        CP->terrain = 2;
        if (mass < 20)
            fx = 0x18;
        else if (mass < 100)
            fx = 0x19;
        else
            fx = 5;
        play_effect(fx, CP->x >> 5, CP->y >> 5, 0);
        return;
    }
    {
        int vol;

        vol = abs(CP->vel[2]) / 10 + (mass - 600) / 50 - 40;
        play_effect(0xF, CP->x >> 5, CP->y >> 5, vol);
    }
    r = do_objhit(MP.hit, Ppd.index);
    if (r & 0x18) {
        if (r & 0x10)
            stop_me();
        else
            MP.done = MP.steps + 1;
        return;
    }
    if (r & 4) {
        char up;
        int speed;
        struct Object far *obj;
        int item;

        up = CP->vel[2] <= 0;
        speed = CP->speed;
        if (CP->index == 1 && (motionbits & 0x20) == 0x20 || CP->bounce == 15)
            CP->vel[2] = -CP->vel[2];
        else {
            CP->vel[2] = -(CP->vel[2] / 15);
            CP->impact = abs(CP->vel[2] * (15 - CP->bounce));
            CP->vel[2] = CP->vel[2] * CP->bounce;
            if (CP->bounce)
                CP->speed -= (15 - CP->bounce) * CP->speed / 30;
            else
                CP->speed = 0;
        }
        if (CP->index != 1 && (TP->ignore & 0x1000) == 0x1000) {
            if (CP->time > 1)
                CP->time--;
        } else if (up && CP->vel[2] < 0x8D) {
            CP->vel[2] = 0;
            CP->acc[2] = 0;
            if (MP.hit != -1) {
                obj = Obj_IntTMem(oCollisions[MP.hit].link.f.index);
                item = OBJ_ITEM(obj);
                if (ComObjData[item].solid)
                    CP->terrain = 1;
                else if (OBJ_MAJOR(Obj_IntTMem(CP->index)) != MAJOR_CREATURE)
                    bounce_that_guy();
                else
                    CP->terrain = 1;
            } else if (Ppd.floor + Ppd.radius >= MP.pos[2])
                CP->terrain = 1 << (Ppd.hits0 & 3);
            else if (OBJ_MAJOR(Obj_IntTMem(CP->index)) != MAJOR_CREATURE)
                bounce_that_guy();
            else if (Ppd.hits1 & 0x10)
                CP->terrain = 2;
            else if (Ppd.hits1 & 0x20)
                CP->terrain = 4;
            else if (Ppd.hits1 & 0x40)
                CP->terrain = 8;
            else
                CP->terrain = 1;
            if (!hit_obj)
                CP->time = 0;
        }
        if ((Ppd.hits0 & 3) == 3 && Ppd.hits0 & 4 || Ppd.hits1 & 0x40 && !(Ppd.hits1 & 0x800))
            CP->speed = speed;
        recalc_vecs(0);
    } else {
        if (MP.hit != -1)
            oCollisions[MP.hit] = oCollisions[--Ppd.found];
        recalc_vecs(0);
    }
}

/* One step with vertical motion: moves z by zspeed (scaled for the last, partial step)
   and calls do_zbounce instead of passing MP.targz; then the x and y step. While moving
   in z the terrain byte is 0x10, in the air. */
unsigned char far full_move(int crossed, int dir)
{
    int32 z;
    register int dz;

    if (MP.steps + (dir == -1) > MP.done) {
        /* 16-bit ints, as Turbo C's (shl ax,5, and neg, before the cwd): zspeed passes 1024
           when the fall is fast and the major axis slow, and then z wraps and the whole step
           goes the wrong way, where the host's int kept 32 bits */
        if (dir * MP.step[2] > 0)
            z = (int16)(MP.zspeed << 5);
        else
            z = (int16)-(MP.zspeed << 5);
    } else if (MP.rem != 0) {
        z = MP.rem;
        z = z * (dir * (MP.step[2] / 0x800) * MP.zspeed);
        z = z / 0x100;
    } else {
        z = 0x40;
        z = z * (dir * (MP.step[2] / 0x800) * MP.zspeed);
    }
    z += MP.frac[2];
    dz = z >= 0 ? z / 0x800 : -(labs(z) / 0x800 + 1);
    MP.frac[2] = z & 0x7FF;
    if (dir == -1)
        MP.pos[2] += dz;
    else if (dz > 0) {
        CP->terrain = 0x10;
        if (MP.pos[2] + dz <= MP.targz)
            MP.pos[2] += dz;
        else {
            do_zbounce();
            return 0;
        }
    } else if (dz < 0) {
        CP->terrain = 0x10;
        if (MP.pos[2] + dz >= MP.targz)
            MP.pos[2] += dz;
        else {
            do_zbounce();
            return 0;
        }
    }
    return flat_move(crossed, dir);
}

/* One step forward (dir 1) or back (dir -1). Stepping back redoes the object check and
   the target height and restores the terrain byte, and, if the step forward had climbed
   (hit_flag), recomputes the collision state. */
unsigned char far grid_move(int dir)
{
    char check;
    unsigned char r;
    register int state;

    check = 0;
    if (dir == -1) {
        ObjectCheck(0, 0);
        set_targz(0);
        CP->terrain = terr_type;
        if (hit_flag)
            check = 1;
    } else
        bounce_flag = bounce_flag - 1;
    if (CP->vel[2] != 0)
        r = full_move(0, dir);
    else
        r = flat_move(0, dir);
    if (check) {
        state = get_pcoll();
        terr_type = CP->terrain;
        CP->terrain = set_resterr(state);
    }
    return r;
}

/* A collision state word to the terrain byte (see the top of the file). */
unsigned char far set_resterr(unsigned bits)
{
    if (bits & 0x1000)
        return 0x10;
    if (bits & 4) {
        if (Ppd.index == 1 && (bits & 3) == 1 && (bits & 0xF8) != (bits & 0x90))
            return 0x20;
        return 1 << (bits & 3);
    }
    if (!(bits & 0x88)) {
        if (bits & 0x10)
            return 2;
        if (bits & 0x20)
            return 4;
        return 8;
    }
    return 1;
}

/* What the mover overlaps at its new cell: runs the terrain and object checks and
   do_objhit on each object in the way, then decides whether it may step up or down to
   the target height (by at most b24, the step height; onto a solid object only if the
   handler allows climbing), and returns the collision state bits (top of the file). A
   step that climbs sets hit_flag and moves MP.pos[2]. */
int far get_pcoll(void)
{
    register unsigned state = 0;
    register int i;
    int r;
    unsigned char ok = 0;
    unsigned char climb = !(TP->noclimb & 0x80);
    unsigned char step;
    unsigned char c6 = 0;

    hit_flag = 0;
    TerrainCheck(CP->b24);
    ObjectCheck(0, 0);
    set_targz(0);
    step = !((state = Ppd.hits0 | Ppd.hits1) & TP->noclimb);
    if (Ppd.found > 0) {
        for (i = Ppd.first; Ppd.first + Ppd.count > i; i++) {
            r = do_objhit(i, Ppd.index);
            if (r & 4)
                state |= 0x400;
            if (r & 0x18) {
                if (r & 0x10)
                    state |= 0x4000;
                else
                    state = state | 0x8000;
                return state;
            }
        }
    }
    if (MP.pos[2] != MP.targz) {
        if (MP.hit == -1 && step) {
            ok = abs(MP.pos[2] - MP.targz) <= CP->b24;
            ok = ok || CP->vel[2] == 0 && !(Ppd.hits1 & 0x800) && Ppd.hits0 & 4;
        } else if (climb) {
            ok = ComObjData[MP.item].solid == 1;
            if (ok) {
                ok = abs(MP.pos[2] - oCollisions[MP.hit].top) <= CP->b24;
                if (ok)
                    state |= 0x80;
            }
        }
        ok = ok && targ_ceil && !c6;
        if (ok && MP.targz >= MP.f23)
            state &= ~0x100;
        else if (!ok && Ppd.top > MP.pos[2])
            state |= 0x100;
        if (ok && MP.targz + CP->height > 0x7F) {
            ok = 0;
            hit_flag = 1;
            state |= 0x200;
        } else if (ok && MP.hit == -1 && MP.targz + CP->height > MP.f23) {
            ok = 0;
            hit_flag = 1;
            state |= 0x400;
        }
        if (!(ok && state & 0x400) && ok && MP.hit != -1
            && (!ComObjData[MP.item].solid || !climb))
            ok = 0;
        if (ok) {
            hit_flag = 1;
            MP.pos[2] = MP.targz;
            if (abs(MP.targz - Ppd.floor) <= CP->radius)
                state |= 4;
            else
                state &= ~4;
        }
    } else if (MP.hit != -1 && ComObjData[MP.item].solid == 1 && climb) {
        state |= 0x80;
        state &= ~4;
    }
    if (hit_flag && ok) {
        process_objlist();
        state &= ~0x400;
        for (i = 0; i < Ppd.count; i++) {
            r = do_objhit(i, Ppd.index);
            if (r & 4)
                state |= 0x400;
        }
    }
    if (state & 0x80 && climb && (oCollisions[MP.hit].link.f.low & 0x10 || Ppd.hits0 & 4))
        state &= ~0x800;
    if (Ppd.hits1 & 0x100 && step && CP->vel[2] == 0 && MP.pos[2] + CP->b24 >= Ppd.top) {
        state &= ~0x100;
        MP.pos[2] = Ppd.top;
        if (MP.pos[2] == Ppd.floor)
            state |= 4;
        else
            state &= ~4;
    }
    if (!(state & 0xFC))
        state |= 0x1000;
    if (!(state & 0x80) && MP.pos[2] - CP->radius > Ppd.top)
        state |= 0x1000;
    return state;
}

/* After each step: gets the collision state and reacts to it. An object hit that stops
   or ends the move steps back. Otherwise the bits not in TP->ignore go to the handler's
   special function if any are in TP->mask (a nonzero return steps back and ends the
   move); walls, high floors and objects (0x700) bounce; nothing underneath (0x1000)
   starts gravity, -4. */
void far check_positions(void)
{
    uint16 state;
    unsigned char bounce2d = 0;

    state = get_pcoll();
    terr_type = CP->terrain;
    CP->terrain = set_resterr(state);
    if (state & 0xC000) {
        grid_move(-1);
        if (state & 0x4000)
            stop_me();
        else
            MP.done = MP.steps + 1;
        return;
    }
    state = state & ~TP->ignore;
    if (state != 0) {
        if (state & TP->mask && TP->special(&state)) {
            grid_move(-1);
            MP.done = MP.steps + 1;
            return;
        }
        if (state & 0x700) {
            do_2dbounce(!(state & 0x400));
            bounce2d = 1;
        }
        if (state & 0x1000 && CP->acc[2] == 0) {
            CP->acc[2] = -4;
            recalc_vecs(bounce2d);
        }
    }
}

/* name: IDA JumpTrap: FM Towns set_jmp_. */
/* Makes CP jump if its horizontal speed is at least min * 0x2F: vertical velocity from
   force (scaled differently for the player and others), gravity -2 unless already
   falling, and the horizontal velocity halved or, with stop, zeroed. Called from
   event/TRIGGER.C. */
void far set_jmp(int force, char stop, int min)
{
    if (abs(CP->vel[0]) + abs(CP->vel[1]) >= min * 0x2F) {
        if (CP->acc[2] != -4)
            CP->acc[2] = -2;
        if (CP->index == 1)
            CP->vel[2] = force * 0x2F / 2;
        else
            CP->vel[2] = force * 0x8D / 4;
        if (stop)
            CP->vel[0] = CP->vel[1] = 0;
        else {
            CP->vel[0] = CP->vel[0] / 2;
            CP->vel[1] = CP->vel[1] / 2;
        }
    }
}

/* name: IDA FindClosedDoorCollision: FM Towns IsaDoor_. */
/* The first object in the mover's way that is a closed door (door class, in-class
   index below 8), with its tile in *x and *y; 0 if none. Used by critter path finding
   to open doors. */
struct Object far * far IsaDoor(unsigned char *x, unsigned char *y)
{
    register int i;
    int item;
    int t;

    for (i = 0; i < Ppd.count; i++) {
        item = OBJ_ITEM(Obj_PtrTMem(&oCollisions[i + Ppd.first].link));
        t = oCollisions[i + Ppd.first].offset & MAP_MASK;
        *x = (Ppd.x >> 3) + t & MAP_MASK;
        t = *x - (Ppd.x >> 3);
        *y = (Ppd.y >> 3) + (oCollisions[i + Ppd.first].offset - t) / MAP_SIZE & MAP_MASK;
        if (item >> 4 == CLASS_DOOR && (item & ID_INCLASS) < 8)
            return Obj_PtrTMem(&oCollisions[i + Ppd.first].link);
    }
    return 0;
}

/* name: IDA GetCollisionObject: FM Towns CollObject_. */
/* The first object in the mover's way, or 0. */
struct Object far * far CollObject(void)
{
    if (Ppd.count)
        return Obj_PtrTMem(&oCollisions[Ppd.first].link);
    return 0;
}
