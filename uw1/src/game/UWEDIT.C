/* target: ovr109 */
/* opts: -mm -1 -G -O -Y -d */
/* The program's main and its startup and shutdown: bringing up every subsystem in turn,
   the title screen, the screen dispatcher that switches between the 3D view, the map and
   conversations, resetting the game after death or a restore, moving the player to a new
   position or level, the divide-by-zero exit, and reading DATA\UW.CFG. The whole of UW1's
   DOS overlay ovr109, in original order. Seeded from UW2Decomp's src/game/UWEDIT.C (UW2's
   ovr112).

   Start-up: main calls init_world (every subsystem, in the order UW2 has them), then
   titlescr (cutscene 9, the title), then real_start(1) (MAINMENU.C's start menu, which
   creates or loads a character), then MAINLOOP.C's mainloop until notdone is cleared,
   and free_world on the way out.

   Screens: the game has three screens, numbered by scrnum (0 the 3D view, 1 the automap,
   2 a conversation) and by the input mode scrmode (1, 2 and 4; change_screen maps one to
   the other). editor_dispatch[scrnum] holds each screen's sixteen change handlers, run by
   do_changes for the bits set in changed; newscr runs the old screen's exit (handler 15)
   and the new one's start (handler 0).

   Moving the player: anything that teleports the player sets NewPlayerX, NewPlayerY and
   NewPlayerLevel; new_player_pos, change handler 3 of the 3D view, does the move on the
   next pass of the main loop, with the fades chosen by NewPlyFade.

   UW1 against UW2: the working directory is the literal SAVE0\ (no HomeDir, no UWHOME),
   and ReadCfg only reads DATA\UW.CFG; main prints two debug lines round titlescr; the
   start-up loads the title pictures from files (LoadBitMap_ovr141_0), does not test
   memory, also blanks a character (init_char), and copies only LEV.ARK; Alt+X quits at
   once (editexit, no graceful_exit); no reset_times or move_initial_files; reset_game
   has no sound effects, pitch, view effects or mushrooms and turns the automap flag on;
   new_player_pos has no trap headings and no player_newsq, and sets PlayerLevel itself;
   the 3D view's handler 10 is SKILLS.C's check_victory.

   Data owned: WorkPath, CurDir, dungeonf (the optional command-line argument), scrmode,
   scrnum, notdone, changed, lastscrmode, the NewPlayer* globals, editor_dispatch,
   change_state, NewPlyFade, in_game, npp_func, and the C library's _heaplen, _stklen and
   _ovrbuffer.

   UW1 has no symbol-bearing build: names are UW2's (the FM Towns symbol table, or
   UW2Decomp's provisional names for its DOS-only functions) where the routine is the
   same; callees with no name yet keep the listing's.
   Name: inferred (UW2's uwedit.exe; this file has main and the editor's start-up and
   exit, init_edit and editexit).

   Entry point: main (the C start-up calls it). Neighbours: MAINLOOP.C (mainloop,
   do_changes), MAINMENU.C (real_start), GAMEWRAP.C (ChangeLevel), every subsystem's
   init_ and free_ functions. */

#include <dos.h>
#include <dir.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "combat.h"
#include "conv.h"
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
#include "view3d.h"
#ifndef __TURBOC__
void port_mouse_look_screen(int leaving, int is_view);     /* ui/MOUSE.C, port only */
#endif

/* Declared in each file that uses it, its own way (no header). */
void far grfx_clear(void);
unsigned char far read_quikpal(int n, void far *dest);
void far player_setup(int x, int y, int how);

/* UW1: the working directory. */
#define HomeDir "SAVE0\\"

void far init_mem(int unused);         /* TMPALLOC.C */
char far grfx_init(void);
/* UW1: start-up and UW.CFG routines with no name yet (the listing's). */

/* This file's uninitialised data, in UW1 DS:565E..5709 (UW2 DS:5D1E..5E0B, which also
   held HomeDir). */
