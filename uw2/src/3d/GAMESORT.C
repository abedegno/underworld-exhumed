/* target: seg034_310D */
/* opts: -mm -1 -G -O -Y -d */
/* GAMESORT.C: sorting the objects of each tile of the 3D view for drawing.

   GRIDDB.C's grdb_elem calls do_objsort with each visible tile's object list (and
   clear_objsort for a tile it does not draw). do_objsort orders the tile's objects
   farthest first and sends each to DRAWOBJ.C's do_obj, after setting the object's
   view position (objxloc, objyloc, objzloc, in 1/256 tile in the turned frame of
   VIEW3D.C's setup_vars) and its distance shade (locsqmod) or, in a pick frame, its
   tile offset (mptrmod).

   The sort key (sortdata[n][0], set_osum) depends on which part of the row the tile
   is in, as GRIDDB.C's subprocess tells sort_setup (dirval): 2 the left half, walked
   towards the middle, 1 the right half, 0 the middle column. A tile with a door or a
   bridge is split around it first (do_partition), so objects on the far side of a
   door, or under a bridge seen from above, are drawn before it.

   Animated objects (critters and the like, ComObjData's animated flag) that reach
   over the edge of their tile, by their radius, are held back and drawn with the
   neighbouring tile that is drawn later, so they are not overdrawn by it: refugees
   holds them per column for the next row nearer the eye, holdmid and holdtmp carry
   them between the halves of a row to the middle column. A held word is the object's
   index with flags: 0x4000 into the nearer row, 0x1000 sideways (0x2000 to the
   right), 0x8000 an animation object.

   name: inferred (map/filenames.tsv: "sorting objects for drawing ...; the job of
   System Shock's GAMESORT.C (partition_sort)"). FM Towns identifies the formerly
   anonymous helpers by their matching positions and operations: sort_setup, sort_obj,
   build_sort, set_sds, set_osum and clear_objsort. */

#include <mem.h>
#include "object.h"
#include "sys.h"
#include "view3d.h"

/* match: this file's _BSS, DS:2F9C..33C5 (seg033's ActDoors ends at 2F9B; seg035's starts at 33C6),
   laid out by name (tools/bssorder.py): holdmid 112, holdtmp 144, locsqmod 228,
   sortlist 267, sortdata 275, mptrmod 421, sd_xmod and sd_ymod 427, dirval 492,
   refugees 666, objxloc, objyloc and objzloc 935, objptrs 959. All FM Towns names: FM Towns
   has objptrs, the same 60 entries, right after refugees. */
uint16 holdmid[9], holdtmp[9];
unsigned char locsqmod;
signed char sortlist[60];
signed char sortdata[60][4];
int16 mptrmod;
int16 sd_xmod, sd_ymod;
signed char dirval;
uint16 refugees[33][9];                       /* 0x252 bytes: the memset clears them all */
int16 objxloc, objyloc, objzloc;
uint16 objptrs[60];
/* An object's fine x and y (0..7 within the tile) turned into the view's frame: for
   quadrant q, entry q * 16 + f * 2 is the turned x and the next byte the turned y;
   set_sds adds the x and y parts.
   This file's _DATA, DS:06FE..073D: between seg033's data and seg035's, and only this
   file uses it (FM Towns has it right after seg033's dirtab, before seg035's tables). */
signed char trans_pos_x[64] = {
    0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0,
    0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7,
    7, 0, 6, 0, 5, 0, 4, 0, 3, 0, 2, 0, 1, 0, 0, 0,
    0, 7, 0, 6, 0, 5, 0, 4, 0, 3, 0, 2, 0, 1, 0, 0 };


/* Called by GRIDDB.C's subprocess: -10 at the start of a frame (clear everything held),
   2 before a row's left half, 1 before its right half (what the left half passed to
   the middle moves to holdmid), 0 before the middle column (both halves' held objects
   are merged into holdtmp, at most 8). */
void far sort_setup(char mode)
{
    switch (mode) {
    case -10:
        memset(refugees, 0, 0x252);
        memset(holdmid, 0, 0x12);
        memset(holdtmp, 0, 0x12);
        break;
    case 2:
        memset(holdmid, 0, 0x12);
        memset(holdtmp, 0, 0x12);
        break;
    case 1:
        memcpy(holdmid, holdtmp, 0x12);
        holdtmp[0] = 0;
        break;
    case 0:
        if (holdmid[0]) {
            if (holdmid[0] + holdtmp[0] >= 9)
                holdmid[0] = 9 - holdtmp[0] - 1;
            memcpy(holdtmp + holdtmp[0] + 1, holdmid + 1, holdmid[0] * 2);
            holdtmp[0] += holdmid[0];
        }
        break;
    }
    dirval = mode;
}

