/* target: ovr149 */
/* opts: -mm -1 -G -O -Y -d */
/* Saving and restoring games, and changing level: the save directory and its four slots,
   copying a slot to and from the working directory, loading and saving one level's map,
   and the special cases some levels need on arrival or departure. The whole of DOS
   overlay ovr149, in original order. Function and global names are the originals from
   the FM Towns symbol table.

   How a save works. The game in progress lives in HomeDir, UWHOME\SAVE0\: UWEDIT.C copies
   the pristine LEV.ARK and SCD.ARK there at start-up, and every level change writes the
   level being left back into SAVE0's LEV.ARK (SaveLevel) and reads the new one from it
   (GetLevel). A saved game is a copy of that directory: SAVE1 to SAVE4. SaveGame asks for
   a description (up to 30 characters; Escape aborts), writes it to SAVE0\DESC, writes
   PLAYER.DAT and the current level (SavePlayerInv, SaveLevel), then copies every file of
   SAVE0 into SAVEn. RestoreGame empties SAVE0, copies SAVEn into it, resets the player
   (UWEDIT.C's reset_game), reads PLAYER.DAT (RestorePlayerInv) and loads the saved
   level. get_save_descs reads each slot's DESC for the menu.

   Changing level: ChangeLevel (UWEDIT.C's new_player_pos) runs do_level_hacks for the
   level being left (mode 1), saves it, loads the new one and runs do_level_hacks for it
   (mode 0), then brings the schedules' clocks up to date (Sched_SetAllClocks).
   do_level_hacks is where some worlds' special rules live (see its comment).

   Entry points: init_save (UWEDIT.C's init_world), ShowSaveRest and DoSaveRest (the
   options panel, WRAPPER.C), GetLevel and do_level_hacks (also MAINMENU.C, when a game
   starts), ChangeLevel, copy_file, clear_dir.
   Data: none of its own.
   Name: original (copy_file is in System Shock's GAMEWRAP.C, saving and loading games in
   both). */
/* name: init_save (IDA MaybeCreateSaveGameFolder_ovr149_0) is the FM Towns function just
   before GetLevel_: both make the working directory, empty it with clear_dir and then
   require 1200 bytes free on the disk. */

#include <string.h>
#include <stdio.h>
#include <dir.h>
#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include <stat.h>                       /* sys\stat.h; the build keeps it flat */
#include "combat.h"
#include "critter.h"
#include "event.h"
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"


/* match: the tests in SaveLevel and copy_file have empty bodies: the bytes keep each
   test with no jump after it, as if a debugging message had been compiled out. */
#define complain(what)

/* Makes the SAVE0 directory, empties it, and checks for 1200 bytes free on the
   current drive. Returns 0 (start-up then stops with "Not enough disk space") if not. */
unsigned char far init_save(void)
{
    int32 space;
    struct dfree df;
    char dir[66];

    strcpy(dir, HomeDir);
    dir[strlen(dir) - 1] = 0;
    mkdir(dir);
    clear_dir(HomeDir);
    getdfree(0, &df);
    if (df.df_sclus != 0xFFFF) {
        space = (int32)df.df_avail * df.df_bsec * df.df_sclus;
        if (space < 0x4B0)
            return 0;
    } else
        return 0;
    return 1;
}

/* Loads level from SAVE0's LEV.ARK (archive 0): the map and objects, with the player's
   inventory kept aside meanwhile, then the level's minimum light (DL.DAT), its texture
   map, its automap (archive 4) and its creatures. PlayerLevel becomes level. */
int far GetLevel(int level)
{
#ifndef __TURBOC__
    port_shared_reset();
#endif
    int sq;
    int ok = 1;
    int flags = 2;

    SavePlayerInv(0);
    sq = GrSq;
    change_GrSq(-1, -1);
    if (!Map_Load(0, level, flags))
        ok = 0;
    RestorePlayerInv(0);
    if (ok > 0) {
        PlayerLevel = level;
        load_dl();
        Txm_Load(0, level, flags | 4);
        ClearAutoMap();
        init_level_creature_stuff();
        if (ok == 1)
            GetAutoMapLevel(4, level);
        close_arc(1);
    }
    change_GrSq(sq, -1);
    return ok;
}


