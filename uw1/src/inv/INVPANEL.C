/* target: ovr121 */
/* opts: -mm -1 -G -O -Y -d */
/* The inventory panel: the paperdoll and backpack slots, their screen rectangles, picking
   objects up and putting them down in the slots, stacking and combining them, the weight
   display and the hit test: the whole of UW1's DOS overlay ovr121 (UW2's ovr125), in
   original order.

   What it does in the game: the right-hand panel in its inventory mode. BeginInventory
   saves the screen under every slot and registers the panel's mouse region; a click there
   comes to DoInventoryMouse (through mous_in_panel), which picks the object up onto the
   cursor (SetCursorObj, asking 'Move how many?' for a stack) or puts the cursor object down
   (RearrangeInventory): into an empty slot, onto a bag (PutObjectInBag, BAGS.C), merged with
   a matching stack (AddTogether), combined with another object (COMBINE.C), or swapped.
   ItemFitsSlot holds the rules for what goes where. DisplayInvSpecial draws the paperdoll
   with the armour worn, each piece's picture chosen by its type and condition;
   displayInventoryArray draws slots with stack counts, and displayEnc the weight the player
   can still carry.

   UW1 against UW2: the panel and its slots sit a few pixels differently (InvDisplay);
   the paperdoll pictures start at 0x2091 (ICON_INV); DisplayInvSpecial walks display positions 1..5
   (no ArmorSlots table); no click-versus-drag test when picking up, no use of a bag's
   arrows by a look click, no void check when dragging in; AskHowMany returns 0 at once for
   Escape; ItemFitsSlot tries a light's lit form in the light slots first and has no key
   ring, and its 'is too full' message is a literal; no storage crystal; fonts are loaded by
   file name; string numbers, item ids and colours differ.

   Callers: INTERACT.C (the panel's mouse region, the look and use commands), BAGS.C,
   INVDATA.C, BARTER.C and CONVERSE.C (the trade slots), INVSAVE.C, USEITEMS.C, COMBINE.C,
   WORLDEV.C. It calls COMBINE.C for drops onto another object and USEITEMS.C's UseFood
   for food dropped on the head.

   Data owned: the slot tables (InvDisplay, a rectangle per display position;
   SlotToDisplay and DisplayToSlot between Inventory[] slots and display positions, see
   inv.h), OpenBag and OpenBagList (BAGS.C's open bags), CursorObjPtr (the object on the
   cursor), the screen saves (SaveHandles) and the paperdoll picture cache (invArmorObj,
   invArmorQ). Containers[] (what each container holds and how much) is OBJCLASS.C's data,
   used here.
   UW1 has no symbol-bearing build: the names are UW2's (the FM Towns symbol table), the
   routines being the same.
   Name: descriptive (the inventory panel: DoInventoryMouse, DisplayInventory). */

#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "inv.h"
#include "object.h"
#include "player.h"
#include "ui.h"
#include "uw2.h"

/* PLAYER.C defines the record's storage as a byte array; this file reaches it as the
   union (player.h). */
extern union PlayerStore PlayerDat;

/* UW1: the paperdoll's pictures (UW2 0x206D): the body, then one per armour position */
#define PIC_BODY        ICON_INV

/* UW1: the font loader takes a file name (UW2: grfx_quikfont(int)). */
unsigned char far grfx_load_font(char *name);

/* This file's _BSS, UW1 DS:5ACA..5B09 (UW2 DS:6AD0..6B0F). */
/* match: laid out by name (tools/bssorder.py): invArmorObj and
   invArmorQ 57, SaveHandles 827, panel_mouse 968, CursorObjPtr 995. panel_mouse
   (DS:5B04) has no FM Towns name and only this file uses it, so it is static, its
   provisional name chosen for its key. Inventory (DS:5A92) stays extern, as in UW2. */
char invArmorObj[6];
char invArmorQ[6];
int16 SaveHandles[NUM_DISPLAY];
static int16 panel_mouse;               /* DS:5B04 */
struct Object far *CursorObjPtr;

