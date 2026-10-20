/* target: seg027_2861 */
/* opts: -mm -1 -G -O -d -Y */
/* The object lists: the map's object storage, allocating and freeing objects, adding
   and removing them from lists, the conversions between object pointers and indices,
   searching lists, consistency checks with their debug reports, and garbage collection.
   The whole of UW1's DOS resident segment seg027_2861 (UW2's seg029_2A8E), in original
   order. UW1 has no symbol-bearing build: function and global names are UW2's (the FM
   Towns symbol table where it has them), the routines being the same, and listing names
   for UW1's own.

   Every object of a level lives in the level block (struct LevelBlock, level.h): 256
   mobile records of 27 bytes (critdata; index 0 is unused and index 1 is the player) and
   768 static records of 8 bytes (objdata, indices 0x100-0x3FF). An object is named by its
   index, and lists are chained through link words (union Link): each tile's list head,
   each object's next link (qn) and, unless the object is is_quant, its contents link
   (ol). Free objects sit on two stacks of indices, critbot..critptr (254 mobile, indices
   2 to 0xFF) and objbot..objptr (768 static); Obj_Alloc pops one and Obj_Free pushes it
   back. ActiveMob..LastActiveMob is a byte list of the mobile objects in use, which the
   critter code walks each frame.
   Main entry points: Obj_Alloc and Obj_Free; Obj_Add, Obj_AddEnd and Obj_Rem on a list;
   Obj_Punt, which deletes an object and its contents; Obj_PtrTMem, Obj_IntTMem and
   Obj_MemTPtr, between indices and pointers; the searches Obj_Find, Obj_InList,
   Obj_FindInMap; Obj_GarbageCollect, which culls distant objects when a free list runs
   out and when the player sleeps; Obj_ListOkay, the consistency check. Map_ObjFix lays
   the store out empty. Nearly every subsystem calls into this file (callers named here
   are UW2's).

   UW1 against UW2: no Obj_FindInMapSquare, check_weight or ObjCrunch; instead two
   debugging aids, seg027_2861_EF9 and seg027_2861_F23, which report through errmsg
   whether the lists are consistent, and seg027_2861_10E4, which prints a list as a tree
   through dprintf (flog_list and count_list also print each index found twice).
   Obj_Rem returns nothing and gives up after 0x401 links; Obj_FreeChain and
   Obj_FreeLinkChain free an object whether or not it was unlinked; Obj_Check has no null
   test and no stray comparison with the player; Obj_Find passes recurse on rather than
   testing it; Obj_ListOkay counts the player as reached when GrSq is negative instead of
   walking its inventory; Map_ObjFix clears only animcount.
   Name: inferred (the Obj_ prefix, and System Shock's object lists are OBJECTS.C).

   match: -Y: Obj_Elem_Fate passes chkTenacious as a far function pointer, and the EXE
   pushes the segment as a relocated constant (push 2674h) where the switches without -Y
   give push cs; nothing else in the file changes with it. */

#include <stdlib.h>
#include "event.h"
#include "file.h"
#include "inv.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
#include "uw2.h"
#include "view3d.h"

/* Declared in each file that uses it, its own way (no header). */
unsigned char far IsMobElem(struct Object far *obj);

/* An object pointer's index in the object store. */
#define MEMTPTR(o)      ((o) < (struct Object far *)objdata ? (o) - critdata \
                         : (struct StaticObj far *)(o) - objdata + NUM_MOBILE)

/* The object store, laid out in the map block by Map_ObjFix. */
/* match: this is the file's _BSS, in UW1 DS:272E..275F (UW2 DS:2588..25BB, which also
   had check_weight's total_weight). Turbo C lays out uninitialised data by a hash of the
   names, not in definition order, so the names fix the layout: the publics are the FM
   Towns names and land in the EXE's order. */
/* name: the three statics have no FM Towns names; the names are UW2Decomp's, chosen to
   hash into the right places. */
struct Object far *critdata;            /* the mobile objects */
unsigned char far *LastActiveMob;       /* the end of the active mobile list */
union Link far *Obj_Find_Head;         /* the list in which Obj_Find found its object */
static int16 mobcount;                  /* Obj_ListOkay's counts */
unsigned char far *ActiveMob;           /* the active mobile list */
static int16 cull_range;                /* chkTenacious's limit */
uint16 far *objtop;                     /* the free static list */
uint16 far *objbot;
uint16 far *objptr;
uint16 far *crittop;                    /* the free mobile list */
uint16 far *critbot;
uint16 far *critptr;
struct StaticObj far *objdata;          /* the static objects */
static int16 objcount;

