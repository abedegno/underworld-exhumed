/* mods.c: replaces nothing in the DOS build. The settings a mod for the original game makes in
   its executable, given to the port instead. A mod for DOS changes bytes of the executable
   (KarlClinckspoor/UltimaUnderworldsModManager installs such mods), but the port runs its own
   compiled code and refuses any executable but the GOG release's (mem/fardata.c), so it takes
   them like this:

     start-level   The level a new game starts on (ui/MAINMENU.C's GetLevel(1), a constant in
                   the executable's code). --start-level N on the command line, for this run,
                   or start-level=N in the settings file; 1 to the game's levels (UW1 9, UW2
                   80), "off" for the game's own. A level LEV.ARK has no map for stops the
                   start with a message.
     start-square  The square a new game starts on (ui/MAINMENU.C's player_setup call, which
                   the executable's code holds as constants). --start-square X,Y on the command
                   line, for this run, or start-square=X,Y in the settings file; 1 to 62 each,
                   "off" for the game's own. With either set, a square the player does not
                   fit on gives way to the nearest one that it does (MAINMENU.C's
                   port_start_fit).
     --features    Prints what this build supports, one line each, and exits: for the mod
                   manager, which tells from it which mods this build can play.

   Both change play. A session played with either says so in its log ("mods: ... (not as DOS)"),
   and its recording keeps them in MODS.TXT beside RECORD.OUT (the black box's session folder,
   or the home folder for --record). --replay takes them from the MODS.TXT beside the recording
   it is given, and never from the settings file or the command line, so a recording replays as
   it was played. With neither set the game is exactly as before. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "port.h"
#include "plat.h"
#include "sys/enhance.h"

int port_config_get(const char *home, const char *key, char *out, size_t outsz);
extern int16_t rp_request;
extern int16_t rp_blackbox;
extern const char *rp_blackbox_name;

static int start_x = -1, start_y = -1;          /* -1: the game's own square */
static int start_level = -1;                    /* -1: the game's own level */
static const char *cl_start, *cl_level;         /* the command line's, for this run */
static char mods_error[700];                    /* the last refusal, for main's message box */

/* A refusal: on stderr, and kept for port_mods_error. */
static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(mods_error, sizeof mods_error, fmt, ap);
    va_end(ap);
    fprintf(stderr, PORT_NAME ": mods: %s\n", mods_error);
    return -1;
}

const char *port_mods_error(void) { return mods_error[0] ? mods_error : NULL; }

void port_mods_cl_start(const char *v) { cl_start = v; }
void port_mods_cl_level(const char *v) { cl_level = v; }

/* ui/MAINMENU.C: the start square, x (axis 0) or y (axis 1), or the game's own (dflt) */
int port_start_square(int axis, int dflt)
{
    int v = axis ? start_y : start_x;
    return v < 0 ? dflt : v;
}

/* ui/MAINMENU.C: the start level, or the game's own (dflt) */
int port_start_level(int dflt) { return start_level < 0 ? dflt : start_level; }

/* ui/MAINMENU.C, with a mod's start level or square: where a new game put the player, on
   stderr (a log line a test can check). (x, y) is the square asked for, (nx, ny) the one the
   player fits on near it; found 0: the player fits nowhere near and stands on (x, y) all the
   same, as the original's start leaves it. */
void port_start_placed(int level, int x, int y, int nx, int ny, int found)
{
    if (!found)
        fprintf(stderr, PORT_NAME ": mods: the player fits nowhere near square (%d, %d) and stands on it\n", x, y);
    else if (nx != x || ny != y)
        fprintf(stderr, PORT_NAME ": mods: square (%d, %d) has no room for the player, so the nearest that has\n", x, y);
    fprintf(stderr, PORT_NAME ": mods: a new game starts on level %d, square (%d, %d)\n", level, nx, ny);
}

int port_mods_active(void) { return start_x >= 0 || start_level >= 0; }

static int parse_level(const char *v, const char *where)
{
    int n;
    char extra;
    while (*v == ' ') v++;
    if (!*v || !strcmp(v, "off")) { start_level = -1; return 0; }
    if (sscanf(v, "%d %c", &n, &extra) != 1 || n < 1 || n > PORT_NUM_LEVELS)
        return fail("%s: the start level must be 1 to %d, or off, not \"%s\"", where, PORT_NUM_LEVELS, v);
    start_level = n;
    return 0;
}

/* The start level has a map in the game's DATA\LEV.ARK (or the one in the home folder that a
   mod puts there), which a new game copies to SAVE0: its level block's offset is not 0. */
static int check_level(void)
{
    char path[1200];
    unsigned char b[4];
    long at = PORT_LEVARK_OFFSETS + 4L * (start_level - 1), off = 0;
    unsigned n = 0;
    FILE *f;
    if (start_level < 0) return 0;
    if (plat_resolve("DATA\\LEV.ARK", PLAT_READ, path, sizeof path) || !(f = fopen(path, "rb")))
        return fail("cannot read the game's DATA\\LEV.ARK to check the start level");
    if (fread(b, 1, 2, f) == 2) n = b[0] | b[1] << 8;
    if ((unsigned)start_level <= n && fseek(f, at, SEEK_SET) == 0 && fread(b, 1, 4, f) == 4)
        off = (long)(b[0] | b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24);
    fclose(f);
    if (off == 0) return fail("level %d has no map in %s, so a new game cannot start there", start_level, path);
    return 0;
}

