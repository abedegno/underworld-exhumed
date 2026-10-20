/* target: ovr108 */
/* opts: -mm -1 -G -O -Y -d */
/* The cutscene player: speech streamed from SOUND\BSPnn.VOC through EMS, the CUTS\csXXX.nXX
   Deluxe Paint LPF animations and their opcode scripts, palette fades and colour cycling run
   as time-based tasks, subtitles, and the LBACK backgrounds. The whole of DOS overlay ovr108,
   in original order.

   show_cutscene(n) plays cutscene n: numbers below 100h fill the screen (the intro, the
   dreams, the ending, the credits), numbers from 100h play in the 3D view's window (pictures shown by
   looking at or using an object, LOOK.C and USEITEMS.C, and by spells). A cutscene is a
   series of files CUTS\csNNN.nXX (NNN and XX written in octal): .n00 is the opcode script,
   and .n01 onwards are Deluxe Paint Animator LPF animations played one after another.
   show_anm reads the first 0x400 bytes of the script into stdat (the far work area,
   struct Stdat) and loops over the LPF files; cuts_process_anm opens one, reads its
   0xB00-byte header (struct AnmHdr), and plays its large pages with cuts_process_lp, which
   draws each record (frame) and runs the script's opcodes for that frame.

   The script is a stream of words: a record is the frame number it fires on, the opcode,
   then the opcode's arguments. cuts_process_opcodes runs every record for the current
   frame through cuts_dispatch; each cutsop_ handler returns how many argument words it
   used, so the stream can be stepped without a length field. Frame 997 (3E5h) runs before
   a file's LPF is opened and frame 999 (3E7h) after its last frame. Opcode 0 to 27 are, in
   cuts_dispatch's order: txt, erase, func, pause, skip, next, end, loop, data, fadeout,
   fadein, jump, punt, say, wait, clang, palrange, palfade, palset, palsimplefade, vscreen,
   focus, lback, pan, splity, music, test, wait_for_sound. func, palset, clang and test do
   nothing in DOS but skip their arguments.

   Memory: the LPF's 64 KB large pages are read into three 64 KB EMS windows (set_cuts_ems),
   which reuse the EMS pages of the level's textures, so show_cutscene reloads the textures
   afterwards (load_txtmaps) when a game is running. While one page plays, the next is read
   a slice per frame (readlpinc). Speech is a SOUND\BSPnn.VOC file streamed through four EMS
   pages and two 2 KB AIL buffers (big_speech_play, update_big_speech); the cutscene loop
   keeps feeding it. Palette fades scheduled by the script run as time-based tasks: up to
   16 callbacks that run_timebased_tasks steps every 8 ticks of *Time.

   Drawing: a record is either a whole frame, drawn with show (GRCORE.ASM), or a run/skip/
   dump delta, which draw_rsd writes straight into planar VGA memory. Wide scenes use a
   virtual screen larger than 320 by 200 (virtual_screen, through the graphics library),
   panned with vscreen_focus, with LBACK backgrounds loaded from CUTS\lbackNNN.byt.

   name: inferred, from the cuts_ and cutsop_ prefixes of the FM Towns names. Names are the
   FM Towns originals where it has the function, matched by position among the
   neighbours, by the opcode table (_cuts_dispatch gives each cutsop_ its number) and by the
   same callees and globals. FM Towns has no counterpart of makeFourChars_ovr108_671,
   writeCutsValue_ovr108_2EAC, record_task, MovePanView_ovr108_3333 or bufferPointer; it has
   get_token_, draw_into_buffer_ and do_update_ where DOS has them inside draw_rsd and
   cuts_process_lp. virtual_screen and lback_vscreen sit elsewhere in FM Towns but here in
   DOS.
   match: the provisional names were chosen for their keys: Turbo C lists a file's publics
   by the tools/bssorder.py key of each name and TLINK numbers overlay stub entries from the
   last one listed, so these names reproduce the EXE's stub order (the target table keeps
   IDA's names). */

#include <dos.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stat.h>
#include "conv.h"
#include "file.h"
#include "gfx.h"
#include "map.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* A colour cycle of the LPF header, 8 bytes, 16 of them at +0x80. */
struct Cycle { uint16 started, period; char pad4[2]; unsigned char first, last; };

/* The cutscene player's far work area (seg049). */
HOST_LAYOUT_BEGIN
struct Stdat {
    int16 n0x_fd, pad2;                 /* 0x00, the opcode file */
    int16 anm_fd, pad6;                 /* 0x04, the LPF file */
    int16 ems_page, padA;               /* 0x08 */
    int16 lp_index, padE;               /* 0x0C */
    int16 ahead_page, pad12;            /* 0x10 */
    int16 ahead_index, pad16;           /* 0x14 */
    int16 frame, pad1A;                 /* 0x18 */
    uint16 far *code;                   /* 0x1C, the next opcode */
    unsigned char far *n00;             /* 0x20, the opcode buffer, then the LPF header */
    unsigned char far *screen;          /* 0x24 */
    unsigned char far *lptab;           /* 0x28 */
    uint32 start;                       /* 0x2C */
    uint32 tick;                        /* 0x30 */
    char pad34[4];
    unsigned char n00buf[0x400];        /* 0x38 */
};
HOST_LAYOUT_END

/* A large page's descriptor, 6 bytes: the first record in it, the records and the bytes. */
struct LpDesc {
    uint16 base;                        /* 0x00 */
    uint16 nrecords;                    /* 0x02 */
    uint16 nbytes;                      /* 0x04 */
};

/* A large page as readlp reads it into EMS: its descriptor again, a pad word, the sizes of
   its records, then the records. */
struct LpPage {
    struct LpDesc desc;                 /* 0x00 */
    uint16 pad;                         /* 0x06 */
    uint16 sizes[1];                    /* 0x08, nrecords of them */
};

/* The LPF file's header as read_anmhdr leaves it, 0xB00 bytes: colour cycles at 0x80, the
   palette at 0x100, the large page descriptors at 0x500. Field names are those of
   DeluxePaint Animate's published LPF layout; the code reads nlps, lastdelta, rate and the
   three tables. */
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
/* Large page n's descriptor */
#define LPDESC(anm, n) (&(anm)->lps[n])

typedef int (far *CutsOp)(uint16 far *code, struct CutsState *st);

/* stdat as the cutscene player's state. */
#define STDAT (*(struct Stdat far *)stdat)

/* EMS and files. */
/* The first EMS page of the speech being streamed: the one-byte far variable at 6388:0000.
   cutsop_say passes sound_fpage, as FM Towns' cutsop_say_ does (_sound_fpage).
   name: FM Towns has it as a static (_task_sofar+0x22, beside sp_npages and audio_inpage at
   +0x20 and +0x21), so the name is provisional.
   match: far, so its own segment (segment table entry 77). */
static unsigned char far speech_fpage;

#define SPEECH_BUF(o) MK_FP(EmsBuff + 0x800, (o))
#define RESTORE_EMS() \
    if (ws_active) seg013_1D3C_E4(0, 0, 4); \
    else if (obj_inpage1 != 0xFF) MapMemory_seg013_1D3C_C7(2, obj_inpage1)

/* Initialised data, DS:1064 to DS:1141. */
static int16 lp_page = -1;              /* the LPF page readlpinc is reading */
static int16 lp_left = -1;              /* and the bytes of it still to read */
int16 pan_dx[4] = { 0, 1, 0, -1 };
int16 pan_dy[4] = { 1, 0, -1, 0 };
int16 focus_x = 0;
int16 focus_y = 0xC7;
int16 splity = -1;
CutsOp cuts_dispatch[29] = {
    cutsop_txt, cutsop_erase, cutsop_func, cutsop_pause, cutsop_skip, cutsop_next,
    cutsop_end, cutsop_loop, cutsop_data, cutsop_fadeout, cutsop_fadein, cutsop_jump,
    cutsop_punt, cutsop_say, cutsop_wait, cutsop_clang, cutsop_palrange, cutsop_palfade,
    cutsop_palset, cutsop_palsimplefade, cutsop_vscreen, cutsop_focus, cutsop_lback,
    cutsop_pan, cutsop_splity, cutsop_music, cutsop_test, cutsop_wait_for_sound, 0
};
unsigned char lmask[4] = { 0x0F, 0x0E, 0x0C, 0x08 };    /* planes from the first pixel on */
unsigned char rmask[4] = { 0x01, 0x03, 0x07, 0x0F };    /* planes up to the last pixel */

