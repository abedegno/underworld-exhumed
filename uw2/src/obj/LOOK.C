/* target: ovr126 */
/* opts: -mm -1 -G -O -Y -d */
/* Looking at things: the description printed when the player looks at an object, its
   quality, enchantment, owner and charges, books, scrolls, gravestones, bones, keys and
   critters, and the short description used elsewhere: the whole of DOS overlay ovr126, in
   original order.

   What it does in the game: LookAt builds 'You see a <quality> <name> of <spell> with N full
   charges belonging to <race>.' from the object's tables: the quality word from string
   block 5 by the item's quality type and quality band, the name from block 4 (get_name), the
   enchantment from block 6, the owner's race from block 1. Then SpecialLook adds what some
   kinds of object say for themselves: a book or scroll's text, a key's description, whose
   bones these are. Critters are described by CritterLook (attitude, kind and name);
   gravestones and plaques by RectLook. GetObjDesc is the short form used by other messages
   (enchanting, mending, bartering). Callers: the look command (INTERACT.C), the inventory
   (INVPANEL.C), the rune panel (RUNES.C), Name Enchantment (SPELLS.C, lore 3).

   The lore argument says how much the player knows: 2 shows that an object is magical, 3
   (identified, from Name Enchantment or a lore check) names the enchantment and charges and
   shows curses and poisoned potions.
   Function and global names are the originals from the FM Towns symbol table.
   Name: descriptive (looking at things: LookAt, GetObjDesc). */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

/* Copy a far string, terminator included, to any address. */
#define far_strcpy(d, s) FAR_COPY(d, s, str_len(s) + 1)



/* Prints the look description of obj (see the file comment). The quality word is skipped
   for quality 0 and for a light at quality 1; indestructible items (qualclass 3) use the
   fifth word of their quality type; quality type 13 takes no article. An ownable object
   with an owner 1..30 is 'belonging to' that race (strings 0x172 + owner: ' a worm', ' a
   slug', ... ' a human'). Non-lookable decals go to RectLook. */
void far LookAt(struct Object far *obj, int lore)
{
    char far *pos;
    char far *qstr;
    char numbuf[6];
    char first;
    char name[10];
    char plural;
    char far *owner;
    /* match: 80 bytes, but an identified, enchanted, owned item's description runs to 85
       with its terminator (the jewelled sword of Unsurpassed Accuracy belonging to a human):
       in DOS the end lands on owner, plural and name, all used by then; the host's frame is
       the compiler's, so the host's array is long enough (FRAME_LEN) */
    char text[FRAME_LEN(80, 96)];
    char qual[26];
    struct ComObj *com;
    int q;

    plural = 0;
    if (obj == 0)
        return;
    com = &ComObjData[OBJ_ITEM(obj)];
    if (com->lookable) {
        strcpy(text, "You see ");
        first = 0;
        name[0] = 0;
        if (do_mods(obj, lore, name))
            first = name[0];
        qual[0] = 0;
        if (OBJ_MAJOR(obj) == MAJOR_CREATURE) {
            CritterLook(obj, text);
            str_cat(text, ".\n");
            scroll_print(text);
            return;
        }
        q = 0;
        if (obj->qn.f.quality != 0 && (obj->qn.f.quality > 1 || (obj->id & 0x1F8) != FIRST_LIGHT))
            q = com->qualclass != 3 ? (obj->qn.f.quality >> 4) + 1 : 5;
        qstr = get_string((com->qualtype * 6 + q) | STR_OBJLOOK);
        if (qstr != 0 && *qstr != 0) {
            far_strcpy(qual, qstr);
            first = *qstr;
        }
        if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL) && obj->ol.f.link > 1) {
            plural = 1;
            first = 'x';
            strcat(text, itoa(obj->ol.f.link, numbuf, 10));
            strcat(text, " ");
        } else if (first != 0 && com->qualtype != 13) {
            switch (first) {
            case 'a': case 'e': case 'i': case 'o': case 'u':
                strcat(text, "an ");
                break;
            default:
                strcat(text, "a ");
            }
        }
        if (qual[0] != 0) {
            strcat(text, qual);
            strcat(text, " ");
        }
        if (name[0] != 0)
            strcat(text, name);
        pos = text + strlen(text);
        get_name(pos, obj, first == 0, plural);
        do_of(obj, lore, text);
        if (ComObjData[OBJ_ITEM(obj)].can_own && obj->ol.f.owner > 0 && (obj->ol.f.owner & 0x1F) <= 0x1E) {
            strcat(text, " belonging to");
            owner = get_string(((obj->ol.f.owner & 0x1F) + 0x172) | STR_GAME);
            str_cat(text, owner);
        }
        strcat(text, ".\n");
        scroll_print(text);
    } else if (OBJ_CLASS(obj) == CLASS_DECAL)
        RectLook(obj, lore);
    SpecialLook(obj, lore);
}