char ValidLightSlots[4] = { INV_SHOULDER, INV_SHOULDER + 1, INV_HAND, INV_HAND + 1 };
struct Bag far *OpenBagList = 0;
struct Bag far *OpenBag = 0;
struct InvRect InvDisplay[NUM_DISPLAY] = {
    { 0x0, 0x0, 0x0, 0x0, 0x104, 0xBC, 0x24, 0x45 },
    { 0x10D, 0x90, 0x11D, 0x80, 0x10C, 0xAF, 0x13, 0x32 },
    { 0x10D, 0xBF, 0x11E, 0xAF, 0x10B, 0xBD, 0x14, 0x14 },
    { 0x107, 0xAE, 0x123, 0x9D, 0x106, 0xB0, 0x21, 0x2C },
    { 0x107, 0x9C, 0x123, 0x90, 0x105, 0x9D, 0x21, 0xC },
    { 0x107, 0x80, 0x123, 0x77, 0x10A, 0x85, 0x15, 0xD },
    { 0xF4, 0xBB, 0x105, 0xAA, 0xF5, 0xBA, 0x10, 0x10 },
    { 0x125, 0xBB, 0x136, 0xAA, 0x126, 0xBA, 0x10, 0x10 },
    { 0xF1, 0xA5, 0x102, 0x92, 0xF2, 0xA4, 0x10, 0x10 },
    { 0x127, 0xA5, 0x138, 0x92, 0x128, 0xA4, 0x10, 0x10 },
    { 0xF1, 0x93, 0x10C, 0x88, 0xFF, 0x94, 0x10, 0x10 },
    { 0x11E, 0x93, 0x138, 0x82, 0x11D, 0x94, 0x10, 0x10 },
    { 0xF0, 0x76, 0x101, 0x65, 0xF1, 0x75, 0x10, 0x10 },
    { 0x103, 0x76, 0x114, 0x65, 0x104, 0x75, 0x10, 0x10 },
    { 0x116, 0x76, 0x127, 0x65, 0x117, 0x75, 0x10, 0x10 },
    { 0x129, 0x76, 0x13A, 0x65, 0x12A, 0x75, 0x10, 0x10 },
    { 0xF0, 0x64, 0x101, 0x53, 0xF1, 0x63, 0x10, 0x10 },
    { 0x103, 0x64, 0x114, 0x53, 0x104, 0x63, 0x10, 0x10 },
    { 0x116, 0x64, 0x127, 0x53, 0x116, 0x63, 0x10, 0x10 },
    { 0x129, 0x64, 0x13A, 0x53, 0x12A, 0x63, 0x10, 0x10 },
    { 0xF0, 0x87, 0x101, 0x76, 0xF1, 0x87, 0x10, 0x10 },
    { 0x127, 0x81, 0x130, 0x78, 0x128, 0x81, 0x8, 0xA },
    { 0x131, 0x81, 0x13A, 0x78, 0x132, 0x81, 0x8, 0xA }
};
#define SLOT_TO_DISPLAY 2, 3, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, \
    12, 13, 14, 15, 16, 17, 18, 19
#define DISPLAY_TO_SLOT 1, 3, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
#ifdef __TURBOC__
char SlotToDisplay[NUM_INV_SLOTS] = { SLOT_TO_DISPLAY };
char DisplayToSlot[21] = { DISPLAY_TO_SLOT };
#else
char SlotToDisplay[NUM_INV_SLOTS + 21] = { SLOT_TO_DISPLAY, DISPLAY_TO_SLOT };     /* inv.h has why */
#endif
/* name: UW2's FM Towns build keeps the next three as unnamed statics; the names are ours. */
static int16 panel_input = 0;           /* DS:189D, the panel's mouse handler */
static int16 shown_capacity = -1;       /* DS:189F, the weight figure on screen */
unsigned char InvUpArrow = 0;
unsigned char InvDownArrow = 0;
static char inv_begun = 0;              /* DS:18A3 */
unsigned char inv_refresh = 1;

/* Loads the paperdoll body picture (BODIES, by sex and body type) and forgets the cached
   armour pictures. */
void far load_inventory_pix(void)
{
    register int i;
    register char *name = "bodies";

    reload_gr_vpic(PIC_BODY, name, player->female * 10 / 2 + player->body);
    for (i = 1; i <= 5; i++)
        invArmorObj[i] = 0;
}

/* Once per game: saves the background under each slot's rectangle for redrawing, and
   registers the panel's mouse region and its handler mous_in_panel. */
void far BeginInventory(void)
{
    int x;
    register int i;
    register int w;

    if (!inv_begun) {
        inv_begun = 1;
        Transparency = 1;
        load_inventory_pix();
        for (i = 6; i < NUM_DISPLAY; i++)
            SaveHandles[i] = valloc(InvDisplay[i].w, InvDisplay[i].h);
        SaveHandles[0] = valloc(0x10, 0x0A);
        SaveHandles[1] = valloc(0x54, 0x29);
        for (i = 6; i < NUM_DISPLAY; i++) {
            if (i == 10) {
                x = InvDisplay[i].x + 5;
                w = InvDisplay[i].w - 5;
            } else if (i == 11) {
                x = InvDisplay[i].x;
                w = InvDisplay[i].w - 5;
            } else {
                x = InvDisplay[i].x;
                w = InvDisplay[i].w;
            }
            save_rect(SaveHandles[i], x, InvDisplay[i].y, w, InvDisplay[i].h);
        }
        save_rect(SaveHandles[1], 0xEC, 0x77, 0x54, 0x29);
        save_rect(SaveHandles[0], 0x12B, 0x8D, 0x10, 0x0A);
        panel_mouse = defineMouseRegion(0xF0, 0x52, 0x13B, 0xBD, ICON_CURSORS);
        panel_input = input_addmouse(0xF0, 0x52, 0x13B, 0xBD, 0, 5, (InputFn)mous_in_panel);
    }
}