/* Uninitialised data, DS:5550 to DS:5D1B.
   match: Turbo C lays _BSS out by a hash of the names (tools/bssorder.py). The publics are
   the FM Towns names; FM Towns has no names for the statics, so theirs are ours, chosen to
   land where the EXE has them. */
static uint32 tclock;                   /* DS:5550 (12) */
static uint32 prev_time;                /* DS:5554 (24) */
static int16 prevunused;                /* DS:5558 (24), never referenced */
int16 task_residue[16];                 /* DS:555A (28) */
static int32 voc_left;                  /* DS:557A (166), speech bytes not yet queued */
unsigned char far *task_pointer_data[16];   /* DS:557E (340) */
static int32 sp_pos;                    /* DS:55BE (363), the next chunk's place in the file */
static int32 sp_nread;                  /* DS:55C2 (379), bytes read into EMS */
static int32 voc_length;                /* DS:55C6 (406) */
unsigned char start_pal[0x300];         /* DS:55CA (443) */
static unsigned char sp_npages;         /* DS:58CA (475), EMS pages holding speech */
Task tasks[16];                         /* DS:58CC (524) */
int16 pan_dir;                          /* DS:590C (632) */
static int16 audio_fd;                  /* DS:590E (657) */
int16 pan_step;                         /* DS:5910 (664) */
int16 num_buf;                          /* DS:5912 (726) */
int16 task_sofar[16];                   /* DS:5914 (764) */
int16 task_total[16];                   /* DS:5934 (764) */
int16 task_data1[16];                   /* DS:5954 (764) */
int16 task_data2[16];                   /* DS:5974 (764) */
static uint32 task_time[16];            /* DS:5994 (796) */
int16 task_flags[16];                   /* DS:59D4 (812) */
unsigned char end_pal[0x300];           /* DS:59F4 (813) */
static int16 cuts_number;               /* DS:5CF4 (843) */
static unsigned char audio_inpage;      /* DS:5CF6 (921), the speech page last read */
int16 logw;                             /* DS:5CF8 (932) */
int16 logh;                             /* DS:5CFA (932) */
int16 task_period[16];                  /* DS:5CFC (940) */

/* 0x0: starts SOUND\BSP<file>.VOC: reads up to pages 16 KB EMS pages of it from page on
   (the rest is read later by update_big_speech), queues the first 2 KB chunk, header
   included, on AIL buffer 0 at volume and pan. Returns 100, or 0xFF if speech is off or
   the file or the driver fails (a driver failure turns speech off for good). */
unsigned char far big_speech_play(unsigned char file, unsigned char volume,
    unsigned char pan, unsigned char page, unsigned char pages)
{
    char num[4];
    int32 amount;
    char name[80];
    struct stat sb;
    register int i, buf;

    if (!speechok) return 0xFF;
    if (pages < 1) return 0xFF;
    if (audio_fd != -1) close(audio_fd);
    audio_fd = -1;
    strcpy(name, "SOUND\\");
    strcat(name, "BSP");
    if (file < 10) strcat(name, "0");
    strcat(name, itoa(file, num, 10));
    strcat(name, ".VOC");
    if ((audio_fd = open(name, 0x8001)) == -1) goto fail;
    if (fstat(audio_fd, &sb) == -1) goto fail;
    voc_length = sb.st_size;
    speech_fpage = page;
    sp_npages = pages;
    sp_nread = 0;
    for (i = 0; i < pages; i++) {
        MapMemory_seg013_1D3C_C7(2, page + i);
        sp_nread += intoFarBuffer_ovr167_5DA(audio_fd, SPEECH_BUF(0), 0x4000);
        RESTORE_EMS();
        if (eof(audio_fd)) {
            close(audio_fd);
            audio_fd = -1;
            break;
        }
    }
    voc_left = voc_length;
    audio_inpage = 0;
    buf = 0;
    MapMemory_seg013_1D3C_C7(2, page);
    amount = voc_left > 0x800 ? 0x800 : voc_left;
    FAR_COPY(dsdata[buf], SPEECH_BUF(0), amount);
    RESTORE_EMS();
    if (!AIL_index_VOC_block(sphdriver, dsdata[buf], -1, &dsbuf[buf])) goto fail_sound;
    dsbuf[1] = dsbuf[0];
    voc_left -= amount + 0x20;
    sp_pos = amount;
    dsbuf[buf].len = amount - 0x20;
    AIL_register_sound_buffer(sphdriver, buf, &dsbuf[buf]);
    AIL_set_digital_playback_volume(sphdriver, volume);
    AIL_set_digital_playback_panpot(sphdriver, pan);
    return 100;
fail_sound:
    speechok = 0;
    kill_all_digi_effects();
fail:
    close(audio_fd);
    return 0xFF;
}

/* 0x32F: refills each of the two AIL buffers that has finished (status 3) with the next
   2 KB of speech, reading the next 16 KB of the file into the EMS ring when playback
   crosses into a new page, and restarts playback. Called every frame and from the fades. */
void far update_big_speech(void)
{
    int32 amount;
    unsigned char page;
    char done[2];
    int32 off;
    register int i;

    done[0] = done[1] = 0;
    for (i = 0; i < 2; i++) {
        if (SND_READ(sphdriver, AIL_sound_buffer_status(sphdriver, i)) == 3 && voc_left > 0) {
            done[i] = 1;
            off = sp_pos & 0x3FFF;
            page = sp_pos >> 14;
            if (page != audio_inpage) {
                if (sp_nread < voc_length) {
                    MapMemory_seg013_1D3C_C7(2, speech_fpage + audio_inpage % sp_npages);
                    sp_nread += intoFarBuffer_ovr167_5DA(audio_fd, SPEECH_BUF(0), 0x4000);
                    RESTORE_EMS();
                    if (sp_nread >= voc_length) {
                        close(audio_fd);
                        audio_fd = -1;
                    }
                }
                audio_inpage = page;
            }
            page = page % sp_npages;
            MapMemory_seg013_1D3C_C7(2, speech_fpage + page);
            amount = voc_left > 0x800 ? 0x800 : voc_left;
            FAR_COPY(dsdata[i], SPEECH_BUF(off), amount);
            RESTORE_EMS();
            voc_left -= amount;
            sp_pos += amount;
            dsbuf[i].data = dsdata[i];
            dsbuf[i].len = amount;
            AIL_register_sound_buffer(sphdriver, i, &dsbuf[i]);
        }
    }
    AIL_start_digital_playback(sphdriver);
}

/* 0x5A8 */
unsigned char far speech_over(void)
{
    int a, b;
    if (!speechok) return 1;
    a = SND_READ(sphdriver, AIL_sound_buffer_status(sphdriver, 0));
    b = SND_READ(sphdriver, AIL_sound_buffer_status(sphdriver, 1));
    if (a == 3 && b == 3 && voc_left == 0) return 1;
    return 0;
}

/* 0x5F7 */
void far cutoff_cutscene_speech(void)
{
    voc_left = 0;
    AIL_stop_digital_playback(sphdriver);
}

/* 0x613 */
int far get_cuts_ems(void)
{
    seg042_35ED_12B();
    return num_buf = 3;
}

/* 0x623: maps LPF buffer which (0..2) at physical page 0: the four EMS pages from
   tmap_fpage + 4 * which, the level textures' pages. Returns the EMS frame, or 0. */