/* The words before the name: 'cursed ' (lore 3, a curse, major 9), 'magical ' (lore 2,
   or a magic book of class 8 or below), or for a blackrock gem 'warm ' or 'cool ' by whether
   its owner field is set. Returns 1 when it added a word. */
char far do_mods(struct Object far *obj, int lore, char *s)
{
    int16 major;
    int16 effect;
    unsigned char flag;
    char far *str;

    if (decode_obj_spell(obj, &major, &effect, &flag)) {
        if (lore == 3) {
            if (!flag && major == 9) {
                strcat(s, "cursed ");
                return 1;
            }
        } else if (lore == 2 || (OBJ_CLASS(obj) == CLASS_BOOK && OBJ_INCLASS(obj) <= 8)) {
            strcat(s, "magical ");
            return 1;
        }
    }
    if ((obj->id & 0x1F8) == FIRST_BLACKROCK_GEM) {
        str = get_string(((obj->ol.f.owner > 0) + 0x164) | STR_GAME);
        far_strcpy(s + strlen(s), str);
        return 1;
    }
    return 0;
}

/* The words after the name, for an identified object (lore 3): ' of Poison' for a
   potion holding a poison trap object, else ' of <enchantment>' (block 6: weapon and armour
   enchantments from 0x1C0, spells from 0x100, others by major class * 16) and, for an object
   whose spell has charges, ' with N full charges' (a potion always shows 1). */