/* Empties the object store: clears every tile's object list, points critdata, objdata and
   the free stacks into the level block, fills the free stacks with every index from 2 to
   0x3FF (mobile 2-0xFF, then static 0x100-0x3FF; 0 is "none" and 1 the player), clears the
   player's links and inventory, and empties the active mobile list and the animation
   count. MAP.C's Map_Load then overwrites the stacks with the level's own. */
void far Map_ObjFix(void)
{
#ifndef __TURBOC__
    port_shared_reset();
#endif
    struct Tile far *t;
    uint16 far *p;
    int i;

    for (t = mapdata, i = 0; i < MAP_SIZE * MAP_SIZE; i++, t++)
        t->objects.f.index = 0;
    critdata = LEVEL->mobile;
    objdata = (struct StaticObj far *)(critdata + NUM_MOBILE);
    critbot = LEVEL->mobfree;
    critptr = crittop = critbot + 0xFD;
    objbot = crittop + 1;
    objptr = objtop = objbot + 0x2FF;
    for (p = critbot, i = 2; i < NUM_OBJECTS; i++, p++)
        *p = i;
    if (ThePlayer) {
        ThePlayer->ol.f.link = ThePlayer->qn.f.next = 0;
        ClearInventory();
    }
    animcount = 0;                      /* UW1 has no timercount */
    ActiveMob = (unsigned char far *)(objtop + 1);
    LastActiveMob = ActiveMob;
}

/* Returns 1 if fn is true for obj or anything after it in its list, searching each
   object's contents depth first before moving to the next one. obj must not be null. */
unsigned char far Obj_Check(struct Object far *obj, unsigned char (far *fn)(struct Object far *obj))
{
    while (!(char)(*fn)(obj)) {         /* UW1: the result taken as a signed char */
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0) {
            if (Obj_Check(Obj_PtrTMem(&obj->ol.link), fn))
                return 1;
        }
        if (obj->qn.f.next == 0)
            return 0;
        obj = Obj_PtrTMem(&obj->qn.link);
    }
    return 1;
}

/* Whether an object resists culling: id bit 13 set, or its ComObj tenacity plus half of
   (quantity - 1) is above cull_range. A big stack is harder to cull than a single item. */
/* name: IDA ObjectCullingRngTest; FM Towns chkTenacious_ sits at the same place, between
   Obj_Check_ and Obj_Elem_Fate_, and does the same: flag bit 13, half the quantity plus
   the 4-bit field at byte 9 of ComObjData, compared with the static Obj_Elem_Fate sets.
   Obj_Elem_Fate passes it to Obj_Check in both builds. */
unsigned char far chkTenacious(struct Object far *obj)
{
    int extra;

    if (OBJ_DOORDIR(obj))
        return 1;
    if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
        extra = obj->ol.f.link - 1;
    else
        extra = 0;
    return ComObjData[OBJ_ITEM(obj)].tenacity + extra / 2 > cull_range;
}

/* Decides whether an object may be deleted (1) or must stay (0). A nonzero range gains a
   random 0 to 2; the object stays if it or anything inside it is tenacious against that
   (chkTenacious), and otherwise goes with chance range in 10. So range 0 never deletes,
   and Obj_Punt's range 10 deletes everything but objects with tenacity above 10 to 12
   (or holding one). */
unsigned char far Obj_Elem_Fate(int range, struct Object far *obj)
{
#ifndef __TURBOC__
    if (port_shared_avatar(obj)) return 0;
#endif
    if (obj == 0)
        return 0;
    if (range)
        range += (int)(((int32)rand() * 3) / 0x8000L);
    cull_range = range;
    if (chkTenacious(obj))
        return 0;
    if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0) {
        if (Obj_Check(Obj_PtrTMem(&obj->ol.link), chkTenacious))
            return 0;
    }
    if ((int)(((int32)rand() * 10) / 0x8000L) < cull_range)
        return 1;
    return 0;
}

/* Obj_Elem_Fate for the first object of the list head (0 for an empty list). */
unsigned char far Obj_Fate(int range, union Link far *head)
{
    struct Object far *obj;

    if (head->f.index == 0)
        return 0;
    obj = Obj_PtrTMem(head);
    return Obj_Elem_Fate(range, obj);
}