unsigned char far * far set_cuts_ems(int which)
{
    seg042_35ED_12B();
    if (num_buf - 1 < which) return 0L;
    if (seg013_1D3C_E4(0, tmap_fpage + (which << 2), 4))
        return MK_FP(ems_frame, 0);
    return 0L;
}

/* 0x667 */
void far free_cuts_ems(void)
{
    seg042_35ED_12B();
}

/* 0x671 */
char * far makeFourChars_ovr108_671(uint32 value, char *out)
{
    int i;
    for (i = 0; i < 4; i++) out[i] = (value >> (i * 8)) & 0xFF;
    out[4] = 0;
    return out;
}

/* 0x6AD: sorts the large page numbers 0..n-1 into order by their first record (a bubble
   sort), giving the order to play them in. */
void far build_lptab(struct LpDesc far *desc, unsigned n, unsigned char far *order)
{
    int tmp;
    unsigned char changed;
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

/* 0x751: reads the LPF header. Each colour cycle's rate word becomes (word >> 8) & 0x3F,
   the value anm_cycle divides by. */
unsigned char far read_anmhdr(unsigned char far *dst)
{
    int wanted, actual;
    int i;
    unsigned value;
    wanted = 0xB00;
    actual = intoFarBuffer_ovr167_5DA(STDAT.anm_fd, dst, wanted);
    for (i = 0; i < 16; i++) {
        value = ((struct AnmHdr far *)dst)->cycles[i].period;
        ((struct AnmHdr far *)dst)->cycles[i].period = (value >> 8) & 0x3F;
    }
    return actual == wanted;
}

/* 0x7BF: reads large page page (at 0xB00 + page * 64 KB in the file) into dst. It returns
   no value, so its caller's -1 test reads whatever is in AX. */
int far readlp(unsigned page, struct LpDesc far *desc, void far *dst)
{
    int size;
    register int got;
    lseek(STDAT.anm_fd, ((int32)page << 16) + 0xB00L, 0);
    size = desc->nbytes + desc->nrecords * 2 + 8;
    got = intoFarBuffer_ovr167_5DA(STDAT.anm_fd, dst, size);
    AX_RESULT(got);                     /* AX still holds intoFarBuffer's result */
}

/* 0x819: reads the next n bytes of large page page into dst, continuing where the last
   call stopped (lp_page, lp_left); returns the bytes read. */
int far readlpinc(unsigned page, struct LpDesc far *desc, unsigned n, void far *dst)
{
    char far *target = dst;
    int out_size, amount;
    if (page < 0 || n == 0) return 0;
    out_size = desc->nrecords * 2 + desc->nbytes + 8;
    if (page != lp_page) {
        lp_page = page;
        lseek(STDAT.anm_fd, ((int32)page << 16) + 0xB00L, 0);
        lp_left = out_size;
    }
    if (lp_left == 0) return 0;
    if (n > lp_left) n = lp_left;
    amount = intoFarBuffer_ovr167_5DA(STDAT.anm_fd,
             EMS_ADD(target, out_size - lp_left), n);
    lp_left -= amount;
    return amount;
}

/* 0x8C8: the LPF palette (blue, green, red, pad; 8 bits) to VGA order and 6 bits. */
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

/* 0x934: rotates each active colour cycle of the LPF once 0x38E / rate ticks have passed
   since it last moved. */
void far anm_cycle(struct Cycle far *cycles)
{
    int i;
    unsigned char n;
    for (i = 0; i < 16; i++) {
        if (cycles[i].period != 0) {
            /* the clock's low word: a 16-bit difference, which wraps (on the host an
               (unsigned) cast keeps all 32 bits, and past FFFFh ticks every cycle turned
               on every call) */
            if ((uint16)(GAME_TIME() - cycles[i].started) <
                0x38E / cycles[i].period) continue;
            n = cycles[i].last - cycles[i].first + 1;
            rotate_bank(cycles[i].first, n, 0);
            local_do_palette(n, cycles[i].first);
            cycles[i].started = GAME_TIME();
        }
    }
}

/* 0x9FE, opcode 27: wait_for_sound(n): keeps the speech going until it ends, cutting it
   off after n * 256 ticks. */
int far cutsop_wait_for_sound(uint16 far *code, struct CutsState *st)
{
    uint32 until;
    until = GAME_TIME() + (*code << 8);
    while (!speech_over()) {
        if (GAME_TIME() >= until) {
            cutoff_cutscene_speech();
            break;
        }
        update_big_speech();
    }
    return 1;
}

/* 0xA55: a pending fade-in (fade47 >= 0): clears the subtitle bar, fades in over fade47,
   keeping the speech going if it is playing, and swallows the input that arrived. */
void far cuts_perform_fadein(struct CutsState *st)
{
    set_the_color(0xF0);
    urectangle(0, splity, 0x13F, 0);
    if (st->fade47 > -1) {
        if (st->flags.bit.b6 && st->fade45 != -1)
            fadein(st->palette, st->fade47, 1);
        else fadein(st->palette, st->fade47, 0);
        gobble_input_events(st);
        st->flags.bit.b1 = 0;
        st->flags.bit.b0 = 0;
        st->fade47 = -2;
        st->fade49 = -1;
    }
}

/* 0xAC6 */
void far cuts_perform_fadeout(struct CutsState *st)
{
    if (st->fade49 > -1) {
        if (st->flags.bit.b6 && st->fade45 != -1)
            fadeout(st->palette, st->fade49, 1);
        else fadeout(st->palette, st->fade49, 0);
        gobble_input_events(st);
        st->flags.bit.b1 = 0;
        st->flags.bit.b0 = 0;
        st->fade49 = -2;
        st->fade47 = -1;
    }
}

/* 0xB1A */
void far cuts_do_fadein(struct CutsState *st, int apply)
{
    set_the_color(0x1B);
    urectangle(0, splity, 0x13F, 0);
    if (!st->flags.bit.b0) {
        if (st->fade47 > -1) cuts_perform_fadein(st);
        else if (!apply) grfx_setpal(st->palette);
    }
}

/* 0xB6D */
void far cuts_do_fadeout(struct CutsState *st, int frame)
{
    if (!st->flags.bit.b0 && st->fade49 > -1)
        cuts_perform_fadeout(st);
}

/* 0xB8E: one step of a pan begun by cutsop_pan: the focus moves step pixels per frame
   in direction dir (pan_dx, pan_dy: 0 adds to y, 1 to x, 2 takes from y, 3 from x). */
void far do_pan(struct CutsState *st)
{
    focus_x = st->panx51 + (STDAT.frame + 1) * st->step57 * pan_dx[st->dir55];
    focus_y = st->pany53 + (STDAT.frame + 1) * st->step57 * pan_dy[st->dir55];
    vscreen_focus(focus_x, focus_y);
    st->remaining59--;
}

/* 0xBE9, opcode 26 */
int far cutsop_test(uint16 far *code, struct CutsState *st)
{
    return 1;
}

/* 0xBF1, opcode 24: splity(y): the row where the picture stops and the subtitle bar
   begins, or none (999). */
int far cutsop_splity(uint16 far *code, struct CutsState *st)
{
    splity = *code == 999 ? -1 : *code - 1;
    return 1;
}

/* 0xC12, opcode 25: music(theme), 0 stops the music. */
int far cutsop_music(uint16 far *code, struct CutsState *st)
{
    if (*code == 0) stop_music();
    else set_new_music(*(unsigned char far *)code);
    return 1;
}

/* 0xC38, opcode 0: txt(colour, string): a subtitle, string string of the cutscene's
   string block, split at newlines and then to lines 320 pixels wide; drawn by
   cuts_draw_text after the frame. String FFFFh clears it. */
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
    if (st->flags.bit.b0) return 2;
    if (code[1] == 0xFFFF) {
        st->flag39 = 0;
        return 2;
    }
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

/* 0xE4D, opcode 1 */
int far cutsop_erase(uint16 far *code, struct CutsState *st)
{
    st->flag39 = 0;
    return 0;
}

/* 0xE5C, opcode 2 */
int far cutsop_func(uint16 far *code, struct CutsState *st)
{
    return 2;
}

/* 0xE64, opcode 3: pause(n): hold this frame for n * 256 ticks or until a key. */
int far cutsop_pause(uint16 far *code, struct CutsState *st)
{
    if (st->flags.bit.b0) return 1;
    st->flags.bit.b1 = 0;
    st->frame3D = code[-2];
    st->frame3F = code[0];
    return 1;
}

/* 0xE91, opcode 14: wait(a, b): pause for a, or for b, ignoring keys, when speech is on. */
int far cutsop_wait(uint16 far *code, struct CutsState *st)
{
    if (st->flags.bit.b0) return 2;
    st->flags.bit.b1 = 0;
    st->frame3D = code[-2];
    if (st->flags.bit.b5) {
        st->flags.bit.b7 = 1;
        st->frame3F = code[1];
    } else st->frame3F = code[0];
    return 2;
}

/* 0xED8, opcode 4: skip(frame, n): a key pressed from here skips (draws nothing) up to
   frame; without speech, also pauses n before it. */
int far cutsop_skip(uint16 far *code, struct CutsState *st)
{
    if (st->flags.bit.b0) return 2;
    st->flags.bit.b1 = 0;
    st->frame3B = code[0];
    if (!st->flags.bit.b5) {
        st->frame3F = code[1];
        st->frame3D = code[0] - 1;
    }
    return 2;
}

/* 0xF19, opcode 5: next: end this LPF file and go on to the next. */
int far cutsop_next(uint16 far *code, struct CutsState *st)
{
    st->flags.bit.b2 = 0;
    return 0;
}

/* 0xF27, opcode 6: end: end the cutscene after this file. */
int far cutsop_end(uint16 far *code, struct CutsState *st)
{
    st->flags.bit.b2 = 0;
    st->flags.bit.b3 = 0;
    return 0;
}

/* 0xF3B, opcode 7: loop(n): when this frame is next reached, replay the file n times. */
int far cutsop_loop(uint16 far *code, struct CutsState *st)
{
    st->repeat41 = code[0];
    st->repeat43 = code[-2];
    st->frame3B = code[-2] + 1;
    st->frame3D = 0;
    st->flags.bit.b1 = 0;
    return 1;
}

/* 0xF69, opcode 8: data(file, ext): the next file's name. File 996 picks one of cutscenes
   034..037 (octal) at random. For any other value file is never set, so the digits
   come from whatever the stack held; the scripts probably only use 996 (an inference from
   the code, not checked against every script). */
int far cutsop_data(uint16 far *code, struct CutsState *st)
{
    int file;
    if (code[0] == 996) file = rand() % 4 + 0x1C;
    st->name[7] = ((file >> 6) & 7) + '0';
    st->name[8] = ((file >> 3) & 7) + '0';
    st->name[9] = (file & 7) + '0';
    st->name[12] = ((code[1] >> 3) & 7) + '0';
    st->name[13] = (code[1] & 7) + '0';
    st->file4B = code[1];
    return 2;
}

/* 0xFDA, opcode 13: say(colour, string, voc): speech file BSP<voc>.VOC, or its subtitle
   (txt) when speech is off or fails; 999 is text only, 998 nothing when speech is on. */
int far cutsop_say(uint16 far *code, register struct CutsState *st)
{
    if (st->flags.bit.b5) {
        st->fade45 = code[2];
        if (st->fade45 == 999) {
            st->fade45 = -1;
            cutsop_txt(code, st);
        } else if (st->fade45 != 998) {
            if (big_speech_play(code[2], 0x7F, 0x40, sound_fpage, 4) == 0xFF)
                cutsop_txt(code, st);
#ifndef __TURBOC__
            else {
                st->flags.bit.b6 = 1;
                if (ENHANCED(ENH_SUBTITLES)) cutsop_txt(code, st);   /* port only: --enhance subtitles */
            }
#else
            else st->flags.bit.b6 = 1;
#endif
        }
    } else cutsop_txt(code, st);
    return 3;
}

/* 0x104C, opcode 9: fadeout(n): fade out over n (fadeout's count), full screen only. */
int far cutsop_fadeout(uint16 far *code, struct CutsState *st)
{
    if (st->windowed == 0 && st->fade49 > -2) st->fade49 = code[0];
    cuts_do_fadeout(st, STDAT.frame);
    return 1;
}

/* 0x1080, opcode 10: fadein(n). */
int far cutsop_fadein(uint16 far *code, struct CutsState *st)
{
    if (st->windowed == 0 && st->fade47 > -2) st->fade47 = code[0];
    cuts_do_fadein(st, STDAT.frame);
    return 1;
}

/* 0x10B4, opcode 11: jump(frame): skip ahead to frame, unless it is the next one. */
int far cutsop_jump(uint16 far *code, struct CutsState *st)
{
    if (code[-2] != (uint16)(code[0] - 1)) {
        st->frame3B = code[0] - 1;
        st->frame3D = 0;
        st->frame3F = 0;
        st->flags.bit.b1 = 1;
    }
    return 1;
}

/* 0x10ED, opcode 12: punt(flag): whether Escape may end the cutscene. */
int far cutsop_punt(uint16 far *code, struct CutsState *st)
{
    st->flags.bit.b4 = code[0];
    return 1;
}

/* 0x110B, opcode 15 */
int far cutsop_clang(uint16 far *code, struct CutsState *st)
{
    return 0;
}

/* 0x1112, opcode 16: set palette entries from the script */
int far cutsop_palrange(uint16 far *code, struct CutsState *st)
{
    int first, end, i;
    first = code[0] * 3;
    end = code[1] * 3 + first;
    for (i = first; i < end; i++)
        st->palette[i] = code[i - first + 2];
    return code[1] * 3 + 2;
}

/* 0x1165, opcode 17: fade palette entries towards the script's colours */
int far cutsop_palfade(uint16 far *code, register struct CutsState *st)
{
    int task;
    int first;
    int end;
    register int i;
    task = install_timebased_task(task_palfade, code[0], code[1]);
    task_data1[task] = code[2];
    task_data2[task] = code[2] + code[3];
    task_pointer_data[task] = st->palette;
    first = code[2] * 3;
    end = first + code[3] * 3;
    for (i = first; i < end; i++) {
        start_pal[i] = st->palette[i];
        end_pal[i] = code[i - first + 4];
    }
    return code[3] * 3 + 4;
}

/* 0x1221, opcode 18 */
int far cutsop_palset(uint16 far *code, struct CutsState *st)
{
    return 4;
}

/* 0x1229, opcode 19: fade the whole palette towards one of PALS.DAT */
int far cutsop_palsimplefade(uint16 far *code, register struct CutsState *st)
{
    int task;
    register int i;
    task = install_timebased_task(task_palfade, code[1], code[2]);
    if (Transparency == 1) task_data1[task] = 1;
    else task_data1[task] = 0;
    task_data2[task] = 0x100;
    task_pointer_data[task] = st->palette;
    if (!read_quikpal(code[0], end_pal)) remove_task(task);
    else for (i = 0; i < 0x300; i++) start_pal[i] = st->palette[i];
    return 3;
}

/* 0x12D3, opcode 20: vscreen(w, h, split): a virtual screen of w by h; 320, 200, 999 is
   the plain screen. */
int far cutsop_vscreen(uint16 far *code, register struct CutsState *st)
{
    virtual_screen(code[0], code[1], code[2] == 999 ? -1 : code[2] - 1);
    if (code[0] == 320 && code[1] == 200 && code[2] == 999) {
        if (st->windowed == 0) st->vscr4F = 0;
    } else st->vscr4F = 1;
    return 3;
}

/* 0x1337, opcode 21: focus(x, y): the point of the virtual screen shown. */
int far cutsop_focus(uint16 far *code, struct CutsState *st)
{
    vscreen_focus(code[0], code[1] == 999 ? -1 : code[1]);
    focus_x = code[0];
    focus_y = code[1];
    return 2;
}

/* 0x1375, opcode 22: lback(x, y, n): draw background CUTS\lbackNNN.byt into the virtual
   screen. */
int far cutsop_lback(uint16 far *code, struct CutsState *st)
{
    lback_vscreen(code[0], code[1], code[2]);
    return 3;
}

/* 0x1393, opcode 23: pan(dir, step, count): pan from the current focus for count frames
   (do_pan). */
int far cutsop_pan(uint16 far *code, register struct CutsState *st)
{
    int dir, step, count;
    dir = code[0];
    step = code[1];
    count = code[2];
    st->panx51 = focus_x;
    st->pany53 = focus_y;
    st->dir55 = dir;
    st->step57 = step;
    st->remaining59 = count;
    return 3;
}

/* 0x13CD */
void far anm_sound_callback(void)
{
    update_big_speech();
}

/* 0x13D6 */
void far anm_sound(register struct CutsState *st)
{
    change_music_maybe();
    if (st->flags.bit.b6 && st->fade45 != -1) {
        update_big_speech();
        if (speech_over()) {
            st->flags.bit.b6 = 0;
            st->fade45 = -1;
        }
    }
}

/* 0x140D: the state at the start of a cutscene. The flag bits, as the code uses them:
   b0 skipping (a key was pressed inside a skip), b1 a key or button arrived, b2 keep
   playing this file, b3 keep playing the cutscene, b4 Escape may end it, b5 speech is
   available, b6 speech is playing, b7 the current wait ignores keys. */
struct CutsState * far cuts_init_info(register struct CutsState *st)
{
    st->fade45 = -1;
    st->flags.bit.b5 = speech_available();
    st->flags.bit.b4 = 1;
    st->fade47 = -1;
    if (st->windowed == 0) st->fade49 = -2;
    else st->fade49 = -1;
    st->flag39 = 0;
    st->frame3B = 0;
    st->frame3D = 0;
    st->frame3F = 0;
    st->repeat41 = 0;
    st->vscr4F = 0;
    st->remaining59 = 0;
    st->flags.bit.b6 = 0;
    st->flags.bit.b0 = 0;
    st->flags.bit.b1 = 0;
    st->flags.bit.b2 = 1;
    st->flags.bit.b3 = 1;
    st->flags.bit.b4 = 1;
    st->flags.bit.b7 = 0;
    return st;
}

/* 0x148C */
char * far cuts_make_fname(register char *name, int n, int ext)
{
    strcpy(name, "CUTS\\csXXX.nXX");
    name[7] = ((n >> 6) & 7) + '0';
    name[8] = ((n >> 3) & 7) + '0';
    name[9] = (n & 7) + '0';
    name[12] = ((ext >> 3) & 7) + '0';
    name[13] = (ext & 7) + '0';
    return name;
}

/* 0x14DF: reads all pending input; returns the last key or button, or -1. With a state,
   any input sets b1, Escape (where allowed, and not in cutscene 2) ends the cutscene and
   cuts off the speech, and input inside a skip starts skipping (b0). */
int far gobble_input_events(register struct CutsState *st)
{
    int key;
    register int in;
    key = -1;
    while ((in = mouse_get_input()) > 3) key = in;
    if (in > -1 && in < 4) key = in;
    if (st) switch (key) {
    case -1:
        break;
    case 0x1B:
        if (st->flags.bit.b4 && cuts_number != 2)
            st->flags.bit.b3 = st->flags.bit.b2 = 0;
        cutoff_cutscene_speech();
    default:
        st->flags.bit.b1 = 1;
    }
    if (st && !st->flags.bit.b0 && st->frame3B && st->flags.bit.b1) {
        st->flags.bit.b0 = 1;
        st->repeat41 = 0;
    }
    return key;
}

/* 0x157B: clears the bar below splity and draws the subtitle lines centred, in colour
   color38 of the cutscene's palette, the first line at the top. */
void far cuts_draw_text(register struct CutsState *st)
{
    int x;
    int y;
    register int i;
    i = 0;
    set_the_color(0xF0);
    rectangle(0, splity, 0x13F, 0);
    *foreground_color = st->color38;
    *background_color = st->color38;
    y = st->y - st->h + cur_font->height * st->flag39 + 2;
    for (i = 0; i < st->flag39; i++) {
        x = (st->x + st->w - string_width(st->text_lines[i])) / 2;
        string_to_screen(st->text_lines[i], x, y);
        y -= cur_font->height;
    }
}

/* 0x162B: the work done while waiting: colour cycles, music and speech, and the timed
   tasks every 8 ticks. */
int far run_time_critical_things(struct CutsState *st, struct AnmHdr far *hdr)
{
    anm_cycle(hdr->cycles);
    anm_sound(st);
    if (GAME_TIME() - STDAT.tick >= 8) {
        STDAT.tick = GAME_TIME();
        run_timebased_tasks(0);
    }
    return 0;
}

/* 0x1693: holds the frame for frame3F * 256 ticks, until a key (unless b7) or b1. */
int far cuts_run_pause(register struct CutsState *st, struct AnmHdr far *hdr)
{
    uint32 last;
    uint32 now;
    register int in;
    in = -1;
    last = (GAME_TIME() - STDAT.start) >> 8;
    while (in == -1 && !st->flags.bit.b1 &&
           (now = (GAME_TIME() - STDAT.start) >> 8) < st->frame3F) {
        if (st->flags.bit.b7) in = gobble_input_events(0);
        else in = gobble_input_events(st);
        if (now > last) last = now;
        run_time_critical_things(st, hdr);
    }
    return 0;
}

/* 0x175F: runs the script records for frame. When a record might run past the end of the
   0x400-byte buffer (the margin taken is 4 words, or 1 << (frame >> 5) once frame is 32 or
   more), the file is moved back by the unread words and the buffer refilled from the
   record. An opcode of 29 or more stops the scan. */
void far cuts_process_opcodes(int frame, register struct CutsState *st)
{
    while (*STDAT.code == frame && st->repeat41 == 0) {
        if ((uint16 far *)STDAT.n00 + 0x200 - ((*STDAT.code >> 5) ? 1 << (*STDAT.code >> 5) : 4) > STDAT.code) {
            STDAT.code += 2;
            if (STDAT.code[-1] < 0x1D)
                STDAT.code += cuts_dispatch[STDAT.code[-1]](STDAT.code, st);
            else return;
        } else {
            lseek(STDAT.n0x_fd, (STDAT.code - (uint16 far *)STDAT.n00 - 0x200) * 2, 1);
            intoFarBuffer_ovr167_5DA(STDAT.n0x_fd, STDAT.n00, 0x400);
            STDAT.code = (uint16 far *)STDAT.n00;
        }
    }
}

/* 0x189F */
#define SCREEN ((unsigned char far *)MK_FP(0xA000, 0))

/* Fetch the next run/skip/dump token of the delta stream. */
#define GET_TOKEN() \
    if (count == 0) { \
        if (*src == 0) { \
            type = 0; count = src[1]; src += 2; \
        } else if (*src < 0x80) { \
            type = 2; count = *src; src++; \
        } else if (*src != 0x80) { \
            type = 1; count = *src & 0x7F; src++; \
        } else { \
            uint16 far *w; \
            if ((w = (uint16 far *)++src, *w) >= 0x8000) { \
                if (*w >= 0xC000) { type = 0; count = *w & 0x3FFF; } \
                else { type = 2; count = *w & 0x7FFF; } \
            } else { \
                if (*w) { type = 1; count = *w; } \
                else return; \
            } \
            src += 2; \
        } \
    }

/* Move the destination k pixels to the right. */
#define ADVANCE(k) \
    dst += (k) / 4; \
    plane += (k) % 4; \
    if (plane >= 4) { plane -= 4; dst++; }

/* Fill k pixels with the run's byte. */
#define RUN(k) { \
    unsigned char mask; int16 i; int16 last; \
    i = 0; \
    last = (plane + (k) - 1) / 4; \
    if (i == last) { \
        mask = lmask[plane] & rmask[(plane + (k) - 1) % 4]; \
        outportb(0x3C4, 2); outportb(0x3C5, mask); \
        PLANAR_STORE(dst, *src); \
    } else { \
        mask = lmask[plane]; \
        outportb(0x3C4, 2); outportb(0x3C5, mask); \
        PLANAR_STORE(dst, *src); \
        i++; \
        mask = rmask[(plane + (k) - 1) % 4]; \
        outportb(0x3C4, 2); outportb(0x3C5, mask); \
        PLANAR_STORE(dst + last, *src); \
        if (i < last) { \
            outportb(0x3C4, 2); outportb(0x3C5, 0x0F); \
            mem_set(dst + i, *src, last - i); \
        } \
    } \
}