/* name: IDA DoesNothing_ovr125_1E6; FM Towns has EndInventory_, also empty, in this place. */
void far EndInventory(void)
{
}

/* Empties every slot and the cursor, forgets the paperdoll pictures and the open bags. */
void far ClearInventory(void)
{
    register int i;

    for (i = 0; i < NUM_INV_SLOTS; i++)
        Inventory[i].f.index = 0;
    for (i = 1; i <= 5; i++)
        invArmorObj[i] = 0;
    OpenBag = OpenBagList = 0;
    InvUpArrow = InvDownArrow = 0;
    CursorObjPtr = 0;
    shown_capacity = -1;
}

/* A click in the inventory panel (how: the kind of click, -2 a look). With an empty
   cursor, a press on an object picks it up (dragging a stack asks how many; a bag already
   open cannot be taken, and in barter mode only a container of class 0xF can, else string
   0xC9); clicking the empty weapon hand toggles fight mode. With an object on the cursor it
   is put down in the slot (RearrangeInventory) or handed to DoSpecialActions (BAGS.C), which also
   handles a use click on a slot with nothing on the cursor, for the special positions: 21
   and 22 the bag's scroll arrows, 0x17 the 3D view (dropping into the world), 0x18 the
   player's barter area. While another panel is shown on the right only the 3D view (0x17)
   takes objects. */
void far DoInventoryMouse(int how)
{
    int x0;
    int y0;
    int16 x;
    int16 y;
    char pick;
    struct Object far *obj;
    struct Object far *split;
    struct Bag far *bag2;
    char held;
    struct Bag far *bag;
    register int hit;
    register int slot;

    pick = 0;
    split = 0;
    held = CursorObjPtr != 0;
    x0 = inplist->x + 0xF0;
    y0 = inplist->y + 0x52;
    hit = FindInventoryHit(x0, y0);
    if (hit > 0 && hit < DISP_UP) {
        slot = DisplayToSlot[hit];
        if (CursorObjPtr == 0 && Inventory[slot].f.index == 0) {
            if (INV_WEAPON_HAND - player->lefty == slot)
                toggle_fightmode();
            mouse_release(1);
            return;
        }
        if (CursorObjPtr == 0 && slot != -1 && slot != INV_BAG)
            pick = 1;
        if (pick && mouse_dragged(1)) {
            obj = Obj_PtrTMem(&Inventory[slot]);
            if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)) {
                if (obj->ol.f.link != 1) {
                    if ((split = AskHowMany(obj)) == 0)
                        return;
                    if (split != obj)
                        Obj_Add(&obj->qn.link, split);
                }
            } else if (OBJ_MAJOR(obj) == MAJOR_MISC && OBJ_MINOR(obj) == MINOR_CONTAINER) {
                if (inplist->mode == 4 && OBJ_INCLASS(obj) != 0xF) {
                    game_sprint(0xBA);  /* 'You cannot barter a container. Instead, remove the contents ...' */
                    return;
                }
                for (bag2 = OpenBagList; bag2 != 0; bag2 = bag2->next)
                    if (Obj_PtrTMem(&bag2->obj) == obj)
                        return;
            }
            held = 1;
            SetCursorObj(slot, split != 0 && split != obj);
            if (slot > INV_BAG) {
                for (bag = OpenBag; bag != 0; bag = bag->prev)
                    bag->weight -= ItemWeight(obj);
                FixOpenBag();
                DisplayOpenBag();
            } else
                DisplayInvObject(SlotToDisplay[slot]);
            if (CursorObjPtr == 0)
                return;
            if (split != 0) {
                GameInputMode = 1;
                return;
            }
        }
        mouse_release(1);
        mouse_getxy(&x, &y);
        hit = FindInventoryHit(x, y);
    }
    mouse_release(1);
    if (CursorObjPtr != 0 && GameInputMode != 2) {
        GameInputMode = 1;
        if (RightPanel != 0 && hit != DISP_WORLD)
            return;
        if (hit > 0) {
            if (hit < DISP_UP)
                RearrangeInventory(DisplayToSlot[hit]);
            else {
                DoSpecialActions(hit);
                held = 0;
            }
        }
    } else if (hit > 0) {
        if (how >= 0) {
            DoSpecialActions(hit);
            held = 0;
        } else if (how == -2)
            inv_look();
    }
    if (held && CursorObjPtr == 0) {
        unforce_mouse_cursor(3);
        GameInputMode = 0;
    }
}