char far do_of(struct Object far *obj, int lore, char *s)
{
    int16 major;
    int16 effect;
    unsigned char flag;
    char far *name;
    char found;
    union Link far *link;
    struct Object far *spell;
    int charges;

    always_decode = 1;
    if (lore == 3 && OBJ_ITEM(obj) >= FIRST_POTION && OBJ_ITEM(obj) <= ITEM_BROWN_POTION && !OBJ_ISQUANT(obj)) {
        link = &obj->ol.link;
        if (Obj_InList(&link, 0, 6, 0, 0) != 0) {
            strcat(s, " of Poison");
            return 1;
        }
    }
    found = decode_obj_spell(obj, &major, &effect, &flag);
    always_decode = 0;
    if (found != 0 && lore == 3) {
        if (major == 12) {
#ifndef __TURBOC__
            /* port only: --enhance true-enchants (docs/ENHANCEMENTS.md). A melee weapon's
               enchantment is named here by string 0x1C0 + effect: 0..3 the Accuracy names
               (Minor, Major, Great, Unsurpassed), 4..7 the Damage ones. But combat/COMBAT.C's
               DoPlayerWeapon, the only code that applies them, adds 2e + 1 damage for effects
               0..3 and 2e - 7 to hit for 4..7 (FINDINGS.md). Flipping bit 2 of the effect for
               the name swaps the halves, so each weapon is named for what it does, with the
               same strength: effect e (damage 2e + 1) is named "e Damage", effect 4 + e (to hit
               2e + 1) "e Accuracy". Only the name: the effect DoPlayerWeapon reads is not
               changed. Missiles, armour (minor 2 and 3, named from 0x1D0) and the special
               weapons (8 and up) are left alone */
            if (ENHANCED(ENH_TRUE_ENCHANTS) && OBJ_MAJOR(obj) == MAJOR_HACK && OBJ_MINOR(obj) == MINOR_WEAPON
                && effect < 8)
                effect ^= 4;
#endif
            major = 0x1C0;
            if (OBJ_MINOR(obj) > 1)
                effect += 0x10;
            effect += major;
        } else if (major == 9)
            return 0;
        else if (flag != 0 && major <= 0)
            effect += 0x100;
        else
            effect += major << 4;
        name = get_string(effect | STR_SPELLS);
        if (name == 0 || *name == 0)
            name = "UNNAMED";
        strcat(s, " of ");
        far_strcpy(s + strlen(s), name);
        if (!OBJ_ISQUANT(obj)) {
            charges = -1;
            link = &obj->ol.link;
            spell = Obj_InList(&link, 0, MAJOR_SPEC, 2, 0);
            if (spell != 0 && (spell->id & 0x800))
                charges = spell->qn.f.quality;
            if (charges >= 0) {
                strcat(s, " with ");
                if (charges > 0) {
                    char num[3] = "00";

                    if (OBJ_ITEM(obj) >= FIRST_POTION && OBJ_ITEM(obj) <= ITEM_BROWN_POTION)
                        charges = 1;
                    num[1] = charges % 10 + '0';
                    if (charges > 9) {
                        num[0] = charges / 10 + '0';
                        strcat(s, num);
                    } else
                        strcat(s, &num[1]);
                } else
                    strcat(s, "no");
                strcat(s, " full charge");
                if (charges != 1)
                    strcat(s, "s");
            }
        }
        return 1;
    }
    return 0;
}

/* Reads a book or scroll: a map says 'Enscribed upon the scroll is your map.'; an
   enchanted (spell) scroll says nothing here; a scroll flagged with ID_FLAG10 plays a
   cutscene (0x100 + its text number); otherwise 'You read the <name>...' and its text from
   block 3. Reading text 6 sets bit 2 of quest variable 26. */
void far BookLook(struct Object far *obj, int print)
{
    char far *str;
    char text[100];

    if (print < 1)
        return;
    if (OBJ_ITEM(obj) == ITEM_MAP) {
        game_sprint(0xA5);  /* 'Enscribed upon the scroll is your map.' */
        return;
    }
    if (OBJ_ITEM(obj) == ITEM_BIT_OF_A_MAP)
        return;
    if ((obj->id & ID_ENCHANT) && OBJ_MAJOR(obj) != MAJOR_RECT)
        return;
    if (obj->id & ID_FLAG10) {
        show_cutscene((obj->ol.f.link & 0x1FF) + 0x100);
        return;
    }
    if ((obj->ol.f.link & 0x1FF) > 0xFF)
        scroll_print(get_string((obj->ol.f.link & 0x1FF) | STR_BOOKS));
    else {
        strcpy(text, "You read the ");
        get_name(text + strlen(text), obj, 0, 0);
        strcat(text, "...\n");
        scroll_print(text);
        if ((obj->ol.f.link & 0x1FF) == 6)
            player->quests[26] = (player->quests[26] & 0xFFFFFFFBL) + 4;
        str = get_string((obj->ol.f.link & 0x1FF) | STR_BOOKS);
        scroll_print(str);
        scroll_print("\n");
    }
}

/* Looks at a decal: classes 14 and 15 describe the texture behind them (and a shaft
   texture, terrain 5, prompts player_look_shaft), class 4 is a bridge, class 5 a gravestone
   and class 6 a plaque. A gravestone's picture number comes from DATA\GRAVE.DAT, indexed by
   its text number; with one, the scroll is cleared and the gravestone shown
   (player_look_grave), otherwise 'The gravestone reads: ' (or for a plaque 'The plaque
   reads: ') and the inscription from block 8. */
