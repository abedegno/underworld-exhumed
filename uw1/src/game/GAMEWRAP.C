/* target: ovr140 */
/* opts: -mm -1 -G -O -Y -d */
/* Saving and restoring games, and changing level: the save directory and its four slots,
   copying a slot to and from the working directory, loading and saving one level's map,
   and the special cases some levels need on arrival or departure. The whole of UW1's DOS
   overlay ovr140, in original order. Seeded from UW2Decomp's src/game/GAMEWRAP.C (UW2's
   ovr149).

   How a save works. The game in progress lives in SAVE0\ (UW1 writes the name as a
   literal where UW2 has HomeDir), and every level change writes the level being left back
   into SAVE0\LEV.ARK (SaveLevel) and reads the new one from it (GetLevel). A saved game is
   a copy of that directory: SAVE1 to SAVE4. SaveGame asks for a description (Escape
   aborts), makes and empties SAVEn, writes the description to SAVE0\DESC, writes
   PLAYER.DAT and the current level (SavePlayerInv, SaveLevel), then copies every file of
   SAVE0 into SAVEn. RestoreGame empties SAVE0, copies SAVEn into it, resets the player
   (UWEDIT.C's reset_game), reads PLAYER.DAT (RestorePlayerInv) and loads the saved level.
   get_save_descs reads each slot's DESC for the menu.

   Changing level: ChangeLevel runs do_level_hacks for the level being left (mode 1), saves
   it, loads the new one and runs do_level_hacks for it (mode 0).

   UW1 against UW2: LEV.ARK is opened and closed here, through an archive record
   (struct Arc, file.h) that the map, texture and automap loaders take (ARC.C, MAP.C,
   TEXTMAPS.C, AUTOMAP.C); no DL.DAT, no schedules, no gruesome_door_hack; copy_file takes two whole
   paths and copy_dir builds them; init_save wants 0x9B0A0 bytes free; the save
   descriptions' heading is a literal string; DoSaveRest has no panel redraws or
   parse_effect, and its messages start at 0xA0; do_level_hacks has no sound effects or
   music and handles level 7 (Tybal's lair: no mana while the orb stands) and level 9 (the
   void: the automap off).

   Data: the save descriptions' labels and the string pool.
   UW1 has no symbol-bearing build: names are UW2's (the FM Towns symbol table) where the
   routine is the same.
   Name: original (copy_file is in System Shock's GAMEWRAP.C, saving and loading games in
   both).

   Entry points: init_save and copy_file (UWEDIT.C's init_world), DoSaveRest and
   ShowSaveRest (OPTIONS.C, the options panel), ChangeLevel (UWEDIT.C's new_player_pos).
   Neighbours: INVSAVE.C (PLAYER.DAT and the inventory), MAP.C, TEXTMAPS.C and AUTOMAP.C
   (a level's blocks), ARC.C (LEV.ARK), CRITTIME.C (the critters between levels). */

#include <string.h>
#include <stdio.h>
#include <dir.h>
#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include <stat.h>                       /* sys\stat.h; the build keeps it flat */
/* UW1: map.h's Map_Load and Map_Save take the archive record and the level. */
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

/* UW1: the working directory. */
#define HomeDir "SAVE0\\"

/* match: copy_file's workspace test has an empty body: the bytes keep the test with no
   jump after it, as if a debugging message had been compiled out. */
#define complain(what)

char far blttodrive(void far *buf, char *name, unsigned n);

/* LEV.ARK's archive access (ARC.C), declared as this file uses it. */
char far open_arc(struct Arc far *arc, char *name);
char far close_arc(struct Arc far *arc);

/* Makes the SAVE0 directory, empties it, and checks for 0x9B0A0 bytes free on the
   current drive. Returns 0 (start-up then stops with "Not enough disk space") if not. */
char far init_save(void)
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
        if (space < 0x9B0A0L)
            return 0;
    } else
        return 0;
    return 1;
}

/* Loads level from SAVE0's LEV.ARK: the map and objects, with the player's inventory kept
   aside meanwhile, then its texture map, its automap and its creatures. */
int far GetLevel(register int level)
{
#ifndef __TURBOC__
    port_shared_reset();
#endif
    register int ok = 1;
    struct Arc arc;

    SavePlayerInv(0);
    if (GrSq >= 0)
        GrSq = -1;
    if (!open_arc(&arc, "SAVE0\\lev.ark"))
        return 0;
    ok = Map_Load(&arc, level);
    RestorePlayerInv(0);
    if (ok > 0) {
        Txm_Load(&arc, level);
        ClearAutoMap();
        clear_paths();
        init_level_creature_stuff();
        if (ok == 1)
            GetAutoMapLevel(&arc, level);
    }
    close_arc(&arc);
    return ok;
}

/* Writes level back into SAVE0's LEV.ARK: the map and objects (without the player's
   inventory, which is freed and restored round it), the texture map and the automap. */
