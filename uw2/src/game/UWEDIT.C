/* target: ovr112 */
/* opts: -mm -1 -G -O -Y -d */
/* The program's main and its startup and shutdown: bringing up every subsystem in turn,
   the title screen, the screen dispatcher that switches between the 3D view, the map and
   conversations, resetting the game after death or a restore, moving the player to a new
   position or level, the divide-by-zero exit, and finding the home directory from the
   UWHOME environment variable. The whole of DOS overlay ovr112, in original order.
   Function and global names are the originals from the FM Towns symbol table where it
   has them.

   Start-up: main calls init_world (every subsystem, in the order FM Towns has them), then
   titlescr (cutscene 9, the title), then real_start(1) (MAINMENU.C's start menu, which
   creates or loads a character), then MAINLOOP.C's mainloop until notdone is cleared,
   and free_world on the way out.

   Screens: the game has three screens, numbered by scrnum (0 the 3D view, 1 the automap,
   2 a conversation) and by the input mode scrmode (1, 2 and 4; change_screen maps one to
   the other). editor_dispatch[scrnum] holds each screen's sixteen change handlers, run by
   do_changes for the bits set in changed; newscr runs the old screen's exit (handler 15)
   and the new one's start (handler 0). change_state is or-ed into changed on every pass:
   0x3800 keeps the 3D view's check_physics, display_scr and update_screen (bits 11 to 13)
   running every frame, and 0x1000 the automap's automap_scr.

   Moving the player: anything that teleports the player (traps, spells, moonstones,
   level changes) sets NewPlayerX, NewPlayerY and NewPlayerLevel; new_player_pos, change
   handler 3 of the 3D view, does the move on the next pass of the main loop, with the
   fades chosen by NewPlyFade.

   Death and the end of the game: real_death (from SKILLS.C) ends the current screen,
   resets the player's transient state with reset_game, and reruns the start menu with
   real_start(0).

   Data owned: HomeDir (UWHOME plus SAVE0, where the working LEV.ARK and SCD.ARK copies
   are kept), WorkPath, CurDir, dungeonf (the optional command-line argument), scrmode,
   scrnum, notdone, changed, the NewPlayer* globals, editor_dispatch, change_state,
   NewPlyFade, in_game, npp_func, and the C library's _heaplen, _stklen and _ovrbuffer.

   Name: inferred (TLINK stored the program's name, uwedit.exe, in UW2.EXE, and this file
   has main and the editor's start-up and exit, init_edit and editexit). */

/* name: five functions carry an IDA name in the target table and take the FM Towns name
   of the function at the same position, each confirmed by its body:
     ovr112_2AC  graceful_exit   both call busywaiting_new_options(quit_buttongroup)
     ovr112_2BB  editexit        both clear notdone
     MaybeSetupConversationUI_ovr112_448  free_demscr  demous_player, clear_gamedisp,
                                 hold_scrgr, and it is the 3D screen's exit in editor_dispatch
     ResetTimers_ovr112_45C  reset_times  clears nextSpellTime, lstime, watertime, nextstep
     ovr112_657  do_3d_view      both call establish_view
   FM Towns has nothing for ovr112_389, div_zero_ovr112_661 and ReadCfg_ovr112_839, which
   are DOS only. ovr112_389 keeps its IDA name. */
/* match: the other two provisional names (div_zero_ovr112_661, ReadCfg_ovr112_839) were
   chosen for their keys: Turbo C lists a file's publics by the tools/bssorder.py key of
   each name and TLINK numbers overlay stub entries from the last one listed, so these names
   reproduce the EXE's stub order (the target table keeps IDA's names). */

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

/* This file's uninitialised data, DS:5D1E to DS:5E0B. */
/* match: Turbo C lays _BSS out by its symbol table's order, not by declaration; with
   these names it gives exactly the EXE's layout. The array sizes are the gaps between
   neighbours. */
char HomeDir[0x42];                     /* the save directory, "<UWHOME>\SAVE0\" */
int16 scrmode;
/* The divide-by-zero vector found at startup, DS:5D62, chained to by div_zero after the
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


/* name: elsewhere in the game. Names not confirmed by the map come from FM Towns init_world,
   whose calls run in the same order where DOS has them: init_mem (DOS seg042's first
   function, before mem_setup), check_dirs and check_fds (the stub167 entries for ovr167_421
   and ovr167_489, which test the data directories and open the temporary files),
   init_debug (empty in FM Towns), init_ai, and init_cutscene (empty in both). grfx_init is
   ovr118_0, the FM Towns function before grfx_load_font. pfatal is FM pfatal_, which sets
   *cPerror from cExitMessage as ovr114_15D does. */

/* This file's data, DS:11F4 to DS:12C5, then its strings. */

/* For each screen, sixteen handlers that the main loop runs while the matching bit of
   `changed` is set; 0 starts the screen and 15 ends it. Row 0 is the 3D view (3 moves
   the player, 9 redraws the inventory, 11 to 13 run every frame), row 1 the automap,
   row 2 a conversation. */