/* Deletes up to count objects (with their contents) from tiles more than 10 - range tiles
   from the player (taxicab distance), each tile's list in order, using Obj_Fate's random
   rule. Obj_Alloc calls it with range 3 when a free list is empty (5 mobile or 10 static
   objects, whichever kind is freed); SKILLS.C calls it with range 1 for 20 objects when
   the player sleeps. */
void far Obj_GarbageCollect(int range, int count)
{
    union Link far *tilehead;
    union Link link;
    int x;
    int y;
    int dy;
    int px;
    int py;
    struct Tile far *t;
    int freed;
    int next;

    px = OBJ_HOMEX(ThePlayer);
    py = OBJ_HOMEY(ThePlayer);
    t = mapdata;
    for (freed = y = 0; y < MAP_SIZE; y++) {
        dy = abs(py - y);
        for (x = 0; x < MAP_SIZE; t++, x++) {
            if (dy + abs(px - x) > 10 - range) {
#ifndef __TURBOC__
                if (port_shared_near_players(x,y,10-range)) continue;
#endif
                tilehead = &t->objects;
                link = *tilehead;
                while (link.f.index > 0) {
                    next = Obj_PtrTMem(&link)->qn.f.next;
                    if (Obj_Fate(range, &link)) {
                        Obj_FreeLinkChain(tilehead, Obj_IntTMem(link.f.index));
                        freed++;
                        if (freed >= count)
                            return;
                    }
                    link.f.index = next;
                }
            }
        }
    }
}

/* Pops a free mobile (mobile nonzero) or static object, garbage collecting once if the
   stack is empty; returns 0 if still none. A mobile object is added to the active list.
   The record is not cleared here (CreateObj in MAPADDR.C fills it in). */
struct Object far * far Obj_Alloc(char mobile)
{
    if (mobile) {
        if (critptr < critbot) {
            Obj_GarbageCollect(3, 5);
            if (critptr < critbot)
                return 0;
        }
        active_critter(*critptr);
        return critdata + *critptr--;
    } else {
        if (objptr < objbot) {
            Obj_GarbageCollect(3, 10);
            if (objptr < objbot)
                return 0;
        }
        return (struct Object far *)(objdata + (*objptr-- - NUM_MOBILE));
    }
}

/* Pushes an object back on its free stack; a mobile one also leaves the active list and,
   if it is the camera object (UsPtr), releases the camera. It must already be off any
   list. */
void far Obj_Free(struct Object far *obj)
{
    if (obj < (struct Object far *)objdata) {
        critptr++;
        *critptr = obj - critdata;
        if (obj == UsPtr)
            release_camera(*critptr);
        free_critter(*critptr);
    } else {
        objptr++;
        *objptr = (struct StaticObj far *)obj - objdata + NUM_MOBILE;
    }
}

/* Obj_Add puts obj at the head of a list, Obj_AddEnd at its tail; Obj_Rem unlinks it,
   giving up after 0x401 links (a looped list). */
void far Obj_Add(union Link far *head, struct Object far *obj)
{
    obj->qn.f.next = head->f.index;
    head->f.index = MEMTPTR(obj);
}

/* Appends obj to the end of the list head. */
void far Obj_AddEnd(union Link far *head, struct Object far *obj)
{
    union Link far *p;
    struct Object far *o;

    for (p = head; (o = Obj_PtrTMem(p)) != 0; p = &o->qn.link)
        ;
    obj->qn.f.next = 0;
    p->f.index = MEMTPTR(obj);
}

/* UW1: returns no value (UW2's returns whether obj was found). */
void far Obj_Rem(union Link far *head, struct Object far *obj)
{
    union Link far *p;
    struct Object far *o;
    int guard = 0;

    if (obj == 0)
        return;
    for (p = head; (o = Obj_PtrTMem(p)) != 0 && guard++ < 0x401; p = &o->qn.link) {
        if (o == obj) {
            p->f.index = obj->qn.f.next;
            obj->qn.f.next = 0;
            return;
        }
    }
}

/* Deletes obj and its contents, from list head if given (otherwise it is assumed to be
   off every list), when force is set or Obj_Elem_Fate(10) allows it. An animated object
   also leaves the animation list. Returns 0 when deleted, else obj. */
struct Object far * far Obj_Punt(union Link far *head, struct Object far *obj, char force)
{
    union Link link;