/* match: Turbo C lays _BSS out by its symbol table's order, not by declaration; with
   these names it gives exactly the EXE's layout. The array sizes are the gaps between
   neighbours. */
int16 scrmode;
/* The divide-by-zero vector found at startup, chained to by div_zero after the
   game has shut down. */
/* name: DOS only, so the name is ours, chosen because it lands between scrmode and
   scrnum as the EXE has it. */
static void interrupt (far *int0_save)();
int16 scrnum;
int16 notdone;
char WorkPath[0x42];
int16 changed;
char CurDir[0x44];
char dungeonf[0x12];
int16 lastscrmode;
int16 NewPlayerX, NewPlayerY;
int16 NewPlayerLevel;

/* name: elsewhere in the game, UW2's names where kin pairs the routine with UW2's at the
   same place in init_world (check_dirs, init_sounds, init_timers, seg001_023B_C,
   init_combinables) or symbols.tsv has them; the rest keep the listing's names. */

/* This file's data, in UW1 DS:12EC..13BD, then its strings. */

/* For each screen, sixteen handlers that the main loop runs while the matching bit of
   `changed` is set; 0 starts the screen and 15 ends it. Row 0 is the 3D view (3 moves
   the player, 9 redraws the inventory, 10 to 13 run every frame), row 1 the automap,
   row 2 a conversation. */
void (far *editor_dispatch[3][16])() = {
    { strt_demscr, do_3d_view, 0, (void (far *)())new_player_pos, 0, 0, 0, 0, 0,
      RedispInv, check_victory, check_physics, display_scr, update_screen, 0, free_demscr },
    { 0, AutoMap, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, automap_scr, 0, 0, ExitAutoMap },
    { strt_converse, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, free_converse }
};
int16 change_state[3] = { 0x3800, 0x1000, 0 };  /* bits kept set per screen */
char NewPlyFade = 3;                    /* new_player_pos: 1 fade out, 2 fade in */
char in_game = 0;                       /* set once the start menu has been left */
uint16 _heaplen = 0x0C00;
uint16 _stklen = 0x1000;
uint16 _ovrbuffer = 0x300;
void (far *npp_func)() = 0;             /* run by new_player_pos after a level change,
                                           before the player is placed (the moonstone
                                           spells set it to do_mstone) */

int main(int argc, char *argv[])
{
    init_world(argc, argv);
    CHECKPOINT(1);
    dprintf("before titlescr\n");
    titlescr();
    CHECKPOINT(2);
    dprintf("after titlescr\n");
#ifndef __TURBOC__
    if (port_shared_test_start()) {
        if (getenv("UW_COOP_TEST_RESUME")) {
            int slot = atoi(getenv("UW_COOP_TEST_RESUME"));
            if (slot < 1 || slot > 4) slot = 1;
            if (!RestoreGame(slot)) pfatal_code(ERR_READ | 0xC);
        } else {
            if (getenv("UW_COOP_BOT_CHARGEN")) {
                if (!create_player()) pfatal_code(ERR_READ | 0xC);
            } else {
                init_char(0);
                strcpy(player->name, "Coop Test");
            }
            if (!GetLevel(1)) pfatal_code(ERR_READ | 0xC);
            player_setup(0x20,2,1);PlayerFacing=0;PlayerHeading=0;
        }
        change_screen(MODE_GAME);editchng(0x7FFE);
    } else
#endif
    real_start(1);
    CHECKPOINT(3);
    in_game = 1;
    mainloop();
    free_world(1);
    return 0;
}

/* Brings up every subsystem in order, failing with a fatal error code if one cannot
   start: the divide-by-zero trap, memory, the data directories and temporary files,
   strings, the map, input, UW.CFG (ReadCfg), sound, timers, the graphics library, the
   title pictures PRES1.BYT and PRES2.BYT and the first music, the graphics, the mouse,
   objects, the 3D view, the player, critter AI, a blank character, lighting, the save
   directory (fatal if there is no room for a save), the working copy of LEV.ARK, and
   the conversation interpreter. */