#define DUMP_PLANE() \
    s2 = s; d2 = d; \
    n4 = cnt / 4; \
    cnt--; \
    outportb(0x3C4, 2); outportb(0x3C5, 1 << p); \
    while (n4-- > 0) { PLANAR_STORE(d2, *s2); s2 += 4; d2++; }

#define NEXT_PLANE() \
    if (++p >= 4) { p -= 4; d++; } \
    s++;

/* Copy k literal pixels, one plane at a time. */
#define DUMP(k) { \
    int16 n4; unsigned char far *s; unsigned char far *d; int16 p; int16 cnt; \
    unsigned char far *s2; unsigned char far *d2; \
    s = src; d = dst; p = plane; cnt = (k) + 3; \
    DUMP_PLANE() NEXT_PLANE() \
    DUMP_PLANE() NEXT_PLANE() \
    DUMP_PLANE() NEXT_PLANE() \
    s2 = s; d2 = d; \
    n4 = cnt / 4; \
    outportb(0x3C4, 2); outportb(0x3C5, 1 << p); \
    while (n4-- > 0) { PLANAR_STORE(d2, *s2); s2 += 4; d2++; } \
}

/* Decodes a delta record (the run/skip/dump stream LPFDELTA.ASM also decodes) straight into
   planar VGA memory at x, y (y bottom-up, rows going down the screen), w by h, leaving
   out skipx columns and skipy rows of the 320-wide frame. Runs write four pixels a byte
   with the map mask; dumps copy one plane at a time. */