static int parse_start(const char *v, const char *where)
{
    int x, y;
    char extra;
    while (*v == ' ') v++;
    if (!*v || !strcmp(v, "off")) { start_x = start_y = -1; return 0; }
    if (sscanf(v, "%d , %d %c", &x, &y, &extra) != 2 || x < 1 || x > 62 || y < 1 || y > 62) {
        return fail("%s: the start square must be X,Y with each 1 to 62, or off, not \"%s\"", where, v);
    }
    start_x = x;
    start_y = y;
    return 0;
}

/* A recording's MODS.TXT: "start-level N" and "start-square X,Y" lines. */
static int read_sidecar(const char *path, FILE *f)
{
    char line[1200];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *e;
        n++;
        while (isspace((unsigned char)*p)) p++;
        if (!*p || *p == '#') continue;
        e = p + strlen(p);
        while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
        if (!strncmp(p, "start-square ", 13)) {
            if (parse_start(p + 13, path)) return -1;
        } else if (!strncmp(p, "start-level ", 12)) {
            if (parse_level(p + 12, path)) return -1;
        } else {
            return fail("%s:%d: unknown line (a newer port's recording?)", path, n);
        }
    }
    return 0;
}

static void dir_of(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
    if (b && (!s || b > s)) s = b;
    if (!s) snprintf(out, n, ".");
    else snprintf(out, n, "%.*s", (int)(s - path), path);
}

/* main, once the game's folder is known: the mods of this run. A replay's are its recording's
   (MODS.TXT beside it); otherwise the command line's, else the settings file's. */
int port_mods_init(const char *home, const char *replay)
{
    char v[1024], path[1200];
    if (replay) {
        FILE *f;
        if (cl_start || cl_level)
            return fail("a replay takes its recording's mods; give no --start-level or --start-square");
        dir_of(replay, v, sizeof v);
        snprintf(path, sizeof path, "%s/MODS.TXT", v);
        f = fopen(path, "r");
        if (f) {
            int r = read_sidecar(path, f);
            fclose(f);
            if (r) return -1;
        }
    } else {
        if (cl_start) { if (parse_start(cl_start, "--start-square")) return -1; }
        else if (port_config_get(home, "start-square", v, sizeof v) == 0 && parse_start(v, "start-square in the settings file")) return -1;
        if (cl_level) { if (parse_level(cl_level, "--start-level")) return -1; }
        else if (port_config_get(home, "start-level", v, sizeof v) == 0 && parse_level(v, "start-level in the settings file")) return -1;
    }
    if (check_level()) return -1;
    if (port_mods_active()) {
        char lv[24] = "", sq[40] = "";
        if (start_level >= 0) snprintf(lv, sizeof lv, "start level %d", start_level);
        if (start_x >= 0) snprintf(sq, sizeof sq, "%sstart square (%d, %d)", lv[0] ? "; " : "", start_x, start_y);
        fprintf(stderr, PORT_NAME ": mods: %s%s (not as DOS)\n", lv, sq);
    }
    return 0;
}

/* main, once the recording's place is known: MODS.TXT beside it, so that its replay has the
   mods (none: a stale one in the home folder is removed, for --record). */
void port_mods_record(const char *home)
{
    char dir[1100], path[1200];
    FILE *f;
    if (rp_request != 1) return;
    if (rp_blackbox && rp_blackbox_name) dir_of(rp_blackbox_name, dir, sizeof dir);
    else snprintf(dir, sizeof dir, "%s", home);
    snprintf(path, sizeof path, "%s/MODS.TXT", dir);
    if (!port_mods_active()) {
        if (!rp_blackbox) remove(path);
        return;
    }
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, PORT_NAME ": mods: cannot write %s; the recording will replay without the mods\n", path);
        return;
    }
    fprintf(f, "# the mods this session was played with (" PORT_NAME " sys/mods.c); --replay applies them again\n");
    if (start_level >= 0) fprintf(f, "start-level %d\n", start_level);
    if (start_x >= 0) fprintf(f, "start-square %d,%d\n", start_x, start_y);
    fclose(f);
}

/* --features: what this build plays, one line each ("NAME" or "NAME DETAILS"). */
void port_mods_features(FILE *f)
{
    char b[600];
    fprintf(f, "features 1 " PORT_NAME "\n");
    fprintf(f, "start-square\n");
    fprintf(f, "start-level 1-%d\n", PORT_NUM_LEVELS);
    fprintf(f, "data-overlay\n");               /* files in the home's DATA etc. are read before the game's (platform/files.c) */
    /* this game's enhancements and fixes, all of which --enhance takes (the kind masks leave
       out the other game's) */
    fprintf(f, "enhance %s\n", enhance_names(enhance_kind_mask(ENH_PRESENTATION) | enhance_kind_mask(ENH_TIMING)
                                             | enhance_kind_mask(ENH_GAMEPLAY) | enhance_kind_mask(ENH_FIX), b, sizeof b));
}
