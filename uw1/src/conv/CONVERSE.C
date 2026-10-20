/* target: ovr100 */
/* opts: -mm -1 -G -O -Y -d */
/* Conversations: starting one (TalkTo, strt_converse), loading the script and registering
   the built-in functions (Converse), the menus, the say, respond and print built-ins, and
   the inventory built-ins a script uses to look at, give and take objects while bartering:
   the whole of UW1's DOS overlay ovr100 (UW2's ovr103), in original order.

   The flow: TalkTo checks the NPC will talk, sets talking_to and cnv_id and switches to
   screen 4. The screen's start function, strt_converse, draws the conversation screen
   (the "converse" art, both portraits and names), sets up the trade slots (barter_init,
   BARTER.C) and calls Converse, which loads the script into the screen buffer
   (load_script, BABL.C), binds the built-ins with bab_fun, hands over the variables,
   runs the script (babl_run) and takes them back. Then newscr(1) returns to the game,
   whose screen exit free_converse puts things back.

   A menu (babl_menu, babl_fmenu) prints numbered options in the player's scroll and runs
   converse_event_loop until conv_play_menu, the handler for the keys and for a click on
   the scroll, records the choice.

   UW1 against UW2: TalkTo has its own cases (item 0x157 is a shrine, ovr143; item 0x16E,
   a picture, answers "There is no reaction from the princess." when its texture's entry
   is 8), lets whoami 0x16, 0x8E and 0xE7 always talk, and has no goal-15 or TimeStop
   refusal; the archive is checked by name (check_arc). strt_converse builds the screen
   from art and draws the frame itself, and free_converse does no catching up, teleport
   or clean-up after the script. UW1 has no babl_hack, give_all_stuff, set_sequence,
   transform_talker, x_clock, x_exp, teleport or switch_pic built-ins, four trade slots a
   side rather than six, and gives the NPC back the main scroll (do_main_scroll) where
   UW2 shows the player's.

   Data owned here: talking_to, cnv_id, conv_buffer and the screen and menu state below.
   Neighbours: started from INTERACT.C's talk command, USEITEMS.C (Garamon's bones),
   WORLDEV.C and TRIGGER.C (talking doors and waiting critters), AI.C (critters that come
   to talk); BABL.C runs the script, BARTER.C and BABLHACK.C supply most built-ins,
   CONVVARS.C hands the variables over, and UWEDIT.C's screen table calls strt_converse and
   free_converse.

   Name: UW2's (map/filenames.tsv: conversations, TalkTo, strt_converse, Converse). */
/* name: The function names are UW2's (the FM Towns originals, or UW2Decomp's
   provisional names for FM Towns statics), the routines being the same. */
/* match: UW2's provisional names reproduce UW1's stub order too (checked against the
   EXE's 31 stub entries). */
/* Each built-in gets a far pointer to the top of the conversation stack, stack[0] being
   the argument count: stack[-1] is UW-Formats' arg1 (the last value pushed before the
   count), stack[-2] arg2 and so on. Each argument is the address of a conversation
   variable, which getmem reads and babl_setmem writes. */