void far draw_rsd(unsigned char far *src, int x, int y, int w, int h,
    int skipx, int skipy)
{
    unsigned char far *dst;
    int col, row, xend, yend, pos, type, n, skip;
    int plane, count;

    plane = x % 4;
    w -= skipx;
    h -= skipy;
    col = row = pos = count = 0;
    dst = SCREEN + (Ytab[y] + x / 4);
    if (skipy > 0 || skipx > 0) {
        skip = skipy * 320 + skipx;
        while (pos < skip) {
            GET_TOKEN()
            if (pos + count <= skip) {
                switch (type) {
                case 0: src++; break;
                case 1: break;
                default: src += count; break;
                }
                pos += count;
                count = 0;
            } else {
                n = skip - pos;
                switch (type) {
                case 0: case 1: break;
                default: src += n; break;
                }
                count -= n;
                pos += n;
            }
        }
        row = skipy;
        col = skipx;
    }
    yend = row + h - 1;
    xend = col + w - 1;
    while (row <= yend) {
        while (col < skipx) {
            GET_TOKEN()
            if (col + count <= skipx) {
                switch (type) {
                case 0: ADVANCE(count) src++; break;
                case 1: ADVANCE(count) break;
                default: ADVANCE(count) src += count; break;
                }
                col += count;
                count = 0;
            } else {
                n = skipx - col;
                switch (type) {
                case 0: ADVANCE(n) break;
                case 1: ADVANCE(n) break;
                default: ADVANCE(n) src += n; break;
                }
                count -= n;
                col += n;
            }
        }
        while (col <= xend) {
            GET_TOKEN()
            if (col + count <= xend + 1) {
                switch (type) {
                case 0: RUN(count) ADVANCE(count) src++; break;
                case 1: ADVANCE(count) break;
                default: DUMP(count) ADVANCE(count) src += count; break;
                }
                col += count;
                count = 0;
            } else {
                n = xend - col + 1;
                switch (type) {
                case 0: RUN(n) ADVANCE(n) break;
                case 1: ADVANCE(n) break;
                default: DUMP(n) ADVANCE(n) src += n; break;
                }
                col += n;
                count -= n;
            }
        }
        row++;
        y--;
        col -= 320;
        dst = SCREEN + Ytab[y] - (320 - w - x) / 4;
    }
}

