/* target: ovr105 */
/* opts: -mm -1 -G -O -Y -d */
/* The cutscene player: the CUTS\csXXX.nXX Deluxe Paint LPF animations and their opcode
   scripts, with subtitles, speech, fades and colour cycling. The whole of UW1's DOS overlay
   ovr105, in original order. Seeded from UW2Decomp's src/gfx/CUTS.C (UW2's ovr108).

   runcutscene(n) plays cutscene n: numbers below 100h fill the screen (the intro, the
   dreams, the ending), numbers from 100h play in the 3D view's window. A cutscene is a
   series of files CUTS\csNNN.nXX (NNN and XX written in octal): .n00 is the opcode script,
   .n01 onwards are LPF animations played one after another. show_anm reads the first
   0x800 bytes of the script into the far work area (stdat) and plays each LPF file in
   turn: its 0xB00-byte header (struct AnmHdr), then its large pages through three 64 KB
   EMS windows (get_cuts_block), the next page read a slice per frame (read_lp_inc). Each
   record (frame) is a whole picture or a run/skip/dump delta, both decoded into the frame
   buffer (cuts_screen) and drawn with show.

   The script is a stream of words: the frame a record fires on, the opcode, then its
   arguments; each cutsop_ handler returns how many argument words it used. Opcodes 0 to
   15, in cuts_dispatch's order: txt, erase, func, pause, skip, next, end, loop, data,
   fadeout, fadein, jump, punt, say, wait, clang.

   UW1 against UW2: one big show_anm does what UW2 splits into cuts_process_anm,
   cuts_process_lp, cuts_process_opcodes, gobble_input_events, cuts_run_pause,
   cuts_draw_text and run_time_critical_things; no virtual screen, pans, palette tasks,
   LBACK backgrounds or music opcodes, and only 16 opcodes (clang plays a sound in UW1).
   Deltas are decoded by the assembly routine seg002_A (UW2's unused LPFDELTA.ASM) into a
   linear buffer rather than by draw_rsd into planar memory. The cutscene state (struct
   CutsState) is two bytes shorter (no loop frame), and its flag bit 0 means drawing, the
   reverse of UW2's skipping bit. File handles are passed to the readers rather than kept
   in stdat.
   UW1 has no symbol-bearing build: names are UW2's (FM Towns) where the routine is the
   same and the name's bssorder key fits the EXE's overlay stub order (verify.py agrees).
   Where UW2's name does not fit, the name is ours, descriptive and chosen for its key:
   get_cut_banks and get_cuts_block (UW2's get_cuts_ems and set_cuts_ems, keys 975 and 987,
   but their stubs come after cutsop_pause's 1019), free_block, anm_lptab and
   read_anim_hdr (UW2's build_lptab, read_anmhdr), read_lp_inc (readlpinc), do_sound
   (anm_sound), cutsop_stop (opcode 5, UW2's cutsop_next), runcutscene (show_cutscene,
   key 803 where its stub needs 923..931), init_cutscene (UW2's empty one; key 281),
   value_cuts and cuts_skipline. Name: inferred, as UW2's. */

#include <dos.h>
#include <string.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include "uw2.h"
/* Turbo C lists publics with equal bssorder keys in the reverse of the order it first sees
   their names, and TLINK numbers the stub entries from that list (the link checks it):
   read_lp_inc must be seen before runcutscene (gfx.h) and cutsop_stop before cutsop_loop
   (the #define below), as UW.EXE's stub table orders them. */
struct LpDesc;
struct CutsState;
int far read_lp_inc(int fd, unsigned page, struct LpDesc far *desc, unsigned n, void far *dst);
int far cutsop_stop(uint16 far *code, struct CutsState *st);
#include "conv.h"
#include "file.h"
#include "gfx.h"
#include "map.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* Declared in each file that uses it, its own way (no header). */
void far grfx_clear(void);

/* A colour cycle of the LPF header, 8 bytes, 16 of them at +0x80. */
struct Cycle { uint16 started, period; char pad4[2]; unsigned char first, last; };

/* A large page's descriptor, 6 bytes: the first record in it, the records and the bytes. */
struct LpDesc {
    uint16 base;                        /* 0x00 */
    uint16 nrecords;                    /* 0x02 */
    uint16 nbytes;                      /* 0x04 */
};