void far init_world(int argc, char *argv[])
{
    register int err;

    int0_save = getvect(0);
    setvect(0, int0_trap);
    int0_ss = _SS;
    int0_sp = _SP;
    init_mem(2);
    check_dirs();
    check_fds();
    init_strings();
    Map_Init();
    init_input();
    init_debug();
    ReadCfg_ovr112_839();
    init_sounds();
    init_timers();
    init_cutscene();
    memcheck();
    if (!grfx_init())
        pfatal_code(ERR_READ | 3);
    LoadBitMap_ovr141_0(5, "DATA\\pres1.byt");
    load_new_music(MUSIC_THEME, 1);
    seg001_023B_C();
    if ((err = load_all_gr()) != 0)
        pfatal_code(err);
    LoadBitMap_ovr141_0(6, "DATA\\pres2.byt");
    if (init_mouse() < 0)
        pfatal_code(2);
    if ((err = init_objects()) != 0)
        pfatal_code(err);
    init_txtlib();
    init_3d();
    init_player();
    init_ai();
    init_char(0);
    init_edit(argc, argv);
    init_lighting();
    init_combinables();
    if (!init_save())
        pfatal("Not enough disk space for save game.$");
    copy_file("DATA\\lev.ark", "SAVE0\\lev.ark");
    if ((err = init_babl()) != 0)
        pfatal_code(err);
    grfx_clear();
    grfx_quikpal(PAL_GAME);
}

/* Shuts everything down and returns to the starting directory, then empties SAVE0 with
   clear_dir, so the working LEV.ARK copy does not outlive the program. */
void far free_world(char flag)
{
#ifndef __TURBOC__
    port_shared_reset();
#endif
    grfx_close();
    chdir(CurDir);
    free_input();
    free_mem();
    free_scrgr();
    free_timers();
    free_sounds();
    free_strings();
    if (flag)
        ;                               /* FM Towns saves the gamma setting here */
    clear_dir(HomeDir);
}

/* Waits for the mouse buttons to be released, then plays cutscene 9, the title. */
void far titlescr(void)
{
    while (mouse_get_input() > 3)
        ;
#ifndef __TURBOC__
    if (ENHANCED(ENH_SKIP_INTRO)) return;   /* port only: --enhance skip-intro */
#endif
    runcutscene(9);
}

/* Records the optional command-line argument in dungeonf, binds Alt+X (0x278, 0x200 is
   KEY_ALT) to the quit dialog and Alt+Q (0x271) to save_screenshot, and marks every
   change bit so the first screen draws in full. scrnum -1 means no screen yet. */
void far init_edit(int argc, char *argv[])
{
    if (argc == 2)
        strcpy(dungeonf, strupr(argv[1]));
    else
        strcpy(dungeonf, "LEVEL1");
    strcpy(WorkPath, "DATA\\");
    getcurdir(0, CurDir);
    _input_addkey(KEY_ALT | 'x', 0, 0xBD, (InputFn)editexit);
    _input_addkey(KEY_ALT | 'q', FP_SEG(stdat) + 1, 0xFF, (InputFn)save_screenshot);
    notdone = 1;
    changed = 0x7FFF;
    scrmode = 0;
    scrnum = -1;
    inplist->mode = 0;
}

/* Alt+X: ends the main loop at once (notdone 0). */
void far editexit(int unused)
{
    notdone = 0;
}

/* Empties the level's objects (Map_ObjFix) and redraws; the player is in no tile
   (GrSq -1). The Armageddon spell (SPELLS.C) calls it, and do_level_hacks on every arrival
   afterwards. */
void far clearobj(int unused)
{
    Map_ObjFix();
    editchng(2);
    GrSq = -1;
}

/* Sets the input mode and picks the screen's handler table: mode 2 the automap,
   4 a conversation, anything else (1, the game) the 3D view. */