#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include "conv.h"
#include "event.h"
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* UW1: declarations the headers do not have (or have in UW2's form). */
void far mantra_advance(int how);                   /* talking to a shrine */
int far check_arc(char *file, int block);      /* 1 if the block is there, 0 if not, -1 if the file cannot be read */
extern int16 w64_types[];                  /* a word for each texture of the level */
char far invRemoveObject(struct Object far *obj, int qty);
void far grfx_load_font(char *name);           /* UW2: grfx_quikfont(int) */

                     /* the level's music again (UW2:
                                                   set_random_walking_music(-1)) */

/* This file's own uninitialised data, DS:4902..4A2D, in its _BSS with talking_to (the
   addresses in the comments are UW2's; UW1 has the same layout 0x106 bytes higher). */
/* name: The FM Towns build keeps all of it static, so none of it has an original name and
   it is static here, with provisional names. */
/* match: The names were chosen so that Turbo C's name order (tools/bssorder.py) lays it
   out as UW2 has it: RightPanelSaved 58, convoPics 187, convo_mode 259, convoScreen 331,
   convoWorkspace 507, convo_askstr 515, convoContinue 587, convo_facedata 643,
   babl_unref4854 650, convo_line_option 795, babl_choice 826, babl_opts 834,
   babl_display 874, talking_to 916, bablOptionIds 962, bablOptCount 978. Nothing reads or
   writes DS:4854..4857, but it lies between two of this file's variables. (DS:4928..4929,
   also never used, is left out: it may as well be ovr104's.) */
static unsigned char RightPanelSaved;           /* DS:47FC, the right panel before the conversation */
static char far *convoPics[17];                 /* DS:47FE, the portrait images */
static int16 convo_mode;                        /* DS:4842 */
static char far *convoScreen;                   /* DS:4844 */
static int16 convoWorkspace;                    /* DS:4848 */
static char far *convo_askstr;                  /* DS:484A */
static int16 convoContinue;                     /* DS:484E */
static char far *convo_facedata;                /* DS:4850, the portrait data */
static char babl_unref4854[4];                  /* DS:4854, never used */
static int16 convo_line_option[10];             /* DS:4858 */
static int16 babl_choice;                       /* DS:486C, the selected option */
static char far *babl_opts[20];                 /* DS:486E */
static char far *babl_display[20];              /* DS:48BE */
struct Object far *talking_to;                  /* DS:490E, FM Towns name */
static int16 bablOptionIds[10];                 /* DS:4912 */
static int16 bablOptCount;                      /* DS:4926 */

/* Initialised data, in the order of the EXE (DS:EA6..EBD). */
static unsigned char convo_say_flag = 0;    /* set by respond, cleared by say */
/* UW1: a word nothing reads (DS:EA7). */
static int16 unref_EA7 = -1;
static int16 babl_ask_handle = 0;           /* the string babl_ask returns */
char far *conv_buffer = 0;
static char cnv_file[] = "DATA\\cnv.ark";
uint16 cnv_id = 0;

#ifndef __TURBOC__
/* Read-only view of the displayed choices for the opt-in campaign driver. */
int port_bot_menu_count(void)
{
    return scrnum == 2 && convoContinue && convo_mode && menus_active ? bablOptCount - 1 : 0;
}
const char *port_bot_menu_option(int option)
{
    return option > 0 && option < bablOptCount ? babl_display[option] : "";
}
#endif

/* Start a conversation with thing, if it will talk. Item 0x157 is handed to mantra_advance
   (talking to a shrine); item 0x16E gets "There is no reaction from the princess." when
   its texture's entry in w64_types is 8, and nothing otherwise. Anything not a
   creature gets a refusal. Whoami 0x16, 0x8E and 0xE7 always talk; else a critter talks
   when its whoami is not 0xFF and it is either an ally, or friendly (attitude not 0) and
   not attacking the player (goal 5, 6 or 9 with target 1), or its goal is 10. The
   conversation is whoami's slot, or 0x100 + the creature class for whoami 0, and must
   exist in cnv_file. */
void far TalkTo(struct Object far *thing)
{
    unsigned char who, subclass;
    if (OBJ_ITEM(thing) == ITEM_SHRINE) {
        mantra_advance(0);
        return;
    }
    if (OBJ_ITEM(thing) == ITEM_TMAP_C) {
        if ((w64_types[thing->ol.f.owner] & 0xFF) == TERR_PRINCESS)
            game_sprint(0x110);  /* "There is no reaction from the princess." */
        return;
    }
    if (OBJ_MAJOR(thing) != MAJOR_CREATURE) {
        scroll_print(get_string(STR_CONV));  /* "You cannot talk to that!\n" */
        return;
    }
    talking_to = thing;
    who = talking_to->whoami;
    subclass = OBJ_INMAJOR(talking_to);
    if (who != 0x16 && who != 0x8E && who != 0xE7 &&
        (((OBJ_GOAL(talking_to) == GOAL_ATTACK || OBJ_GOAL(talking_to) == GOAL_FLEE ||
           OBJ_GOAL(talking_to) == GOAL_CORNERED) &&
#ifndef __TURBOC__
          port_shared_player_index(OBJ_GTARG(talking_to)) ||
#else
          OBJ_GTARG(talking_to) == 1 ||
#endif
          OBJ_ATTITUDE(talking_to) == ATT_HOSTILE) && !OBJ_ALLY(talking_to) || who == 0xff) &&
        OBJ_GOAL(talking_to) != GOAL_TALK) {
        scroll_print(get_string(STR_CONV | 1));  /* "You get no response.\n" */
        return;
    }
    if (who == 0) cnv_id = (unsigned)subclass + 0x100;
    else cnv_id = who;
    if (check_arc(cnv_file, cnv_id) <= 0) {
        scroll_print(get_string(STR_CONV | 1));  /* "You get no response.\n" */
        return;
    }
    newscr(4);
}

/* name: adr_convpic and move_convpic are FM Towns names (TalkTo, adr_convpic,
   move_convpic, strt_converse in the same order); the code matches the FM Towns
   functions. */

/* gronk_gr's allocator: the next n bytes of the portrait buffer */
char far * far adr_convpic(int n)
{
    char far *old = convo_facedata;
    convo_facedata += n;
    return old;
}

/* gronk_gr's loader: note where portrait number `which` landed, past its 5-byte header */
char far move_convpic(char far *image, int ok, int which)
{
    convoPics[which] = image + 5;
    if (ok == 0) return 0;
    return 1;
}

/* The conversation screen's start function: the frame and panels from the "converse"
   art, the player's head (portrait female * 5 + body of "heads") and name, and the NPC's
   head (whoami - 1 of "charhead", else its class of "genhead") and name. If the screen
   art or the player's head cannot be loaded, the screen is closed again. */
void far strt_converse(void)
{
    unsigned char who;
    char name[0x28];
    update_sprites();
    if ((convoWorkspace = get_workspace()) == 0) return;
    set_new_music(MUSIC_MAPS);
    change_music_maybe();
    convoScreen = MK_FP(convoWorkspace, 0);
    mouse_hide();
    grSoftPageFlip();
    set_the_window(0, 0xC7, 0x13F, 0);
    copy_visible_to_hidden();
    convo_facedata = convoScreen;
    if (!gronk_gr("converse", 0, -1, (ArtAllocFn)adr_convpic, (ArtMoveFn)move_convpic)) {
        free_converse();
        return;
    }
    set_the_color(0xF1);
    rectangle(0x2A, 0xC7, 0xC2, 0x99);
    show(0x2B, 0xC7, convoPics[0], 9, 0x5E, 0, 0);
    show(0x8B, 0xC7, convoPics[0], 9, 0x5E, 0, 0);
    show(0x52, 0xBE, convoPics[1], 0x26, 0x37, 0, 0);
    show(0x8B, 0xBE, convoPics[1], 0x26, 0x37, 0, 0);
    show(0x2B, 0xBE, convoPics[2], 0x26, 0x26, 0, 0);
    show(0xC3, 0xBE, convoPics[2], 0x26, 0x26, 0, 0);
    show(0x2A, 0x97, convoPics[3], 0xA, 0xC0, 0, 0);
    show(0x2A, 0x49, convoPics[4], 0xA, 0xC0, 0, 0);
    show(0xEC, 0xC0, convoPics[5], 0x72, 0x54, 0, 0);
    set_the_color(0xF1);
    uhline(0x34, 0x98, 0xDC);
    RightPanelSaved = RightPanel;
    RightPanel = 0;
    inv_refresh = 0;
    BeginInventory();
    RedispInv();
    inv_refresh = 1;
    do_play_scroll();
    scroll_clear(0);
    do_npc_scroll();
    scroll_clear(0);
    grfx_load_font("font5x6p.sys");
    *foreground_color = 0x65;
    *background_color = 0x65;
    convo_facedata = convoScreen;
    if (!gronk_gr("heads", player->female * 5 + player->body, 1, (ArtAllocFn)adr_convpic, (ArtMoveFn)move_convpic)) {
        free_converse();
        return;
    }
    Transparency = 1;
    show(0xC5, 0xBC, convoPics[0], 0x22, 0x22, 0, 0);
    Transparency = 0;
    str_copy(name, get_string(player_name_handle));
    string_to_screen(name, 0x90, 0xC5);
    convo_facedata = convoScreen;
    who = talking_to->whoami;
    if (who > 0) {
        if (!gronk_gr("charhead", who - 1, 1, (ArtAllocFn)adr_convpic, (ArtMoveFn)move_convpic))
            gronk_gr("genhead", OBJ_INMAJOR(talking_to), 1, (ArtAllocFn)adr_convpic, (ArtMoveFn)move_convpic);
    } else
        gronk_gr("genhead", OBJ_INMAJOR(talking_to), 1, (ArtAllocFn)adr_convpic, (ArtMoveFn)move_convpic);
    Transparency = 1;
    show(0x2D, 0xBC, convoPics[0], 0x22, 0x22, 0, 0);
    Transparency = 0;
    if (get_name(name, (struct Object far *)talking_to, 0, 0))
        string_to_screen(name, 0x30, 0xC5);
    grPageFlip();
    grSoftPageFlip();
    conv_buffer = convoScreen;
    barter_init();
    do_main_scroll();
    mouse_show();
    set_workspace();
    pmouseHandled = 0;
    mouse_freereign();
    Converse(talking_to->whoami, OBJ_INMAJOR(talking_to));
    newscr(1);
}

/* The conversation screen's exit function: release the screen buffer, give back what is
   left in the trade slots (end_barter), restart the level's music and restore the right
   panel and the main scroll. */
void far free_converse(void)
{
    if (convoWorkspace) {
        release_workspace();
        end_barter();
    }
    seg014_1DC5_15C5();
    RightPanel = RightPanelSaved;
    do_main_scroll();
}

/* name: FM Towns do_escape_key_, also empty, at this position */
void far do_escape_key(void) { }

/* Load and run the conversation (who and subclass are not used; cnv_id already says
   which). The script's heap is the conversation screen buffer from offset 0x400. An NPC
   without an inventory gets one generated first. At the end the last line stays for
   0x1F4 (scroll_wait units) when the player spoke last, and none when the NPC spoke last
   or turned hostile. */
void far Converse(unsigned char who, int subclass)
{
    register int wait_time;
    if (load_script(cnv_file, convoScreen + 0x400) < 0) {
        scroll_print(get_string(STR_CONV | 1));  /* "You get no response.\n" */
        return;
    }
    bab_fun("babl_menu", (BablFn)conv_choice_ovr103_A13);
    bab_fun("babl_fmenu", (BablFn)conv_fmenu_ovr103_BF2);
    bab_fun("say", (BablFn)npc_say);
    bab_fun("respond", (BablFn)play_respond);
    bab_fun("get_quest", (BablFn)get_quest);
    bab_fun("set_quest", (BablFn)set_quest);
    bab_fun("sex", (BablFn)sex);
    bab_fun("babl_ask", (BablFn)getInputText_ovr103_1117);
    bab_fun("print", (BablFn)conv_print);
    bab_fun("show_inv", (BablFn)conv_check_inv);
    bab_fun("give_to_npc", (BablFn)conv_give_inv);
    bab_fun("find_inv", (BablFn)conv_find_inv);
    bab_fun("take_from_npc", (BablFn)conv_take_inv);
    bab_fun("take_id_from_npc", (BablFn)conv_take_inv_id);
    bab_fun("identify_inv", (BablFn)conv_inv_name);
    bab_fun("do_offer", (BablFn)do_offer);
    bab_fun("do_demand", (BablFn)do_demand);
    bab_fun("do_decline", (BablFn)do_decline);
    bab_fun("do_judgement", (BablFn)do_judgement);
    bab_fun("end_barter", (BablFn)end_barter);
    bab_fun("setup_to_barter", (BablFn)setup_to_barter);
    bab_fun("pause", (BablFn)conv_pause_ovr103_10DD);
    bab_fun("set_likes_dislikes", (BablFn)npc_likes_dislikes);
    bab_fun("do_inv_create", (BablFn)conv_inv_create);
    bab_fun("do_inv_delete", (BablFn)conv_inv_delete);
    bab_fun("check_inv_quality", (BablFn)check_inv_quality);
    bab_fun("set_inv_quality", (BablFn)set_inv_quality);
    bab_fun("count_inv", (BablFn)count_inv);
    bab_fun("gronk_door", (BablFn)gronk_door);
    bab_fun("set_attitude", (BablFn)set_attitude);
    bab_fun("set_race_attitude", (BablFn)set_race_attitude);
    bab_fun("take_from_npc_inv", (BablFn)take_from_npc_inv);
    bab_fun("add_to_npc_inv", (BablFn)add_to_npc_inv);
    bab_fun("place_object", (BablFn)place_object);
    bab_fun("remove_talker", (BablFn)remove_talker);
    bab_fun("x_skills", (BablFn)x_skills);
    bab_fun("x_traps", (BablFn)x_traps);
    bab_fun("x_obj_stuff", (BablFn)x_obj_stuff);
    bab_fun("x_obj_pos", (BablFn)x_obj_pos);
    bab_fun("find_barter", (BablFn)find_barter);
    bab_fun("find_barter_total", (BablFn)find_barter_total);
    bab_fun("give_ptr_npc", (BablFn)give_ptr_npc);
    setup_converse_data(talking_to);
    convo_askstr = bab_malloc(0xa0L);
    if (!OBJ_HAS_INV(talking_to))
        generate_inventory(talking_to);
    babl_run();
    wait_time = 0x1f4;
    if (update_converse_data(talking_to) != 0 || convo_say_flag == 0)
        wait_time = 0;
    scroll_wait(wait_time, 0);
}

/* Wait for a menu choice, keeping music and animation going; when menus_active has been
   cleared it reprints the options. */
/* name: FM Towns converse_event_loop_ is this loop (loop_music_maybe, do_changes, the
   menu, input_dispatch); Converse registers babl_menu and babl_fmenu as the two statics
   after it, converse_event_loop_+0xD8 and +0x27C. */
void far converse_event_loop(void)
{
    int i;
    char line[0xa0];
    while (convoContinue) {
        loop_music_maybe();
        if (changed) do_changes();
        if (menus_active != 0) goto dispatch;
        {
            scroll_wait(0x1f4, 0);
            do_play_scroll();
            scroll_clear(1);
            set_workspace();
            i = 1;
            while (i < bablOptCount) {
                line[0] = i + '0';
                line[1] = '.';
                line[2] = ' ';
                str_copy(line + 3, babl_display[i]);
                strcat(line, "\n");
                scroll_print(line);
                set_workspace();
                i++;
            }
        }
        do_main_scroll();
        menus_active = 1;
dispatch:
        input_dispatch(inplist);
        set_workspace();
    }
}

/* babl_menu(arg1 array): show the strings of a 0-terminated array of string ids as a
   numbered menu and return the number chosen (from 1). Only 1..5 have keys. */
int far conv_choice_ovr103_A13(int16 far *stack)
{
    int i, base, string_no, used;
    char line[0xa0];
    register int row;
    register int lines;
    convo_mode = 1;
    bablOptCount = 1;
    used = lines = 0;
    base = stack[-1];
    string_no = getmem(base);
    while (string_no != 0) {
        babl_opts[bablOptCount] = get_string(string_no);
        babl_display[bablOptCount] = convert_string(babl_opts[bablOptCount]);
#ifndef __TURBOC__
        if (getenv("UW_COOP_TEST_CAMPAIGN")) fprintf(stderr, "SHARED MENU %u option %d: %s\n", cnv_id, bablOptCount, babl_display[bablOptCount]);
#endif
        if (babl_display[bablOptCount] == babl_opts[bablOptCount]) {
            babl_display[bablOptCount] = bab_malloc((int32)(str_len(babl_opts[bablOptCount]) + 1));
            str_copy(babl_display[bablOptCount], babl_opts[bablOptCount]);
        }
        bablOptionIds[bablOptCount] = string_no;
        bablOptCount++;
        string_no = getmem(base + bablOptCount - 1);
    }
    do_play_scroll();
    scroll_clear(1);
    set_workspace();
    for (i = 0; i < 10; i++) convo_line_option[i] = -1;
    for (i = 1; i < bablOptCount; i++) {
        line[0] = i + '0';
        line[1] = '.';
        line[2] = ' ';
        str_copy(line + 3, babl_display[i]);
        str_cat(line, "\n");
        lines = scroll_print(line);
        set_workspace();
        for (row = used; row <= lines; row++) convo_line_option[row] = i;
        used = lines + 1;
    }
    do_main_scroll();
    convoContinue = 1;
    menus_active = 1;
    converse_event_loop();
    return babl_choice;
}

/* babl_fmenu(arg1 strings, arg2 flags): the same, showing only the strings whose flag
   is non-zero, and returning the chosen string's id rather than its number. */
int far conv_fmenu_ovr103_BF2(int16 far *stack)
{
    int i, base, string_no, value, value_base, used;
    char line[0xa0];
    register int row;
    register int lines;
    convo_mode = 1;
    bablOptCount = 1;
    used = 0;
    base = stack[-1];
    value_base = stack[-2];
    string_no = getmem(base);
    value = getmem(value_base);
    i = 1;
    while (string_no != 0) {
        if (value != 0) {
            babl_opts[bablOptCount] = get_string(string_no);
            babl_display[bablOptCount] = convert_string(babl_opts[bablOptCount]);
#ifndef __TURBOC__
            if (getenv("UW_COOP_BOT_HOME")) fprintf(stderr, "SHARED MENU %u option %d: %s\n", cnv_id, bablOptCount, babl_display[bablOptCount]);
#endif
            if (babl_display[bablOptCount] == babl_opts[bablOptCount]) {
                babl_display[bablOptCount] = bab_malloc((int32)(str_len(babl_opts[bablOptCount]) + 1));
                str_copy(babl_display[bablOptCount], babl_opts[bablOptCount]);
            }
            bablOptionIds[bablOptCount] = string_no;
            bablOptCount++;
        }
        i++;
        string_no = getmem(base + i - 1);
        value = getmem(value_base + i - 1);
    }
    do_play_scroll();
    scroll_clear(1);
    set_workspace();
    for (i = 0; i < 10; i++) convo_line_option[i] = -1;
    for (i = 1; i < bablOptCount; i++) {
        line[0] = i + '0';
        line[1] = '.';
        line[2] = ' ';
        str_copy(line + 3, babl_display[i]);
        str_cat(line, "\n");
        lines = scroll_print(line);
        set_workspace();
        for (row = used; row <= lines; row++) convo_line_option[row] = i;
        used = lines + 1;
    }
    do_main_scroll();
    convoContinue = 1;
    menus_active = 1;
    converse_event_loop();
    return bablOptionIds[babl_choice];
}

/* The menu's input handler: option is the key's number, or 0 for a click, whose row in
   the scroll picks the option. Echoes the choice as the player's line and frees the
   substituted texts. */
void far conv_play_menu(int option)
{
    int x, y, row;
    register int i;
    register int selected;
    if (convo_mode == 0) return;
    set_workspace();
    if (option == 0) {
        x = inplist->x + 0xF;
        y = inplist->y + 1;
        row = (0x1e - y) / cur_font->height;
        selected = convo_line_option[row];
    } else selected = option;
    if (selected <= 0 || selected >= bablOptCount) return;
    convo_mode = 0;
    convoContinue = 0;
    mouse_release(0);
    do_play_scroll();
    scroll_clear(1);
    set_workspace();
    do_main_scroll();
    menus_active = 0;
    do_npc_scroll();
    for (i = 1; i < bablOptCount; i++) {
        if (i == selected) play_say(babl_display[i]);
        if (babl_opts[i] != babl_display[i]) bab_free(babl_display[i]);
    }
    do_main_scroll();
    babl_choice = selected;
}

/* The "say" built-in (and SAY_OP): the NPC's line, in the NPC's scroll, \P first. */
void far npc_say(char far *s)
{
#ifndef __TURBOC__
    if (getenv("UW_COOP_BOT_HOME")) fprintf(stderr,"SHARED SAY %u: %s\n",cnv_id,s);
#endif
    str_copy(conv_buffer, "\\P");
    str_cat(conv_buffer, s);
    str_cat(conv_buffer, "\n");
    do_npc_scroll();
    scroll_print(conv_buffer);
    set_workspace();
    do_main_scroll();
    convo_say_flag = 0;
}

/* The "respond" built-in (and RESPOND_OP): a line in the player's scroll. */
void far play_respond(char far *s)
{
#ifndef __TURBOC__
    if (getenv("UW_COOP_BOT_HOME")) fprintf(stderr,"SHARED RESPOND %u: %s\n",cnv_id,s);
#endif
    str_copy(conv_buffer, s);
    str_cat(conv_buffer, "\n");
    do_play_scroll();
    scroll_print(conv_buffer);
    set_workspace();
    do_main_scroll();
    convo_say_flag = 1;
}

/* The player's chosen line, in the conversation scroll in colour \1. */
void far play_say(char far *s)
{
    str_copy(conv_buffer, "\\1");
    str_cat(conv_buffer, s);
    str_cat(conv_buffer, "\\0\n");
    do_npc_scroll();
    scroll_print(conv_buffer);
    set_workspace();
    do_main_scroll();
    convo_say_flag = 1;
}

/* print(arg1 string): narration, in colour \2. */
void far conv_print(int16 far *stack)
{
    char far *original, far *expanded;
    register int id;
    id = getmem(stack[-1]);
    original = get_string(id);
    expanded = convert_string(original);
    str_copy(conv_buffer, "\\2");
    str_cat(conv_buffer, expanded);
    str_cat(conv_buffer, "\\0\n");
    do_npc_scroll();
    scroll_print(conv_buffer);
    do_main_scroll();
    set_workspace();
    if (expanded != original) bab_free(expanded);
}

/* pause(arg1): wait arg1 * 0x1F4 (scroll_wait units). */
int far conv_pause_ovr103_10DD(int16 far *stack)
{
    register int duration;
    mouse_release(0);
    duration = getmem(stack[-1]);
    scroll_wait(duration * 0x1f4, 0);
    return 1;
}

/* babl_ask: let the player type up to 0x32 characters; returns the id of a dynamic
   string holding them, the same id for every call in a conversation. */
int far getInputText_ovr103_1117(void)
{
    char response[0xa0];
    wdialog(0, 0, response, 1, 0x32);
    scroll_print("\n");
    set_workspace();
    str_copy(convo_askstr, response);
    if (babl_ask_handle == 0)
        babl_ask_handle = make_string(convo_askstr, STRBLK_DYNAMIC);
    else if (get_string(babl_ask_handle) == 0)
        replace_string(convo_askstr, babl_ask_handle);
    set_workspace();
    return babl_ask_handle;
}

/* name: The inventory built-ins, in FM Towns order: conv_check_inv (show_inv), find_barter,
   find_barter_total, conv_give_inv (give_to_npc), give_ptr_npc, conv_inv_delete,
   conv_find_inv, conv_take_inv, conv_take_inv_id, conv_inv_create, conv_inv_name
   (identify_inv), count_inv, check_inv_quality, set_inv_quality (UW2 adds switch_pic).
   Each one's callees agree with its FM Towns namesake. */

/* show_inv(arg1 indices, arg2 item ids): fill the two arrays (4 entries) with the
   player's selected trade items; returns how many. */
int far conv_check_inv(int16 far *stack)
{
    int16 ids[NUM_TRADE_SLOTS], indices[NUM_TRADE_SLOTS];
    register int i, count;
    count = player_barter_items(ids, indices);
    for (i = 0; i < NUM_TRADE_SLOTS; i++) {
        if (count > i) {
            babl_setmem(stack[-2] + i, ids[i]);
            babl_setmem(stack[-1] + i, indices[i]);
        } else {
            babl_setmem(stack[-1] + i, 0);
            babl_setmem(stack[-2] + i, 0);
        }
    }
    return count;
}

/* find_barter(arg1 item): the object index of the player's selected trade item of that
   type, or (1000 and up) of major (item - 1000) >> 2, minor (item - 1000) & 3; else 0. */
int far find_barter(int16 far *stack)
{
    int16 count, ids[NUM_TRADE_SLOTS], indices[NUM_TRADE_SLOTS];
    register int i, wanted;
    wanted = getmem(stack[-1]);
    count = player_barter_items(ids, indices);
    if (wanted < BARTER_CLASS) {
        for (i = 0; i < count; i++)
            if (ids[i] == wanted) return indices[i];
    } else {
        for (i = 0; i < count; i++)
            if ((ids[i] >> 6) == ((wanted - BARTER_CLASS) >> 2) &&
                ((ids[i] & ID_MINOR) >> 4) == ((wanted - BARTER_CLASS) & 3))
                return indices[i];
    }
    return 0;
}

/* find_barter_total(arg4 item, arg3 count out, arg2 indices out, arg1 total out): the
   player's selected trade items of type item, and their total quantity; 1 when any.
   A class search (1000 and up) is coded in the test but the enclosing check skips it,
   so it always finds nothing. Its arrays hold 5 entries; UW1's player_barter_items
   writes at most four (UW2's six could overrun them). */
int far find_barter_total(int16 far *stack)
{
    int16 wanted, matches, ids[5], indices[5], matching[5], count;
    struct Object far *obj;
    register int i, total;
    wanted = getmem(stack[-4]);
    matches = 0;
    total = 0;
    count = player_barter_items(ids, indices);
    if (wanted < BARTER_CLASS) {
        for (i = 0; i < count; i++) {
            if (wanted < BARTER_CLASS ? ids[i] == wanted : (ids[i] >> 4) == wanted - BARTER_CLASS) {
                obj = Obj_IntTMem(indices[i]);
                matching[matches] = indices[i];
                if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
                    total += obj->ol.f.link;
                else total++;
                matches++;
            }
        }
    }
    babl_setmem(stack[-3], matches);
    babl_setmem(stack[-1], total);
    for (i = 0; i < matches; i++) babl_setmem(stack[-2] + i, matching[i]);
    return total > 0 ? 1 : 0;
}

/* give_to_npc(arg2 count, arg1 indices): give the listed objects to the NPC if every
   one is among the player's selected trade items; 1 if given, else 0. */
int far conv_give_inv(int16 far *stack)
{
    int16 ids[NUM_TRADE_SLOTS], indices[NUM_TRADE_SLOTS], slot_of[NUM_TRADE_SLOTS], item_of[NUM_TRADE_SLOTS], count, have;
    register int j, i;
    count = getmem(stack[-2]);
    if ((have = player_barter_items(ids, indices)) < count) return 0;
    slot_of[0] = slot_of[1] = slot_of[2] = slot_of[3] = -1;
    item_of[0] = item_of[1] = item_of[2] = item_of[3] = -1;
    for (i = 0; i < count; i++) {
        for (j = 0; j < have; j++) {
            if (getmem(stack[-1] + i) == indices[j] && item_of[j] == -1) {
                slot_of[i] = j;
                item_of[j] = i;
                break;
            }
        }
        if (slot_of[i] == -1) return 0;
    }
    for (j = 0; j < count; j++) player_barter_give(getmem(stack[-1] + j));
    return 1;
}

/* give_ptr_npc(arg2 object, arg1 qty): give the NPC an object: all of it from the trade
   slots, or else qty of it from the player's inventory (all of it for a negative qty or
   an object that is not a quantity). */
int far give_ptr_npc(int16 far *stack)
{
    int qty;
    int16 ids[NUM_TRADE_SLOTS], indices[NUM_TRADE_SLOTS];
    struct Object far *obj;
    register int i, index;
    index = getmem(stack[-2]);
    qty = player_barter_items(ids, indices);
    for (i = 0; i < NUM_TRADE_SLOTS; i++) {
        if (ids[i] != 0 && indices[i] == index) {
            player_barter_give(index);
            return 1;
        }
    }
    qty = getmem(stack[-1]);
    obj = Obj_IntTMem(index);
    if (qty < 0 || !OBJ_ISQUANT(obj) || (obj->ol.f.link & LINK_SPECIAL)) qty = -1;
    if (!invRemoveObject(obj, qty)) return 0;
    npc_inv_add(obj);
    return 1;
}

/* do_inv_delete, find_inv, take_from_npc, take_id_from_npc and do_inv_create: thin
   wrappers of BARTER.C's npc_inv_delete, npc_barter_find, npc_barter_give,
   npc_barter_give_id and npc_inv_create. */
int far conv_inv_delete(int16 far *stack) { return npc_inv_delete(getmem(stack[-1])); }

int far conv_find_inv(int16 far *stack) { return npc_barter_find(getmem(stack[-2]), getmem(stack[-1])); }

int far conv_take_inv(int16 far *stack) { return npc_barter_give(getmem(stack[-1])); }

int far conv_take_inv_id(int16 far *stack) { return npc_barter_give_id(getmem(stack[-1])); }

int far conv_inv_create(int16 far *stack) { return npc_inv_create(getmem(stack[-1])); }

/* identify_inv(arg4 object, arg3 with article, arg2 name out, arg1 identified): the
   object's name with an article or a count, as a new dynamic string stored in arg2, and
   its value to the NPC (assess_value with likes, blurred by npc_assess) returned. */
int far conv_inv_name(int16 far *stack)
{
    int index, value, handle, describe;
    struct Object far *obj;
    char far *copy;
    char mods[10];
    char number[6];
    char article;
    int ident;
    char name[0x50];
    register char *p;
    register int qty;
    index = getmem(stack[-4]);
    describe = getmem(stack[-3]);
    ident = getmem(stack[-1]);
    value = assess_value(1, index, npc_assess);
    obj = Obj_IntTMem(index);
    if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
        qty = obj->ol.f.link;
    else qty = 1;
    p = name;
    name[0] = 0;
    article = 0;
    mods[0] = 0;
    if (do_mods(obj, ident, mods))
        article = mods[0];
    if (article && describe && qty == 1) {
        switch (article) {
        case 'a': case 'e': case 'i': case 'o': case 'u':
            strcpy(name, "an ");
            break;
        default:
            strcpy(name, "a ");
            break;
        }
        describe = 0;
    } else if (qty > 1) {
        strcat(name, itoa(qty, number, 10));
        strcat(name, " ");
        describe = 0;
    }
    if (mods[0]) strcat(name, mods);
    p = name + strlen(name);
    get_name(p, obj, describe, qty > 1 ? 1 : 0);
    p = name;
    do_of(obj, ident, p);
    copy = bab_malloc(strlen(p) + 1);
    str_copy(copy, p);
    handle = make_string(copy, STRBLK_DYNAMIC);
    babl_setmem(stack[-2], handle);
    return value;
}

/* how many of an object: its quantity, or 1 */
int far count_inv(int16 far *stack)
{
    struct Object far *obj;
    register int qty, index;
    index = getmem(stack[-1]);
    obj = Obj_IntTMem(index);
    if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)) qty = obj->ol.f.link;
    else qty = 1;
    return qty;
}

/* check_inv_quality: reads the quality but never returns it, so the script gets
   whatever AX holds at the end. */
int far check_inv_quality(int16 far *stack)
{
    struct Object far *obj;
    register int quality;
    int index;
    index = getmem(stack[-1]);
    obj = Obj_IntTMem(index);
    quality = obj->qn.f.quality;
    AX_RESULT(quality);                 /* the quality is still in AX */
}

/* set_inv_quality(arg2 object, arg1 quality): set it, to 6 bits. */
int far set_inv_quality(int16 far *stack)
{
    struct Object far *obj;
    register int index;
    index = getmem(stack[-2]);
    obj = Obj_IntTMem(index);
    obj->qn.f.quality = getmem(stack[-1]) & 0x3f;
    return 1;
}
