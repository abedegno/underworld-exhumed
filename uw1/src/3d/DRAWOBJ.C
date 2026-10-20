/* target: seg032_2DCA */
/* opts: -mm -1 -G -O -Y -d */
/* DRAWOBJ.C: drawing one object into the 3D view's render database.

   GAMESORT.C's do_objsort calls do_obj for each object of a tile, farthest first,
   with objxloc, objyloc and objzloc set to its view position and locsqmod to its
   distance shade. do_obj picks the kind of drawing from the object type's render
   field in COMOBJ.DAT (ComObjData): 0 a sprite (do_uwobj, opcode 0x3A), 1 a critter
   (do_uwcrit, 0x5A, with its view and animation frame), 2 a 3D model
   (do_rect) or a door (do_door), 3 a texture-mapped object (a 3D model with a
   texture). Opcode names are from the FM Towns opcode table (see GRIDDB.C; view3d.h's OP_*).

   do_rect emits one call of a 3D model: the model's colours and texture in the model
   variables (Clk(n), opcode 0x02), the origin (0x18 do_org, three longs), and a call
   of the model's code at label 0x60 + model (0x50 do_ihcall turned by a heading, or
   0x12 do_sfcal unturned), then the origin set back to 0. The models' bytecode is
   the 3D object data in the renderer's far data (docs/LAYOUT.md); grdb_blank reads
   their label positions from the table before the database buffer.

   In a pick frame (PickUp) each object first gets its pick colour (0xAE
   do_setbmcol), and color_to_obj and color_to_map record the object and its tile for
   UI/INTERACT.C's pick_3d.

   Data owned: rect_cols and rect_sub (the models' colours and the model for each 3D
   object type), dirtab, and ActDoors (used elsewhere; see below).

   name: descriptive (UW2Decomp's map/filenames.tsv: "drawing an object into the render database
   (do_obj, do_rect, do_door)"). The whole of UW1's DOS resident segment seg032_2DCA,
   in original order. UW1 has no symbol-bearing build: function and global names are
   UW2's (FM Towns), the routines being the same.

   UW1's differences from UW2's seg033_2FBE: 0xBF pick colours (UW2 0xAB) and wall pick
   colours 0xC0 + texture; a critter's frame is worked out from the object (byte 0x15
   and its view) rather than read from the art page; a texture's colour is the first
   pixel of its bitmap (GRSPIC.C's seg009_38C) where UW2 has TxmCol; rect_cols has four
   bytes a model, the texture count and first texture packed in one; no model 0x1E
   (UW2's blackrock gem); door textures from first_tmobj + 0x30; the detail setting
   in UW1's player record (player.h). */

#include <dos.h>
#include "conv.h"
#include "gfx.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "view3d.h"

/* Declared in each file that uses it, its own way (no header). */
unsigned char far IsMobElem(struct Object far *obj);

/* UW1: a word for each texture of the level (CONVERSE.C and LOOK.C declare it too). */
extern int16 w64_types[];

/* match: tmapson, lighton, curautocode and the other scalars at DS:546-550, and the tables
   after them up to DS:609, belong to GRIDDB.C, the file before this one in the data
   segment, so they are extern here. This file's _DATA is DS:60A..6E5. */

/* match: this file's _BSS, DS:3148..314D, though nothing here uses it: the level's door
   textures, which LOADGR.C uses. It lies between GRIDDB.C's _BSS (keys rising to
   tCacheOK's 1004) and the next file's (from holdmid's 112), and its key (209) fits
   neither run, as in UW2. */
unsigned char ActDoors[6];

/* Per model, as do_rect reads it: the flags (bits 0-2 the number of colours that
   follow, 0x08 an extra 0x4C command below the model, 0x10 a textured face, 0x20 the
   caller's texture colour, 0x40 turned by the object's own heading, 0x80 colours
   animated by the clock), then the colours; for a textured face byte 3 is the first
   texture (bits 0-4) and the number of textures less one (bits 5-7). */
