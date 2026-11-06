/* portgame.h: UW2's side of Exhume's runtime (docs/PORT.md, "The runtime"). The runtime's
   port.h, compat.h, plat.h and its sound and system C include it, ahead of their own defaults;
   compat.h brings it into the game's C too, so it declares only the port's names.

   The program's name in its messages, and the game's main it runs; DGROUP's paragraph in
   UW2.EXE, which the pseudo-registers _DS and _SS start with, and where Borland's _ctype table
   sits in DGROUP; the termination chain exit runs, which seg021's init hooked; the black box;
   the window; how the game's folder is found and what the settings file is called; the sound
   library's environment variables; and the far data blocks of UW2.EXE that no source defines
   yet (mem/fardata.c), whose initial bytes are read from the user's own UW2.EXE at start-up,
   never shipped. The load segment and the far heap are the runtime's defaults. */
#ifndef UW2_PORTGAME_H
#define UW2_PORTGAME_H

#define PORT_NAME "uw2port"
#define PORT_GAME_MAIN uw2_main                 /* UWEDIT.C's main (compat.h) */
#define PORT_DGROUP_PARA 0x65E9u
#define PORT_CTYPE_AT 0x1BF6
void seg021_exit_chain(void);                   /* sys/sysentry.c */
#define PORT_EXIT_CHAIN() seg021_exit_chain()

/* the black box (sys/blackbox.c): stage/ copies only the home directory's files a replay reads
   back: DATA\ whole (UW.CFG, the sound cards, and any file the player put there to be read ahead
   of the game's own, such as a translated STRINGS.PAK), SAVE0 (the game in progress) and the saved
   games, SAVE1 to SAVE4; never gog-cd/, roms/, a game folder or the port's own files */
#define PORT_BLACKBOX 1
#define BLACKBOX_STAGE "DATA", "SAVE0", "SAVE1", "SAVE2", "SAVE3", "SAVE4"

/* the window (plat.h): its title when main gives none, and its icon, written by
   tools/dist/icon/make-icons.py (not on macOS, where the app's own icon is used) */
#define PLAT_TITLE "UW2"
#define PLAT_ICON "platform/sdl3/icon.h"

/* finding the game (sys/gamedir.c): the GOG release's UW2.EXE (port_check_exe, mem/fardata.c),
   in a UW2 folder of an install or of GOG's CD image game.gog; Ultima Underworld 1+2 is GOG's
   product 1207658937 (catalog.gog.com); the settings file is uw2port.cfg in the home directory
   (docs/BUILDING.md, "Running the port") */
#define PORT_GAME_EXE "UW2.EXE"
#define PORT_GAME_FOLDER "UW2"
#define PORT_GAME_HINTS "underworld", "uw2"
#define PORT_GAME_DESC "the GOG release's UW2.EXE"
#define PORT_GOG_ID "1207658937"
#define PORT_DATA_ENV "UW2PORT_DATA"
#ifdef _WIN32
#define PORT_GAME_ROOTS "$USERPROFILE/UWGOG"
#else
#define PORT_GAME_ROOTS "~/UWGOG"
#endif
#define PORT_CONFIG_FILE "uw2port.cfg"
#define PORT_CONFIG_TITLE "uw2port settings (docs/BUILDING.md, \"Running the port\")"

/* the sound library's environment variables (sound/ail.c, sound/audio.c) */
#define AIL_SNDCHECK_ENV "UW2PORT_SNDCHECK"     /* list each sound read that differs from DOS's */
#define AUDIO_ROMS_ENV "UW2PORT_MT32_ROMS"      /* the user's MT-32 or CM-32L ROMs */

extern unsigned char seg_370D[];           /* seg003's data, the graphics library's */
extern unsigned char seg052_519C[];        /* seg004's data, the 3D renderer's */
extern unsigned char dseg062_62a6[];       /* seg021's data (FD71) */
#define SEG_370D_SIZE     0x5E76
#define SEG052_519C_SIZE  0xE4D6
#define DSEG062_62A6_SIZE 0x0C40

/* The enhancements (Exhume's runtime/port/sys/enhance.h; src/port/sys/enhtab.c): bits of
   enhance_on, in the table's order. */
enum { ENH_SKIP_INTRO, ENH_WRAP_MENU, ENH_FAST_PANELS, ENH_FREE_HEADING, ENH_SUBTITLES,
       ENH_SKILL_MESSAGES, ENH_PERSPECTIVE, ENH_FULL_SPRITES,
       ENH_WIDE_PITCH, ENH_MOUSE_LOOK, ENH_INVERT_LOOK, ENH_MODERN_KEYS, ENH_RUNE_KEYS,
       ENH_TIDY_OBJECTS, ENH_RESTORE_AT_REST, ENH_PLAIN_DEBRIS,
       ENH_COUNT };
/* --enhance modern-keys: the key code a lone left Shift gives (ui/MOUSE.C), unused by the game */
#define PORT_KEY_LSHIFT 0xBF
int port_pitch_bound(void);                 /* game/PLAYER.C: the pitch's bound either way */

#endif