    if (force || Obj_Elem_Fate(10, obj)) {
        link.f.index = Obj_MemTPtr(obj);
        if (OBJ_MAJOR(obj) == MAJOR_ANIMOBJ)
            rem_anim_from_list(link.f.index);
        if (head)
            Obj_FreeLinkChain(head, obj);
        else
            Obj_FreeChain(&link);
        return 0;
    }
    return obj;
}

/* Frees every object in a list, last first, and everything inside them. A trap goes
   through trap_obj_del instead (the trap code unlinks and frees it). */
void far Obj_FreeChain(union Link far *head)
{
    struct Object far *obj;

    obj = Obj_PtrTMem(head);
    if (obj == 0)
        return;
    if (OBJ_MAJOR(obj) == MAJOR_TRAP) {
        trap_obj_del(head, obj);
    } else {
        if (obj->qn.f.next > 0)
            Obj_FreeChain(&obj->qn.link);
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
            Obj_FreeChain(&obj->ol.link);
        Obj_Rem(head, obj);
        Obj_Free(obj);
    }
}

/* Frees obj and its contents, unlinking it from head first when head is given. */
void far Obj_FreeLinkChain(union Link far *head, struct Object far *obj)
{
    if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
        Obj_FreeChain(&obj->ol.link);
    if (head)
        Obj_Rem(head, obj);
    Obj_Free(obj);
}

/* The object a link names (0 for none): indices below NUM_MOBILE are in critdata, the
   rest in objdata. */
struct Object far * far Obj_PtrTMem(union Link far *link)
{
    if (link == 0 || link->f.index == 0)
        return 0;
    if (link->f.index < NUM_MOBILE)
        return critdata + link->f.index;
    return (struct Object far *)(objdata + (link->f.index - NUM_MOBILE));
}

/* An object's index (0 for a null pointer). */
int far Obj_MemTPtr(struct Object far *obj)
{
    if (obj == 0)
        return 0;
    return MEMTPTR(obj);
}

/* The object with index index (0 for none). */
struct Object far * far Obj_IntTMem(int index)
{
    if (index == 0)
        return 0;
    if (index < NUM_MOBILE)
        return critdata + index;
    return (struct Object far *)(objdata + index - NUM_MOBILE);
}

/* Finds the object with index index in a list and the contents of its objects (UW1
   searches contents whatever recurse says). On success Obj_Find_Head is the list head it was in (top level only: a match
   inside a container leaves it at the container's list). */
struct Object far * far Obj_Find(union Link far *head, char recurse, int index)
{
    struct Object far *found;
    struct Object far *obj;

    if (head->f.index == 0)
        return 0;
    Obj_Find_Head = head;
    for (obj = Obj_PtrTMem(head); Obj_MemTPtr(obj) != index; obj = Obj_PtrTMem(&obj->qn.link)) {
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link != 0) {   /* UW1: recurse is only passed on */
            if ((found = Obj_Find(&obj->ol.link, recurse, index)) != 0)
                return found;
        }
        if (obj->qn.f.next == 0)
            return 0;
    }
    Obj_Find_Head = head;
    return obj;
}

/* True for a mobile object (one in critdata, below objdata). */
unsigned char far IsMobElem(struct Object far *obj)
{
    if (obj == 0)
        return 0;
    if (obj < (struct Object far *)objdata)
        return 1;
    return 0;
}

/* Appends a mobile index to the active list. */
/* name: IDA AddtoEndOfTileMapMobiles; FM Towns active_critter_ is at the same place after
   IsMobElem_ and appends a byte at LastActiveMob the same way. */
void far active_critter(int index)
{
    *LastActiveMob++ = index;
}

/* Removes a mobile index from the active list, moving the last entry into the hole. */
/* name: IDA RemoveFromMobilesList; FM Towns free_critter_ follows active_critter_ and does
   the same search of ActiveMob..LastActiveMob. */
/* match: the byte compare (cmp al,cl) with a word parameter is the cast. */
void far free_critter(int index)
{
    unsigned char far *p;

    for (p = ActiveMob; p < LastActiveMob; p++) {
        if (*p == (unsigned char)index) {
            LastActiveMob--;
            if (p < LastActiveMob)
                *p = *LastActiveMob;
            return;
        }
    }
}