/* Bubble sort sortlist[first..count] by key, largest (farthest) first. */
void far sort_obj(int first, int count)
{
    int last = count - 1;
    int i;
    for (; last >= first; last--) {
        for (i = first; i <= last; i++) {
            if (sortdata[sortlist[i]][0] < sortdata[sortlist[i+1]][0]) {
                char t = sortlist[i];
                sortlist[i] = sortlist[i+1];
                sortlist[i+1] = t;
            }
        }
    }
}

void far build_sort(int count)
{
    int i;
    for (i = 0; i < count; i++)
        sortlist[i] = i;
}

/* Split the tile's objects around object skip (a door or bridge): those whose value
   (sortdata[i][from_data], or the height for from_data 0) is above height go to one
   end of sortlist, the rest to the other, reverse saying which end is drawn first.
   The slot left between them, *point, gets skip itself. */
void far do_partition(char reverse, int16 *point, int skip, int count,
                      int height, int from_data)
{
    int16 ends[2], steps[2];
    int value;
    int side;
    register int i;
    if (reverse) {
        ends[0] = count - 1; steps[0] = -1;
        ends[1] = 0; steps[1] = 1;
    } else {
        ends[0] = 0; steps[0] = 1;
        ends[1] = count - 1; steps[1] = -1;
    }
    for (i = 0; i < count;) {
        if (i != skip) {
            if (from_data == 0)
                value = ((struct Object far *)Obj_IntTMem(objptrs[i]))->pos & POS_Z;
            else
                value = sortdata[i][from_data];
            side = value > height;
            /* match: the post-increment here keeps i in SI: storing it as a
               char in its own statement left it on the stack */
            sortlist[ends[side]] = i++;
            ends[side] += steps[side];
        } else
            i++;
    }
    /* match: an early return: the nested if reused AX for *point instead of
       reloading ends[0] */
    if (ends[0] != ends[1]) return;
    *point = ends[0];
    sortlist[*point] = skip;
}

/* Partition around a bridge by height: objects above it are drawn first when the
   eye is below the bridge (a table counts its own height). */
void far z_part(int index, int16 *point, int count)
{
    struct Object far *object;
    int z;
    object = Obj_IntTMem(objptrs[index]);
    z = object->pos & POS_Z;
    if (OBJ_ITEM(object) == ITEM_TABLE)
        z = z + ComObjData[object->id & ID_ITEM].height;
    do_partition(z * 8 > cPlayer->z, point, index, count, z, 0);
}

/* Partition around a door along the axis it lies across. Which side is drawn first
   comes from the half of the row (dirval) or, in the middle column, from the eye's
   position; probably the side away from the eye, which has not been checked. */
void far door_part(int index, int16 *point, int count)
{
    char reverse;
    int pos, axis;
    if (!(((((struct Object far *)Obj_IntTMem(objptrs[index]))->pos & POS_HEADING) >> 7) + quad * 2 & 3)) {
        axis = 2;
        pos = sortdata[index][2];
        reverse = 1;
    } else {
        axis = 1;
        pos = sortdata[index][1];
        if (dirval == 2) reverse = 0;
        else if (dirval == 1) reverse = 1;
        else reverse = (cPlayer->x >> 5) < pos;
    }
    do_partition(reverse, point, index, count, pos, axis);
}

/* An object's position in the tile, turned into the view: data[1] across, data[2]
   along the view, data[3] its height. */
void far set_sds(signed char *data, struct Object far *object)
{
    data[1] = trans_pos_x[quad * 16 + OBJ_FINEX(object) * 2]
            + trans_pos_x[((quad + 1) & 3) * 16 + OBJ_FINEY(object) * 2];
    data[2] = trans_pos_x[quad * 16 + OBJ_FINEX(object) * 2 + 1]
            + trans_pos_x[((quad + 1) & 3) * 16 + OBJ_FINEY(object) * 2 + 1];
    data[3] = object->pos & POS_Z;
}

/* The sort key for the part of the row being drawn: distance along the view, plus
   the distance from the middle column on either side. */
void far set_osum(signed char *data)
{
    switch (dirval) {
    case 0: data[0] = data[2] << 1; break;
    case 1: data[0] = data[1] + data[2] + 1; break;
    case 2: data[0] = 8 - data[1] + data[2]; break;
    }
}

/* For a tile not drawn: still draw any objects held for this column. */
void far clear_objsort(void)
{
    if (refugees[loopx][0] > 0) do_objsort(0L);
    if (holdtmp[0] > 0) do_objsort(0L);
}

/* Sort and draw one tile's objects (link is the tile's object list, 0 for only the
   held ones): first the objects held for this column, then up to 60 from the list,
   holding back animated objects that reach into a tile drawn later. */