/* Writes level back into SAVE0's LEV.ARK: the map and objects (without the player's
   inventory, which is freed and restored round it), the texture map and the automap. */
char far SaveLevel(int level)
{
#ifndef __TURBOC__
    port_shared_before_save();
#endif
    char ok;
    char saved;
    int sq;

    SavePlayerInv(0);
    FreePlayerInv(&ThePlayer->ol.link);
    sq = GrSq;
    change_GrSq(-1, -1);
    SET_MAJOR(ThePlayer, 0);
    if ((saved = Map_Save(0, level, 2)) == 0)
        complain("map");
    ok = saved;
    if (ok && (ok = Txm_Save(0, level, 6)) == 0)
        complain("textures");
    if (ok && (ok = SaveAutoMapLevel(4, level)) == 0)
        complain("automap");
    if (saved)
        close_arc(1);
    RestorePlayerInv(0);
    change_GrSq(sq, -1);
    return ok;
}

/* Reads SAVE1\DESC to SAVE4\DESC (the slot digit replaces the last '0' of HomeDir) into
   descs, setting bit n - 1 of *found for each slot n that exists; the rest read
   "<not used yet>". */
void far get_save_descs(char descs[][40], int16 *found)
{
    int i;
    char *num;
    char *d;
    char path[66];
    FILE *fp;

    strcpy(path, HomeDir);
    num = strrchr(path, '0');
    strcat(path, "desc");
    *found = 0;
    for (i = 0; i < 4; i++) {
        *num = i + '1';
        if (access(path, 0) >= 0 && (fp = fopen(path, "r")) != 0) {
            d = descs[i];
            fgets(d, 39, fp);
            *found |= 1 << i;
            fclose(fp);
        }
        if (!(*found & (1 << i)))
            strcpy(descs[i], "<not used yet>");
    }
}

void far ShowSaveRest(void)
{
    int16 found;
    char *labels[4] = { "I- ", "II- ", "III- ", "IV- " };
    char descs[4][40];
    int i;

    scroll_clear(1);
    get_save_descs(descs, &found);
    game_sprint(0x120);                 /* "Save Game Descriptions" */
    scroll_esc = 0;
    for (i = 0; i < 4; i++) {
        scroll_print("\n");
        scroll_print(labels[i]);
        scroll_print(descs[i]);
    }
    scroll_esc = 1;
    scroll_print("\\0");
}


/* Saves to or restores from slot 1 to 4 and prints the outcome, string 0xAE + msg:
   restore 1 no game there, 2 complete, 3 failed; save 4 failed, 5 succeeded,
   6 aborted (SaveGame returns 0, 1 or 2). After a restore the screen, the effects and
   the physics are brought up to date. */
void far DoSaveRest(int restore, int slot)
{
    int16 found;
    char descs[4][40];
    int msg;

    scroll_clear(0);
    get_save_descs(descs, &found);
    if (restore) {
        if (!(found & (1 << (slot - 1)))) {
            msg = 1;
            pretty_panelagain();
        } else if (RestoreGame(slot)) {
            load_weapcm();
            msg = 2;
            realDScheck = 0;
            display_scr();
            realDScheck = 1;
            init_scrgr();
            newFPS(-1);
            parse_effect();
            fiz_update = 1;
            editchng(0x7FFE);
        } else {
            msg = 3;
            pretty_panelagain();
        }
    } else
        msg = SaveGame(slot, descs[slot - 1]) + 4;
    game_sprint(msg + 0xAE);            /* "No save game there." ... "Save Game Aborted." */
}

char far RestoreGame(char slot)
{
    char path[66];
    char *p;

    strcpy(path, HomeDir);
    p = strrchr(path, '0');
    *p = slot + '0';
    game_sprint(0xB5);                  /* "Restoring Game " */
    if (clear_dir(HomeDir)) {
        game_sprint(0xB9);              /* "..." after each step */
        if (copy_dir(path, HomeDir)) {
            game_sprint(0xB9);  /* '...' */
            reset_game();
            if (RestorePlayerInv(HomeDir)) {
                game_sprint(0xB9);  /* '...' */
                if (GetLevel(PlayerLevel)) {
                    do_level_hacks(PlayerLevel, 3);
                    game_sprint(0xB9);  /* '...' */
                    set_creatures_from_saved_game();
                    return 1;
                }
            }
        }
    }
    scroll_print("\n");
    return 0;
}