/* Whether an index is on a free list. */
/* name: UW2's UNREFERENCED_seg029_2A8E_B04 (no FM Towns counterpart, no callers). UW1's
   listing has no procedure here (the target table counts it into free_critter), so it is
   named by its address; static, which the bytes cannot tell from public in a resident
   segment, so that match.py measures free_critter to the end of the table's row. */
static unsigned char far seg027_2861_AC4(int index)
{
    uint16 far *p;

    if (index <= 0xFF) {
        for (p = critptr; p >= critbot; p--)
            if (*p == index)
                return 1;
    } else {
        for (p = objptr; p >= objbot; p--)
            if (*p == index)
                return 1;
    }
    return 0;
}

/* Finds the first object in *head's list whose major class, minor class and index within
   the class match (-1 matches anything), searching contents when recurse is set. When the
   object is inside a container, *head is changed to the container's list. */
struct Object far * far Obj_InList(union Link far **head, char recurse, int major, int minor, int index)
{
    struct Object far *obj;
    struct Object far *found;
    union Link far *sub;
    union Link far *list;

    for (obj = Obj_PtrTMem(*head); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        if ((major == -1 || OBJ_MAJOR(obj) == major)
         && (minor == -1 || OBJ_MINOR(obj) == minor)
         && (index == -1 || OBJ_INCLASS(obj) == index))
            return obj;
        if (recurse && !OBJ_ISQUANT(obj) && (sub = &obj->ol.link)->f.index != 0) {
            list = sub;
            if ((found = Obj_InList(&list, recurse, major, minor, index)) != 0) {
                *head = list;
                return found;
            }
        }
    }
    return 0;
}

/* Whether obj is item id or holds one anywhere inside it. */
unsigned char far HasOrIsObj(struct Object far *obj, int id)
{
    union Link far *list;

    if (OBJ_ITEM(obj) == id)
        return 1;
    if (!OBJ_ISQUANT(obj)) {
        list = &obj->ol.link;
        if (Obj_InList(&list, 1, id >> 6, (id & ID_MINOR) >> 4, id & ID_INCLASS))
            return 1;
    }
    return 0;
}

/* Scans the map's tile lists (and their contents) for a match from tile (*x, *y) onward,
   row by row, and returns it with *x and *y at its tile. The caller resumes the search by
   advancing *x; an *x past the row moves to the next row. */
struct Object far * far Obj_FindInMap(int major, int minor, int index, int16 *x, int16 *y)
{
    struct Tile far *t;
    union Link far *head;
    struct Object far *found = 0;

    if (*x >= MAP_SIZE) {
        *x = 0;
        (*y)++;
    }
    t = mapdata + (*y << 6) + *x;
    for (; *y < MAP_SIZE; (*y)++) {
        for (; *x < MAP_SIZE; (*x)++, t++) {
            head = &t->objects;
            if (head->f.index > 0) {
                if ((found = Obj_InList(&head, 1, major, minor, index)) != 0)
                    return found;
            }
        }
        *x = 0;
    }
    return 0;
}

/* flog_list (a tile's list) and count_list (any chain) count how often each index is
   reached, for Obj_ListOkay; an object reached twice is reported through dprintf and
   makes them return 1 ("bad"). */
unsigned char far flog_list(int x, int y, unsigned char *counts, union Link far *head)
{
    struct Object far *obj = Obj_PtrTMem(head);
    unsigned char bad = 0;
    int i;

    for (obj = Obj_PtrTMem(head); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        i = Obj_MemTPtr(obj);
        if (++counts[i] > 1) {
            dprintf("====ERROR==== @%d,%d: Object %d occured for time #%d.\n", x, y, i,
                       counts[i]);
            bad = 1;
            if (counts[i] > 10)
                return 1;
        }
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
            bad = flog_list(x, y, counts, &obj->ol.link);
        if (IsMobElem(obj))
            mobcount++;
        else
            objcount++;
    }
    return bad;
}

/* A debugging aid: counts each index met in the list from obj and in the contents of its
   objects, printing (dprintf) any index met twice and giving up after a tenth time;
   mobcount and objcount count the objects. Returns 1 for a bad list. The result of a
   container's contents replaces the flag, so a duplicate seen earlier in the same list is
   forgotten when the contents after it are clean. */