void far change_screen(int mode)
{
#ifndef __TURBOC__
    port_shared_modal(mode);
    /* port only: --enhance mouse-look off while the 3D view gives way, and back as it was when
       it returns: the input modes with a bit of 0x11, as UltimaHacks' setInterfaceMode tests */
    port_mouse_look_screen(1, (inplist->mode & 0x11) != 0);
#endif
    switch (scrmode = inplist->mode = mode) {
    case 2:
        scrnum = 1;
        break;
    case 4:
        scrnum = 2;
        break;
    case 1:
        scrnum = 0;
        break;
    default:
        scrnum = 0;
        break;
    }
#ifndef __TURBOC__
    port_mouse_look_screen(0, (inplist->mode & 0x11) != 0);
#endif
}

/* Switches screens: runs the current screen's exit handler, then the new one's start
   handler. A negative mode returns to the screen before the last switch (lastscrmode).
   For any screen but the 3D view every change bit but 0 is set, to redraw in full. */
void far newscr(int mode)
{
    if (scrnum != -1 && editor_dispatch[scrnum][15])
        editor_dispatch[scrnum][15]();
    if (mode >= 0)
        lastscrmode = scrmode;
    else
        mode = lastscrmode;
    change_screen(mode);
    if (editor_dispatch[scrnum][0])
        editor_dispatch[scrnum][0]();
    if (mode != 1)
        editchng(0x7FFE);
}

/* Copies the change bits into *bits. No matched source calls it, though it has an
   overlay stub. */
void far ovr112_389(int16 *bits)
{
    *bits = changed;
}

/* Start handler of the 3D view: fades out, draws the game screen's frame (screen
   image 4), places the 3D view at x 0x34, y 0xB4, 0xAC by 0x71, renders one frame and fades
   back in with the game palette. */
void far strt_demscr(void)
{
    unsigned char pal[0x300];

    mouse_hide();
    demous_player();
    place_3d_view(0x34, 0xB4, 0xAC, 0x71);
    FAR_COPY((unsigned char far *)pal, palette, 0x300);
    fadeout(pal, 2);
    if (!LoadBitMap_ovr141_0(-1, "DATA\\main.byt"))
        pfatal_code(ERR_READ | 0xB);
    init_gamedisp();
    editchng(0x7DFE);
    FixPlayerEquips();
    CHECKPOINT(4);                      /* the game screen drawn, before its first 3D frame */
    render_FB();
    send_FB();
    mouse_show();
    read_quikpal(PAL_GAME, pal);
    fadein(pal, 2);
    CHECKPOINT(5);                      /* the first game screen, faded in */
}

/* The 3D view's exit handler: its mouse regions, the game screen's panels and the screen graphics are put away. */
void far free_demscr(void)
{
    demous_player();
    clear_gamedisp();
    hold_scrgr();
}

/* Clears the player's transient state before a new or restored game: inventory freed,
   equipment refitted, heading zero, the automap flag on, fight mode off, the left panel
   back to 2 (the inventory, inferred), lstime and nextSpellTime zeroed and any
   special cursor mode ended. */
void far reset_game(void)
{
#ifndef __TURBOC__
    port_shared_campaign_reset();
#endif
    Punt_player_inv();
    FixPlayerEquips();
    SET_HEADING(ThePlayer, 0);
    SET_FINEHEAD(ThePlayer, 0);
    PlayerHeading = PlayerFacing = 0;
    ProbablyAutomapEnabled_dseg_5c99_546 = 1;
    reset_scrgr();
    lastscrmode = 0;
    punt_fightmode();
    LeftPanel = 2;
    if (RightButtonThing == 1 || RightButtonThing == 3 || RightButtonThing == 4)
        unforce_mouse_cursor(3);
    RightButtonThing = 0;
    lstime = nextSpellTime = 0;
    if (GameInputMode != 0) {
        if (GameInputMode < 4) {
            unforce_mouse_cursor(3);
            CursorObjPtr = 0;
            GameInputMode = 0;
        } else {
            GameInputMode = 0;
            attach_eye(1);
        }
    }
}

/* Ends the game in progress and goes back to the start menu. how 1 is death: the
   panels are reset and cutscene 0x103 plays first (any number from 0x100 is drawn inside
   the 3D view window). SKILLS.C calls it with how 0 when the game is won, after the
   credits. real_start(0) then lets the player load or create a character, and the
   screen that was current is started again. */