int far SaveGame(char slot, char *desc)
{
    int ret = 0;
    char path[66];
    char *p;

    set_creatures_to_saved_game();
    strcpy(path, HomeDir);
    scroll_clear(1);
    game_sprint(0xB7);                  /* "Please enter a save file description:" */
    if (wdialog(0, desc, desc, 1, 0x1E) == 0x1B) {
        ret = 2;
        goto fail;
    }
    if (strlen(desc) == 0)
        strcpy(desc, " ");
    scroll_print("\n");
    game_sprint(0xB6);                  /* "Saving Game " */
    strcat(path, "desc");
    if (!blttodrive(desc, path, strlen(desc)))
        goto fail;
    p = strrchr(path, '0');
    *p = slot + '0';
    p[1] = 0;
    if (access(path, 0) < 0 && mkdir(path) < 0)
        goto fail;
    strcat(path, "\\");
    if (!clear_dir(path))
        goto fail;
    game_sprint(0xB9);  /* '...' */
    if (SavePlayerInv(HomeDir)) {
        game_sprint(0xB9);  /* '...' */
        if (SaveLevel(PlayerLevel)) {
            game_sprint(0xB9);  /* '...' */
            if (copy_dir(HomeDir, path)) {
                scroll_print("\\0");
                scroll_clear(1);
                return 1;
            }
        }
    }
fail:
    scroll_clear(1);
    return ret;
}

unsigned char far clear_dir(char *dir)
{
    char path[66];
    struct ffblk ff;
    char *end;
    int r;

    strcpy(path, dir);
    end = path + strlen(path);
    strcat(path, "*.*");
    for (r = findfirst(path, &ff, 0); r == 0; r = findnext(&ff)) {
        strcpy(end, ff.ff_name);
        if (unlink(path) != 0)
            return 0;
    }
    return 1;
}

/* Copies srcdir + name to dstdir + name through the workspace, 0xF000 bytes at a time.
   Returns 0 if either file cannot be opened, there is no workspace, or a write falls
   short. */
unsigned char far copy_file(char *srcdir, char *dstdir, char *name)
{
    int out;
    unsigned ws;
    char far *buf;
    char ok;
    char src[66];
    char dst[66];
    int in;
    unsigned n;

    in = -1;
    out = -1;
    ok = 0;
    strcpy(src, srcdir);
    strcat(src, name);
    strcpy(dst, dstdir);
    strcat(dst, name);
    in = open(src, O_RDONLY | O_BINARY);
    if (in < 0)
        return 0;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
    if (out >= 0) {
        lseek(in, 0L, SEEK_SET);
        if ((ws = get_workspace()) == 0)
            complain("no workspace");
        else {
            buf = MK_FP(ws, 0);
            ok = 1;
            do {
                n = intoFarBuffer_ovr167_5DA(in, buf, 0xF000);
                if (FarWrite_ovr167_627(out, buf, n) != n) {
                    ok = 0;
                    break;
                }
            } while (n == 0xF000);
            release_workspace();
        }
    }
    if (in >= 0)
        close(in);
    if (out >= 0)
        close(out);
    return ok;
}

/* Copies every file of src into dst; returns 0 if a copy failed. */
unsigned char far copy_dir(char *src, char *dst)
{
    char path[66];
    struct ffblk ff;
    int r;

    strcpy(path, src);
    strcat(path, "*.*");
    for (r = findfirst(path, &ff, 0); r == 0; r = findnext(&ff))
        if (!copy_file(src, dst, ff.ff_name))
            break;
    if (r == 0)
        return 0;
    return 1;
}

/* Moves the game from level from to level to: ends fighting and any object held on
   the cursor, runs the leaving hacks, saves from, loads to, runs the arriving hacks,
   recomputes mana and runs the schedules' clocks. Returns 0 if the load fails
   (new_player_pos then stops the game with a fatal error). */
