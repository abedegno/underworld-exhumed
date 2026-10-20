/* target: ovr118 */
/* opts: -mm -1 -G -O -Y -d */
/* Saving and restoring the player's inventory in player.dat: the player object, the object
   in the cursor and every object reachable from them are copied into the workspace, with
   their links renumbered to the copy's own indexes, and back again. The whole of DOS
   overlay ovr118, in original order. Seeded from UW2's INVSAVE.C (ovr122).

   What it does in the game: the inventory lives in the level's object lists, which are
   saved per level, so the player's own objects travel separately. SavePlayerInv writes
   PLAYER.DAT: the player data (save_player_data), a word, and the workspace copy. With no
   name it only fills the workspace, and RestorePlayerInv with no name puts it back.
   Restoring frees the old inventory first (Punt_player_inv, which in UW1 is not in this
   file: it is called through stub134_2F).

   UW1's differences from UW2: Punt_player_inv is elsewhere; the cursor object is copied
   and restored whenever GameInputMode is 1, with no cursorSaved flag, and CursorObjPtr is
   not cleared after the copy; Obj_Rem's result is not tested in FreePlayerInv; and
   RestorePlayerInv with a name takes the player object off and puts it back on its tile's
   object list itself (Obj_Rem and Obj_Add on mapdata[GrSq], when GrSq >= 0) where UW2
   calls change_GrSq.

   The workspace holds the player object at 0, the cursor object at 0x1B, a copy of the
   inventory slots at 0x23, and from 0x5B the saved objects (8-byte static object records),
   numbered from 1 so that 0 can stay the empty link. The word written before it is one more
   than the number of objects, which makes saveNum * 8 + 0x5B the length of the copy.

   Function names are UW2's (FM Towns originals); RestorePlayerInv is UW2's name for the
   last function, which targets/ovr118.tsv leaves as ovr118_639.
   Name: descriptive (saving and restoring the player's inventory). */

#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include <stat.h>                       /* match: sys\stat.h; the build keeps it flat */
#include <string.h>
#include "file.h"
#include "inv.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
#include "uw2.h"

/* name: FM Towns keeps these as statics after BagSaveHandles, so their names are not known. */
/* match: these names were chosen to land in _BSS in the EXE's order (UW1: DS:5A82, 5A86,
   5A8A, 5A8C; UW2 also had cursorSaved). */
static union Link far *invSlots;        /* the inventory slots in the workspace */
static struct StaticObj far *saveObjs;     /* the saved objects, numbered from 1 */
static int16 saveNum;                   /* the number of saved objects */
static void far *saveBuf;               /* the workspace */
#ifndef __TURBOC__
static int shared_pack;
#endif

/* Fills the workspace ws with the player object, the inventory and, in input mode 1, the
   cursor object (which is freed from memory: it is in the copy now). */
void far makePlayerInvCopy(void far *ws)
{
    struct Object far *p;
    struct StaticObj far *cursor;
    union Link chain;
    int i;

    FixBagArea();
    p = ws;
    *p = *ThePlayer;
    p->qn.f.next = 0;
    cursor = (struct StaticObj far *)(p + 1);
    invSlots = (union Link far *)(cursor + 1);
    saveObjs = (struct StaticObj far *)(invSlots + NUM_INV_SLOTS);
    saveNum = 0;
    for (i = 0; i <= INV_PACK_LAST; i++)
        invSlots[i].f.index = invSlots[i].f.low = 0;
    InvSaveNexts(&ThePlayer->ol.link, &p->ol.word);
    if (GameInputMode == 1) {
        *cursor = *(struct StaticObj far *)CursorObjPtr;
        if (!OBJ_ISQUANT(CursorObjPtr))
            InvSaveNexts(&CursorObjPtr->ol.link, &cursor->ol.word);
        chain.f.index = Obj_MemTPtr(CursorObjPtr);
#ifndef __TURBOC__
        if (!shared_pack)
#endif
        Obj_FreeChain(&chain);
    }
}

/* Copies the inventory to the workspace and, given a save directory name, writes
   name + "player.dat" and releases the workspace. Returns 0 if the workspace or the file
   could not be had. */