/* The LPF file's header as read_anim_hdr leaves it, 0xB00 bytes (UW2's CUTS.C). */
struct AnmHdr {
    char id[4];                         /* 0x00, "LPF " */
    uint16 maxlps;                      /* 0x04 */
    uint16 nlps;                        /* 0x06, large pages */
    uint32 nrecords;                    /* 0x08 */
    uint16 maxrecsperlp;                /* 0x0C */
    uint16 lptableoffset;               /* 0x0E */
    char contenttype[4];                /* 0x10, "ANIM" */
    uint16 width;                       /* 0x14 */
    uint16 height;                      /* 0x16 */
    unsigned char variant;              /* 0x18 */
    unsigned char version;              /* 0x19 */
    unsigned char lastdelta;            /* 0x1A, the last record is a delta back to the first */
    unsigned char lastdeltavalid;       /* 0x1B */
    unsigned char pixeltype;            /* 0x1C */
    unsigned char compression;          /* 0x1D */
    unsigned char otherrecsperfrm;      /* 0x1E */
    unsigned char bitmaptype;           /* 0x1F */
    unsigned char recordtypes[32];      /* 0x20 */
    uint32 nframes;                     /* 0x40 */
    uint16 rate;                        /* 0x44, frames per second */
    uint16 pad46[29];                   /* 0x46 */
    struct Cycle cycles[16];            /* 0x80 */
    unsigned char palette[0x400];       /* 0x100, 256 of blue, green, red and 0 */
    struct LpDesc lps[256];             /* 0x500 */
};

typedef int (far *CutsOp)(uint16 far *code, struct CutsState *st);

/* UW1's declarations where they differ from the headers (UW2's). */
unsigned char far grfx_load_font(char *name);           /* UW2: grfx_quikfont(int) */
/* SOUND.C's speech (UW1's speech_available returns a char). */
/* TMPALLOC.C's conventional workspace (DS:364A), the frame buffer here. */
extern unsigned char far *conv_ws;

int far cutsop_wait(uint16 far *code, struct CutsState *st);
int far cutsop_skip(uint16 far *code, struct CutsState *st);
int far cutsop_stop(uint16 far *code, struct CutsState *st);

/* Initialised data, DS:1236 to DS:12B0. */
static int16 lp_page = -1;              /* the LPF page read_lp_inc is reading */
static uint16 lp_left = -1;             /* and the bytes of it still to read */
CutsOp cuts_dispatch[16] = {
    cutsop_txt, cutsop_erase, cutsop_func, cutsop_pause, cutsop_skip, cutsop_stop,
    cutsop_end, cutsop_loop, cutsop_data, cutsop_fadeout, cutsop_fadein, cutsop_jump,
    cutsop_punt, cutsop_say, cutsop_wait, cutsop_clang
};

/* Uninitialised data, DS:5656 to DS:565D.
   name: num_buf is UW2's; the other two are ours, chosen for layout (bssorder.py keys
   726, 779, 843). */
int16 num_buf;                          /* DS:5656 */
uint16 cur_frame;                       /* DS:5658, the frame being played */
unsigned char far *cuts_screen;         /* DS:565A, the frame buffer records decode into */

/* 0x0 */
int far get_cut_banks(void)
{
    seg042_19B();
    return num_buf = 3;
}

/* 0x10: maps LPF buffer which (0..2) at physical page 0: the four EMS pages from the
   textures' first page + 4 * which. Returns the EMS frame, or 0. */
unsigned char far * far get_cuts_block(int which)
{
    seg042_19B();
    if (num_buf - 1 < which) return 0L;
    if (seg012_BB(0, seg051_C375 + (which << 2), 4))
        return MK_FP(ems_frame, 0);
    return 0L;
}

/* 0x54 */
void far free_cuts_ems(void)
{
    seg042_19B();
}

/* 0x5E */
char * far makeFourChars_ovr108_671(uint32 value, char *out)
{
    int i;
    for (i = 0; i < 4; i++) out[i] = (value >> (i * 8)) & 0xFF;
    out[4] = 0;
    return out;
}

/* 0x9A: sorts the large page numbers 0..n-1 into order by their first record (a bubble
   sort), giving the order to play them in. */