char far ChangeLevel(int from, int to)
{
    char ok;

    clear_fight_state();
    if (GameInputMode == 2 && CursorObjPtr != 0) {
        GameInputMode = 0;
        CursorObjPtr = 0;
        unforce_mouse_cursor(3);
    }
    do_level_hacks(from, 1);
    if ((ok = SaveLevel(from)) != 0) {
        if ((ok = GetLevel(to)) != 0) {
            do_level_hacks(to, 0);
            restore_mana(ThePlayer, 0);
            Sched_SetAllClocks(1);
        } else
            return 0;
    }
    return ok;
}

/* Unlocks and opens the door in square (x, y) and ends its animation. Used on arrival
   in level 1, Lord British's castle, at the arrival square (probably so the player
   never arrives behind a shut door; inferred). */
void far gruesome_door_hack(int x, int y)
{
    struct Object far *door;
    int anim;

    door = Obj_FindInMapSquare(5, 0, -1, x, y);
    if (door != 0) {
        checkLock(0L, door, -0x2D);
        OpenDoor(0L, door);
        anim = find_anim(door);
        if (anim >= 0)
            toast_animobj(find_anim(door), 5);
    }
}

/* Per-level special cases. mode 0 is arriving, 1 leaving, 3 after a restore (which only
   reloads the sound effects). The world is (level - 1) / 8 (Guide: 0 Britannia,
   1 Prison Tower, 2 Killorn, 3 Ice, 4 Talorus, 5 Academy, 6 Tombs, 7 Pits, 8 the
   Ethereal Void):
   - arriving anywhere: the creatures are set up and, with no weapon drawn, walking
     music starts; leaving: the critters are moved on as if time had passed. If
     player->b60_11 is set, arriving only runs clearobj.
   - level 1, arriving: the door at the arrival square is opened (gruesome_door_hack).
   - Prison Tower, leaving: prison_alarm_check; leaving its level 2 also fires the
     triggers at (31, 36) and (35, 34).
   - Killorn, leaving its level 1 with quest 50 set (the keep is going to crash):
     Killorn_just_crashed(1). The case then falls into the Academy's, so leaving level 3
     of Killorn takes the telekinesis wand away from the player (remove_TK_wand) as leaving
     level 3 of the Academy does.
   - Tombs, arriving with quest 7 set (Loth is dead): genocide(0xFF), which kills the
     critters is_my_race matches for race 0xFF and destroys the floating skulls.
   - Ethereal Void: the automap is switched off on arrival and back on when leaving. */
void far do_level_hacks(int level, int mode)
{
    punt_all_digi_fx();
    load_digi_fx(1);
    load_digi_fx(2);
    load_digi_fx(0xB);
    if (player->b60_11 && mode == 0) {
        clearobj(0);
        return;
    }
    if (mode == 0) {
        init_level_creature_stuff();
        if (!player->drawn)
            set_random_walking_music(-2);
    } else if (mode == 1)
        update_all_critters_whilst_player_snoozes();
    if (level == 1 && mode == 0) {
        ThePlayer->last_hit = 0;
        gruesome_door_hack(NewPlayerX, NewPlayerY);
    }
    switch ((level - 1) / 8) {
    case 1:
        if (mode == 1) {
            prison_alarm_check();
            if ((level - 1) % 8 + 1 == 2) {
                fire_trigger_at(0x1F, 0x24);
                fire_trigger_at(0x23, 0x22);
            }
        }
        break;
    case 2:
        if ((int)((player->quests[12] & 4) >> 2) && mode == 1 && (level - 1) % LEVELS_PER_WORLD + 1 == 1)
            Killorn_just_crashed(1);
    case 5:
        if ((level - 1) % 8 + 1 == 3 && mode == 1)
            remove_TK_wand();
        break;
    case 6:
        if (mode == 0 && (int)((player->quests[1] & 8) >> 3))
            genocide(0xFF);
        break;
    case 8:
        if (mode == 0) {
            player->saved_automap = player->automap;
            player->automap = 0;
        }
        if (mode == 1)
            player->automap = player->saved_automap;
        break;
    }
}