char far SavePlayerInv(char *name)
{
    char ok = 1;
    char path[0x42];
    register int fd;

    saveBuf = MK_FP(get_workspace(), 0);
    if (saveBuf == 0)
        return 0;
    makePlayerInvCopy(saveBuf);
    saveNum++;
    if (name) {
        strcpy(path, name);
        strcat(path, "player.dat");
        fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
        if (fd >= 0) {
            save_player_data(fd);
            FarWrite_ovr167_627(fd, &saveNum, 2);
            FarWrite_ovr167_627(fd, saveBuf, saveNum * 8 + 0x5B);
            close(fd);
        } else
            ok = 0;
        release_workspace();
        saveBuf = 0;
        editchng(0x200);
    }
    return ok;
}

/* Copies the list at src, and every list inside it, to the save area, pointing dst at it. */
void far InvSaveNexts(union Link far *src, uint16 far *dst)
{
    struct Object far *obj;
    struct Object far *copy;
    union Link far *contents;
    union Link far *copied;

    for (; obj = Obj_PtrTMem(src), obj; ) {
        copy = allocSaveObj();
        *(struct StaticObj far *)copy = *(struct StaticObj far *)obj;
        ((union Link far *)dst)->f.index = saveNum;
        replaceInInv((union Link far *)src, (union Link far *)dst);
        src = &obj->qn.link;
        dst = &copy->qn.word;
        contents = &obj->ol.link;
        copied = &copy->ol.link;
        if (!OBJ_ISQUANT(obj) && contents->f.index != 0)
            InvSaveNexts(contents, &copied->word);
    }
}

/* An inventory slot holding the object at old is pointed at its copy instead. */
void far replaceInInv(union Link far *old, union Link far *new)
{
    int i;

    for (i = 0; i <= INV_PACK_LAST; i++)
        if (Inventory[i].f.index == old->f.index)
            invSlots[i].f.index = new->f.index;
}

/* The next free record of the save area (numbered from 1). */
struct Object far * far allocSaveObj(void)
{
    ++saveNum;
    return (struct Object far *)(saveObjs + saveNum);
}

/* Saved object n of the save area, or 0 for n 0. */
struct Object far * far getSaveObj(int n)
{
    if (n == 0)
        return 0;
    return (struct Object far *)(saveObjs + n);
}

/* An inventory slot holding the saved object at saved is pointed at its restored copy. */
void far putInInv(union Link far *mem, union Link far *saved)
{
    int i;

    for (i = 0; i <= INV_PACK_LAST; i++)
        if (invSlots[i].f.index == saved->f.index)
            Inventory[i].f.index = mem->f.index;
}

/* Restores the saved list at src, and every list inside it, pointing dst at it. */
void far InvRestoreNexts(uint16 far *dst, union Link far *src)
{
    struct Object far *obj;
    struct Object far *saved;
    union Link far *contents;
    union Link far *copied;

    for (; saved = getSaveObj(((union Link far *)src)->f.index), saved; ) {
        obj = Obj_Alloc(0);
        *(struct StaticObj far *)obj = *(struct StaticObj far *)saved;
        ((union Link far *)dst)->f.index = Obj_MemTPtr(obj);
        putInInv((union Link far *)dst, (union Link far *)src);
        dst = &obj->qn.word;
        src = &saved->qn.link;
        contents = &obj->ol.link;
        copied = &saved->ol.link;
        if (!OBJ_ISQUANT(saved) && copied->f.index != 0)
            InvRestoreNexts(&contents->word, copied);
    }
}

/* Frees every object in the list head and in their contents. */
void far FreePlayerInv(union Link far *head)
{
    struct Object far *obj;
    union Link far *link;

    obj = Obj_PtrTMem(head);
    if (obj == 0)
        return;
    link = &obj->ol.link;
    if (!OBJ_ISQUANT(obj) && link->f.index != 0)
        FreePlayerInv(&obj->ol.link);
    link = &obj->qn.link;
    if (link->f.index != 0)
        FreePlayerInv(link);
    Obj_Rem(head, obj);
    Obj_Free(obj);
}

/* Rebuilds the player object, the inventory and the cursor object from the workspace. */
void far getPlayerInvCopy(void far *ws)
{
    struct Object far *p;
    struct StaticObj far *cursor;

    p = ws;
    cursor = (struct StaticObj far *)(p + 1);
    invSlots = (union Link far *)(cursor + 1);
    saveObjs = (struct StaticObj far *)(invSlots + NUM_INV_SLOTS);
    *ThePlayer = *p;
    InvRestoreNexts(&ThePlayer->ol.word, &p->ol.link);
    if (GameInputMode == 1) {
        CursorObjPtr = Obj_Alloc(0);
        *(struct StaticObj far *)CursorObjPtr = *cursor;
        if (!OBJ_ISQUANT(cursor))
            InvRestoreNexts(&CursorObjPtr->ol.word, &cursor->ol.link);
    }
}