char far SaveLevel(register int level)
{
#ifndef __TURBOC__
    port_shared_before_save();
#endif
    char ok;
    struct Arc arc;

    SavePlayerInv(0);
    FreePlayerInv(&ThePlayer->ol.link);
    if (GrSq >= 0)
        Obj_Rem(&(mapdata + GrSq)->objects, ThePlayer);
    GrSq = -1;
    SET_MAJOR(ThePlayer, 0);
    if ((ok = open_arc(&arc, "SAVE0\\lev.ark")) != 0) {
        ok = Map_Save(&arc, level);
        ok = ok && Txm_Save(&arc, level);
        ok = ok && SaveAutoMapLevel(&arc, level);
        ok = ok && close_arc(&arc);
    }
    RestorePlayerInv(0);
    return ok;
}

/* Reads SAVE1\DESC to SAVE4\DESC (the slot digit replaces the '0' of SAVE0) into descs,
   setting bit n - 1 of *found for each slot n that exists; the rest read
   "<not used yet>". */
void far get_save_descs(char descs[][40], register int16 *found)
{
    int i;
    char *num;
    char *d;
    char path[30];
    register FILE *fp;

    strcpy(path, HomeDir);
    num = strchr(path, '0');
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

/* Lists the four save slots' descriptions in the message scroll, as "I- " to "IV- "
   and each slot's DESC (or "<not used yet>"), under the heading "Save Game
   Descriptions". */
void far ShowSaveRest(void)
{
    int16 found;
    char *labels[4] = { "I- ", "II- ", "III- ", "IV- " };
    char descs[4][40];
    register int i;

    scroll_clear(1);
    get_save_descs(descs, &found);
    scroll_print("\\6    Save Game Descriptions");
    scroll_esc = 0;
    for (i = 0; i < 4; i++) {
        scroll_print("\n");
        scroll_print(labels[i]);
        scroll_print(descs[i]);
    }
    scroll_esc = 1;
    scroll_print("\\0");
}

/* name: realDScheck is UW2's name for DS:12B3, inferred from its place (set to 1 after
   display_scr, as in UW2's DoSaveRest); UW1 never clears it here. */
/* Saves to or restores from slot 1 to 4 and prints the outcome, string 0xA0 + msg:
   restore 1 no game there, 2 complete, 3 failed; save 4 failed, 5 succeeded. After a
   restore the screen and the physics are brought up to date. */
void far DoSaveRest(int restore, int slot)
{
    int16 found;
    char descs[4][40];
    register int msg;

    get_save_descs(descs, &found);
    if (restore) {
        if (!(found & (1 << (slot - 1))))
            msg = 1;
        else if (RestoreGame(slot)) {
            load_weapcm();
            msg = 2;
            display_scr();
            realDScheck = 1;
            init_scrgr();
            newFPS(-1);
            fiz_update = 1;
            editchng(0x7FFE);
        } else
            msg = 3;
    } else if (!SaveGame(slot, descs[slot - 1]))
        msg = 4;
    else
        msg = 5;
    game_sprint(msg + 0xA0);
}

/* Restores slot (1 to 4): empties SAVE0, copies SAVEn into it, resets the game
   (reset_game), reads PLAYER.DAT and the inventory (RestorePlayerInv), loads the saved
   level and runs its after-restore hacks (mode 3), and puts back the record of the last
   critter hit. Prints "Restoring Game " and "..." after each step; returns 0 on the first
   failure. */
char far RestoreGame(char slot)
{
    char path[30];
    register char *p;

#ifndef __TURBOC__
    if (port_shared_guest()) return 0;
#endif

    strcpy(path, HomeDir);
    p = strchr(path, '0');
    *p = slot + '0';
    game_sprint(0xA6);                  /* "Restoring Game " */
    if (clear_dir(HomeDir)) {
        game_sprint(0xAA);              /* "..." after each step */
        if (copy_dir(path, HomeDir)) {
            game_sprint(0xAA);
            reset_game();
            if (RestorePlayerInv(HomeDir)) {
                game_sprint(0xAA);
                if (GetLevel(PlayerLevel)) {
                    do_level_hacks(PlayerLevel, 3);
                    game_sprint(0xAA);
                    set_creatures_from_saved_game();
#ifndef __TURBOC__
                    port_shared_campaign_load();
#endif
                    return 1;
                }
            }
        }
    }
    scroll_print("\n");
    return 0;
}

/* Saves to slot (1 to 4): asks for the description (Escape abandons the save), makes
   and empties SAVEn, writes the description to SAVE0\DESC (blttodrive, raw, no
   terminator), writes PLAYER.DAT and the inventory (SavePlayerInv) and the current level
   (SaveLevel), then copies all of SAVE0 to SAVEn. Returns 0 on any failure. */
char far SaveGame(char slot, register char *desc)
{
    char path[30];
    register char *p;

#ifndef __TURBOC__
    if (port_shared_guest()) return 0;
#endif

    set_creatures_to_saved_game();
    strcpy(path, HomeDir);
    p = strchr(path, '0');
    *p = slot + '0';
    p[1] = 0;
    scroll_clear(1);
    game_sprint(0xA8);                  /* "Please enter a save file description:" */
    if (wdialog(0, desc, desc, 1, 0x1E) == 0x1B)
        goto fail;
    scroll_print("\n");
    game_sprint(0xA7);                  /* "Saving Game " */
    if (access(path, 0) < 0 && mkdir(path) < 0)
        goto fail;
    strcat(path, "\\");
    if (!clear_dir(path))
        goto fail;
    if (!blttodrive(desc, "SAVE0\\desc", strlen(desc)))
        goto fail;
    p[2] = 0;
    game_sprint(0xAA);
    if (SavePlayerInv(HomeDir)) {
        game_sprint(0xAA);
        if (SaveLevel(PlayerLevel)) {
            game_sprint(0xAA);
            if (copy_dir(HomeDir, path)) {
                scroll_print("\\0");
                scroll_clear(1);
                return 1;
            }
        }
    }
fail:
    scroll_clear(1);
    return 0;
}

/* Deletes every file in the directory dir (a path ending in a backslash); returns 0 if
   one cannot be deleted. */
char far clear_dir(char *dir)
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

/* Copies the file src to dst through the workspace, 0xF000 bytes at a time. Returns 0
   if either file cannot be opened, there is no workspace, or a write falls short. */
char far copy_file(char *src, char *dst)
{
    int out;
    unsigned ws;
    char far *buf;
    char ok;
    register int in;
    register unsigned n;

    in = -1;
    out = -1;
    ok = 0;
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

/* Copies every file of the directory src into dst; returns 0 if a copy failed. */
unsigned char far copy_dir(char *src, char *dst)
{
    char *send;
    char spath[66];
    char dpath[66];
    struct ffblk ff;
    register char *dend;
    register int r;

    strcpy(spath, src);
    send = spath + strlen(spath);
    strcat(spath, "*.*");
    strcpy(dpath, dst);
    dend = dpath + strlen(dpath);
    for (r = findfirst(spath, &ff, 0); r == 0; r = findnext(&ff)) {
        strcpy(send, ff.ff_name);
        strcpy(dend, ff.ff_name);
        if (!copy_file(spath, dpath))
            break;
    }
    if (r == 0)
        return 0;
    return 1;
}

/* Moves the game from level from to level to: ends fighting and any object held on the
   cursor, runs the leaving hacks, saves from, loads to, runs the arriving hacks. Returns
   0 if the load fails. */
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
        if ((ok = GetLevel(to)) != 0)
            do_level_hacks(to, 0);
        else
            return 0;
    }
    if (ok)
        realDScheck = 1;
    return ok;
}