void (far *editor_dispatch[3][16])() = {
    { strt_demscr, do_3d_view, 0, (void (far *)())new_player_pos, 0, 0, 0, 0, 0,
      RedispInv, 0, check_physics, display_scr, update_screen, 0, free_demscr },
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
    titlescr();
    CHECKPOINT(2);
#ifndef __TURBOC__
    if (port_shared_test_start()) {
        init_char(0);
        strcpy(player->name, "Coop Test");
        if (!GetLevel(1)) pfatal_code(ERR_READ | 0xC);
        player_setup(0x20,2,1);PlayerFacing=0;PlayerHeading=0;
        change_screen(1);editchng(0x7FFE);
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
   strings, the map, input, UW.CFG and UWHOME (ReadCfg), sound, timers, enough free
   memory (dropping the sound drivers with punt_sound_stuff and trying again if not), the
   graphics library, title screens 5 and 6 and the first music, the graphics, the mouse,
   objects, the 3D view, the player, critter AI, lighting, the save directory (fatal if
   there is no room for a save), the working copies of LEV.ARK and SCD.ARK, and the
   conversation interpreter. */
void far init_world(int argc, char *argv[])
{
    int err;

    int0_save = getvect(0);
    setvect(0, int0_trap);
    int0_ss = _SS;
    int0_sp = _SP;
    SRAND((unsigned)WALL_TIME(NULL));
    init_mem();
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
    if (!(unsigned char)OkEnoughMem_ovr167_463()) {
        punt_sound_stuff(1);
        if (!(unsigned char)OkEnoughMem_ovr167_463())
            first_punt(ERR_LOWMEM | 4);
    }
    if (!grfx_init())
        pfatal_code(ERR_READ | 3);
    display_screen(5, 6);
    load_new_music(MUSIC_THEME, 1);
    seg001_023B_C();
    if ((err = load_all_gr()) != 0)
        pfatal_code(err);
    display_screen(6, 7);
    if (init_mouse() < 0)
        pfatal_code(2);
    if ((err = init_objects()) != 0)
        pfatal_code(err);
    init_txtlib();
    init_3d();
    init_player();
    init_ai();
    init_edit(argc, argv);
    init_lighting();
    init_combinables();
    if (!init_save())
        pfatal("Not enough disk space for save game.$");
    move_initial_files();
    if ((err = init_babl()) != 0)
        pfatal_code(err);
    grfx_clear();
    grfx_quikpal(PAL_GAME);
}

/* Shuts everything down and returns to the starting directory, then empties HomeDir
   with clear_dir (inferred from its name: clear_dir has no source yet), so the working
   LEV.ARK and SCD.ARK copies do not outlive the program. */
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

void far titlescr(void)
{
    while (mouse_get_input() > 3)
        ;
#ifndef __TURBOC__
    if (ENHANCED(ENH_SKIP_INTRO)) return;   /* port only: --enhance skip-intro */
#endif
    show_cutscene(9);
}

/* Records the optional command-line argument in dungeonf, binds Alt+X (0x278, 0x200 is
   KEY_ALT) to the quit dialog and Alt+Q (0x271) to save_screenshot, and marks every
   change bit so the first screen draws in full. scrnum -1 means no screen yet. */
void far init_edit(int argc, char *argv[])
{
    if (argc == 2)
        strcpy(dungeonf, strupr(argv[1]));
    else
        dungeonf[0] = 0;
    strcpy(WorkPath, "DATA\\");
    getcurdir(0, CurDir);
    _input_addkey(KEY_ALT | 'x', 0, 1, (InputFn)graceful_exit);
    _input_addkey(KEY_ALT | 'q', FP_SEG(stdat) + 1, 0xFF, (InputFn)save_screenshot);
    notdone = 1;
    changed = 0x7FFF;
    scrmode = 0;
    scrnum = -1;
    inplist->mode = 0;
}

void far graceful_exit(void)
{
    busywaiting_new_options(&quit_buttongroup);
}

void far editexit(int unused)
{
    notdone = 0;
}

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

void far ovr112_389(int16 *bits)
{
    *bits = changed;
}

/* Start handler of the 3D view: fades out, draws the game screen's frame (screen
   image 4), places the 3D view at x 16, y 182, 208 by 128, renders one frame and fades
   back in with the game palette. */
void far strt_demscr(void)
{
    unsigned char pal[0x300];

    mouse_hide();
    demous_player();
    place_3d_view(0x10, 0xB6, 0xD0, 0x80);
    FAR_COPY((unsigned char far *)pal, palette, 0x300);
    fadeout(pal, 2, 0);
    if (!(unsigned char)display_screen(-1, 4))
        pfatal_code(ERR_READ | 0xB);
    init_gamedisp();
    editchng(0x7DFE);
    FixPlayerEquips();
    CHECKPOINT(4);                      /* the game screen drawn, before its first 3D frame */
    render_FB();
    send_FB();
    mouse_show();
    read_quikpal(PAL_GAME, pal);
    fadein(pal, 2, 0);
    CHECKPOINT(5);                      /* the first game screen, faded in */
}

void far free_demscr(void)
{
    demous_player();
    clear_gamedisp();
    hold_scrgr();
}

void far reset_times(void)
{
    lstime = nextSpellTime = 0;
    nextstep = watertime = 0;
}

/* Clears the player's transient state before a new or restored game: sound effects
   reloaded, timers zeroed, inventory freed, equipment refitted, pitch and heading zero,
   the view effects (combEfflen, tremEfflen, slidEfflen) cleared, the motion state reset, the automap on, the drug effect off,
   fight mode off, the left panel back to 2 (the inventory, inferred) and any special
   cursor mode ended. */
void far reset_game(void)
{
#ifndef __TURBOC__
    port_shared_reset();
#endif
    punt_all_digi_fx();
    load_digi_fx(1);
    load_digi_fx(2);
    load_digi_fx(0xB);
    reset_times();
    Punt_player_inv();
    FixPlayerEquips();
    SET_HEADING(ThePlayer, 0);
    SET_FINEHEAD(ThePlayer, 0);
    PlayerPitch = PlayerBank = PlayerHeading = PlayerFacing = 0;
    combEfflen = tremEfflen = slidEfflen = 0;
    player->motion_state = 0;
    player->automap = 1;
    set_drugged(0);
    reset_scrgr();
    lastscrmode = 0;
    punt_fightmode();
    LeftPanel = 2;
    if (RightButtonThing == 1 || RightButtonThing == 3 || RightButtonThing == 4)
        unforce_mouse_cursor(3);
    RightButtonThing = 0;
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
        show_cutscene(0x103);
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
    fadeout(pal, 2, 0);
    in_game = 1;
    real_start(0);
    scrnum = scr;
    scrmode = 1 << scrnum;
    editor_dispatch[scrnum][0]();
}

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
   loose). If there is none the player dies (hp 0). trap_teleport_data, set by the
   teleport trap, gives the setup mode in bit 0 and, with bit 0x20, a new heading in bits
   2 to 4 (an eighth of a turn each). Then the new square is entered with player_newsq
   and, with NewPlyFade bit 1, the screen fades back in. */
unsigned char far new_player_pos(void)
{
    int16 x, y;

    if (NewPlayerX > 0) {
        if (NewPlyFade & 1) {
            render_FB();
            fadeout3d(curvrad);
        }
        if (PlayerLevel != NewPlayerLevel)
            if (!(unsigned char)ChangeLevel(PlayerLevel, NewPlayerLevel))
                pfatal_code(ERR_READ | 0xC);
        if (npp_func)
            npp_func();
        if (!find_good_x_and_y(ThePlayer, NewPlayerX, NewPlayerY, &x, &y, 0)
            && !find_good_x_and_y(ThePlayer, NewPlayerX, NewPlayerY, &x, &y, 1)) {
            NewPlayerX = 0;
            ThePlayer->hp = 0;
            trap_teleport_data = -1;
            return 0;
        }
        NewPlayerX = x;
        NewPlayerY = y;
        if (trap_teleport_data != -1) {
            player_setup(NewPlayerX, NewPlayerY, (trap_teleport_data & 1) - 1);
            if (trap_teleport_data & 0x20) {
                trap_teleport_data &= ~0x23;
                SET_HEADING(ThePlayer, trap_teleport_data >> 2);
                PlayerHeading = PlayerFacing = trap_teleport_data << 11;
            }
            trap_teleport_data = -1;
        } else
            player_setup(NewPlayerX, NewPlayerY, -1);
        if (NewPlyFade & 2) {
            render_FB();
            fadein3d(curvrad);
        }
        player_newsq((NewPlayerY << 6) + NewPlayerX);
        NewPlayerX = 0;
        if (NewPlyFade & 2)
            editchng(0x7FFE);
    }
    return 1;
}

/* Finds the home directory from the UWHOME environment variable (default "."), reads
   UW.CFG from there (from DATA when UWHOME is unset or "."), and leaves HomeDir as
   UWHOME plus SAVE0, the directory of the game in progress. */
void far ReadCfg_ovr112_839(void)
{
    FILE *fp;
    char path[0x50];
    int i;
    register int j;

    for (i = 0; environ[i] != 0; i++) {
        if (strnicmp("UWHOME", environ[i], 6) == 0) {
            j = 6;
            while (environ[i][j++] != '=')
                ;
            while (environ[i][j++] == ' ')
                ;
            strcpy(HomeDir, environ[i] + --j);
            break;
        }
    }
    if (environ[i] == 0)
        strcpy(HomeDir, ".");
    if (strcmp(HomeDir, ".") == 0)
        strcpy(path, "DATA\\");
    else {
        strcpy(path, HomeDir);
        strcat(path, "\\");
    }
    strcat(path, "uw.cfg");
    fp = fopen(path, "r");
    if (fp != 0) {
        seg016_1E73_2FCB(fp);
        fclose(fp);
    }
    strcat(HomeDir, "\\SAVE0\\");
}

/* Copies the pristine LEV.ARK and SCD.ARK from DATA into SAVE0, where the game in
   progress changes them. */
void far move_initial_files(void)
{
    copy_file("DATA\\", HomeDir, "lev.ark");
    copy_file("DATA\\", HomeDir, "scd.ark");
}