/* Frees the inventory and rebuilds it, from name + "player.dat" when a save directory is
   given (also reading the player data and reloading the paperdoll pictures, with the player
   object off its tile's list meanwhile), or from the workspace a nameless SavePlayerInv
   filled. */
char far RestorePlayerInv(register char *name)
{
    char ok = 1;
    char path[0x42];
    register int fd;

    if (name && GrSq >= 0)
        Obj_Rem(&mapdata[GrSq].objects, ThePlayer);
    Punt_player_inv();
    if (saveBuf == 0) {
        saveBuf = MK_FP(get_workspace(), 0);
        if (saveBuf == 0)
            return 0;
    }
    if (name) {
        strcpy(path, name);
        strcat(path, "player.dat");
        fd = open(path, O_RDWR | O_BINARY);
        if (fd >= 0) {
            read_player_data(fd);
            intoFarBuffer_ovr167_5DA(fd, &saveNum, 2);
            intoFarBuffer_ovr167_5DA(fd, saveBuf, saveNum * 8 + 0x5B);
            close(fd);
            load_inventory_pix();
        } else {
            ok = 0;
            goto done;
        }
    }
    getPlayerInvCopy(saveBuf);
    FixPlayerEquips();
done:
    release_workspace();
    saveBuf = 0;
    if (name && GrSq >= 0)
        Obj_Add(&mapdata[GrSq].objects, ThePlayer);
    return ok;
}

#ifndef __TURBOC__
/* The same relocatable inventory image as PLAYER.DAT, in a separate bounded
   buffer. Packing preserves the live cursor; world saves detach it afterwards. */
int port_shared_inventory_pack(void *buffer,int capacity)
{
    int size;
    if(capacity<0x63+NUM_STATIC*8)return 0;
    memset(buffer,0,(unsigned)capacity);shared_pack=1;
    makePlayerInvCopy(buffer);shared_pack=0;
    size=0x5B+(saveNum+1)*8;
    return size<=capacity ? size : 0;
}

int port_shared_inventory_valid(const void *buffer,int length,int carrying)
{
    const unsigned char *bytes=buffer;
    const struct Object *root=buffer;
    const struct StaticObj *cursor=(const struct StaticObj*)(bytes+sizeof(struct Object));
    const union Link *slots=(const union Link*)(cursor+1);
    const struct StaticObj *objects=(const struct StaticObj*)(slots+NUM_INV_SLOTS);
    unsigned short stack[NUM_STATIC*2+2];unsigned char seen[NUM_STATIC+1];
    int count,top=0,i,n,visited=0;
    if(length<0x63 || length>0x63+NUM_STATIC*8 || (length-0x5B)%8 || carrying<0 || carrying>1)return 0;
    count=(length-0x5B)/8-1;memset(seen,0,sizeof seen);
    if(OBJ_ITEM(root)!=ITEM_ADVENTURER || root->qn.f.next || root->whoami!=0xFD)return 0;
    stack[top++]=(unsigned short)root->ol.f.link;
    if(carrying && !OBJ_ISQUANT(cursor))stack[top++]=(unsigned short)cursor->ol.f.link;
    while(top) {
        n=stack[--top];if(!n)continue;
        if(n>count || seen[n] || top>NUM_STATIC*2-2)return 0;
        seen[n]=1;visited++;
        stack[top++]=(unsigned short)objects[n].qn.f.next;
        if(!OBJ_ISQUANT(&objects[n]))stack[top++]=(unsigned short)objects[n].ol.f.link;
    }
    if(visited!=count)return 0;
    for(i=0;i<=INV_PACK_LAST;i++)if(slots[i].f.index>count || (slots[i].f.index && !seen[slots[i].f.index]))return 0;
    return 1;
}

int port_shared_inventory_unpack(const void *buffer,int length,int carrying)
{
    extern uint16 *objptr,*objbot;
    int count=(length-0x5B)/8-1;
    if(!port_shared_inventory_valid(buffer,length,carrying) || objptr-objbot+1<count+carrying)return 0;
    GameInputMode=carrying;CursorObjPtr=NULL;ClearInventory();
    getPlayerInvCopy((void*)buffer);return 1;
}
#endif