void far real_death(int how)
{
    unsigned char pal[0x300];
    int scr;

    pmouseHandled = 0;
    mouse_freereign();
    while (mouse_get_input() > -1)
        ;
    if (how == 1) {
        set_screen_frame(2, 0);
        set_compass();
        set_flask(0);
        runcutscene(0x103);
        scroll_clear(1);
    }
    editor_dispatch[scrnum][15]();
    scrmode = inplist->mode = 0;
    scr = scrnum;
    scrnum = -1;
    in_game = 0;
    if (how == 1)
        mouse_hide();
    reset_game();
    FAR_COPY((unsigned char far *)pal, palette, 0x300);
    fadeout(pal, 2);
#ifndef __TURBOC__
    if (getenv("UW_COOP_TEST_CAMPAIGN")) fprintf(stderr, "SHARED CAMPAIGN ending returned to menu how=%d\n", how);
#endif
    real_start(0);
    in_game = 1;
    scrnum = scr;
    scrmode = 1 << scrnum;
    editor_dispatch[scrnum][0]();
}

/* The 3D view's change handler 1: draws the view (establish_view). */
void far do_3d_view(void)
{
    establish_view();
}

/* The divide-by-zero handler's exit: sets the DOS exit message, shuts the game down and
   chains to the original INT 0 vector. */
void far div_zero_ovr112_661(void)
{
    char msg[0x50];

    strcpy(msg, "Underworld exiting, divide by zero.\r\n$");
    *cPerror = FP_OFF(cExitMessage);
    FAR_COPY(cExitMessage, (char far *)msg, strlen(msg));
    free_world(1);
    (*int0_save)();
}

/* 3D view change handler 3: carries out a pending teleport (NewPlayerX > 0). Fades
   out if NewPlyFade bit 0 is set, changes level when NewPlayerLevel differs, runs
   npp_func, then finds a free square near the target (find_good_x_and_y, strict then
   loose). If there is none the player dies (hp 0). Then the player is placed, and with
   NewPlyFade bit 1 the screen fades back in. */
unsigned char far new_player_pos(void)
{
    int16 x, y;

#ifndef __TURBOC__
    if (port_shared_guest_travel()) return 1;
#endif

    if (NewPlayerX > 0) {
        if (NewPlyFade & 1) {
            render_FB();
            fadeout3d(curvrad);
        }
        if (PlayerLevel != NewPlayerLevel) {
            if (!ChangeLevel(PlayerLevel, NewPlayerLevel))
                pfatal_code(ERR_READ | 0xC);
            PlayerLevel = NewPlayerLevel;
        }
        if (npp_func)
            npp_func();
        if (!find_good_x_and_y(ThePlayer, NewPlayerX, NewPlayerY, &x, &y, 0)
            && !find_good_x_and_y(ThePlayer, NewPlayerX, NewPlayerY, &x, &y, 1)) {
            NewPlayerX = 0;
            ThePlayer->hp = 0;
            return 0;
        }
        NewPlayerX = x;
        NewPlayerY = y;
        player_setup(NewPlayerX, NewPlayerY, 1);
        if (NewPlyFade & 2) {
            render_FB();
            fadein3d(curvrad);
        }
        NewPlayerX = 0;
        if (NewPlyFade & 2)
            editchng(0x7FFE);
    }
    return 1;
}

/* Reads DATA\UW.CFG: seg014_1DC5_1D0D reads from it first, then cuts_skipline reads a line
   of up to 99 characters. */
/* name: UW2Decomp's provisional name for UW2's UW.CFG reader; its key also puts it in
   UW1's stub order. */
void far ReadCfg_ovr112_839(void)
{
    register FILE *fp;

    fp = fopen("DATA\\uw.cfg", "r");
    if (fp != 0) {
        seg014_1DC5_1D0D(fp);
        cuts_skipline(fp);
        fclose(fp);
    }
}