void far anm_lptab(struct LpDesc far *desc, register unsigned n, unsigned char far *order)
{
    int tmp;
    char changed;
    int i;
    changed = 0;
    for (i = 0; i < n; i++) order[i] = i;
    do {
        changed = 1;
        for (i = 1; i < n; i++) {
            if (desc[order[i - 1]].base > desc[order[i]].base) {
                changed = 0;
                tmp = order[i - 1];
                order[i - 1] = order[i];
                order[i] = tmp;
            }
        }
    } while (!changed);
}

/* 0x13D: reads the LPF header from fd. Each colour cycle's rate word becomes
   (word >> 8) & 0x3F, the value anm_cycle divides by. */
char far read_anim_hdr(int fd, unsigned char far *dst)
{
    int wanted, actual;
    int i;
    unsigned value;
    wanted = 0xB00;
    actual = intoFarBuffer_ovr167_5DA(fd, dst, wanted);
    for (i = 0; i < 16; i++) {
        value = ((struct AnmHdr far *)dst)->cycles[i].period;
        ((struct AnmHdr far *)dst)->cycles[i].period = (value >> 8) & 0x3F;
    }
    return actual == wanted;
}

/* 0x1A4: reads large page page (at 0xB00 + page * 64 KB in the file) into dst; returns
   what intoFarBuffer does, with no return statement. */
int far readlp(int fd, unsigned page, struct LpDesc far *desc, void far *dst)
{
    register int size;
    lseek(fd, ((int32)page << 16) + 0xB00L, 0);
    size = desc->nbytes + desc->nrecords * 2 + 8;
    AX_LAST(intoFarBuffer_ovr167_5DA(fd, dst, size));
}

/* 0x1EC: reads the next n bytes of large page page into dst, continuing where the last
   call stopped (lp_page, lp_left); returns the bytes read. */
int far read_lp_inc(int fd, register unsigned page, struct LpDesc far *desc,
    register unsigned n, void far *dst)
{
    if (page < 0 || n == 0) return 0;
    if (page != lp_page) {
        lp_page = page;
        lseek(fd, ((int32)page << 16) + 0xB00L, 0);
        lp_left = desc->nbytes + desc->nrecords * 2 + 8;
    } else if (lp_left == 0) return 0;
    if (lp_left >= n) lp_left -= n;
    else {
        n = lp_left;
        lp_left = 0;
    }
    return intoFarBuffer_ovr167_5DA(fd, dst, n);
}

/* 0x275: the LPF palette (blue, green, red, pad; 8 bits) to VGA order and 6 bits. */
void far conv_anmpal(unsigned char far *src, unsigned char far *dst)
{
    unsigned char far *pal;
    unsigned char far *output;
    int i;
    pal = src;
    output = dst;
    for (i = 0; i < 256; i++) {
        output[0] = pal[2] >> 2;
        output[1] = pal[1] >> 2;
        output[2] = pal[0] >> 2;
        pal += 4;
        output += 3;
    }
}

/* 0x2E1: rotates each active colour cycle of the LPF once 0x38E / rate ticks have passed
   since it last moved. */
void far anm_cycle(struct Cycle far *cycles)
{
    int i;
    unsigned char n;
    for (i = 0; i < 16; i++) {
        if (cycles[i].period != 0) {
            if ((unsigned)(GAME_TIME() - cycles[i].started) <
                0x38E / cycles[i].period) continue;
            n = cycles[i].last - cycles[i].first + 1;
            rotate_bank(cycles[i].first, n, 0);
            local_do_palette(n, cycles[i].first);
            cycles[i].started = GAME_TIME();
        }
    }
}

/* 0x3AB: the part of stdat above the cutscene player's work area, which SOUND.C's init_voc
   takes for the speech buffers. */
unsigned char far * far free_block(void)
{
    return stdat + 0x2100;
}

/* 0x3B6, opcode 0: txt(colour, string): a subtitle, string string of the cutscene's
   string block, split at newlines and then to lines 320 pixels wide; drawn after the
   frame. Nothing while skipping. */