unsigned char rect_cols[30][4] = {
    { 0x01, 0xEC, 0x00, 0x00 },
    { 0x21, 0xEB, 0x00, 0x00 },
    { 0x11, 0xEC, 0x00, 0x3E },
    { 0x01, 0xE4, 0x00, 0x00 },
    { 0x02, 0xB6, 0xB0, 0x00 },
    { 0x02, 0x64, 0x6C, 0x00 },
    { 0x02, 0x64, 0x6C, 0x00 },
    { 0x02, 0x64, 0x6C, 0x00 },
    { 0x42, 0xE8, 0xB8, 0x00 },
    { 0x01, 0xE4, 0x00, 0x00 },
    { 0x19, 0xE4, 0x00, 0x60 },
    { 0x03, 0xA3, 0xA4, 0xA6 },
    { 0x01, 0x68, 0x00, 0x00 },
    { 0x01, 0x68, 0x00, 0x00 },
    { 0x11, 0xEC, 0x00, 0x00 },
    { 0x21, 0xEC, 0x00, 0x00 },
    { 0x51, 0xB0, 0x00, 0xE4 },
    { 0x51, 0xB0, 0x00, 0xEC },
    { 0x11, 0xB0, 0x00, 0xF4 },
    { 0x11, 0x6A, 0x00, 0x3C },
    { 0x51, 0xB0, 0x00, 0x00 },
    { 0x11, 0xB0, 0x00, 0x00 },
    { 0x21, 0xB0, 0x00, 0x00 },
    { 0x83, 0x00, 0x02, 0x04 },
    { 0x02, 0xE4, 0x68, 0x00 },
    { 0x02, 0xE6, 0x68, 0x00 },
    { 0x01, 0xE4, 0x00, 0x00 },
    { 0x02, 0xE4, 0x6A, 0x00 },
    { 0x03, 0xE6, 0x6A, 0x71 },
    { 0x03, 0xE2, 0x62, 0xC4 }
};
/* The model drawn for each 3D object type, indexed by item & 0x3F less 0x10 for
   render type 2 objects (item & 0x3F below 0x10 is a door), -1 for none. */
int16 rect_sub[32] = {
    0x03, 0x08, 0x08, 0x07, 0x07, 0x06, 0x05, 0x0B,
    0x18, 0x09, 0x17, 0x1B, 0x1C, 0x19, 0x1A, 0x04,
    0x0A, 0x10, 0x11, -1, 0x02, 0x13, 0x12, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};
/* An NPC's heading relative to the camera, in 32nds, as one of eight animation views. */
unsigned char dirtab[32] = {
    0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4,
    4, 4, 4, 5, 5, 5, 6, 6, 6, 6, 6, 7, 7, 7, 0, 0
};
/* Colours for model 2 when its flags are below its texture count, indexed by the
   object's flags. Static: FM Towns has no name for it and keeps it after dirtab too
   (as dirtab+0x20), so the name is ours. */
static int16 rect_flagcol[2] = { 0xE4, 0x66 };

/* Draw one object. The player's own object and invisible objects are skipped. A
   mobile object that is not a critter takes its fine position within the tile from
   goal_word and attitude_word, turned for the quadrant. An animation object
   (MAJOR_ANIMOBJ) draws as its owner field's item. Items 0xE8 to 0xFF (other than
   0xE0 to 0xE7) draw as the runestone sprite. A critter's frame is byte 0x15's low
   six bits: from 0x20 on an animation with eight views (dir, from its heading
   against the camera's), below that a single frame, except that a frame other than
   0xC is replaced by frame 0x20 + dir unless dir is 3 to 5. */