unsigned char far count_list(struct Object far *obj, unsigned char *counts)
{
    unsigned char bad = 0;
    int i;

    for (; obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        i = Obj_MemTPtr(obj);
        if (++counts[i] > 1) {
            dprintf("====ERROR==== Object %d occured for time #%d.\n", i, counts[i]);
            bad = 1;
            if (counts[i] > 10)
                return 1;
        }
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
            bad = count_list(Obj_PtrTMem(&obj->ol.link), counts);
        if (IsMobElem(obj))
            mobcount++;
        else
            objcount++;
    }
    return bad;
}

/* Set once seg027_2861_EF9 has reported a bad object list, so it says so only once until
   the lists are good again. */
/* name: ours (no symbol-bearing build); match: this file's _DATA starts here, DS:02D6. */
static char objlist_complained = 0;

/* Checks the object lists (Obj_ListOkay) and reports the first failure through errmsg. */
void far seg027_2861_EF9(void)
{
    if (!(char)Obj_ListOkay(0)) {
        if (!objlist_complained) {
            errmsg("Problems in", "object list");
            objlist_complained = 1;
        }
    } else
        objlist_complained = 0;
}

/* Checks the object lists and says how they are, through errmsg. */
/* name: no procedure in UW1's listing (the target table counts it into
   seg027_2861_EF9), so named by its address; static, as nothing in the listing calls it,
   which the bytes cannot tell from public in a resident segment. */
static void far seg027_2861_F23(void)
{
    if (!(char)Obj_ListOkay(1))
        errmsg("Problem in object list", "Should clear objects");
    else
        errmsg("Object list is just fine.", "Have a nice day.");
}

/* Checks the object store: no index twice on the free stacks or in the lists, and every
   object either free or reachable from a tile or the object on the cursor (the player
   counts as reached when GrSq is negative, probably when the player is off the map).
   Returns 1 when all is consistent. how is not read. */
unsigned char far Obj_ListOkay(char how)
{
    uint16 far *p;
    int nstatic;
    int y;
    int nmobile;
    struct Tile far *t;
    union Link far *head;
    char bad;
    int x;
    unsigned char *counts;

    if ((counts = calloc(1, NUM_OBJECTS)) == 0)
        return 1;
    bad = 0;
    nmobile = GrSq < 0;
    for (p = critptr; p >= critbot; p--) {
        if (counts[*p])
            bad = 1;
        counts[*p]++;
        nmobile++;
    }
    nstatic = 0;
    for (p = objptr; p >= objbot; p--) {
        if (counts[*p])
            bad = 1;
        counts[*p]++;
        nstatic++;
    }
    objcount = mobcount = 0;
    t = mapdata;
    for (y = 0; y < MAP_SIZE; y++) {
        for (x = 0; x < MAP_SIZE; x++, t++) {
            head = &t->objects;
            if (head->f.index > 0)
                bad = flog_list(x, y, counts, head) || bad;
        }
    }
    if ((GameInputMode == 1 || GameInputMode == 0) && CursorObjPtr != 0)
        bad = count_list(CursorObjPtr, counts) || bad;
    if (nmobile + mobcount != 0xFF)
        bad = 1;
    if (nstatic + objcount != NUM_STATIC)
        bad = 1;
    free(counts);
    return !bad;
}

/* Prints an object list as a tree through dprintf, one line per object (index, major,
   minor and class, next link, the contents link or quantity, and a mobile object's
   tile), recursing into containers with depth + 1. Nothing in UW1's listing calls it. */
void far seg027_2861_10E4(union Link far *head, int depth)
{
    struct Object far *obj;
    int i;

    for (obj = Obj_PtrTMem(head); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        if (depth > 0) {
            for (i = 1; i < depth; i++)
                dprintf("\xB3");
            if (obj->qn.f.next == 0)
                dprintf("\xC0");
            else
                dprintf("\xC3");
            dprintf("\x10");
        }
        dprintf("<%4d> %1d|%1d|%2d, Next: %4d, %5s: %4d", Obj_MemTPtr(obj), OBJ_MAJOR(obj),
                   OBJ_MINOR(obj), OBJ_INCLASS(obj), obj->qn.f.next,
                   OBJ_ISQUANT(obj) ? ((obj->ol.f.link & LINK_SPECIAL) ? "Other" : "Count") : "Link",
                   obj->ol.f.link);
        if (IsMobElem(obj))
            dprintf("; @%2d,%2d", OBJ_HOMEX(obj), OBJ_HOMEY(obj));
        dprintf(".\n");
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
            seg027_2861_10E4(&obj->ol.link, depth + 1);
    }
}
