/* portgame.h: UW1's side of Exhume's runtime (Exhume's docs/port.md, "The runtime"; its
   runtime/README.md, "The project's side"). The runtime's port.h, compat.h, plat.h and its sound
   and system C include it, ahead of their own defaults; compat.h brings it into the game's C too,
   so it declares only the port's names.

   The program's name in its messages, and the game's main it runs; DGROUP's paragraph in UW.EXE,
   which the pseudo-registers _DS and _SS start with, and where Borland's _ctype table sits in
   DGROUP (DS:1E00, after the C library's ATEXIT word at 1DFE: build/LINK/out/UW.MAP, and the
   bytes); the termination chain exit runs, which seg019's init hooked; the black box; the
   window; how the game's folder is found and what the settings file is called; the sound
   library's environment variables; and the far data blocks of UW.EXE that no source defines
   yet (mem/fardata.c), whose initial bytes are read from the user's own UW.EXE at start-up,
   never shipped. The load segment is the runtime's default; the far heap starts where DOS's
   does (PORT_HEAP_FIRST, below). */
#ifndef UW1_PORTGAME_H
#define UW1_PORTGAME_H

#define PORT_NAME "uw1port"
#define PORT_GAME_MAIN uw1_main                 /* UWEDIT.C's main (compat.h) */
#define PORT_DGROUP_PARA 0x5AACu
#define PORT_CTYPE_AT 0x1E00
void seg019_exit_chain(void);                   /* sys/sysentry.c */
#define PORT_EXIT_CHAIN() seg019_exit_chain()

/* Names of UW1's that the host's C library also declares, even with only POSIX 2008's names
   visible: renamed in the port, after compat.h has included the host's headers.
   dprintf is DEBUG.C's (ovr106_1B, UW1's debug print); POSIX's writes to a descriptor. */
#define dprintf uw1_dprintf

/* The far heap's first paragraph: DOS's heap starts 6955h paragraphs above the load segment,
   past the image (6250h), the stack and the overlay manager's buffer, which the port does not
   have. Measured: in the DOS replays of the newgame session the first block farmalloc gives
   (the buffer seg048:55EA names at its checkpoint 4) is at the load segment plus 6955h. */
#define PORT_HEAP_FIRST (0x6955u + 0x0800u)     /* the runtime's PORT_LOAD_SEG, 0800h, plus 6955h */

/* the black box (sys/blackbox.c): stage/ copies only the home directory's files a replay reads
   back: DATA\ whole (UW.CFG, the sound cards, and any file the player put there to be read ahead
   of the game's own, such as a translated STRINGS.PAK), SAVE0 (the game in progress) and the saved
   games, SAVE1 to SAVE4; never gog-cd/, roms/, a game folder or the port's own files */
#define PORT_BLACKBOX 1
#define BLACKBOX_STAGE "DATA", "SAVE0", "SAVE1", "SAVE2", "SAVE3", "SAVE4"

/* the window (plat.h): its title when main gives none, and its icon, written by Exhume's
   tools/icons.py from tools/dist/icon/uw1.svg (not on macOS, where the app's own icon is used) */
#define PLAT_TITLE "UW1"
#define PLAT_ICON "platform/sdl3/icon.h"

/* finding the game (sys/gamedir.c): the GOG release's UW.EXE (port_check_exe, mem/fardata.c),
   in a UW1 folder of an install, or the UW folder of GOG's CD image; GOG sells UW1 and UW2 as
   one product, Ultima Underworld 1+2, 1207658937 (UW2Decomp's portgame.h) */
#define PORT_GAME_EXE "UW.EXE"
#define PORT_GAME_FOLDER "UW1"
#define PORT_GAME_IMAGE_FOLDER "UW"             /* game.gog, the 1+2 CD in GOG's Mac app: UW\UW.EXE */
#define PORT_GAME_HINTS "underworld", "uw1"
#define PORT_GAME_DESC "the GOG release's UW.EXE"
#define PORT_GOG_ID "1207658937"
#define PORT_DATA_ENV "UW1PORT_DATA"
#ifdef _WIN32
#define PORT_GAME_ROOTS "$USERPROFILE/UWGOG"
#else
#define PORT_GAME_ROOTS "~/UWGOG"
#endif
#define PORT_CONFIG_FILE "uw1port.cfg"
#define PORT_CONFIG_TITLE "uw1port settings"

/* the sound library's environment variables (sound/ail.c, sound/audio.c) */
#define AIL_SNDCHECK_ENV "UW1PORT_SNDCHECK"     /* list each sound read that differs from DOS's */
#define AUDIO_ROMS_ENV "UW1PORT_MT32_ROMS"      /* the user's MT-32 or CM-32L ROMs */

/* The far data segments the assembly modules keep their data in (build/LINK/out/UW.MAP):
   seg048, the graphics library's (FD52, 3963:0000, its first eight bytes FD51's last), seg051,
   the 3D renderer's (FD58, 4723:0000), and seg063, the system library's (FD72, 5624:0000). */
extern unsigned char seg048[];
extern unsigned char seg051[];
extern unsigned char seg063[];
#define SEG048_SIZE 0x5E7C
#define SEG051_SIZE 0xC4D9
#define SEG063_SIZE 0x0AB0

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