/* An object dragged into the panel from the 3D view becomes the cursor object and is put
   down where the button is released. */
void far DoInventoryDrag(struct Object far *obj)
{
    int16 x;
    int16 y;
    int16 buttons;
    register int hit;

    CursorObjPtr = obj;
    force_mouse_cursor(OBJ_ITEM(CursorObjPtr));
    mouse_getbut(&buttons);
    if (buttons != 0) {
        mouse_release(1);
        if (CursorObjPtr != 0) {
            mouse_getxy(&x, &y);
            hit = FindInventoryHit(x, y);
            if (hit > 0) {
                GameInputMode = 1;
                if (RightPanel != 0 && hit != DISP_WORLD)
                    return;
                if (hit < DISP_UP) {
                    RearrangeInventory(DisplayToSlot[hit]);
                    if (CursorObjPtr == 0) {
                        GameInputMode = 0;
                        unforce_mouse_cursor(3);
                    }
                } else
                    DoSpecialActions(hit);
            }
        }
    }
}

/* Loads paperdoll picture img for display slot n from ARMOR_F or ARMOR_M by the player's
   sex, by patching the name's last letter in place. */
/* name: IDA LoadMaleOrFemaleArmourArt_ovr125_68B; FM Towns load_inv_pic_ sits in the same
   place and patches the same string the same way. */
char far load_inv_pic(int n, int img)
{
    register char *name = WRITABLE_STR("armor_f");

    if (player->female != 1)
        name[6] = 'm';
    else
        name[6] = 'f';
    reload_gr_vpic(n + PIC_BODY, name, img);
    return 1;
}

/* Draws the paperdoll: the body, then the armour in the five armour slots, each picture
   chosen by the armour's type (minor index & 0x1F) and condition (quality / 16; types
   above 14 always use condition 3), loaded only when it changed; then the hands and
   rings, and the weight left. */
void far DisplayInvSpecial(void)
{
    struct Object far *obj;
    int type;
    int q;
    register int slot;

    if (RightPanel == 0) {
        mouse_hide();
        if (inv_refresh) {
            grSoftPageFlip();
            set_the_color(0x106);
            rectangle(0xF0, 0xBD, 0x13B, 0x52);
        }
        pic_to_screen(PIC_BODY, InvDisplay[DISP_BODY].x, InvDisplay[DISP_BODY].y, InvDisplay[DISP_BODY].h, InvDisplay[DISP_BODY].w);
        Transparency = 1;
        for (slot = 1; slot <= 5; slot++) {
            if (Inventory[DisplayToSlot[slot]].f.index != 0) {
                obj = Obj_PtrTMem(&Inventory[DisplayToSlot[slot]]);
                type = OBJ_INMAJOR(obj) & 0x1F;
                if (type > 14)
                    q = 3;
                else
                    q = obj->qn.f.quality >> 4;
                if (type + 1 != invArmorObj[slot] || q + 1 != invArmorQ[slot]) {
                    invArmorObj[slot] = type + 1;
                    invArmorQ[slot] = q + 1;
                    type += q * 15;
                    load_inv_pic(slot, type);
                }
                pic_to_screen(slot + PIC_BODY, InvDisplay[slot].x, InvDisplay[slot].y,
                              InvDisplay[slot].h, InvDisplay[slot].w);
            }
        }
        Transparency = 0;
        save_rect(SaveHandles[11], InvDisplay[11].x, InvDisplay[11].y, InvDisplay[11].w - 5,
                  InvDisplay[11].h);
        save_rect(SaveHandles[10], InvDisplay[10].x + 5, InvDisplay[10].y, InvDisplay[10].w - 5,
                  InvDisplay[10].h);
        if (Inventory[INV_RING].f.index != 0 || Inventory[INV_RING + 1].f.index != 0)
            displayInventoryArray(10, 11);
        if (inv_refresh) {
            grSoftPageFlip();
            set_the_color(0x106);
            rectangle(0xF0, 0xBD, 0x13B, 0x52);
        }
        if (displayEnc(1))
            grfx_load_font("font5x6p.sys");
        mouse_show();
    }
}

/* Redraws display position slot: the paperdoll for the body and armour (below 6), a slot's
   object (to 20), or a scroll arrow. */