/* 0x2359: plays the records of one large page. Each record is drawn (unless skipping),
   page-flipped where the scene allows it, the next large page is read a slice further,
   the frame's opcodes run, and the loop waits until 256 / rate ticks have passed since the
   previous frame started. Returns 1 to replay the file (loop), else 0. */
int far cuts_process_lp(register struct CutsState *st, struct AnmHdr far *anm,
    struct LpDesc far *lp)
{
    unsigned char far *page;
    unsigned char far *ems2;
    unsigned perrec;
    uint16 far *sizes;
    unsigned char far *data;
    int size;
    int last;
    int nrec;
    uint32 now;
    unsigned char first;
    unsigned char flipped;
    register int i;

    first = 1;
    STDAT.start = GAME_TIME();
    page = set_cuts_ems(STDAT.ems_page);
    sizes = ((struct LpPage far *)page)->sizes;
    data = (unsigned char far *)(sizes + lp->nrecords);
    last = (uint16)(anm->nlps - 1) == (uint16)STDAT.lp_index && anm->lastdelta ? 1 : 0;
    nrec = lp->nrecords - last;
    st->frame3D = st->frame3F = 0;
    st->flags.bit.b1 = 0;
    if (nrec > 0 && (uint16)(anm->nlps - 1) > (uint16)STDAT.lp_index) {
        struct LpDesc far *d;
        register unsigned sz;
        d = LPDESC(anm, STDAT.lptab[STDAT.lp_index + 1]);
        sz = d->nbytes + d->nrecords * 2 + 8;
        perrec = (nrec + sz - 1) / nrec;
    } else perrec = -2;
    for (i = 0; i < nrec && st->flags.bit.b2 && st->flags.bit.b3; i++) {
        page = set_cuts_ems(STDAT.ems_page);
        size = sizes[i];
        if (size != 0) size -= 4;
        if (st->vscr4F == 0 && STDAT.frame == 0 && st->fade49 > -2 && st->windowed == 0) {
            grSoftPageFlip();
            grfx_clear();
            flipped = 1;
        } else flipped = 0;
        if (!st->flags.bit.b0 && st->remaining59 != 0) do_pan(st);
        if (size != 0) {
            int hb;
            int wd;
            int off;
            hb = EMS_WRAP(data)[1];
            wd = (*(uint16 far *)(EMS_WRAP(data) + 2) + 1) & ~1;
            off = hb ? wd + 4 : 2;
            if (!st->flags.bit.b0) {
                if (splity == -1 && st->vscr4F == 0 && !flipped) grSoftPageFlip();
                if (EMS_WRAP(data)[off] == 0)
                    show(st->x + (focus_x & 3), st->y, EMS_WRAP(data) + off + 2, 200 - (splity + 1),
                        320, 320 - st->w, 200 - st->h);
                else if (EMS_WRAP(data)[off] == 1) {
                    if (splity == -1 && st->vscr4F == 0 && !flipped) copy_visible_to_hidden();
                    draw_rsd(EMS_WRAP(data) + off + 2, st->x + (focus_x & 3), st->y,
                        320, 200 - (splity + 1), 320 - st->w, 200 - st->h);
                }
                if (splity == -1 && st->vscr4F == 0 && !flipped) {
                    grPageFlip();
                    grSoftPageFlip();
                }
            }
        }
        if (st->vscr4F == 0 && STDAT.frame == 0 && st->fade49 > -2 && st->windowed == 0) {
            grPageFlip();
            grfx_setpal(st->palette);
            grSoftPageFlip();
        }
        data += sizes[i];
        if (STDAT.ahead_page >= 0 && anm->nlps > STDAT.ahead_index) {
            ems2 = set_cuts_ems(STDAT.ahead_page);
            readlpinc(STDAT.lptab[STDAT.ahead_index], LPDESC(anm, STDAT.lptab[STDAT.ahead_index]), perrec, ems2);
        }
        STDAT.frame++;
        run_time_critical_things(st, anm);
        gobble_input_events(st);
        if (st->repeat41 == 0) cuts_process_opcodes(STDAT.frame, st);
        if (!st->flags.bit.b0 && first)
            while (!st->flags.bit.b0 && (now = GAME_TIME()) - STDAT.start < 0x100 / anm->rate) {
                run_time_critical_things(st, anm);
                gobble_input_events(st);
            }
        if (!st->flags.bit.b0) first = 1;
        if (!st->flags.bit.b0 && st->flag39 > 0) {
            cuts_draw_text(st);
            st->flag39 = 0;
        }
        if (!st->flags.bit.b0 && st->frame3F != 0 && st->frame3D == STDAT.frame)
            cuts_run_pause(st, anm);
        if (st->frame3B == STDAT.frame) {
            st->frame3B = 0;
            if (st->flags.bit.b0) st->flags.bit.b0 = 0;
        }
        if (st->repeat41 > 0 && st->repeat43 == STDAT.frame) {
            first = 1;
            st->repeat41--;
            st->file4B = st->file4D;
            return 1;
        }
        STDAT.start = now;
    }
    return 0;
}