void far do_objsort(union Link far *link)
{
    register int word;
    struct Object far *next;
    uint16 *dest;
    int16 visited, i, item, radius, partition, n, pivot;
    unsigned char candidate, held;
    int temp, distance;

    visited = 0;
    partition = 0;
    n = 0;
    pivot = -1;
    for (i = 1; (unsigned)i <= refugees[loopx][0] && n < 9; i++) {
        register signed char *data;
        word = refugees[loopx][i];
        objptrs[n] = word & 0x3ff;
        next = Obj_IntTMem(word & 0x3ff);
        data = sortdata[n];
        set_sds(data, next);
        if (word & 0x1000) {
            if (word & 0x2000) data[1] += -8;
            else data[1] += 8;
        }
        if (word & 0x4000) data[2] += 8;
        set_osum(data);
        if (word & 0x8000) data[0]--;
        word = next->id & ID_ITEM;
        n++;
    }
    if (holdtmp[0])
        memcpy(refugees[loopx], holdtmp, 0x12);
    else
        refugees[loopx][0] = 0;
    holdtmp[0] = 0;

    next = Obj_PtrTMem(link);
    while (next && visited < 60) {
        register signed char *data;
        candidate = held = 0;
        item = next->id & ID_ITEM;
        if (item == ITEM_BRIDGE || (item >> 4) == CLASS_DOOR || item == ITEM_MOVING_DOOR) {
            partition = n + (item << 6);
        } else if (ComObjData[item].animated) {
            word = 0;
            held = 1;
            data = sortdata[n];
            set_sds(data, next);
            radius = ComObjData[item].radius;
            if (data[2] - radius < 0) word |= 0x4000;
            if (dirval == 1) radius = -radius;
            if (dirval && ((data[1] + radius) & ~7)) {
                word |= 0x1000;
                if (dirval == 2) word |= 0x2000;
            }
            if (word) {
                candidate = 1;
                word |= (link->word >> 6) & 0x3ff;
                if ((item & ID_MAJOR) == FIRST_ANIMOBJ) word |= 0x8000;
                if ((word & 0x5000) == 0x5000) dest = holdtmp;
                else if (word & 0x4000) dest = refugees[loopx];
                else if (word & 0x2000) dest = refugees[loopx] + 9;
                else dest = refugees[loopx - 1];
                if (dest[0] >= 9) candidate = 0;
                else {
                    dest[0]++;
                    dest[dest[0]] = word;
                }
            }
        }
        if (!candidate) {
            if (!held) {
                data = sortdata[n];
                set_sds(data, next);
            }
            set_osum(data);
            if (held && item != ITEM_TABLE) data[0] -= radius * 2;
            if ((item & ID_MAJOR) == FIRST_ANIMOBJ) data[0]--;
            else if ((item & 0x1fe) == ITEM_TMAP_C) data[0] += 0x20;
            objptrs[n] = link->f.index;
            if (n < 60) n++;
        }
        link = &next->qn.link;
        next = Obj_PtrTMem(link);
        visited++;
    }
    if (partition && n > 1) {
        if ((partition >> 6) == ITEM_BRIDGE)
            z_part(partition & 0x3f, &pivot, n);
        else
            door_part(partition & 0x3f, &pivot, n);
        if (pivot > 1) sort_obj(0, pivot - 1);
        if (n - 2 > pivot) sort_obj(pivot + 1, n - 1);
    } else {
        build_sort(n);
        if (n > 1) sort_obj(0, n - 1);
    }
    for (i = 0; i < n; i++) {
        word = sortlist[i];
        next = Obj_IntTMem(objptrs[word]);
        objxloc = ((loopx - 16) << 8) + ((int)sortdata[word][1] << 5) + 16;
        objzloc = (loopy << 8) + ((int)sortdata[word][2] << 5) + 16;
        if (OBJ_MAJOR(next) == MAJOR_CREATURE || !IsMobElem(next))
            objyloc = OBJ_Z(next) << 3;
        else
            objyloc = next->b0F;
        if (PickUp) {
            mptrmod = (sortdata[word][2] / 8) * sd_ymod;
            mptrmod += ((sortdata[word][1] + 64) / 8 - 8) * sd_xmod;
        } else {
            temp = (objxloc - (cPlayer->x & 0xff)) >> 5;
            distance = temp * temp;
            temp = (objzloc - (cPlayer->y & 0xff)) >> 5;
            distance += temp * temp;
            temp = (objyloc - cPlayer->z) >> 5;
            distance += temp * temp;
            /* the sum is a 16-bit int (imul and add, then a signed test and cwd), which wraps
               for an object about 23 tiles off, which DOS then shades as if beside the player;
               the host's int kept 32 bits */
            if ((int16)distance > 0) temp = cSqRt((int16)distance);
            else temp = 0;
            distance = temp * smooth_div >> 6;
            distance += smooth_lowpass;
            if (distance < 0) distance = 0;
            locsqmod = distance + (unsigned char)smooth_base;  /* match: read as a byte */
            if (locsqmod > 14) locsqmod = 14;
        }
        do_obj(next);
    }
}