int far cutsop_txt(uint16 far *code, struct CutsState *st)
{
    char far *text;
    char far *next;
    int width;
    int used;
    char saved;
    int parts;
    char far *lines[6];
    register int count, line;
    if (!st->flags.bit.b0) return 2;
    st->color38 = *(unsigned char far *)code;
    text = get_string(code[1]);
    lines[0] = text;
    line = count = parts = 0;
    while ((lines[line + 1] = FindStringDelimiter(lines[line], '\n')) != 0 && line < 6) {
        line++;
        *lines[line]++ = 0;
    }
    for (line = 0; line < 6 && lines[line] && count < 6; line++) {
        next = lines[line];
        used = 0;
        while (next) {
            text = next;
            if ((next = FindStringDelimiter(text, ' ')) != 0) {
                next++;
                saved = *next;
                *next = 0;
                width = string_width(text);
                *next = saved;
            } else {
                width = string_width(text);
                if (used + width <= 320) {
                    st->text_lines[count++] = lines[line];
                    parts++;
                }
            }
            if (used + width > 320) {
                text[-1] = 0;
                st->text_lines[count++] = lines[line];
                lines[line] = next = text;
                parts++;
                used = 0;
            } else used += width;
        }
    }
    st->flag39 = parts < 6 ? parts : 6;
    return 2;
}

/* 0x5B6, opcode 1 */
int far cutsop_erase(uint16 far *code, struct CutsState *st)
{
    st->flag39 = 0;
    return 0;
}

/* 0x5C5, opcode 2 */
int far cutsop_func(uint16 far *code, struct CutsState *st)
{
    return 2;
}

/* 0x5CD, opcode 3: pause(n): hold this frame for n * 256 ticks or until a key. */
int far cutsop_pause(uint16 far *code, register struct CutsState *st)
{
    if (!st->flags.bit.b0) return 1;
    st->flags.bit.b1 = 0;
    st->frame3D = code[-2];
    st->frame3F = code[0];
    return 1;
}

/* 0x5FA, opcode 14: wait(a, b): pause for a, or for b, ignoring keys, when speech is on. */
int far cutsop_wait(uint16 far *code, register struct CutsState *st)
{
    if (!st->flags.bit.b0) return 2;
    st->flags.bit.b1 = 0;
    st->frame3D = code[-2];
    if (st->flags.bit.b5) {
        st->flags.bit.b7 = 1;
        st->frame3F = code[1];
    } else st->frame3F = code[0];
    return 2;
}

/* 0x641, opcode 4: skip(frame, n): a key pressed from here skips to frame; without
   speech, also pauses n before it. */
int far cutsop_skip(uint16 far *code, register struct CutsState *st)
{
    if (!st->flags.bit.b0) return 2;
    st->flags.bit.b1 = 0;
    st->frame3B = code[0];
    if (!st->flags.bit.b5) {
        st->frame3F = code[1];
        st->frame3D = code[0] - 1;
    }
    return 2;
}

/* 0x682, opcode 5: next: end this LPF file and go on to the next. UW1 returns 1. */
int far cutsop_stop(uint16 far *code, struct CutsState *st)
{
    st->flags.bit.b2 = 0;
    return 1;
}

/* 0x691, opcode 6: end: end the cutscene after this file. */
int far cutsop_end(uint16 far *code, register struct CutsState *st)
{
    st->flags.bit.b2 = 0;
    st->flags.bit.b3 = 0;
    return 0;
}

/* 0x6A5, opcode 7: loop(n): replay the file n times from the frame after this one. */
int far cutsop_loop(uint16 far *code, register struct CutsState *st)
{
    st->repeat41 = code[0];
    st->frame3B = code[-2] + 1;
    st->frame3D = 0;
    st->flags.bit.b1 = 0;
    return 1;
}

/* 0x6CC, opcode 8: data(file, ext): the next file's name. */
int far cutsop_data(uint16 far *code, register struct CutsState *st)
{
    st->name[7] = ((code[0] >> 6) & 7) + '0';
    st->name[8] = ((code[0] >> 3) & 7) + '0';
    st->name[9] = (*(unsigned char far *)code & 7) + '0';
    st->name[12] = ((code[1] >> 3) & 7) + '0';
    st->name[13] = (*(unsigned char far *)&code[1] & 7) + '0';
    return 2;
}

/* 0x719, opcode 13: say(colour, string, voc): speech file BSP<voc>.VOC, started by
   show_anm, or its subtitle (txt) when speech is off or cannot start; 999 is text only. */