void far DisplayInvObject(int slot)
{
    int pic;

    pic = -1;
    if (RightPanel == 0) {
        if (slot < 6)
            DisplayInvSpecial();
        else if (slot < DISP_UP)
            displayInventoryArray(slot, slot);
        else {
            restore_rect(SaveHandles[slot]);
            if (slot == DISP_UP) {
                if (InvUpArrow)
                    pic = ICON_BUTTONS + 0x1B;
            } else if (InvDownArrow)
                pic = ICON_BUTTONS + 0x1C;
            if (pic >= 0) {
                Transparency = 1;
                pic_to_screen(pic, InvDisplay[slot].x, InvDisplay[slot].y, InvDisplay[slot].h,
                              InvDisplay[slot].w);
                Transparency = 0;
            }
        }
    }
}

/* Takes the object in slot onto the mouse cursor. keep: the slot holds a split stack, and
   the remainder (the next object in the list) stays in the slot. */
void far SetCursorObj(int slot, char keep)
{
    char had;
    int link;

    had = CursorObjPtr != 0;
    if (keep)
        link = Obj_MemTPtr(Obj_PtrTMem(&AskInventory(slot)->qn.link));
    CursorObjPtr = takeFromSlot(-1, -1, -1, slot, 0);
    if (CursorObjPtr != 0) {
        if (keep) {
            Inventory[slot].f.index = link;
            FixPlayerEquips();
        }
        mouse_hide();
        if (had)
            unforce_mouse_cursor(0);
        force_mouse_cursor(OBJ_ITEM(CursorObjPtr));
        mouse_show();
        FixPlayerEquips();
    }
}

/* Asks 'Move how many? ' for a stack obj of n objects; Escape gives 0. Returns the object
   to move: obj itself, or obj cut down to the number asked with the rest split off into a
   new object, or 0. */
struct Object far * far AskHowMany(struct Object far *obj)
{
    int orig;
    struct Object far *split;
    char *msg;
    char initial[4];
    char result[4];
    register int n;
    register int key;

    orig = obj->ol.f.link;
    split = 0;
    n = 1;
    msg = "Move how many? ";
    initial[0] = '1';
    initial[1] = 0;
    key = wdialog(msg, initial, result, 0, 3);
    if (key != 27 && key != 3) {
        if (key != 0 && key < 4) {
            if (key == 1)
                n = 1;
            else if (key == 2)
                n = orig;
            else if (key == 27)
                n = 0;
            wd_replace(n);
        } else {
            n = atoi(result);
            if (n > orig)
                n = orig;
        }
        scroll_print("\n");
        if (n != 0) {
            if (obj->ol.f.link != n) {
                split = Obj_Alloc(0);
                *(struct StaticObj far *)split = *(struct StaticObj far *)obj;
                split->ol.f.link -= n;
                obj->ol.f.link = n;
            } else
                split = obj;
        }
    }
    mouse_dragged(1);
    return split;
}

/* Whether obj may go into slot: 1 yes, 0 no, -1 it was eaten (food dropped on slot 0, the
   head, inferred: UseFood took it). The armour slots take armour of the matching wearable
   type (cls->category: 8 head, 1 torso, 4 gloves, 3 legs, 5 boots; Guide, "Armour and Wearables
   Table"), the ring slots type 9. The weapon hand refuses a stack of weapons. A light
   source (MAJOR_MISC minor 1 class 4..7) is tested as its unlit form and stays lit only in
   a shoulder or hand slot (ValidLightSlots); elsewhere it is put out. Into a container (a
   bag slot or the open bag) the weight must fit the capacity of the container and every
   open bag around it ('The <bag> is too full.'), and a container with a mask takes only
   that item or kind (CONT_* in object.h): the rune bag runestones, the quiver sling
   stones, bolts and arrows, the map case scrolls and the map, the bowl food and drink and
   a few edible items; any other mask refuses. Otherwise the item's ComObjData pickup
   flag decides. */