void far RectLook(struct Object far *obj, int look)
{
    int fd;
    unsigned char c;
    unsigned char ok;
    char far *grave;
    char far *str;
    struct Object far *o;
    register int base;
    register int index;

    base = 0x160;
    c = 0;
    switch OBJ_INCLASS(obj) {
    case 14:
    case 15:
        if (look >= 0)
            look_nothing(2, obj->ol.f.owner + 1);
        if (look > 0 && (TxmTerr[obj->ol.f.owner] & 7) == 5)
            player_look_shaft();
        else if (look == -1)
            break;
        break;
    case 4:
        if (OBJ_FLAGS(obj) >= 2)
            look_nothing(2, OBJ_FLAGS(obj) + 0x3F);
        else
            game_sprint(0xBA);  /* 'You see a bridge.' */
        break;
    case 6:
        base += 0x10;
    case 5:
        index = OBJ_ISQUANT(obj) ? obj->ol.f.link & 0x1FF : obj->ol.f.owner;
        if (OBJ_INCLASS(obj) == 5) {
            ok = (fd = open("DATA\\grave.dat", O_RDONLY | O_BINARY)) != -1;
            ok &= lseek(fd, index, 0) != -1L;
            ok &= read(fd, &c, 1) == 1;
            ok &= close(fd) != -1;
            if (!ok)
                break;
        }
        if ((grave = get_string(index | STR_WRITING)) != 0 && c != 0)
            scroll_clear(1);
        if (OBJ_INCLASS(obj) == 6 || c == 0) {
            base = base + OBJ_FLAGS(obj);
            str = get_string(base | STR_WRITING);
            scroll_print(str);
        }
        if (grave != 0) {
            o = obj;
            if (index != 0 || find_obj(6, 2, 3, &o) == 0) {
                grave = fix_name_string(grave, 1, 0);
                scroll_print(grave);
                scroll_print("\n");
            }
        }
        if (c != 0)
            player_look_grave(c);
        break;
    }
}

/* Whose bones: 'It looks to be that of ' (or 'They look to be those of ' for a pile) and
   the creature kind from the owner field (+ FIRST_CREATURE), 'Relk.' for owner 0x3D or 'an
   adventurer.' for 0x3F. Owners 0, 0x28 and 0x3C..0x3E say nothing, so the 'Relk.' case for
   0x3D is never reached. */
void far BonesLook(struct Object far *obj, int print)
{
    struct Object tmp;
    char text[40];
    int id;

    if (print != 0 && obj->ol.f.owner != 0 && obj->ol.f.owner != 0x28
        && (obj->ol.f.owner < 0x3C || obj->ol.f.owner == 0x3F)) {
        id = 0x1A;
        if (OBJ_ITEM(obj) == ITEM_PILE_OF_BONES_C6 || obj->ol.f.link > 1)
            id++;
        game_sprint(id);
        if (obj->ol.f.owner == 0x3D)
            scroll_print("Relk.\n");
        if (obj->ol.f.owner == 0x3F)
            scroll_print("an adventurer.\n");
        else {
            SET_ITEM(&tmp, obj->ol.f.owner + FIRST_CREATURE);
            tmp.whoami = 0;
            get_name(text, &tmp, 1, 0);
            scroll_print(text);
            scroll_print(".\n");
        }
    }
}

/* A key's description, string 0x64 + owner of block 5 (by lock, e.g. 'The key feels
   unnaturally heavy.'), if there is one. */
void far KeyLook(struct Object far *obj, int print)
{
    char far *str;

    if (print != 0) {
        str = get_string((obj->ol.f.owner + 0x64) | STR_OBJLOOK);
        if (str != 0)
            scroll_print(str);
    }
}

/* Describes a critter into s: 'a <attitude> <kind> named <Name>'. The attitude word is
   block 5 0x60 + attitude (hostile, upset, peaceful, friendly), shown only for whoami below
   0xF0 or 0xFF; the kind is its item name, or for a nameless kind with goal 11 a string
   from 0x115 by its animation sequence; the name is the NPC's conversation name (block 7,
   whoami + 0x10). A name that starts in lower case replaces the kind instead of following
   it. */