int far cutsop_say(uint16 far *code, register struct CutsState *st)
{
    if (st->flags.bit.b5) {
        if (!st->flags.bit.b6 && !init_voc()) {
            st->speech43 = -1;
            st->flags.bit.b5 = 0;
            cutsop_txt(code, st);
        } else {
            st->speech43 = code[2];
            if (code[2] == 999) st->speech43 = -1;
        }
    } else cutsop_txt(code, st);
    return 3;
}

/* 0x77C, opcode 9: fadeout(n): fade out over n, full screen only. */
int far cutsop_fadeout(uint16 far *code, register struct CutsState *st)
{
    if (st->windowed == 0 && st->fade47 > -2) st->fade47 = code[0];
    return 1;
}

/* 0x79E, opcode 10: fadein(n). */
int far cutsop_fadein(uint16 far *code, register struct CutsState *st)
{
    if (st->windowed == 0 && st->fade45 > -2) st->fade45 = code[0];
    return 1;
}

/* 0x7C0, opcode 11: jump(frame): skip ahead to frame, unless it is the next one. */
int far cutsop_jump(uint16 far *code, register struct CutsState *st)
{
    if (code[-2] != (uint16)(code[0] - 1)) {
        st->frame3B = code[0] - 1;
        st->frame3D = 0;
        st->frame3F = 0;
        st->flags.bit.b1 = 1;
        st->flags.bit.b0 = 0;
    }
    return 1;
}

/* 0x7FD, opcode 12: punt(flag): whether Escape may end the cutscene. */
int far cutsop_punt(uint16 far *code, struct CutsState *st)
{
    st->flags.bit.b4 = code[0];
    return 1;
}

/* 0x81B, opcode 15: clang: UW1 plays sound effect 11h. */
int far cutsop_clang(uint16 far *code, struct CutsState *st)
{
    play_effect_here(0x11, 0x40, 0);
    return 0;
}

/* 0x830: music, and the speech while it plays. */
void far do_sound(register struct CutsState *st)
{
    loop_music_maybe();
    if (st->flags.bit.b6 && st->speech43 != -1) {
        update_speech();
        if (speech_over()) {
            st->flags.bit.b6 = 0;
            st->speech43 = -1;
        }
    }
}

/* 0x869: plays cutscene cuts in the window x, y, w, h (y the top row, bottom-up): loads
   the script, then plays .n01, .n02 ... until an end, Escape, or a missing file. For each
   file: the header, palette and first large pages, then every record of every large page
   in order: decoded into cuts_screen and shown (while drawing), the subtitle, the fades,
   the speech, the next page read a slice further, the frame's script records, and the
   wait until 256 / rate ticks have passed, then any pause. */