int far ItemFitsSlot(struct Object far *obj, int slot)
{
    struct ComObj *com;
    struct Object far *cont;
    struct Armour *cls;
    int major;
    int minor;
    int sub;
    int16 weight;
    int id;
    register int cap;
    register int i;

    cont = 0;
    id = OBJ_ITEM(obj);
    ActiveObj = obj;
    com = &ComObjData[id];
    major = OBJ_MAJOR(ActiveObj);
    minor = OBJ_MINOR(ActiveObj);
    sub = OBJ_INCLASS(ActiveObj);
    if (slot == INV_BAG) {
        int j;

        if (OpenBag == 0)
            return 0;
        if (OpenBag->prev == 0) {
            for (j = INV_PACK; j <= INV_PACK_LAST; j++)
                if (Inventory[j].f.index == 0)
                    break;
            if (j > INV_PACK_LAST)
                game_sprint(0x102);  /* 'There is no place to put that.' */
            return j <= INV_PACK_LAST;
        }
        cont = Obj_PtrTMem(&OpenBag->prev->obj);
    } else if (slot > INV_BAG) {
        struct Object far *o;

        o = Obj_PtrTMem(&Inventory[slot]);
        if (o != 0 && OBJ_CLASS(o) == CLASS_CONTAINER)
            cont = o;
        else
            cont = Obj_PtrTMem(&Inventory[INV_BAG]);
    } else
        cont = Obj_PtrTMem(&Inventory[slot]);
    if (slot < INV_SHOULDER) {
        if (major != MAJOR_HACK) {
            if (slot == INV_HEAD && UseFood(ThePlayer, obj, 0) > 0)
                return -1;
            return 0;
        }
        if (OBJ_MINOR(obj) < 2)
            return 0;
        cls = (struct Armour *)get_class_data();
        switch (slot) {
        case INV_TORSO:
            return cls->category == ARMOUR_BODY;
        case INV_LEGS:
            return cls->category == ARMOUR_LEGGINGS;
        case INV_GLOVES:
            return cls->category == ARMOUR_GLOVES;
        case INV_HEAD:
            return cls->category == ARMOUR_HAT;
        case INV_BOOTS:
            return cls->category == ARMOUR_BOOTS;
        }
        return 0;
    }
    if (slot == INV_RING || slot == INV_RING + 1) {
        if (major != MAJOR_HACK || OBJ_MINOR(obj) < MINOR_ARMOR)
            return 0;
        cls = (struct Armour *)get_class_data();
        return cls->category == ARMOUR_RING;
    }
    if (INV_WEAPON_HAND - player->lefty == slot && major == MAJOR_HACK && minor == 0) {
        if (cont != 0 && OBJ_ITEM(cont) == id
            || OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL) && obj->ol.f.link > 1)
            return 0;
    } else if (major == MAJOR_MISC && minor == 1 && sub >= 4 && sub < 8) {
        for (i = 0; i < 4; i++)
            if (ValidLightSlots[i] == slot)
                return 1;
        SET_INCLASS(obj, sub - 4);
        if (!ItemFitsSlot(obj, slot)) {
            SET_INCLASS(obj, sub);
            return 0;
        }
        return 1;
    }
    if (cont != 0 && OBJ_CLASS(cont) == CLASS_CONTAINER) {
        int ok;

        ok = 1;
        weight = ItemWeight(obj);
        {
            struct Bag far *bag;
            char name[26];

            if (slot > INV_BAG) {
                for (bag = OpenBag; bag != 0; bag = bag->prev) {
                    cap = Containers[Obj_PtrTMem(&bag->obj)->id & ID_INCLASS].capacity;
                    /* 16 bits, as Turbo C's: past 32767 the sum goes negative and fits */
                    ok &= cap == 0 || (int16)(bag->weight + weight) <= cap;
                }
            }
            BagWeight(&cont->ol.link, &weight);
            cap = Containers[OBJ_INCLASS(cont)].capacity;
            ok &= cap == 0 || weight <= cap;
            if (!ok) {
                if (!get_name(name, cont, 0, 0))
                    strcpy(name, "UNNAMED");
                scroll_print("The ");
                scroll_print(name);
                scroll_print(" is too full.\n");
                return 0;
            }
        }
        cap = Containers[OBJ_INCLASS(cont)].mask;
        if (cap >= 0) {
            char res;

            if (cap < CONT_RUNES) {
                if (id != cap)
                    game_sprint(0xF8);  /* 'That item does not fit.' */
                return id == cap;
            }
            switch (cap) {
            case CONT_RUNES:
                if (!(res = major == MAJOR_STUFF && (minor == 3 || minor == 2 && sub > 7))) {
                    game_sprint(0xF7);  /* 'You can only put runes in the rune bag.' */
                    return 0;
                }
                break;
            case CONT_MISSILES:
                res = major == MAJOR_HACK && minor == 1 && sub < 3;
                break;
            case CONT_SCROLLS:
                res = major == MAJOR_SPEC && minor == 3 && sub >= 8;
                break;
            case CONT_FOOD:
                res = major == MAJOR_MISC && minor == 3 || id == ITEM_PLANT_CE || id == ITEM_PLANT_CF || id == ITEM_CANDLE
                    || id == ITEM_LEECHES || id == ITEM_ROTWORM_STEW || id == ITEM_DEAD_ROTWORM;
                break;
            default:
                res = 0;
            }
            if (!res)
                game_sprint(0xF8);  /* 'That item does not fit.' */
            return res;
        }
    }
    return com->pickup;
}

