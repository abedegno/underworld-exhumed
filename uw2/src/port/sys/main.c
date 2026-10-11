/* main.c: replaces nothing in the game; it is what DOS and C0 did before the game's main
   (UWEDIT.C, renamed uw2_main by compat.h): it reads the port's options, finds the user's game
   (the data root, sys/gamedir.c) and maps it and the port's home directory, loads and checks
   the far data from the user's UW2.EXE, starts the PIT, and runs the game on its own thread
   under the platform layer. `uw2port --help` lists the options (help_text, below). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "port.h"
#include "plat.h"
#include "sound/audio.h"
#include "sound/mt32roms.h"
#include "sys/enhance.h"
#include "sys/inscript.h"
#include "ui/settings.h"

extern const struct enhance_flag enhance_table[];   /* src/port/sys/enhtab.c */

int uw2_main(int argc, char *argv[]);
void borland_init(void);
void port_crash_handlers(void);
void port_report_motion(void);          /* src/port/3d/render.c */

int port_trace;
extern int16_t rp_request;              /* the runtime's replay/replay.c: 0 off, 1 record, 2 replay */

void port_log(const char *fmt, ...)
{
    va_list ap;
    if (!port_trace) return;
    va_start(ap, fmt);
    fputs("uw2port: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void port_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("uw2port: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
#ifdef __EMSCRIPTEN__
    {
        /* in a browser the page's end runs (plat_game_stop): an exit on the game's thread would
           only stop that thread, leaving the picture frozen and the files unwritten */
        char why[512];
        va_start(ap, fmt);
        vsnprintf(why, sizeof why, fmt, ap);
        va_end(ap);
        plat_game_stop(1, why);
    }
#endif
    exit(1);
}

void port_backtrace(void);

static int exit_on_halt;

void rp_halt(void);

void port_halt(const char *why)
{
    rp_halt();
#ifdef __EMSCRIPTEN__
    fprintf(stderr, "uw2port: %s; the game stops here\n", why);
#else
    fprintf(stderr, "uw2port: %s; the game stops here%s\n", why, exit_on_halt ? "" : " (the window stays)");
#endif
    if (port_trace) port_backtrace();
    fflush(stdout);
#ifdef __EMSCRIPTEN__
    /* in a browser the game ends as if it had quit, its files written out and the menu back
       (plat_game_stop, with the reason for the page), whatever --exit-on-halt says */
    plat_game_stop(3, why);
#else
    if (exit_on_halt) plat_game_exit(3);
    plat_game_park();
#endif
}

/* --shot-at-flip K:FILE: the scan-out right after the game's K-th grPageFlip, so a screen
   can be compared with DOS's whatever the timing (display_screen(5, 6) is flip 1,
   display_screen(6, 7) flip 2). */
#define MAXSHOTS 8
static struct { int flip; const char *path; } shots[MAXSHOTS];
static int nshots, flips;

void port_on_flip(void)
{
    static uint8_t pix[640 * 480];
    uint8_t pal[768];
    int i, w, h;
    flips++;
    for (i = 0; i < nshots; i++)
        if (shots[i].flip == flips) {
            vga_scanout_now(pix, &w, &h, pal);
            if (plat_write_png(shots[i].path, pix, w, h, pal) == 0)
                fprintf(stderr, "uw2port: wrote %s (%dx%d) at flip %d\n", shots[i].path, w, h, flips);
        }
}

static int game_argc;
static char **game_argv;

static int game(void *arg)
{
    (void)arg;
    if (port_coop_start()) return 2;
    return uw2_main(game_argc, game_argv);
}

static void on_lifecycle(int ev)
{
    port_log("lifecycle event %d\n", ev);
}

/* --replay: the recording goes into the home directory as REPLAY.IN, where the game's own
   open finds it. */
static int copy_to_home(const char *src, const char *home, const char *name)
{
    char path[1200];
    unsigned char buf[65536];
    size_t n;
    FILE *in = fopen(src, "rb"), *out;
    snprintf(path, sizeof path, "%s/%s", home, name);
    out = in ? fopen(path, "wb") : NULL;
    if (!out) {
        fprintf(stderr, "uw2port: cannot copy %s to %s\n", src, path);
        if (in) fclose(in);
        return 1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

/* DATA\UW.CFG and the home directory: uwcfg.c (settab.c uses them too, and the fuzz host links
   the port's files without this one) */
int write_uw_cfg(const char *spec);
void read_uw_cfg(int *music, int *speech);
extern const char *port_home;

/* The music on the Roland MT-32 (card 5), the speech card kept (else the Sound Blaster's). */
static int music_to_mt32(void)
{
    char spec[16];
    int music, speech;
    read_uw_cfg(&music, &speech);
    if (music == 5) return 0;
    snprintf(spec, sizeof spec, "5,%d", speech >= 0 ? speech : 1);
    return write_uw_cfg(spec);
}

/* A folder or file dropped on the window or the app: when it holds MT-32 or CM-32L ROMs (any
   names, whole or in halves), they are remembered and the music goes to the MT-32 from the next
   start, since the game has its sound driver loaded already; a message says which. */
static void on_drop(const char *path)
{
    struct mt32roms_set set;
    char text[1600];
    if (!mt32roms_pick(path, &set)) {
        snprintf(text, sizeof text, "No Roland MT-32 or CM-32L ROMs were found in %s.\n\nDrop the folder "
                 "that holds a control ROM and its PCM ROM (any file names, whole images or their halves).", path);
        plat_message(1, "Ultima Underworld II: no MT-32 ROMs", text);
        return;
    }
    port_config_set(port_home, "mt32-roms", set.dir);
    port_config_set(port_home, "sound-auto", "0");
    music_to_mt32();
    fprintf(stderr, "uw2port: mt32: ROMs dropped %s (%s, %s); the MT-32 plays the music from the next start\n",
            set.dir, set.ctrl_id, set.pcm_id);
    snprintf(text, sizeof text, "%s ROMs found in %s.\n\nThe music will play on the Roland MT-32 from the next "
             "time you start the game.", strncmp(set.ctrl_id, "ctrl_cm32l", 10) ? "MT-32" : "CM-32L", set.dir);
    plat_message(0, "Ultima Underworld II: MT-32 ROMs found", text);
}

static const char help_text[] =
    "usage: uw2port [options] [game arguments]\n"
    "\n"
    "Plays Ultima Underworld II from your own copy of the game (the GOG release's UW2.EXE).\n"
    "\n"
    "Game and files:\n"
    "  --data DIR             the UW2 folder (UW2.EXE, DATA, CRIT, CUTS, SOUND), or a folder\n"
    "                         holding it or GOG's game.gog; found by itself when not given\n"
    "                         ($UW2PORT_DATA, the last folder used, GOG's install folders)\n"
    "  --home DIR             where saved games and settings go ($UW2PORT_HOME, else ~/.uw2port)\n"
    "Enhancements (off by default; docs/ENHANCEMENTS.md):\n"
    "  --enhance NAME[,NAME]  turn on changes the original game does not have, kept until\n"
    "                         changed; --enhance list lists them\n"
    "  --no-enhance NAME      turn one off\n"
    "Experimental co-op presence (native only; docs/COOP.md):\n"
    "  --coop-host PORT      host a two-player presence session on a TCP port\n"
    "  --coop-join IPV4:PORT  join a host; live partner health and automap marker\n"
    "                         worlds, combat, inventory and saves remain independent\n"
    "Sound:\n"
    "  --sound MUSIC[,SPEECH] the sound cards, kept until changed (first run: 3,1, or 5,1\n"
    "                         when MT-32 ROMs are given or found):\n"
    "                         music 0 none, 2 Ad Lib, 3 Sound Blaster, 4 Sound Blaster Pro 1,\n"
    "                         5 Roland MT-32, 6 Pro Audio Spectrum, 7 Sound Blaster Pro 2;\n"
    "                         speech 0 none, 1 Sound Blaster, 2 Sound Blaster Pro,\n"
    "                         3 Pro Audio Spectrum\n"
    "  --mt32-roms PATH       your MT-32 or CM-32L ROM images (a folder, or a file in\n"
    "                         it; any names), kept until changed (also $UW2PORT_MT32_ROMS);\n"
    "                         without, the port looks in its home's roms/ and mt32-roms/,\n"
    "                         beside the game and itself, and where DOSBox keeps them\n"
    "                         (or drop the folder on the program or its window)\n"
    "  --no-audio             open no audio device\n"
    "Window:\n"
    "  --scale N              initial window scale, 1 to 8 (3; scale= in the settings, F11)\n"
    "  --no-aspect            square pixels (default: 200 lines shown as 240, as on a 4:3 CRT)\n"
    "  --no-integer           scale freely (default: whole multiples)\n"
    "  --mouse follow|lock    follow: the game's cursor follows the system pointer (default);\n"
    "                         lock: a click captures the pointer, as in DOSBox, and Ctrl+F10\n"
    "                         releases it; kept until changed\n"
    "Testing and debugging:\n"
    "  --hidden               no window; the scan-out still runs\n"
    "  --screenshot-after MS  write the screen to a PNG MS milliseconds after start\n"
    "  --screenshot FILE      that PNG's name (uw2port.png)\n"
    "  --window-shot FILE     with --screenshot-after, also the window's scaled contents\n"
    "  --shot-at-flip K:FILE  write the screen right after the game's K-th page flip\n"
    "  --exit-after MS        quit MS milliseconds after start\n"
    "  --exit-on-halt         quit (status 3) where the port stops instead of leaving the window up\n"
    "  --record               record the session to RECORD.OUT in the home directory (F12 ends it)\n"
    "  --input-script FILE    keys and mouse events at set times, for tests (docs/BUILDING.md)\n"
    "  --replay FILE          replay a recording instead of reading the clock, keyboard and mouse\n"
    "  --audio-wav FILE       write everything the sound cards play to a WAV file (44100 Hz)\n"
    "  --ail-log FILE         log every AIL call and driver service\n"
    "  --hw-log FILE          log every register write and MIDI byte the drivers make\n"
    "  --settings-list        print the settings screen's rows and exit\n"
    "  --no-recording         do not record this session (by default each session's inputs go\n"
    "                         to recordings/ in the home directory, the newest five kept, so\n"
    "                         that a crash can be replayed)\n"
    "  -v                     trace (also UW2PORT_TRACE=1)\n"
    "  -h, --help             this list\n"
    "\n"
    "Anything else goes to the game as its own command line.\n";

static void usage(int full)
{
    if (full) {
        fputs(help_text, stdout);
        exit(0);
    }
    fputs("usage: uw2port [options] [game arguments]; uw2port --help lists the options\n", stderr);
    exit(2);
}

static const char not_found[] =
    "Ultima Underworld II needs the files of your own copy of the game, and none was found.\n\n"
    "Install the game from GOG, or choose the folder that holds UW2.EXE, the GOG install "
    "folder, or the GOG app. The folder is remembered.\n\n"
    "From a command line: uw2port --data /path/to/UW2";

/* A path as an absolute one, for the settings file. */
static const char *absolute(const char *p, char *buf, size_t n)
{
#ifdef _WIN32
    return _fullpath(buf, p, n) ? buf : p;
#else
    char cwd[1024];
    if (p[0] == '/' || !getcwd(cwd, sizeof cwd)) return p;
    if (!strcmp(p, ".")) snprintf(buf, n, "%s", cwd);
    else snprintf(buf, n, "%s/%s", cwd, p);
    return buf;
#endif
}

/* The game's directory when --data names none: found (sys/gamedir.c), or, with a window,
   chosen by the user in a folder dialog. NULL if there is none. */
static const char *find_data(const char *home, int interactive, char *buf, size_t n)
{
    char chosen[1024], msg[3000];
    if (port_find_game(home, buf, n) == 0) return buf;
    snprintf(msg, sizeof msg, "%s%s", not_found, port_game_refused());
    if (!interactive) {
        fprintf(stderr, "uw2port: %s\n", msg);
        return NULL;
    }
    while (plat_choose_folder("Ultima Underworld II: where is the game?", msg, chosen, sizeof chosen) == 0) {
        if (port_game_in(chosen, home, buf, n) == 0) return buf;
        snprintf(msg, sizeof msg, "%s holds no copy of Ultima Underworld II that this program can use.%s\n\n%s",
                 chosen, port_game_refused(), not_found);
    }
    return NULL;
}

static int home_dir(const char *p)
{
    char buf[1024];
    char *s;
    struct stat st;
    if (snprintf(buf, sizeof buf, "%s", p) >= (int)sizeof buf) return -1;
    for (s = buf + 1; ; s++)
        if (*s == '/' || *s == '\\' || !*s) {
            char c = *s;
            *s = 0;
            if (stat(buf, &st) != 0) {
#ifdef _WIN32
                _mkdir(buf);
#else
                mkdir(buf, 0755);
#endif
            }
            if (!c) break;
            *s = c;
        }
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

extern const struct setting port_settings[];     /* settab.c */
extern const int port_settings_count;
void port_settings_list(FILE *f);
void port_settings_seed(int fullscreen, int scale, int aspect, int integer_scale, int lock);
void port_settings_start(int *fullscreen, int *scale, int *aspect, int *integer_scale);
void port_settings_roms(const char *dir);

int main(int argc, char *argv[])
{
    static char home_buf[1024], exe[1200], data_buf[1024], abs_buf[1024];
    const char *data = NULL, *home = getenv("UW2PORT_HOME"), *replay = NULL;
    const char *sound = NULL, *roms = NULL, *wav = NULL, *ail_log = NULL, *hw_log = NULL, *mouse = NULL;
    char mouse_buf[16];
    int audio_device = 1, interactive, recording = 1, status;
    PlatConfig cfg;
    PlatHooks hooks;
    int i, dropped = 0, first_run = 0;
    const char *enh_on[16], *enh_off[16];   /* --enhance and --no-enhance lists, in order */
    int n_on = 0, n_off = 0, enh_list = 0;
    int settings_list = 0;
    int cl_scale = 0, cl_aspect = 0, cl_integer = 0;   /* given on the command line: the settings file does not override them */
    const char *input_script = NULL;    /* --input-script: keys and mouse at set times (inscript.c) */
    static char spec_buf[16];

    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Ultima Underworld II";
    cfg.scale = 3;
    cfg.integer_scale = 1;
    cfg.aspect = 1;
    game_argv = calloc((size_t)argc + 1, sizeof *game_argv);
    game_argv[game_argc++] = "UW2.EXE";
    port_trace = getenv("UW2PORT_TRACE") != NULL;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(a, "--home") && i + 1 < argc) home = argv[++i];
        else if ((!strcmp(a, "--coop-host") || !strcmp(a, "--coop-join")) && i + 1 < argc) {
            int role = !strcmp(a, "--coop-host") ? PORT_COOP_HOST : PORT_COOP_JOIN;
            if (port_coop_configure(role, argv[++i])) return 2;
        }
        else if (!strcmp(a, "--scale") && i + 1 < argc) {
            cfg.scale = atoi(argv[++i]);
            cfg.scale = cfg.scale < 1 ? 1 : cfg.scale > 8 ? 8 : cfg.scale;   /* the screen's 1x to 8x */
            cl_scale = 1;
        }
        else if (!strcmp(a, "--no-aspect")) { cfg.aspect = 0; cl_aspect = 1; }
        else if (!strcmp(a, "--no-integer")) { cfg.integer_scale = 0; cl_integer = 1; }
        else if (!strcmp(a, "--mouse") && i + 1 < argc && (!strcmp(argv[i + 1], "follow") || !strcmp(argv[i + 1], "lock")))
            mouse = argv[++i];
        else if (!strcmp(a, "--hidden")) cfg.hidden = 1;
        else if (!strcmp(a, "--screenshot-after") && i + 1 < argc) cfg.screenshot_after_ms = atol(argv[++i]);
        else if (!strcmp(a, "--screenshot") && i + 1 < argc) cfg.screenshot_path = argv[++i];
        else if (!strcmp(a, "--window-shot") && i + 1 < argc) cfg.window_shot_path = argv[++i];
        else if (!strcmp(a, "--shot-at-flip") && i + 1 < argc && nshots < MAXSHOTS && strchr(argv[i + 1], ':')) {
            shots[nshots].flip = atoi(argv[++i]);
            shots[nshots++].path = strchr(argv[i], ':') + 1;
        }
        else if (!strcmp(a, "--exit-after") && i + 1 < argc) cfg.exit_after_ms = atol(argv[++i]);
        else if (!strcmp(a, "--exit-on-halt")) exit_on_halt = 1;
        else if (!strcmp(a, "--record")) rp_request = 1;
        else if (!strcmp(a, "--replay") && i + 1 < argc) { rp_request = 2; replay = argv[++i]; }
        else if (!strcmp(a, "--enhance") && i + 1 < argc) {
            if (!strcmp(argv[i + 1], "list")) { enh_list = 1; i++; }
            else if (n_on < 16) enh_on[n_on++] = argv[++i];
            else i++;
        }
        else if (!strcmp(a, "--no-enhance") && i + 1 < argc) {
            if (n_off < 16) enh_off[n_off++] = argv[++i];
            else i++;
        }
        else if (!strcmp(a, "--input-script") && i + 1 < argc) input_script = argv[++i];
        else if (!strcmp(a, "--sound") && i + 1 < argc) sound = argv[++i];
        else if (!strcmp(a, "--mt32-roms") && i + 1 < argc) roms = argv[++i];
        else if (!strcmp(a, "--audio-wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(a, "--no-audio")) audio_device = 0;
        else if (!strcmp(a, "--no-recording")) recording = 0;
        else if (!strcmp(a, "--settings-list")) settings_list = 1;
        else if (!strcmp(a, "--ail-log") && i + 1 < argc) ail_log = argv[++i];
        else if (!strcmp(a, "--hw-log") && i + 1 < argc) hw_log = argv[++i];
        else if (!strcmp(a, "-v")) port_trace = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) usage(1);
        else if (a[0] == '-' && a[1] == '-') usage(0);
        /* a folder or file dropped on the program's icon (Windows, Linux) arrives as an argument */
        else if (!roms && a[0] != '-' && mt32roms_pick(a, NULL)) { roms = a; dropped = 1; }
        else game_argv[game_argc++] = argv[i];
    }
    if (port_coop_enabled() && (rp_request == 1 || rp_request == 2 || nshots)) {
        fprintf(stderr, "uw2port: co-op presence cannot be combined with --record, --replay or --shot-at-flip\n");
        return 2;
    }
    if (settings_list) {
        port_settings_list(stdout);
        return 0;
    }
    enhance_init(enhance_table, ENH_COUNT, "UW2");
    if (enh_list) {
        enhance_list(stdout);
        return 0;
    }
    /* a bad input script stops the run before anything is written */
    if (input_script && inscript_load(input_script, plat_key_byte, plat_pointer_event) < 0) return 1;
    if (!home) {
        const char *h = getenv("HOME");
#ifdef _WIN32
        if (!h && getenv("APPDATA")) {          /* started from Explorer: no HOME */
            snprintf(home_buf, sizeof home_buf, "%s/uw2port", getenv("APPDATA"));
        } else
#endif
        snprintf(home_buf, sizeof home_buf, "%s/.uw2port", h ? h : ".");
        home = home_buf;
    }
    /* A player's run, as opposed to a test's: it may ask with dialogs, remembers the game's
       folder and the ROMs, and gets a sound card on its first run. */
    interactive = !cfg.hidden && rp_request != 1 && rp_request != 2;
    if (home_dir(home)) {
        fprintf(stderr, "uw2port: cannot create the home directory %s (--home)\n", home);
        return 1;
    }
    if (!data && !(data = find_data(home, interactive, data_buf, sizeof data_buf))) return 1;
    if (plat_files_init(data, home)) {
        fprintf(stderr, "uw2port: %s is not a directory (give the UW2 directory with --data)\n", data);
        return 1;
    }
    if (plat_resolve("UW2.EXE", PLAT_READ, exe, sizeof exe) || port_load_exe(exe)) {
        if (interactive)
            plat_message(1, "Ultima Underworld II: the game cannot start",
                         "The folder given does not hold the GOG release's UW2.EXE, which this program "
                         "needs. Run it without --data to find or choose the game's folder.");
        return 1;
    }
    if (interactive) port_config_set(home, "data", absolute(data, abs_buf, sizeof abs_buf));
    if (mouse && interactive) port_config_set(home, "mouse", mouse);
    else if (!mouse && port_config_get(home, "mouse", mouse_buf, sizeof mouse_buf) == 0)
        mouse = mouse_buf;
    cfg.mouse_lock = mouse && !strcmp(mouse, "lock");
    {
        /* the display options, the volume and mouse-look's speed are read below, as the settings
           screen reads them (settings_init) */
        char v[16];
        if (port_config_get(home, "recording", v, sizeof v) == 0 && !strcmp(v, "0")) recording = 0;
    }
    {
        /* the MT-32 ROMs (Exhume's sound/mt32roms.c): --mt32-roms (a folder, or a file in it),
           $UW2PORT_MT32_ROMS, the remembered setting, else a search of the home, the game, the
           program's folder and other emulators' ROM folders; any file names. A folder given or
           found is remembered by a run with a window. */
        static char kept[1024], found[1024];
        const char *src, *given = roms ? absolute(roms, abs_buf, sizeof abs_buf) : NULL;
        if (port_config_get(home, "mt32-roms", kept, sizeof kept) != 0) kept[0] = 0;
        src = mt32roms_locate(given, getenv("UW2PORT_MT32_ROMS"), kept, home, data, found, sizeof found);
        roms = src ? found : NULL;
        if (src && interactive && (!strcmp(src, "given") || !strcmp(src, "found")) && strcmp(found, kept))
            port_config_set(home, "mt32-roms", found);
    }
    /* The first run (no DATA\UW.CFG in the home directory yet): a Sound Blaster, its FM music
       and its digital effects; or, when a ROM pair is given or found (--mt32-roms,
       $UW2PORT_MT32_ROMS, the remembered setting or the search: mt32roms_locate), the MT-32 for the music and the Sound Blaster
       for the effects. Kept until --sound changes it. */
    /* The enhancements (docs/ENHANCEMENTS.md), all off unless turned on: a replay's are its
       recording's alone (format 5), and only a presentation one may be added to a replay of a
       recording that has none; else the settings (a player's run only), then the options, which
       a player's run keeps. */
    {
        uint32_t m = 0;
        int carries = 0, k;
        if (replay) {
            if (enhance_from_recording(replay, &m, &carries) < 0) return 1;
            if (carries && (n_on || n_off)) {
                fprintf(stderr, "uw2port: enhance: a replay takes its recording's enhancements; give no --enhance or --no-enhance\n");
                return 1;
            }
        } else if (!cfg.hidden)
            enhance_load(home, &m);
        for (k = 0; k < n_on; k++)
            if (enhance_parse(enh_on[k], &m, 1, 1) < 0) return 1;
        for (k = 0; k < n_off; k++)
            if (enhance_parse(enh_off[k], &m, 0, 1) < 0) return 1;
        if (replay && !carries && (m & ~enhance_kind_mask(ENH_PRESENTATION))) {
            fprintf(stderr, "uw2port: enhance: only a presentation enhancement can be added to a replay of a recording made without them\n");
            return 1;
        }
        if (interactive && (n_on || n_off)) enhance_save(home, m);
        enhance_on = m;
        enhance_log();
    }
    /* Later runs: the music goes to the MT-32 when ROMs turn up and either they were dropped on the
       program or the port chose the Sound Blaster itself on the first run (sound-auto in the
       settings, cleared by any --sound). */
    if (interactive && sound) port_config_set(home, "sound-auto", "0");
    else if (interactive && roms) {
        char m[8];
        int music, speech;
        read_uw_cfg(&music, &speech);
        if (music >= 0 && music != 5 && (dropped || (port_config_get(home, "sound-auto", m, sizeof m) == 0 && !strcmp(m, "1")))) {
            snprintf(spec_buf, sizeof spec_buf, "5,%d", speech >= 0 ? speech : 1);
            sound = spec_buf;
            port_config_set(home, "sound-auto", "0");
            fprintf(stderr, "uw2port: mt32: the music now plays on the Roland MT-32 (--sound changes it)\n");
        }
    }
    if (interactive && !sound) {
        char cfgpath[1200];
        FILE *f = NULL;
        if (plat_resolve("DATA\\UW.CFG", PLAT_CREATE, cfgpath, sizeof cfgpath) == 0 && !(f = fopen(cfgpath, "rb"))) {
            sound = audio_mt32_roms_present(roms) ? "5,1" : "3,1";
            first_run = 1;
        } else if (f)
            fclose(f);
        if (first_run) port_config_set(home, "sound-auto", strcmp(sound, "3,1") ? "0" : "1");
    }
    vga_window_init();
    if (rp_request != 1 && rp_request != 2) rp_request = 0;   /* the port reads the world */
    if (interactive && recording && rp_request == 0) port_blackbox_start(home);
    if (replay && copy_to_home(replay, home, "REPLAY.IN")) return 1;
    if (sound && write_uw_cfg(sound)) return 1;
    audio_config(wav, roms, audio_device && !cfg.hidden);
    ail_set_logs(ail_log, hw_log);
    ail_set_slaved(rp_request == 2);    /* under replay AIL's timers follow the replayed clock */
    borland_init();
    port_crash_handlers();
    /* The game finds its home through UWHOME; the port's home directory plays that part,
       laid over the data root, so the game's default (".", the game's own directory) is
       right and the host's own UWHOME must not leak in. */
#ifdef _WIN32
    _putenv("UWHOME=");                 /* Windows has no unsetenv; an empty value removes it */
#else
    unsetenv("UWHOME");
#endif
    port_home = home;
    /* the settings file as the screen reads it is what the run starts with: a value the screen
       would not show (volume=abc, scale=99) is its default there and here; the command line's
       display options win */
    settings_init(home, port_settings, port_settings_count, "Ultima Underworld II");
    /* what this run really uses (the command line over the file) is what the screen shows and applies */
    port_settings_start(&cfg.fullscreen, cl_scale ? NULL : &cfg.scale, cl_aspect ? NULL : &cfg.aspect,
                        cl_integer ? NULL : &cfg.integer_scale);
    port_settings_roms(roms);
    port_settings_seed(cfg.fullscreen, cfg.scale, cfg.aspect, cfg.integer_scale, cfg.mouse_lock);
    pit_start();
    memset(&hooks, 0, sizeof hooks);
    hooks.scanout = port_coop_enabled() ? port_coop_scanout : vga_scanout;
    hooks.key = kbd_byte;
    hooks.pointer = mouse_event;
    hooks.lifecycle = on_lifecycle;
    if (input_script) hooks.tick = inscript_tick;
    if (interactive) {
        port_home = home;
        hooks.drop = on_drop;
    }
    {
        /* a player's run shows the settings screen first (the first run does, as nothing has said
           otherwise), the game waiting; "Show this at start" in the Game tab turns it off */
        char v[16];
        if (interactive && (port_config_get(home, "settings-at-start", v, sizeof v) != 0 || strcmp(v, "0"))) {
            settings_show(1);
            port_pause(1);
        }
    }
    status = plat_run(&cfg, &hooks, game, NULL);
    /* the window closed (or --exit-after) with the game still running: the recording's last
       runs and chunks, which only the game's own exit or a fault wrote before, so that a
       replay of it does not end at a stream it never got */
    port_blackbox_close(0);
    if (getenv("UW2PORT_POS_LOG")) port_report_motion();
    return status;
}