void far show_anm(int cuts, int x, int y, int w, int h)
{
    int n00_fd;
    int anm_fd;
    unsigned char lp;
    unsigned char far *ems;
    unsigned char far *n0x;
    struct AnmHdr far *hdr;
    struct LpDesc far *desc;
    unsigned char far *lptab;
    uint16 far *sizes;
    unsigned char far *data;
    uint16 far *n00;
    uint16 far *code;
    int ahead_page;
    int ems_page;
    unsigned next_size;
    int read_off;
    unsigned char ahead_lp;
    int got;
    int off;
    unsigned char hb;
    unsigned char first;
    unsigned i;
    int rec;
    int recno;
    int wd;
    uint32 now;
    uint32 frame_start;
    uint32 input_time;
    int last;
    char reading;
    unsigned char far *src;
    int tx;
    struct CutsState st;
    register int key, in;

    /* flag b7 (a pause waiting for the speech) is never set before the pause loop reads it:
       in DOS it is whatever the stack held, so a pause runs on until the speech is over, or
       not; the replay build and the port take 0, the path with no wait */
    STACK_JUNK_SET(st.flags.bit.b7, 0);
    strcpy(st.name, "CUTS\\csXXX.nXX");
    st.x = x;
    st.y = y;
    st.w = w;
    st.h = h;
    st.flags.bit.b6 = 0;
    st.frame3B = st.frame3D = st.frame3F = 0;
    if (x == 0 && y == 0xC7 && w == 0x140 && h == 0xC8) st.windowed = 0;
    else st.windowed = 1;
    st.speech43 = -1;
    st.flags.bit.b5 = speech_available();
    n0x = stdat;
    st.palette = n0x + 0xB00;
    lptab = st.palette + 0x300;
    sizes = (uint16 far *)(lptab + 0x100);
    n00 = sizes + 0x100;
    st.name[7] = ((cuts >> 6) & 7) + '0';
    st.name[8] = ((cuts >> 3) & 7) + '0';
    st.name[9] = (cuts & 7) + '0';
    st.name[12] = '0';
    st.name[13] = '0';
    if (get_cut_banks() >= 2) {
        if ((n00_fd = open(st.name, O_RDONLY)) == -1) {
            seg042_19B();
            return;
        }
        intoFarBuffer_ovr167_5DA(n00_fd, n00, 0x800);
        code = n00;
        st.flags.bit.b3 = 1;
        st.flags.bit.b0 = 1;
        st.flags.bit.b4 = 1;
        st.fade45 = -1;
        st.fade47 = -2;
        FAR_COPY(st.palette, palette, 0x300);
        if (st.windowed == 0) fadeout(st.palette, 2);
        grSoftPageFlip();
        copy_visible_to_hidden();
        if (++st.name[13] > '7') {
            st.name[12]++;
            st.name[13] = '0';
        }
        while (*code == 0) {
            code += 2;
            if (code[-1] < 0x10) code += cuts_dispatch[code[-1]](code, &st);
        }
        while ((anm_fd = open(st.name, O_RDONLY | O_BINARY)) > 0 && st.flags.bit.b3) {
            lp_page = -1;
            if (!read_anim_hdr(anm_fd, n0x)) goto fail;
            hdr = (struct AnmHdr far *)n0x;
            desc = (struct LpDesc far *)(n0x + 0x500);
            if (st.windowed == 0) conv_anmpal(n0x + 0x100, st.palette);
            now = input_time = GAME_TIME();
            anm_lptab(desc, hdr->nlps, lptab);
            st.repeat41 = 0;
            if (++st.name[13] > '7') {
                st.name[12]++;
                st.name[13] = '0';
            }
            st.flag39 = 0;
restart:
            ahead_page = ems_page = -1;
            reading = 0;
            if (st.flags.bit.b3) st.flags.bit.b2 = 1;
            cur_frame = 1;
            for (i = 0; i < num_buf && i < hdr->nlps; i++) {
                ems = get_cuts_block(i);
                if (readlp(anm_fd, lptab[i], &desc[lptab[i]], ems) == -1) goto fail;
            }
            ahead_lp = i - 1;
            while (*code == 0 && st.flags.bit.b2 && st.repeat41 == 0) {
                code += 2;
                if (code[-1] < 0x10) code += cuts_dispatch[code[-1]](code, &st);
            }
            for (i = 0; i < hdr->nlps && st.flags.bit.b2; i++) {
                recno = 0;
                lp = lptab[i];
                if (++ems_page >= num_buf) ems_page = 0;
                ems = get_cuts_block(ems_page);
                src = ems + 8;
                FAR_COPY(sizes, src, desc[lp].nrecords * 2);
                data = src + desc[lp].nrecords * 2;
                first = 1;
                last = hdr->nlps - 1 == i && hdr->lastdelta ? 1 : 0;
                st.frame3D = st.frame3F = 0;
                st.flags.bit.b1 = 0;
                for (rec = 0; desc[lp].nrecords - last > rec && st.flags.bit.b2; rec++) {
                    hb = data[1];
                    wd = (*(uint16 far *)(data + 2) + 1) & ~1;
                    off = hb ? wd + 4 : 2;
                    ems = get_cuts_block(ems_page);
                    frame_start = now;
                    if (sizes[recno] != 0 && sizes[recno] - off != 0) {
                        if (data[off] == 0)
                            FAR_COPY(cuts_screen, data + off + 2, 0xFA00);
                        else if (data[off] == 1)
                            seg002_A(data + off + 2, cuts_screen);
                    }
                    if (st.flags.bit.b0) {
                        show(x, y, cuts_screen, 0xC8, 0x140, 0x140 - w, 0xC8 - h);
                        do {
                            key = -1;
                            anm_cycle(((struct AnmHdr far *)n0x)->cycles);
                            do_sound(&st);
                            get_cuts_block(ems_page);
                            while ((in = mouse_get_input()) > 3) key = in;
                            if (in > -1 && in < 4) key = in;
                            ems = get_cuts_block(ems_page);
                            if (key != -1 && st.speech43 == -1 &&
                                (key > 3 || GAME_TIME() - input_time > 0x40)) {
                                st.flags.bit.b1 = 1;
                                input_time = GAME_TIME();
                            }
                            if (key == KEY_ESC && st.flags.bit.b4) {
                                close(anm_fd);
                                goto done;
                            }
                        } while ((now = GAME_TIME()) - frame_start < 0x100 / hdr->rate);
                        if (cur_frame < st.frame3B && st.flags.bit.b1) {
                            if (st.fade47 == -2) st.flags.bit.b1 = 0;
                            else {
                                st.flags.bit.b0 = 0;
                                st.frame3F = 0;
                                st.frame3D = 0;
                                st.repeat41 = 0;
                            }
                        }
                    } else if (cur_frame == st.frame3B) {
                        st.frame3B = st.frame3D = st.frame3F = 0;
                        st.flags.bit.b0 = 1;
                        st.flags.bit.b1 = 0;
                    }
                    while (st.flags.bit.b2 && *code == cur_frame && st.repeat41 == 0) {
                        if (n00 + 0x3FD > code) {
                            code += 2;
                            if (code[-1] < 0x10) code += cuts_dispatch[code[-1]](code, &st);
                        } else {
                            lseek(n00_fd, code - n00 - 0x400L, 1);
                            intoFarBuffer_ovr167_5DA(n00_fd, n00, 0x800);
                            code = n00;
                        }
                    }
                    if (st.flags.bit.b0) {
                        if (st.flag39 > 0) {
                            key = 0;
                            *foreground_color = st.color38;
                            *background_color = st.color38;
                            in = st.y - st.h + cur_font->height * st.flag39 + 2;
                            for (key = 0; key < st.flag39; key++) {
                                tx = (st.x + st.w - string_width(st.text_lines[key])) / 2;
                                string_to_screen(st.text_lines[key], tx, in);
                                in -= cur_font->height;
                            }
                        }
                        frame_start = now;
                        grPageFlip();
                        if (st.fade45 > -1) {
                            fadein(st.palette, st.fade45);
                            st.fade45 = -2;
                            st.fade47 = -1;
                        }
                    }
                    if (st.flags.bit.b5 && !st.flags.bit.b6 && st.speech43 > -1 &&
                        st.speech43 < 999) {
                        play_speech(st.speech43);
                        st.flags.bit.b6 = 1;
                    }
                    data += sizes[recno];
                    if (desc[lp].nrecords - 1 == rec) next_size = -1;
                    else next_size = sizes[recno];
                    if (!reading && first) {
                        read_off = 0;
                        reading = 1;
                        ahead_lp++;
                        if (++ahead_page >= num_buf) ahead_page = 0;
                    }
                    if (hdr->nlps > ahead_lp) {
                        ems = get_cuts_block(ahead_page);
                        got = read_lp_inc(anm_fd, lptab[ahead_lp], &desc[lptab[ahead_lp]],
                            next_size, ems + read_off);
                        if (got != -1) read_off += got;
                        if ((got == 0 || got < next_size) && next_size != 0) reading = 0;
                    }
                    if (st.frame3B - 1 == cur_frame && st.repeat41 != 0) {
                        lp_page = -1;
                        st.repeat41--;
                        goto restart;
                    }
                    while (cur_frame == st.frame3D && (st.frame3F == 999 ||
                           (GAME_TIME() - frame_start) >> 8 < st.frame3F || st.flags.bit.b7)) {
                        key = -1;
                        anm_cycle(((struct AnmHdr far *)n0x)->cycles);
                        do_sound(&st);
                        get_cuts_block(ems_page);
                        if (st.flags.bit.b7 && speech_over()) {
                            st.flags.bit.b7 = 0;
                            st.frame3F += (int)((GAME_TIME() - frame_start) >> 8);
                        }
                        while ((in = mouse_get_input()) > 3) key = in;
                        if (in > -1 && in < 4) key = in;
                        if (key == KEY_ESC && st.flags.bit.b4) {
                            close(anm_fd);
                            goto done;
                        }
                        st.flags.bit.b1 |= key != -1 && st.speech43 == -1 &&
                            (key > 3 || GAME_TIME() - input_time > 0x40);
                        if (st.flags.bit.b1) {
                            st.flags.bit.b1 = 0;
                            break;
                        }
                    }
                    if (st.fade47 > -1) {
                        fadeout(st.palette, st.fade47);
                        st.fade47 = -2;
                        st.fade45 = -1;
                    }
                    do_sound(&st);
                    get_cuts_block(ems_page);
                    first = 0;
                    recno++;
                    cur_frame++;
                }
            }
            close(anm_fd);
        }
        if (st.name[14] == '0' && st.name[15] == '1') ;
done:
        if (st.speech43 != -1) stop_speech();
        grSoftPageFlip();
        if (st.windowed == 0) {
            if (st.fade47 != -2) fadeout(st.palette, 2);
            grfx_clear();
        }
fail:
        free_cuts_ems();
    }
    free_speech_stuff();
    close(n00_fd);
}