/* Puts the cursor object down in slot: into it if empty, else onto what is there. */
void far RearrangeInventory(int slot)
{
    if (Inventory[slot].f.index == 0) {
        if (AddToEmptySlot(CursorObjPtr, slot))
            CursorObjPtr = 0;
    } else if (AddToOccupiedSlot(CursorObjPtr, slot))
        CursorObjPtr = 0;
}

/* Puts obj in the empty slot (not the open bag's own slot 19) and redraws it. Returns 1
   when it went in. */
char far AddToEmptySlot(struct Object far *obj, int slot)
{
    char ok;

    ok = 0;
    if (slot == INV_BAG)
        return 0;
    if (AddToInventory(obj, slot)) {
        if (slot > INV_PACK_LAST) {
            FixOpenBag();
            DisplayOpenBag();
        } else
            DisplayInvObject(SlotToDisplay[slot]);
        ok = 1;
    }
    return ok;
}

/* Whether obj can be merged into the stack onto: the same item, both plain stacks (not
   containers, not special links), a stackable item (ComObjData stack not 1 or 3), keys
   with the same owner (the lock they open), under 999 together, and, except for sling
   stones, bolts and arrows, the same quality band (quality / 16) with neither ruined
   unless both are. Storage crystals never merge. */
char far AddTogether(struct Object far *obj, struct Object far *onto)
{
    register int q1;
    register int q2;

    /* match: every return 0 here is one jump to the final return 0: the compiler shares
       them only when that last statement is itself reachable. */
    if (OBJ_ITEM(obj) != OBJ_ITEM(onto))
        return 0;
    if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0
        || !OBJ_ISQUANT(onto) && onto->ol.f.link > 0
        || obj->ol.f.link & LINK_SPECIAL || onto->ol.f.link & LINK_SPECIAL
        || ComObjData[OBJ_ITEM(obj)].stack == 1 || ComObjData[OBJ_ITEM(obj)].stack == 3)
        return 0;
    if (OBJ_CLASS(obj) == CLASS_KEY && obj->ol.f.owner != onto->ol.f.owner)
        return 0;
    if (obj->ol.f.link + onto->ol.f.link < 999) {
        if (OBJ_ITEM(obj) >= ITEM_SLING_STONE && OBJ_ITEM(obj) <= ITEM_ARROW_12)     /* UW1's missiles */
            return 1;
        q1 = obj->qn.f.quality;
        q2 = onto->qn.f.quality;
        return q1 >> 4 == q2 >> 4 && (q1 != 0 && q2 != 0 || q1 == q2);
    }
    return 0;
}

/* Puts obj down on the occupied slot: into a bag that is there, merged with a matching
   stack (the merged quality is the average of the two), combined if the two combine
   (COMBINE.C: the result goes to the slot or the cursor), else swapped with what is there
   (the old object comes onto the cursor). Returns 1 when obj was used up. */
char far AddToOccupiedSlot(struct Object far *obj, register int slot)
{
    struct Object far *target;
    char ok;
    int qty;
    int combo;
    register int weight;

    ok = 0;
    target = Obj_PtrTMem(&Inventory[slot]);
    if (OBJ_MAJOR(target) == MAJOR_MISC && OBJ_MINOR(target) == MINOR_CONTAINER) {
        char r;

        r = PutObjectInBag(obj, slot);
        FixPlayerEquips();
        return r;
    }
    if (AddTogether(obj, target)) {
        struct Bag far *bag;

        if (ItemFitsSlot(obj, slot) != 1)
            return 0;
        if (OBJ_ISQUANT(obj))
            qty = obj->ol.f.link;
        else
            qty = 1;
        if (!OBJ_ISQUANT(target)) {
            SET_ISQUANT(target, 1);
            target->ol.f.link = 1;
        }
        weight = ComObjData[OBJ_ITEM(obj)].mass * qty;
        if (slot > INV_BAG)
            for (bag = OpenBag; bag != 0; bag = bag->prev)
                bag->weight += weight;
        PlayerDat.rec.weight += weight;
        FixPlayerEquips();
        target->ol.f.link = target->ol.f.link + qty;
        target->qn.f.quality = (target->qn.f.quality + obj->qn.f.quality) >> 1;
        Obj_Free(obj);
        ok = 1;
    } else if ((combo = ObjsBeCombinable(obj, target)) >= 0) {
        struct Object far *made;
        char used;

        if ((made = CombineObjs(combo)) == 0)
            return 0;
        if ((used = RemoveAfterCombine(obj, combo)) == 1) {
            Obj_Punt(0, obj, 1);
            CursorObjPtr = made;
            GameInputMode = 1;
            unforce_mouse_cursor(3);
            force_mouse_cursor(OBJ_ITEM(CursorObjPtr));
        }
        if (RemoveAfterCombine(target, combo)) {
            if (!used)
                AddToEmptySlot(made, slot);
            InvRemoveObject(target);
            Obj_Punt(0, target, 1);
        }
        ok = 0;
    } else if (slot <= INV_PACK_LAST) {
        struct Object far *old;

        old = removeFromSlot(-1, -1, -1, slot, 0);
        if (!AddToEmptySlot(obj, slot))
            AddToEmptySlot(old, slot);
        else
            CursorObjPtr = old;
        if (CursorObjPtr != 0) {
            unforce_mouse_cursor(0);
            force_mouse_cursor(OBJ_ITEM(CursorObjPtr));
        }
    } else
        SwapItemsInBag(obj, slot);
    DisplayInvObject(SlotToDisplay[slot]);
    return ok;
}

