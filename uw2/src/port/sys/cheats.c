/* UW2's cheats for tests (Exhume's runtime/port/sys/cheat.c, which reads them from an input
   script's `cheat` lines, refuses them unless EXHUME_TEST_CHEATS=1, records and replays them,
   and runs each at the main loop's safe point when cheat_ready says it may). Each goes through
   the game's own routines, so the state it leaves is one the game itself could hold, and a game
   saved after it loads in DOS:

     goto LEVEL X Y          do_teleport, as a teleport trap, and the 3D view's change bit 3,
                             as a step of the player's physics sets it: the move is carried
                             out by the main loop's new_player_pos (ChangeLevel when LEVEL is
                             another, then player_setup). A square with no free place near
                             it is refused on this level; on another, which is not loaded until
                             the move, the game kills the player it cannot place, as it does
                             after any teleport; LEVEL 1 to 80 (a level the game has: the
                             game stops with its own error for one it lacks), X and Y 1 to 62
     give ITEM [QTY [QUALITY]] [drop]
                             CreateObj (the game's allocator), QTY of a stack (1 to 511) and
                             QUALITY (0 to 63; else the game's 40), into the first empty
                             backpack slot on show (AddToInventory) when the player can carry it
                             (EncumCheck); else, or with drop, at the player's feet, as a
                             conversation puts an object there (put_at, stay_centered). ITEM is
                             the game's item id (docs/FORMATS.md; src/include/items.h), decimal
                             or 0x hex, an object a player can carry: 0x000 to 0x03F or 0x080 to
                             0x13F
     set FIELD VALUE         hp (1 to the maximum), mana (0 to the maximum), str, dex, int (1 to
                             99, then player_compute: the maxima, carrying weight str * 13 + 300
                             among them, as a level gained does), or a skill by name (0 to 30; enum
                             Skill, lower case: attack, defense ... swimming)

   The numbers are checked when the script is read; what depends on the game (a free object, a
   free square) when the cheat runs, which then says what failed and changes nothing. */
#include "compat.h"
#include "event.h"
#include "inv.h"
#include "items.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

static const char *skill_names[NUM_SKILLS] = {
    "attack", "defense", "barehand", "sword", "axe", "mace", "missile", "mana", "lore",
    "casting", "traps", "search", "track", "stealth", "repair", "charisma", "picklock",
    "acrobat", "appraise", "swimming"
};

/* in the game, on its 3D view, with nothing on the cursor, no move pending and the player
   alive */
static int cheat_ready(void)
{
    return in_game && scrnum == 0 && GameInputMode == 0 && CursorObjPtr == 0 && NewPlayerX == 0
        && ThePlayer != 0 && ThePlayer->hp > 0;
}

static const char *c_goto(int argc, char *argv[], int run)
{
    long level, x, y;
    if (argc != 3 || cheat_num(argv[0], 1, NUM_LEVELS, &level) || cheat_num(argv[1], 1, 62, &x)
        || cheat_num(argv[2], 1, 62, &y))
        return "a level 1 to 80 and a square 1 to 62, 1 to 62";
    if (!run) return NULL;
    if (do_teleport(ThePlayer, (int)x, (int)y, (int)level) != 0x10)
        return "no free square there";
    /* the 3D view's change handler 3, new_player_pos, carries the move out, as after each step
       of the player's physics (PLAYMOVE.C's editchng(10)): a player standing still takes none */
    editchng(8);
    return NULL;
}

static const char *c_give(int argc, char *argv[], int run)
{
    long item = -1, qty = 0, quality = -1, v;
    int drop = 0, i, n = 0, slot;
    struct Object far *obj;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "drop")) {
            drop = 1;
            continue;
        }
        if (n == 3 || cheat_num(argv[i], n == 1 ? 1 : 0, n == 2 ? 63 : 0x1FF, &v))
            return "an item, then a quantity 1 to 511 and a quality 0 to 63 if given";
        if (n == 0) item = v;
        else if (n == 1) qty = v;
        else quality = v;
        n++;
    }
    if (n == 0) return "an item";
    if (!((item >= 0 && item <= 0x3F) || (item >= 0x80 && item <= 0x13F)))
        return "an object a player can carry: 0x000 to 0x03F or 0x080 to 0x13F";
    if (!run) return NULL;
    if ((obj = CreateObj((int)item, 0)) == 0) return "no free object";
    if (qty) {
        if (!OBJ_ISQUANT(obj)) {
            Obj_Free(obj);
            return "not an object with a quantity";
        }
        obj->ol.f.link = (uint16)qty;
    }
    if (quality >= 0) obj->qn.f.quality = (uint16)quality;
    if (!drop && OpenBag == 0 && EncumCheck(obj))
        for (slot = 11; slot <= 18; slot++)             /* the backpack (inv.h) */
            if (Inventory[slot].f.index == 0 && ItemFitsSlot(obj, slot) > 0) {
                AddToInventory(obj, slot);
                RedisplayInvSlot(slot);
                return NULL;
            }
    stay_centered = 1;
    i = near_mob_put_at(ThePlayer, obj, 6, 1);
    stay_centered = 0;
    return i ? NULL : "no room at the player's feet";
}

static const char *c_set(int argc, char *argv[], int run)
{
    long v;
    int k;
    if (argc != 2) return "a field and a value";
    if (!strcmp(argv[0], "hp") || !strcmp(argv[0], "mana")) {
        if (cheat_num(argv[1], 0, 255, &v)) return "a value 0 to 255";
        if (!run) return NULL;
        if (argv[0][0] == 'h') {
            if (v < 1 || v > playerdat->avghit) return "hp 1 to the player's maximum";
            ThePlayer->hp = (unsigned char)v;
        } else {
            if (v > player->max_mana) return "mana 0 to the player's maximum";
            player->play_mana = (unsigned char)v;
        }
        panel_check_hpmp();
        return NULL;
    }
    /* attr[]: strength, dexterity, intelligence (PLAYDATA.C) */
    k = !strcmp(argv[0], "str") ? 0 : !strcmp(argv[0], "dex") ? 1 : !strcmp(argv[0], "int") ? 2 : -1;
    if (k >= 0) {
        if (cheat_num(argv[1], 1, 99, &v)) return "a value 1 to 99";
        if (!run) return NULL;
        playerdat->attr[k] = (unsigned char)v;
    } else {
        for (k = 0; k < NUM_SKILLS; k++)
            if (!strcmp(argv[0], skill_names[k])) break;
        if (k == NUM_SKILLS) return "hp, mana, str, dex, int or a skill (attack, defense ... swimming)";
        if (cheat_num(argv[1], 0, 30, &v)) return "a value 0 to 30";
        if (!run) return NULL;
        player->skills[k] = (unsigned char)v;
    }
    /* the maxima again, as a level gained does; the vitals kept within them */
    player_compute(0);
    if (ThePlayer->hp > playerdat->avghit) ThePlayer->hp = playerdat->avghit;
    if (player->play_mana > player->max_mana) player->play_mana = player->max_mana;
    panel_check_hpmp();
    return NULL;
}

static const struct cheat_cmd cmds[] = {
    { "goto", "LEVEL X Y", c_goto },
    { "give", "ITEM [QTY [QUALITY]] [drop]", c_give },
    { "set", "FIELD VALUE", c_set },
};

const struct cheat_game cheat_game = { cmds, (int)(sizeof cmds / sizeof cmds[0]), cheat_ready };