/* 0x1402: the entry point (see the file comment). Sets the big font and the string block
   C00h + n, plays it, then puts the game screen back. Cutscenes 1 to 3 start music theme 4;
   cutscene 103h's window is a few rows taller. */
void far runcutscene(register unsigned n)
{
    int x;
    int y;
    int w;
    register int h;
#ifndef __TURBOC__
    port_shared_story(1);
    if (getenv("UW_COOP_TEST_TRACE")) fprintf(stderr, "SHARED SCENE %u begin guest=%d\n", n, port_shared_guest());
#endif
    if (n < 0x100) {
        x = 0;
        y = 0xC7;
        w = 0x140;
        h = 0xC8;
    } else {
        x = 0x34;
        y = 0xB4;
        w = 0xAC;
        h = 0x70;
    }
    if (n == 1 || n == 2 || n == 3) load_new_music(4, 1);
    grfx_load_font("fontbig.sys");
    CutsceneOrConversationStringBlock = n + STRBLK_CUTSCENE;
    mouse_hide();
    if (n == 0x103) {
        y += 9;
        h += 0xB;
    }
    show_anm(n, x, y, w, h);
    grfx_load_font("font5x6p.sys");
    if (in_game) load_txtmaps();
    if (n < 0x100) {
        if (inplist->mode == MODE_GAME) newscr(MODE_GAME);
        else if (inplist->mode != 0) {
            grfx_quikpal(PAL_GAME);
            editchng(0x7FFE);
        }
    } else editchng(2);
    mouse_show();
#ifndef __TURBOC__
    if (getenv("UW_COOP_TEST_TRACE")) fprintf(stderr, "SHARED SCENE %u completed\n", n);
    port_shared_story(0);
#endif
}