void far do_obj(struct Object far *o)
{
    int type;
    int item;
    int model;
    int dir;
    int x;
    int y;
    char tm;
    register int frame;

    if (o == ThePlayer)
        return;
#ifndef __TURBOC__
    if (port_shared_avatar(o)) {
        int dx=(OBJ_HOMEX(o)-OBJ_HOMEX(ThePlayer))*8+OBJ_FINEX(o)-OBJ_FINEX(ThePlayer);
        int dy=(OBJ_HOMEY(o)-OBJ_HOMEY(ThePlayer))*8+OBJ_FINEY(o)-OBJ_FINEY(ThePlayer);
        /* Companions can pass through one another. The original sprite
           projector assumes collisions keep a creature outside the camera. */
        if (dx*dx+dy*dy<16 && abs((int)OBJ_Z(o)-(int)OBJ_Z(ThePlayer))<24)
            return;
    }
#endif
    if (OBJ_INVIS(o) == 1)
        return;
    if (PickUp) {
        color_to_map[PickUp - 1] = (int)(tmptr - mlowptr) + mptrmod;
        color_to_obj[PickUp - 1] = Obj_MemTPtr(o);
        *dbptr++ = OP_SETBMCOL;
        *dbptr++ = PickUp;
        if (++PickUp >= PICK_WALL)
            PickUp = 1;
    }
    if (IsMobElem(o) && OBJ_MAJOR(o) != MAJOR_CREATURE) {
        register int fx;
        register int fy;

        fx = o->goal_word & 0xFF;
        fy = o->attitude_word & 0xFF;
        switch (quad) {
        case 0:
            y = fy;
            x = fx;
            break;
        case 1:
            y = fx;
            x = 0xFF - fy;
            break;
        case 2:
            x = 0xFF - fx;
            y = 0xFF - fy;
            break;
        case 3:
            y = 0xFF - fx;
            x = fy;
            break;
        }
        objxloc = (objxloc & 0xFF00) + x;
        objzloc = (objzloc & 0xFF00) + y;
    }
    type = ComObjData[OBJ_ITEM(o)].render;
    if (OBJ_MAJOR(o) == MAJOR_ANIMOBJ) {
        AnimObjInPipe = 1;
        item = o->ol.f.owner;
        if (type == 0) {
            if (item > 0)
                item += FIRST_ANIMOBJ;
            else
                item = OBJ_ITEM(o);
        }
    }
    else
        item = OBJ_ITEM(o);
#ifndef __TURBOC__
    item = port_shared_art(o, item);
    if (port_shared_avatar(o)) type = 1;
#endif
    switch (type) {
    case 2:
        item &= 0x3F;
        if (item >> 4) {
            item -= 0x10;
            if ((model = rect_sub[item]) < 0)
                return;
            if (item >= 0x20)
                return;
            do_rect(model, o, -1, -1);
            return;
        }
        do_door(item, o);
        return;
    case 1:
        *dbptr++ = OP_DEFRES;
        *dbptr++ = objxloc;
        *dbptr++ = objzloc;
        *dbptr++ = objyloc;
        *dbptr++ = 0x7F8;
        frame = o->b15 & 0x3F;
        dir = dirtab[(uint16)((OBJ_HEADING(o) << 2) + 0x20
                      - ((uint16)(cPlayer->heading + headmod[quad]) >> 11)) % 0x20];
        if (frame >= 0x20)
            frame = (frame - 0x20 << 3) + dir + 0x20;
        else if (frame != 0xC && (dir + 5 & 7) >= 3)
            frame = dir + 0x20;
        RENDER_TAG(o);
        *dbptr++ = OP_UWCRIT;
        *dbptr++ = item & 0x3F;
        *dbptr++ = locsqmod * lighton;
        *dbptr++ = frame;
        *dbptr++ = OBJ_FRAME(o);
        *dbptr++ = 0x7F8;
        break;
    case 0:
        if ((item & 0x1E0) == 0xE0 && (item & 0x18))
            item = ITEM_RUNESTONE;
        *dbptr++ = OP_DEFRES;
        *dbptr++ = objxloc;
        *dbptr++ = objzloc;
        *dbptr++ = objyloc;
        *dbptr++ = 0x7F8;
        RENDER_TAG(o);
        *dbptr++ = OP_UWOBJ;
        *dbptr++ = item;
        *dbptr++ = locsqmod * lighton;
        *dbptr++ = 0x7F8;
        break;
    case 3:
        if (OBJ_MINOR(o) == 3) {
            if ((tmapson | PickUp) == 0) {
                *dbptr++ = OP_MOVEC;
                *dbptr++ = Clk(8);
                *dbptr++ = 0;
            }
            do_rect(0x14, o, -1, first_tmobj + (item & 0xF));
            if (tmapson | PickUp)
                return;
            *dbptr++ = OP_MOVEC;
            *dbptr++ = Clk(8);
            *dbptr++ = 1;
        }
        else {
            tCacheOK = 0xE0;
            txtwal(0, sqmod, 4, o->ol.f.owner);
            *dbptr++ = OP_SET_TMCTXT;
            *dbptr++ = cTmBm;
            tm = w64_types[o->ol.f.owner] & 0xFF;
            if (tm == 3 || tm == 4) {
                curautocode = 3;
                tm = 1;
            }
            else
                tm = tm == 8 || tm == 0xB;
            tm = tm && (tmapson | PickUp) == 0;
            if (tm) {
                *dbptr++ = OP_MOVEC;
                *dbptr++ = Clk(8);
                *dbptr++ = 0;
            }
            do_rect(0x16, o, -1, o->ol.f.owner);
            if (tm) {
                *dbptr++ = OP_MOVEC;
                *dbptr++ = Clk(8);
                *dbptr++ = 1;
            }
        }
        break;
    }
}