/* Redraws the backpack area, or the open bag's background, and every slot from position
   6 on. */
void far DisplayInventory(void)
{
    if (RightPanel == 0) {
        shown_capacity = -1;
        if (OpenBag != 0)
            pic_to_screen(ICON_INV + 6, 0xEC, 0x77, 0x29, 0x54);
        else
            restore_rect(1);            /* match: FM Towns restores SaveHandles[1]; DOS pushes 1 */
        displayInventoryArray(6, DISP_DOWN);
    }
}

/* Redraws display positions from..to: each object's picture, the stack counts over them
   in the small font, and the weight left. */
void far displayInventoryArray(int from, int to)
{
    int id;
    struct Object far *obj;
    char buf[6];
    char font_set;
    char any_qty;
    int slot;
    int16 qty[23];
    register int i;
    register int n;

    font_set = 0;
    any_qty = 0;
    mouse_hide();
    Transparency = 1;
    for (i = from; i <= to; i++) {
        restore_rect(SaveHandles[i]);
        qty[i] = 1;
        if (i <= DISP_BAG) {
            slot = DisplayToSlot[i];
            if (Inventory[slot].f.index != 0) {
                obj = Obj_PtrTMem(&Inventory[slot]);
                id = OBJ_ITEM(obj);
                pic_to_screen(id, InvDisplay[i].x, InvDisplay[i].y, InvDisplay[i].h, InvDisplay[i].w);
                if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)) {
                    n = obj->ol.f.link;
                    if (n > 1) {
                        qty[i] = n;
                        any_qty = 1;
                    }
                }
            }
        } else
            DisplayInvObject(i);
    }
    Transparency = 0;
    if (any_qty) {
        grfx_load_font("font4x5p.sys");
        *foreground_color = 0x60;
        font_set = 1;
        for (i = from; i <= to; i++)
            if ((n = qty[i]) > 1)
                string_to_screen(itoa(n, buf, 10), InvDisplay[i].x + 3, InvDisplay[i].y - 1);
    }
    if (font_set)
        grfx_load_font("font5x6p.sys");
    displayEnc(0);
    mouse_show();
}

/* Draws the weight the player can still carry (max_weight - weight, shown / 10) if it
   changed; returns 1 when it drew and show was set. */
char far displayEnc(char show)
{
    char drawn;
    char buf[5];
    register int left;

    drawn = 0;
    /* 16 bits, as Turbo C's: a carried weight above the maximum, or one wrapped below 0 (a
       drop from a weight of 0), wraps here, where the host's int went negative */
    left = (int16)(PlayerDat.rec.max_weight - PlayerDat.rec.weight);
    if (shown_capacity != left) {
        restore_rect(SaveHandles[0]);
        shown_capacity = left;
        if (show)
            drawn = 1;
        *foreground_color = 0xE0;
        itoa(left / 10, buf, 10);
        string_to_screen(buf, 8 - string_width(buf) / 2 + 0x129, 0x8C);
    }
    return drawn;
}

/* The display position under screen point x, y: 0..22 from InvDisplay, 0x17 for the
   3D view (PLeft, PBot, PWid, PHgt), 0x18 for the player's barter area in barter mode, or
   -1. */
int far FindInventoryHit(int x, int y)
{
    register struct InvRect *r;
    register int i;

    if (inplist->mode != 4) {
        if (x > PLeft && PLeft + PWid > x && y > PBot && PBot + PHgt > y)
            return DISP_WORLD;
    } else if (x > 0x8B && x < 0xC1 && y > 0x98 && y < 0xBE)
        return DISP_BARTER;
    for (i = 0; i < NUM_DISPLAY; i++) {
        r = &InvDisplay[i];
        if (r->left <= x && r->right >= x && r->bottom <= y && r->top >= y)
            return i;
    }
    return -1;
}