/* 0x14E3, at start-up (UWEDIT.C): the frame buffer is TMPALLOC.C's conventional
   workspace. */
void far init_cutscene(void)
{
    cuts_screen = conv_ws;
}

/* 0x14F6: shows cutscene n with value written over the words at byte offsets 4, 6 and 12
   of its script csNNN.n00, the first record's frame and arguments (PANELS.C: cutscene
   100h with the dungeon level, 101h with a grave's number). */
void far value_cuts(unsigned n, int value)
{
    char ok;
    char name[16];
    register int fd;
    strcpy(name, "CUTS\\csXXX.n00");
    name[7] = ((n >> 6) & 7) + '0';
    name[8] = ((n >> 3) & 7) + '0';
    name[9] = (n & 7) + '0';
    ok = (fd = open(name, O_RDWR | O_BINARY)) != -1;
    ok &= lseek(fd, 4L, 0) != -1L;
    ok &= write(fd, &value, 2) == 2;
    ok &= write(fd, &value, 2) == 2;
    ok &= lseek(fd, 4L, 1) != -1L;
    ok &= write(fd, &value, 2) == 2;
    ok &= close(fd) != -1;
    if (!ok) return;
    runcutscene(n);
}

/* 0x1639: reads a line from fp and throws it away (UWEDIT.C, on DATA\UW.CFG). */
void far cuts_skipline(FILE *fp)
{
    char line[100];
    fgets(line, 99, fp);
}