/* 0x287B */
void far reset_inf_values_eof(register struct CutsState *st)
{
    st->flags.bit.b0 = st->flags.bit.b1 = st->flags.bit.b6 = 0;
    st->flags.bit.b4 = st->flags.bit.b2 = 1;
    st->flag39 = st->frame3B = st->frame3D = st->remaining59 = st->frame3F = 0;
    st->flags.bit.b7 = 0;
}

/* 0x28C7: plays the LPF file named in st: header, palette, the first three large pages
   into EMS, then every large page in order, then the frame-999 records. */
void far cuts_process_anm(struct CutsState *st)
{
    unsigned char far *img;
    unsigned char far *n0x;
    struct AnmHdr far *hdr;
    struct LpDesc far *desc;
    int result;
    int i;

    result = 0;
    run_timebased_tasks(1);
    n0x = STDAT.n00 + 0x400;
    hdr = (struct AnmHdr far *)n0x;
    desc = ((struct AnmHdr far *)n0x)->lps;
    st->palette = n0x + 0xB00;
    STDAT.lptab = st->palette + 0x300;
    cuts_process_opcodes(0x3E5, st);
    if ((STDAT.anm_fd = open(st->name, 0x8001)) == -1) {
        st->flags.bit.b3 = 0;
        return;
    }
    if (!read_anmhdr(n0x)) {
        st->flags.bit.b3 = 0;
        goto done;
    }
    if (st->windowed == 0) conv_anmpal(((struct AnmHdr far *)n0x)->palette, st->palette);
    build_lptab(desc, hdr->nlps, STDAT.lptab);
    lp_page = -1;
    for (i = 0; i < num_buf && i < hdr->nlps; i++) {
        img = set_cuts_ems(i);
        if (img != 0)
            if (readlp(STDAT.lptab[i], &desc[STDAT.lptab[i]], img) == -1) ;
    }
    STDAT.ahead_page = 1 - num_buf;
    STDAT.ems_page = 0;
    STDAT.ahead_index = 1;
    STDAT.lp_index = 0;
    STDAT.frame = 0;
    st->flags.bit.b1 = 0;
    st->flags.bit.b2 = 1;
    if (st->repeat41 == 0) cuts_process_opcodes(STDAT.frame, st);
    for (i = 0; i < hdr->nlps && st->flags.bit.b2 && st->flags.bit.b3 && !result; i++) {
        result = cuts_process_lp(st, (struct AnmHdr far *)n0x,
            &desc[STDAT.lptab[STDAT.lp_index]]);
        STDAT.lp_index++;
        STDAT.ahead_index++;
        if (++STDAT.ems_page >= num_buf) STDAT.ems_page = 0;
        if (++STDAT.ahead_page >= num_buf) STDAT.ahead_page = 0;
    }
    if (st->repeat41 == 0) {
        reset_inf_values_eof(st);
        cuts_process_opcodes(0x3E7, st);
        if (st->fade49 == -2) grfx_clear();
    }
done:
    close(STDAT.anm_fd);
}

/* 0x2B73: plays cutscene cuts in the window x, y, w, h (y is the top row, bottom-up):
   loads the script, then plays .n01, .n02 ... until an end, Escape, or a missing file;
   fades out and restores the plain screen after a full-screen one. */
void far show_anm(int cuts, int x, int y, int w, int h)
{
    unsigned char far *n0x;
    struct CutsState st;

    gobble_input_events(0);
    focus_x = 0;
    focus_y = 199;
    st.x = x;
    st.y = y;
    st.w = w;
    st.h = h;
    if (x == 0 && y == 199 && w == 320 && h == 200) st.windowed = 0;
    else st.windowed = 1;
    STDAT.n00 = STDAT.n00buf;
    n0x = STDAT.n00 + 0x400;
    st.palette = n0x + 0xB00;
    STDAT.screen = MK_FP(FP_SEG(stdat) + 0x134, 0);
    if (get_cuts_ems() >= 3) {
        st.file4B = 0;
        cuts_make_fname(st.name, cuts, st.file4B);
        if ((STDAT.n0x_fd = open(st.name, 1)) == -1) {
            seg042_35ED_12B();
            return;
        }
        if (st.windowed == 0) grfx_clear();
        intoFarBuffer_ovr167_5DA(STDAT.n0x_fd, STDAT.n00, 0x400);
        STDAT.code = (uint16 far *)STDAT.n00;
        cuts_init_info(&st);
        FAR_COPY(st.palette, palette, 0x300);
        if (st.windowed == 0) fadeout(st.palette, 2, 1);
        st.file4B = st.file4D = 1;
        while (st.flags.bit.b3) {
            st.file4D = st.file4B;
            cuts_make_fname(st.name, cuts, st.file4B);
            st.file4B++;
            cuts_process_anm(&st);
            punt_tasks();
        }
        if (st.fade45 != -1) cutoff_cutscene_speech();
        if (st.windowed == 0) {
            if (st.fade49 != -2) fadeout(st.palette, 2, 1);
            grfx_clear();
        }
        if (st.windowed == 0) {
            virtual_screen(320, 200, -1);
            vscreen_focus(0, 199);
        }
        set_the_window(0, 199, 319, 0);
        if (st.windowed == 0) grfx_clear();
        free_cuts_ems();
    }
    free_speech_stuff();
    close(STDAT.n0x_fd);
    mem_set(STDAT.screen, 0, 0xFA00);
}

/* 0x2DC5: the entry point (see the file comment). Sets the big font and the string block
   C00h + n, plays it, then puts the game screen back. Cutscene 2 starts music theme 1;
   cutscene 103h's window is a few rows taller. */