/* Emit a 3D model for object o. heading -1 turns it by the object's own heading
   (in eighths of a turn), otherwise by heading in sixteenths; tex is the texture
   for models that take one (-1: from rect_cols and the object's flags). Flag 0x40
   models get variable 5 set: 0 for a static object, and for a mobile one its pitch,
   with the heading taken from its fine heading as well. Models 0x10 and 0x11 with no
   texture mapping,
   and model 2 at detail level 1, get variable 8 set around the call (hilite;
   variable 8 is also process_grid's texture-mapping flag). */
void far do_rect(unsigned char model, struct Object far *o, char heading, int tex)
{
    int head;
    int val;
    int hilite;
    register int i;
    register int flags;

    hilite = -1;
    flags = rect_cols[model][0];
    *dbptr++ = OP_MOVEC;
    *dbptr++ = Clk(0xA);
    *dbptr++ = locsqmod * lighton;
    if (flags & 0x20) {
        unsigned char far *pix;

        pix = MK_FP(seg009_38C(tex + 0x3A), 0);
        val = *pix;
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(0);
        *dbptr++ = val;
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(0xA);
        *dbptr++ = sqmod * lighton;
    }
    else if (flags & 0x80) {
        AnimObjInPipe = 1;
        val = (GAME_TIME() & 0x1FF) >> 6;
        if (val & 4)
            val = 3 - (val & 3);
        for (i = 0; i < (flags & 7); i++) {
            *dbptr++ = OP_MOVEC;
            *dbptr++ = Clk(i);
            *dbptr++ = (o->ol.f.link & 0x1FF) + val + rect_cols[model][i + 1];
        }
    }
    else
        for (i = 0; i < (flags & 7); i++) {
            *dbptr++ = OP_MOVEC;
            *dbptr++ = Clk(i);
            *dbptr++ = rect_cols[model][i + 1];
        }
    if (flags & 0x10) {
        unsigned char first;

        first = rect_cols[model][3];
        if (tex < 0) {
            if (model == 2) {
                if (OBJ_FLAGS(o) >= (first >> 5) + 1) {
                    tex = OBJ_FLAGS(o) - ((first >> 5) + 1);
                    txtflr(0, sqmod, tex);
                    *dbptr++ = OP_SET_TMCTXT;
                    *dbptr++ = cTmBm;
                    tex = -1;
                }
                else {
                    curautocode = 2;
                    tex = (int)((first & 0x1F) + first_tmobj + 0x10) + OBJ_FLAGS(o) % ((first >> 5) + 1);
                    *dbptr++ = OP_MOVEC;
                    *dbptr++ = Clk(0xB);
                    *dbptr++ = rect_flagcol[OBJ_FLAGS(o)];
                    *dbptr++ = OP_SET_TMCTXT;
                    *dbptr++ = 6;
                }
            }
            else if ((first >> 5) + 1)
                tex = (int)((first & 0x1F) + first_tmobj + 0x10) + OBJ_FLAGS(o) % ((first >> 5) + 1);
        }
        if (tex >= 0) {
            *dbptr++ = OP_SETTMOBJ;
            *dbptr++ = tex;
            *dbptr++ = locsqmod * lighton;
        }
        tCacheOK = 0xE0;
    }
    if (flags & 8) {
        i = 0x400 - objyloc;
        *dbptr++ = OP_DEFDELTA;
        *dbptr++ = 0;
        *dbptr++ = 0;
        *dbptr++ = i;
        *dbptr++ = 0x800;
        *dbptr++ = OP_MOVEC;
        *dbptr++ = bmhgtoff + 0x30;
        *dbptr++ = i * 2 - 1;
    }
    *dbptr++ = OP_ORG;
    *dbptr++ = objxloc & 0xFFFF;
    *dbptr++ = objxloc >> 16;
    *dbptr++ = objyloc & 0xFFFF;
    *dbptr++ = objyloc >> 16;
    *dbptr++ = objzloc & 0xFFFF;
    *dbptr++ = objzloc >> 16;
    if (heading < 0)
        head = ((OBJ_HEADING(o) + 8 - quad * 2) % 8) << 13;
    else
        head = ((heading + 0x10 - quad * 4) & 0xF) << 12;
    if ((flags & 0x40) && !(flags & 0x10)) {
        int j;

        if (o >= (struct Object far *)objdata)
            j = 0;
        else {
            j = (OBJ_PITCH(o) - 0x10) * 0x266;
            head = (((OBJ_HEADING(o) << 5) + OBJ_FINEHEAD(o) + 0x100 - quad * 64) % 0x100) << 8;
        }
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(5);
        *dbptr++ = j;
    }
    if ((model == 0x10 || model == 0x11) && (tmapson | PickUp) == 0)
        hilite = 0;
    else if (PickUp == 0 && player->detail == 1 && model == 2)
        hilite = 1;
    if (hilite != -1) {
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(8);
        *dbptr++ = hilite;
    }
    if (head) {
        *dbptr++ = OP_IHCALL;
        *dbptr++ = head;
    }
    else
        *dbptr++ = OP_SFCAL;
    Ref(model + 0x60, 1);
    if (hilite != -1) {
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(8);
        *dbptr++ = (hilite + 1) & 1;
    }
    *dbptr++ = OP_ORG;
    *dbptr++ = 0;
    *dbptr++ = 0;
    *dbptr++ = 0;
    *dbptr++ = 0;
    *dbptr++ = 0;
    *dbptr++ = 0;
}