/* Per-level special cases. mode 0 is arriving, 1 leaving, 3 after a restore:
   - arriving anywhere the creatures are set up; leaving, the critters are moved on as if
     time had passed. With player->armageddon set (bit 4 of byte 0x60, the Armageddon
     spell), arriving only runs clearobj.
   - level 7 (Tybal's lair), while the orb stands: arriving keeps the maximum mana aside
     and leaves none, and reapplies the maze spell; leaving restores the maximum and a
     quarter of it as mana.
   - level 9 (the void): the automap flag is kept aside and turned off on arrival, put back
     when leaving, and turned off after a restore. It is kept in player->saved_mana, the
     byte level 7 keeps the maximum mana in. */
void far do_level_hacks(int level, register int mode)
{
#ifndef __TURBOC__
    port_shared_level_hacks(level,mode);
#endif
    if (player->armageddon && mode == 0) {
        clearobj(0);
        return;
    }
    if (mode == 0)
        init_level_creature_stuff();
    else if (mode == 1)
        update_all_critters_whilst_player_snoozes();
    switch (level) {
    case LEVEL_TYBAL:
        if (!player->orb) {
            if (mode == 0) {
                player->saved_mana = player->max_mana;
                player->max_mana = 0;
                player->play_mana = 0;
                set_maze(player->maze);
            } else if (mode == 1) {
                player->max_mana = player->saved_mana;
                player->play_mana = player->saved_mana >> 2;
            }
        }
        break;
    case LEVEL_VOID:
        if (mode == 0) {
            player->saved_mana = ProbablyAutomapEnabled_dseg_5c99_546;
            ProbablyAutomapEnabled_dseg_5c99_546 = 0;
        } else if (mode == 1)
            ProbablyAutomapEnabled_dseg_5c99_546 = player->saved_mana;
        else if (mode == 3)
            ProbablyAutomapEnabled_dseg_5c99_546 = 0;
        break;
    }
}