void far show_cutscene(register unsigned n)
{
    int x;
    int y;
    int w;
    register int h;
    cuts_number = n;
    punt_all_digi_fx();
    cutoff_cutscene_speech();
    if (n < 0x100) {
        x = 0;
        y = 0xC7;
        w = 0x140;
        h = 0xC8;
    } else {
        x = 0x10;
        y = 0xB6;
        w = 0xD0;
        h = 0x7F;
    }
    if (n == 2) load_new_music(MUSIC_THEME, 1);
    grfx_quikfont(FONT_BIG);
    CutsceneOrConversationStringBlock = n + STRBLK_CUTSCENE;
    mouse_hide();
    if (n == 0x103) {
        y += 4;
        h += 5;
    }
    show_anm(n, x, y, w, h);
    grfx_quikfont(FONT_5X6P);
    if (in_game) load_txtmaps();
    if (n < 0x100) {
        if (inplist->mode == 1) newscr(1);
        else if (inplist->mode != 0) {
            grfx_quikpal(PAL_GAME);
            editchng(0x7FFE);
        }
    } else editchng(2);
    mouse_show();
}

/* 0x2EA7 */
void far init_cutscene(void)
{
}

/* 0x2EAC: write a value into a cutscene's opcode file, then show it. It writes value
   over the words at byte offsets 4, 6 and 12 of csNNN.n00. Nothing in the C calls it; probably a development aid. */
void far writeCutsValue_ovr108_2EAC(unsigned n, int value)
{
    unsigned char ok;
    char name[16];
    register int fd;
    strcpy(name, "CUTS\\csXXX.n00");
    name[7] = ((n >> 6) & 7) + '0';
    name[8] = ((n >> 3) & 7) + '0';
    name[9] = (n & 7) + '0';
    ok = (fd = open(name, 0x8004)) != -1;
    ok &= lseek(fd, 4L, 0) != -1L;
    ok &= write(fd, &value, 2) == 2;
    ok &= write(fd, &value, 2) == 2;
    ok &= lseek(fd, 4L, 1) != -1L;
    ok &= write(fd, &value, 2) == 2;
    ok &= close(fd) != -1;
    if (!ok) return;
    show_cutscene(n);
}

/* 0x2FF0: advances the tasks by the time since the last call, in steps of 8 ticks (the
   remainder carried in residue): each active task counts steps against its period and
   is called when its count rises, then a last time with done set when it reaches its
   total, and removed. reset starts the clock again. */
void far run_timebased_tasks(int reset)
{
    static int16 residue = 0;
    static uint32 last = 0;
    uint32 diff;
    int old;
    register int i, steps;
    if (reset) {
        last = 0;
        return;
    }
    if (last) diff = GAME_TIME() - last;
    else diff = 0;
    last = GAME_TIME();
    steps = diff >> 3;
    residue += (int)diff & 7;
    if (residue > 7) {
        residue -= 8;
        steps++;
    }
    for (i = 0; i < 16; i++) {
        if (tasks[i] && (task_flags[i] & 1)) {
            old = task_sofar[i];
            task_sofar[i] += steps / task_period[i];
            task_residue[i] += steps % task_period[i];
            if (task_residue[i] >= task_period[i]) {
                task_residue[i] -= task_period[i];
                task_sofar[i]++;
            }
            if (task_sofar[i] >= task_total[i]) {
                (*tasks[i])(i, 1);
                remove_task(i);
                return;
            }
            if (task_sofar[i] > old) (*tasks[i])(i, 0);
        }
    }
}

/* 0x3148 */
void far punt_tasks(void)
{
    register int i;
    for (i = 0; i < 16; i++) punt_single_task(i);
}

/* 0x3161 */
void far punt_single_task(register int task)
{
    if (tasks[task]) {
        (*tasks[task])(task, 1);
        remove_task(task);
    }
}

/* 0x3191: installs fn in a free task slot, to run total times, once per period steps;
   returns the slot, or 0 when all 16 are taken (the same as slot 0). */
int far install_timebased_task(Task fn, int period, int total)
{
    register int i;
    for (i = 0; i < 16; i++) {
        if (!tasks[i]) {
            tasks[i] = fn;
            task_sofar[i] = task_residue[i] = 0;
            task_period[i] = period;
            task_total[i] = total;
            task_flags[i] = 1;
            return i;
        }
    }
    return 0;
}

/* 0x3200: a task that notes when it last ran */
void far record_task(int task)
{
    tclock = GAME_TIME();
    prev_time = task_time[task];
    task_time[task] = tclock;
}

/* 0x3241 */
void far task_palfade(register int task, int done)
{
    if (done)
        palette_fade(task_total[task], task_total[task], task_data1[task], task_data2[task],
            task_pointer_data[task]);
    else
        palette_fade(task_sofar[task], task_total[task], task_data1[task], task_data2[task],
            task_pointer_data[task]);
}

/* 0x32B5 */
void far remove_task(int task)
{
    tasks[task] = 0;
}

/* 0x32CC: sets entries first..last-1 of pal to step / total of the way from start_pal to
   end_pal and loads them into the DAC. */
void far palette_fade(int step, int total, int first, int last, unsigned char far *pal)
{
    int from;
    register int i, end;
    from = first * 3;
    end = last * 3;
    for (i = from; i < end; i++)
        pal[i] = start_pal[i] + step * (end_pal[i] - start_pal[i]) / total;
    grfx_palrange(pal, first, last - first);
}

/* 0x3333: a task that pans the view */
void far MovePanView_ovr108_3333(int task, int done)
{
    register int d, t;
    if (done) t = task_total[task];
    else t = task_sofar[task];
    d = t * pan_step;
    focus_x = task_data1[task] + d * pan_dx[pan_dir];
    focus_y = task_data2[task] + d * pan_dy[pan_dir];
    vscreen_focus(focus_x, focus_y);
}

/* 0x33A8: sets a virtual screen of w by h (graphics library seg003_0272_4A3A) and the
   split row. */
int far virtual_screen(int w, int h, int split)
{
    logw = w;
    logh = h;
    splity = split == 999 ? -1 : split;
    seg003_0272_4A3A(w, h, splity);
    return 1;
}

/* Focus the view and set the window to the part of the screen it covers. */
#define LBACK_WINDOW(fy) \
    fx = x; \
    fy_ = (fy); \
    vscreen_focus(fx, fy_); \
    top = 0; \
    bottom = 0xC7; \
    right = logw - fx; \
    left = 0xC7 - fy_ - 1; \
    if (left < 0) left = 0; \
    if (right >= 0x140) right = 0x13F; \
    ShowClip = 1; \
    set_the_window(top, bottom, right, left)

/* 0x33E0: loads CUTS\lbackNNN.byt (a raw 320 by 200 picture) into STDAT.screen and draws
   it into the virtual screen with its top left at x, y, in two parts when the screen has
   a split. Returns bltfromdrive's result. */
int far lback_vscreen(int x, int y, unsigned n)
{
    int split = splity == -1 ? 0 : splity;
    int ok;
    int fx;
    int fy_;
    int top;
    int bottom;
    char name[18] = "CUTS\\lbackxxx.byt";
    register int left, right;
    name[10] = ((n >> 6) & 7) + '0';
    name[11] = ((n >> 3) & 7) + '0';
    name[12] = (n & 7) + '0';
    ok = bltfromdrive(name, STDAT.screen, 0xFA00);
    if (split == 0) {
        LBACK_WINDOW(y);
        show(0, 0xC7, STDAT.screen, 200, 320, 0, 0);
    } else {
        LBACK_WINDOW(y);
        show(0, 0xC7, STDAT.screen, 200 - (split + 1), 320, 0, 0);
        ShowClip = 0;
        LBACK_WINDOW(y - (0xC7 - split));
        if (bottom != left)
            show(0, 0xC7, STDAT.screen, 200, 320, 0, 200 - (split + 1));
    }
    ShowClip = 0;
    vscreen_focus(focus_x, focus_y);
    set_the_window(0, 0xC7, 0x13F, 0);
    return ok;
}

/* 0x3620: the digital effects' buffer; nothing in the C calls it. */
char far * far bufferPointer(void)
{
    return dfx_buffer;
}