/* Draw a door: item & 7 is the door type (6 the portcullis, 7 the secret door,
   others a textured door using door texture first_tmobj + 0x40 + type), and the
   object's flags & 7 how far it is open. A door is drawn as its frame (model 1, in
   the tile's wall texture) and its leaf (model 0xE, 0xF for the secret door, or
   0xC for the portcullis, which is raised by objyloc rather than turned). The
   order of the two (first, step) comes from the door's heading, its direction of
   opening and the eye's side of it: probably the part farther from the eye first. Sets curautocode 1 so
   the automap marks the tile as having a door. */
void far do_door(unsigned char item, struct Object far *o)
{
    int h;
    int dir;
    int oldz;
    char first;
    char step;
    char pass;
    char vis;
    register int z;

    z = -1;
    first = 0;
    step = 1;
    vis = 0;
    curautocode = 1;
    if ((item & 7) == 6) {
        dir = 0;
        z = objyloc;
        oldz = objyloc - (OBJ_FLAGS(o) & 7) * 0x30;
        vis = 1;
        h = 0x400 - oldz - 0xD0;
        first = 1;
        step = -1;
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(5);
        *dbptr++ = OBJ_FLAGS(o) & 7;
        *dbptr++ = OP_DEFDELTA;
        *dbptr++ = 0;
        *dbptr++ = 0;
        *dbptr++ = 0xD0 - (OBJ_FLAGS(o) & 7) * 0x30;
        *dbptr++ = 0x400;
    }
    else {
        if ((OBJ_FLAGS(o) & 7) || OBJ_MAJOR(o) == MAJOR_ANIMOBJ)
            objyloc -= 0xC0;
        dir = (OBJ_FLAGS(o) & 7) * (OBJ_DOORDIR(o) * 2 - 1);
        h = 0x400 - objyloc - 0xD0;
        *dbptr++ = OP_MOVEC;
        *dbptr++ = Clk(5);
        *dbptr++ = dir << 12;
    }
    /* a portcullis gets this after its own 0x4C block.
       match: the 0x400 store jumps into the shared tail here */
    *dbptr++ = OP_DEFDELTA;
    *dbptr++ = 0;
    *dbptr++ = 0;
    *dbptr++ = h;
    *dbptr++ = 0x800;
    if (vis == 0) {
        vis = (OBJ_HEADING(o) - OBJ_HEADING(ThePlayer) + 8) & 7;
        if (vis > 4)
            vis -= 8;
        vis = vis >= -1 && vis <= 2;
    }
    if ((OBJ_FLAGS(o) & 7) && z == -1) {
        int q;
        int q3;
        int ddx;
        register int k;
        register int dy;

        q = (OBJ_HEADING(o) - quad * 2) & 7;
        q3 = q & 3;
        ddx = objxloc - cPlayer->x;
        dy = objzloc - cPlayer->y;
        switch (q3) {
        case 0:
            k = dy < 0;
            break;
        case 1:
            k = -dy > ddx;
            break;
        case 2:
            k = ddx < 0;
            break;
        case 3:
            k = ddx < dy;
            break;
        }
        k = q / 4 + OBJ_DOORDIR(o) + k;
        if (!(k & 1)) {
            first = 1;
            step = -1;
        }
    }
    *dbptr++ = OP_MOVEC;
    *dbptr++ = Clk(3);
    *dbptr++ = (OBJ_FLAGS(o) & 7) + (z >= 0);
    for (pass = first; pass <= 1 && pass >= 0; pass += step) {
        if (pass == 0) {
            if (tCacheOK < 1)
                txtwal(0, locsqmod, objyloc >> 6, TILE_WALL(tmptr));
            *dbptr++ = OP_MOVEC;
            *dbptr++ = bmhgtoff + (cTmBm << 3);
            *dbptr++ = (cTmDm * cTmDm >> 8) * h - 1;
            *dbptr++ = OP_MOVEC;
            *dbptr++ = Clk(7);
            *dbptr++ = cTmDm * cTmDm - 1;
            *dbptr++ = OP_MOVEC;
            *dbptr++ = Clk(6);
            *dbptr++ = bmhgtoff + (cTmBm << 3);
            if (PickUp) {
                *dbptr++ = OP_SETBMCOL;
                *dbptr++ = TILE_WALL(tmptr) + PICK_WALL;
            }
            *dbptr++ = OP_SET_TMCTXT;
            *dbptr++ = cTmBm;
            do_rect(1, o, OBJ_HEADING(o) << 1, TILE_WALL(tmptr));
        }
        else {
            if (PickUp) {
                *dbptr++ = OP_SETBMCOL;
                *dbptr++ = PickUp - 1;
            }
            if (z >= 0) {
                objyloc = z;
                do_rect(0xC, o, OBJ_HEADING(o) << 1, 0);
                objyloc = oldz;
            }
            else {
                register int k;

                k = item & 7;
                if (k == 7) {
                    if (tCacheOK < 1)
                        txtwal(0, locsqmod, objyloc >> 6, TILE_WALL(tmptr));
                    *dbptr++ = OP_SET_TMCTXT;
                    *dbptr++ = cTmBm;
                    *dbptr++ = OP_MOVEC;
                    *dbptr++ = bmhgtoff + (cTmBm << 3);
                    *dbptr++ = cTmDm * cTmDm - 1;
                    do_rect(0xF, o, OBJ_HEADING(o) << 1, TILE_WALL(tmptr));
                }
                else
                    do_rect(0xE, o, OBJ_HEADING(o) << 1, first_tmobj + k + 0x30);
            }
        }
    }
}