void far CritterLook(struct Object far *obj, char *s)
{
    char far *desc;
    char far *name;
    char far *attitude;
    unsigned char who;

    desc = get_string(OBJ_ITEM(obj) | STR_OBJNAMES);
    if (*desc == 0) {
        if (OBJ_GOAL(obj) == 11)
            desc = get_string((OBJ_SEQ(obj) + 0x115) | STR_GAME);
        else
            desc = 0;
    }
    who = obj->whoami;
    if (who < 0xF0 || who == 0xFF) {
        attitude = get_string((OBJ_ATTITUDE(obj) + 0x60) | STR_OBJLOOK);
        if (*attitude == 0)
            attitude = 0;
    } else
        attitude = 0;
    if (who > 0) {
        name = get_string((who + 0x10) | STR_CONV);
        if (*name == 0)
            name = 0;
    } else
        name = 0;
    if (desc != 0 && attitude != 0) {
        switch (*attitude) {
        case 'a': case 'e': case 'i': case 'o': case 'u':
            strcat(s, "an ");
            break;
        default:
            strcat(s, "a ");
        }
        str_cat(s, attitude);
        strcat(s, " ");
    }
    if (desc != 0 && (name == 0 || isupper(*name)))
        str_cat(s, fix_name_string(desc, attitude == 0, 0));
    if (name != 0) {
        if (desc != 0 && isupper(*name))
            strcat(s, " named ");
        str_cat(s, fix_name_string(name, desc == 0, 0));
    }
}

/* The second line of a look for books and scrolls (BookLook), keys (KeyLook), bones
   (BonesLook) and doors: a closed door whose owner bit 0 is set prints string 0x91, which is
   empty in the game's STRINGS.PAK. */
void far SpecialLook(struct Object far *obj, int print)
{
    int minor;

    minor = OBJ_MINOR(obj);
    switch (OBJ_MAJOR(obj)) {
    case MAJOR_SPEC:
        if (minor == 3 && OBJ_INCLASS(obj) < 10)
            BookLook(obj, print);
        else if (minor == 0)
            KeyLook(obj, print);
        break;
    case MAJOR_STUFF:
        if (minor == 0 && OBJ_ITEM(obj) >= ITEM_SKULL_C2 && OBJ_ITEM(obj) <= ITEM_PILE_OF_BONES_C6)
            BonesLook(obj, print);
        break;
    case MAJOR_RECT:
        if (minor == 0 && OBJ_INCLASS(obj) < 8 && (obj->ol.f.owner & 1))
            game_sprint(0x91);  /* an empty string */
        break;
    }
}

/* The short description of obj into s: quality word and name (plural for a stack), or a
   critter's CritterLook. Returns the quantity. */
int far GetObjDesc(struct Object far *obj, int lore, char *s)
{
    char far *qstr;
    char *end;
    int qty;
    int q;
    char qual[26];
    struct ComObj *com;

    qty = 1;
    com = &ComObjData[OBJ_ITEM(obj)];
    q = 0;
    *s = 0;
    if (OBJ_MAJOR(obj) == MAJOR_CREATURE)
        CritterLook(obj, s);
    else {
        if (obj->qn.f.quality != 0)
            q = com->qualclass != 3 ? (obj->qn.f.quality >> 4) + 1 : 5;
        qstr = get_string((com->qualtype * 6 + q) | STR_OBJLOOK);
        if (qstr != 0 && *qstr != 0)
            str_copy(qual, qstr);
        else
            qual[0] = 0;
        if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
            qty = obj->ol.f.link;
        if (qual[0] != 0) {
            str_cat(s, qual);
            str_cat(s, " ");
        }
        end = s + strlen(s);
        get_name(end, obj, 0, qty > 1);
    }
    return qty;
}
