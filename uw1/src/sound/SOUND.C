/* target: seg014_1DC5 */
/* opts: -mm -1 -G -O -Y -d */
/* Sound and music: MIDI effects and instruments played on spare channels of the XMIDI
   driver, the music sequences and their timbres, the digitised speech of the cutscenes
   streamed from SOUND\nn.VOC through EMS, and the sound settings. The whole of UW1's DOS
   resident segment seg014_1DC5 (UW2's seg016), in original order. Seeded from UW2Decomp's
   src/sound/SOUND.C.

   Start-up (UWEDIT.C): seg014_1DC5_1D0D reads the music and speech card numbers from
   UW.CFG; init_sounds loads the music driver named for the card (music_drivers: card 1
   PCSPKR.ADV, 2 ADLIB, 3 SBFM, 4 SBPFM, 5 PASFM, 6 MT32MPU) and, when there is a speech
   card, init_speech loads its digital driver (speech_drivers: 1 SBDIG.ADV, 2 SBPDIG,
   3 PASDIG); then SOUND\SOUNDS.DAT into `effects` with the 16 Hz effects timer
   (init_fx), and the timbre library SOUND\UW.<suffix>. init_timers starts the game
   clock: an AIL timer at 256 Hz whose callback, cllbck_tst, increments *Time.

   Effects: play_effect and its variants take an entry of SOUNDS.DAT (patch, note,
   volume, length) and play it as a MIDI note on one of four channels locked from the
   music driver (fx_play), panned and faded by the source's position. UW1 has no
   digitised effects.

   Music: the theme playing (curmusic) and the one wanted next (newmusic) are numbers
   whose two octal digits name SOUND\UWnn.XMI on the MT-32 and SOUND\AWnn.XMI otherwise.
   change_music_maybe, from the main loop, switches themes: 2 to 4 are the walking themes
   (chosen at random), 5 to 7 the combat themes, 8 plays while the weapon is drawn.

   Speech: play_speech(n) reads SOUND\nn.VOC into six 16 KB EMS pages (three critter
   page pairs lent by ovr113_2A2) and plays it through two 4 KB buffers that
   update_speech refills.

   UW1 against UW2: no digitised effects (no digi_fx_timer, init_digi_fx, load_digi_fx,
   punt_all_digi_fx, free_dfx_ems, load_dfx_page, digi_fx_play, update_digi_playback,
   stop_digi_file, kill_all_digi_effects, play_effect_on_mobile_src), so sound_move is
   part of play_effect; no punt_sound_stuff (init_sounds clears the flags itself), no
   free_s_mem, fx_available or music_available; four MIDI effect slots, not three; the
   driver file names come from tables, not from the card number; no walking_music table
   (set_random_walking_music is seg014_1DC5_15C5, a random theme 2 to 4); new: the cup
   of wonder's tune (play_cup_tune) and the speech player (init_voc, play_speech,
   update_speech, speech_over, stop_speech; UW2 keeps only empty or unused remains of
   them). The flags are plain (signed) char.

   name: function names are UW2's (the FM Towns symbol table) where the routine is the
   same, and symbols.tsv's where other files already call them (seg014_1DC5_C7C is UW2's
   kill_all_effects, seg014_1DC5_15C5 its set_random_walking_music, seg014_1DC5_1D0D its
   seg016_1E73_2FCB). The new ones are descriptive. Statics are named for their place in
   _BSS (see below). */

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <alloc.h>
#include "critter.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
#include "file.h"
#include "gfx.h"
#include "sound.h"

/* An entry of a global timbre library's directory, 6 bytes. */
struct GtlHdr {
    unsigned char patch;
    unsigned char bank;                 /* 0xFF ends the directory */
    uint32 offset;
};

/* An entry of SOUNDS.DAT, 5 bytes (UW2's has a .VOC flag and a priority too). */
struct Effect {
    unsigned char patch;                /* in bank 1 of the timbres */
    unsigned char note;                 /* 0x01 */
    unsigned char vol;                  /* 0x02 */
    uint16 length;                      /* 0x03 */
};

/* AIL.ASM (seg020) */

/* Other files */
char far bltfromdrive(char *name, void far *buf, unsigned n);
/* CUTS.C (ovr105): the cutscenes' buffer (seg049:2100), lent to the speech. */

/* This file, called before their definitions */

/* match: far variables, each its own segment: music_repeats is the listing's seg061
   (561B:0000), effects its seg062 (561C:0000), after AI.C's atk_charge. */
/* Whether a theme may start again by itself when it ends (change_music_maybe picks a
   walking theme for the others). name: chosen (static, so never named). */
static unsigned char far music_repeats[14] = {
    0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1
};
/* The effects table, SOUNDS.DAT's entries (24 at most). */
struct Effect far effects[24];

/* Uninitialised data, DS:25CA to DS:264F. */
/* match: Turbo C lays _BSS out by a hash of the names, ties in definition order
   (bssorder.py). The names are UW2's where the variable is; the new ones (sp_buf,
   speech_left, speech_pos, ems_pages) were chosen to land where the EXE has them (key in
   brackets). */
static uint16 timbre_size;              /* DS:25CA (4), the size of the timbre being read */
static struct GtlHdr timbre_entry;      /* DS:25CC (4), its directory entry */
static int16 seq_state_sz;              /* DS:25D2 (11), AIL's state table size */
static int16 htimer;                    /* DS:25D4 (272), the game clock's AIL timer */
static int16 m_settings[3];             /* DS:25D6 (293), the music card's IRQ, port and DMA */
static int16 fx_clock;                  /* DS:25DC (382), the effects' AIL timer */
static struct SoundBuff sp_buf[2];      /* DS:25DE (411), the speech's two buffers */
static unsigned char fx_mask;           /* DS:25F6 (446), the MIDI effects playing */
static unsigned char fx_note[4];        /* DS:25F7 (454), each one's note */
static void far *speech_mem;            /* DS:25FC (539), the digital driver */
static int16 speech_cfg[3];             /* DS:2600 (547), the speech card's IRQ, port and DMA */
static unsigned char fx_timbre[4];      /* DS:2606 (566), each effect's patch */
static int32 speech_left;               /* DS:260A (611), the .VOC bytes not yet played */
static int32 speech_pos;                /* DS:260E (619), where the next buffer starts */
static unsigned char fx_midi_chan[4];   /* DS:2612 (622), each effect's MIDI channel */
static struct DrvrDesc far *speech_descr;   /* DS:2616 (651) */
static void far *state_table;           /* DS:261A (659) */
static unsigned char curmusic;          /* DS:261E (683), the theme playing */
static unsigned char newmusic;          /* DS:261F (694), the theme wanted next */
/* The critter page pairs holding the .VOC. match: ovr113_2A2 fills three; the 12 bytes
   before fx_ticks_left say six. */
static int16 ems_pages[6];              /* DS:2620 (717) */
static int16 fx_ticks_left[4];          /* DS:262C (726), -1 for an effect that holds */
static uint32 theme_changed;            /* DS:2634 (732), when a combat theme last began */
static void far *midi_drv;              /* DS:2638 (765), the XMIDI driver */
static void far *midi_buf;              /* DS:263C (789), the XMI file being played */
static void far *drv_mem;               /* DS:2640 (844), the block the driver sits in */
static unsigned char numeffects;        /* DS:2644 (910), the entries of SOUNDS.DAT */
static int16 xmi_sequence;              /* DS:2646 (912), the music's AIL sequence, -1 for none */
static void far *timbre_cache;          /* DS:2648 (948) */
static struct DrvrDesc far *drv_desc;   /* DS:264C (1020), the XMIDI driver's description */

/* Initialised data, DS:0134 to DS:0231. */
static char sound_b292 = 1;             /* cleared on init, never read */
static char music_on = 1;
static char fx_on = 1;
static char fx_ok = 1;
static char music_ok = 1;
char speechok = 0;
/* match: UW.CFG's card numbers are read with %d into these bytes (DS:13A, 13B), and %d
   stores a 16-bit int: the music card's high byte goes into speech_card, which the speech
   line then sets, and the speech card's into the low byte of music_driver (DS:13C), which
   becomes 0FF00h for a card below 256 (init_sounds sets it again when it loads a music driver). The port keeps the three in
   one record so that the stores land where DOS's do (its sscanf stores Borland's widths). */
#ifdef __TURBOC__
static unsigned char sound_card = 0;
static unsigned char speech_card = 0;
static int16 music_driver = -1;
#else
static struct { unsigned char sound_card, speech_card; int16 music_driver; } cfg_13A = { 0, 0, -1 };
#define sound_card cfg_13A.sound_card
#define speech_card cfg_13A.speech_card
#define music_driver cfg_13A.music_driver
#endif
int16 sphdriver = -1;
static int16 timbre_fd = -1;
char far *dsdata[2] = { 0, 0 };
static int16 voc_fd = -1;               /* the .VOC being played */
static uint16 fx_channels = 0;
/* The drivers, by card number. name: chosen. */
static char *music_drivers[7] = {
    0, "pcspkr.adv", "adlib.adv", "sbfm.adv", "sbpfm.adv", "pasfm.adv", "mt32mpu.adv"
};
static char *speech_drivers[4] = { 0, "sbdig.adv", "sbpdig.adv", "pasdig.adv" };

/* The game clock: AIL calls it 256 times a second (init_timers). */
void far cllbck_tst(void)
{
    (*Time)++;
}

/* Starts the game clock: an AIL timer at 256 Hz calling cllbck_tst. Failing to get a timer is fatal (first_punt(1)). */
unsigned char far init_timers(void)
{
    htimer = AIL_register_timer(cllbck_tst);
    if (htimer == -1)
        first_punt(1);
    AIL_set_timer_frequency(htimer, 0x100);
    AIL_start_timer(htimer);
    return 1;
}

/* Sets up the music driver for sound_card and, when there is one, the speech driver;
   the effects table and timer (init_fx); the music buffer and the timbres. Card 1 (the
   PC speaker) gets effects only, no music. Any failure prints "Sound system
   initialization failed." and turns all sound off. */
unsigned char far init_sounds(void)
{
    char path[80];

    AIL_startup();
    if (sound_card == CARD_NONE) {
        if (speech_drivers[speech_card])
            init_speech();
    } else {
        if (sound_card == CARD_PCSPKR) {
            music_ok = 0;
            music_on = 0;
        }
        strcpy(path, "SOUND\\");
        strcat(path, music_drivers[sound_card]);
        if ((midi_drv = load_sound_driver(path)) == 0)
            goto fail;
        if ((music_driver = AIL_register_driver(midi_drv)) == -1)
            goto fail;
        drv_desc = AIL_describe_driver(music_driver);
        if (drv_desc->drvr_type != DRVR_XMIDI)
            goto fail;
        do_settings(drv_desc, m_settings);
        if (!SND_READ(music_driver, AIL_detect_device(music_driver, drv_desc->io, drv_desc->irq, drv_desc->dma, drv_desc->drq)))
            goto fail;
        AIL_init_driver(music_driver, drv_desc->io, drv_desc->irq, drv_desc->dma, drv_desc->drq);
        if (!init_fx())
            goto fail;
        if (speech_drivers[speech_card])
            init_speech();
        newmusic = 0;
        if (music_ok) {
            if ((midi_buf = farmalloc(6300)) == 0)
                goto fail;
            seq_state_sz = AIL_state_table_size(music_driver);
            if ((state_table = farmalloc(seq_state_sz)) == 0)
                goto fail;
            if (!init_timbres())
                goto fail;
        }
        sound_b292 = 0;
        return 1;
fail:
        printf("Sound system initialization failed.\n");
    }
    fx_ok = 0;
    music_ok = 0;
    music_on = 0;
    fx_on = 0;
    sound_b292 = 0;
    return 0;
}

/* Reads one timbre from the global timbre library open on fd: a directory of 6-byte
   entries (struct GtlHdr) ending at bank 0xFF, each pointing at a timbre that starts
   with its own length word. Returns the timbre in a new far block (the caller frees it)
   or 0. */
void far * far load_global_timbre(int fd, unsigned char bank, unsigned char patch)
{
    uint16 far *p;

    if (fd == -1)
        return 0;
    lseek(fd, 0L, 0);
    do {
        if (intoFarBuffer_ovr167_5DA(fd, &timbre_entry, 6) != 6)
            return 0;
        if (timbre_entry.bank == 0xFF)
            return 0;
    } while (timbre_entry.bank != bank || timbre_entry.patch != patch);
    lseek(fd, timbre_entry.offset, 0);
    intoFarBuffer_ovr167_5DA(fd, &timbre_size, 2);
    if ((p = farmalloc(timbre_size)) == 0)
        return 0;
    *p = timbre_size;
    if (intoFarBuffer_ovr167_5DA(fd, p + 1, timbre_size - 2) != timbre_size - 2)
        return 0;
    return p;
}

/* Opens SOUND\UW.<suffix> (the driver's data_suffix), gives the driver a timbre cache
   of 9130 bytes if it wants one, and on the MT-32 (card 6) installs every effect's timbre
   from bank 1 at once. */
char far init_timbres(void)
{
    int i;
    char path[80];
    register int size;
    register int n;

    strcpy(path, "SOUND\\");
    strcat(path, "uw.");
    str_cat(path, drv_desc->data_suffix);
    size = AIL_default_timbre_cache_size(music_driver);
    if (size > 0) {
        n = 0x23AA;
        if ((timbre_cache = farmalloc(n)) == 0)
            goto fail;
        AIL_define_timbre_cache(music_driver, timbre_cache, n);
    }
    if ((timbre_fd = open(path, O_RDONLY | O_BINARY)) == -1)
        goto fail;
    if (sound_card == CARD_MT32)
        for (i = 0; i < numeffects; i++)
            install_timbre(1, i);
    return 1;
fail:
    music_on = 0;
    music_ok = 0;
    return 0;
}

/* Loads timbre bank, patch from the timbre library (load_global_timbre) and hands it to the music driver. Returns 0 if it is not in the library. */
char far install_timbre(unsigned char bank, unsigned char patch)
{
    void far *p;

    p = load_global_timbre(timbre_fd, bank, patch);
    if (p) {
        AIL_install_timbre(music_driver, bank, patch, p);
        farfree(p);
        return 1;
    }
    return 0;
}

/* Loads theme `music` as the AIL sequence, unless it is already loaded, installing every
   timbre the sequence asks for; with start, starts it at normal tempo and volume 0x60.
   A failure turns the music off for the session (music_ok = 0). */
unsigned char far load_new_music(unsigned char music, char start)
{
    register char *name = WRITABLE_STR("uw00.xmi");
    unsigned t;
    char path[80];

    if (!music_ok || !music_on)
        return 0;
    if (music != curmusic) {
        if (sound_card != CARD_MT32)
            name[0] = 'a';
        name[2] = (music >> 3) + '0';
        name[3] = (music & 7) + '0';
        strcpy(path, "SOUND\\");
        strcat(path, name);
        if (!read_file_to_mbuf(path))
            goto fail;
        AIL_stop_sequence(music_driver, xmi_sequence);
        AIL_release_sequence_handle(music_driver, xmi_sequence);
        if ((xmi_sequence = AIL_register_sequence(music_driver, midi_buf, 0, state_table, 0L)) == -1)
            goto fail;
        for (t = SND_READ(music_driver, AIL_timbre_request(music_driver, xmi_sequence)); t != 0xFFFF;
             t = SND_READ(music_driver, AIL_timbre_request(music_driver, xmi_sequence)))
            if (!install_timbre(t >> 8, t & 0xFF))
                goto fail;
    }
    if (start) {
        AIL_start_sequence(music_driver, xmi_sequence);
        AIL_set_relative_tempo(music_driver, xmi_sequence, 100, 0);
        AIL_set_relative_volume(music_driver, xmi_sequence, 0x60, 0);
    }
    curmusic = music;
    newmusic = 0;
    return 1;
fail:
    xmi_sequence = -1;
    music_ok = 0;
    return 0;
}

/* Loads an AIL driver file into drv_mem. The block is a paragraph larger than the file
   and the driver is put at the start of its next paragraph, so it begins at offset 0 of
   a segment. */
void far * far load_sound_driver(char *name)
{
    void far *p;
    int32 len;
    register FILE *fp;
    register int fd;

    if ((fp = fopen(name, "rb")) == 0)
        return 0;
    fd = fileno(fp);
    len = filelength(fd);
    fclose(fp);
    if ((drv_mem = farmalloc(len + 0x10)) == 0)
        return 0;
    p = MK_FP(FP_SEG(drv_mem) + 1, 0);
    if (!bltfromdrive(name, p, (unsigned)len))
        return 0;
    return p;
}

/* Starts the loaded theme again if it is not playing, at volume 0x60. */
void far play_music(void)
{
    if (!music_ok || !music_on || xmi_sequence == -1)
        return;
    if (SND_READ(music_driver, AIL_sequence_status(music_driver, xmi_sequence)) != 1)
        AIL_start_sequence(music_driver, xmi_sequence);
    AIL_set_relative_volume(music_driver, xmi_sequence, 0x60, 0);
}

/* Reads a whole file into a new far block. Not called anywhere (UW2's seg016_1E73_19DE).
   match: static, inside play_music's row of the target table. */
static void far * far load_whole_file(char *name)
{
    int32 len;
    void far *p;
    register FILE *fp;
    register int fd;

    if ((fp = fopen(name, "rb")) == 0)
        goto fail;
    fd = fileno(fp);
    len = filelength(fd);
    fclose(fp);
    if ((p = farmalloc(len)) == 0)
        goto fail;
    if (!bltfromdrive(name, p, (unsigned)len))
        goto fail;
    return p;
fail:
    return 0;
}

/* Reads the whole file name into midi_buf (bltfromdrive). midi_buf is 6300 bytes
   (init_sounds) and the length is not checked against it: the largest theme UW1 ships is
   6248 bytes. */
char far read_file_to_mbuf(char *name)
{
    int32 len;
    register FILE *fp;
    register int fd;

    if ((fp = fopen(name, "rb")) == 0)
        goto fail;
    fd = fileno(fp);
    len = filelength(fd);
    fclose(fp);
    if (!bltfromdrive(name, midi_buf, (unsigned)len))
        goto fail;
    return 1;
fail:
    return 0;
}

/* The theme playing (curmusic). */
unsigned char far get_current_music(void)
{
    return curmusic;
}

/* Whether music is on: 0 when the music could not be set up. */
char far music_is_on(void)
{
    if (!music_ok)
        return 0;
    return music_on;
}

/* Whether sound effects are on: 0 when they could not be set up. */
char far fx_is_on(void)
{
    if (!fx_ok)
        return 0;
    return fx_on;
}

/* Music on or off (the options panel, PLAYDATA.C's read_player_data). Turning it on
   with no theme loaded picks a walking theme (seg014_1DC5_15C5) and loads it. */
void far turn_music(char on)
{
    if (!music_ok)
        return;
    if (on && !music_on) {
        music_on = 1;
        if (xmi_sequence == -1) {
            seg014_1DC5_15C5();
            load_new_music(newmusic, 1);
        }
        AIL_start_sequence(music_driver, xmi_sequence);
    } else if ((!on & music_on) && xmi_sequence != -1) {
        music_on = 0;
        AIL_stop_sequence(music_driver, xmi_sequence);
    }
}

/* Not called anywhere. match: static, inside turn_music's row. */
static void far toggle_music(void)
{
    if (!music_ok)
        return;
    turn_music(!music_on);
}

/* Sound effects on or off; off silences the effects playing (seg014_1DC5_C7C). */
void far turn_fx(char on)
{
    if (!fx_ok)
        return;
    if (on)
        fx_on = 1;
    else
        fx_on = 0;
    if (!fx_on)
        seg014_1DC5_C7C();
}

/* Toggles the effects. Not called anywhere (UW2's seg016_1E73_1BC3). match: static,
   inside turn_fx's row. */
static void far toggle_fx(void)
{
    if (!fx_ok)
        return;
    fx_on = !fx_on;
    if (!fx_on)
        seg014_1DC5_C7C();
}

/* Stops the theme playing, when music is on. */
void far stop_music(void)
{
    if (!music_ok || !music_on)
        return;
    AIL_stop_sequence(music_driver, xmi_sequence);
}

/* Fades the music up or down over four seconds. Not called anywhere. match: static,
   inside stop_music's row. */
static void far fade_music(char up)
{
    if (!music_ok || !music_on || xmi_sequence == -1)
        return;
    if (up)
        AIL_set_relative_volume(music_driver, xmi_sequence, 0x60, 4000);
    else
        AIL_set_relative_volume(music_driver, xmi_sequence, 0, 4000);
}

/* Plays effect fx as if from x, y (eighths of a tile), vol added to its own volume
   (UW2's play_effect with its sound_move). The pan is 0x40 less the sideways part of the
   direction to the source in the player's frame, clamped to 0..0x7F; the volume is the
   effect's within one tile, nothing beyond six tiles (the effect is not played), and
   falls off linearly between. Returns the effect's slot (0 to 3) or 0xFF. */
unsigned char far play_effect(unsigned char fx, int x, int y, char vol)
{
    struct Effect far *e;
    int32 ldx;
    int32 ldy;
    int px;
    int py;
    int32 d2;
    int angle;
    int16 v;
    int16 pan;
    int ny;
    int16 a;
    int16 b;
    int side;
    register unsigned dist;
    register int nx;

    if (!fx_ok || !fx_on)
        return 0xFF;
    e = &effects[fx];
    px = (OBJ_HOMEX(ThePlayer) << 3) + OBJ_FINEX(ThePlayer);
    py = (OBJ_HOMEY(ThePlayer) << 3) + OBJ_FINEY(ThePlayer);
    ldx = x - px;
    ldy = y - py;
    d2 = ldx * ldx + ldy * ldy;
    dist = cSqRt(d2);
    if (dist == 0) {
        v = e->vol + vol;
        pan = 0x40;
    } else {
        if (ldy == dist)
            ny = 0x7F;
        else if (-ldy == dist)
            ny = 0x80;
        else
            ny = (ldy << 7) / dist;
        if (ldx == dist)
            nx = 0x7F;
        else if (-ldx == dist)
            nx = 0x80;
        else
            nx = (ldx << 7) / dist;
        angle = ((OBJ_HEADING(ThePlayer) << 5) + OBJ_FINEHEAD(ThePlayer)) << 8;
        angle = (0x4000 - angle) & 0xFFFF;
        cFstSinCos(angle, &a, &b);
        a >>= 8;
        b >>= 8;
        side = (ny * b - nx * a) >> 8;
        pan = 0x40 - side;
        if (pan > 0x7F)
            pan = 0x7F;
        else if (pan < 0)
            pan = 0;
        v = e->vol + vol;
        if (dist > 0x30)
            return 0xFF;
        if (dist >= 8)
            v = v * (0x30 - dist) / 0x28;
    }
    if (v > 0x7F)
        v = 0x7F;
    else if (v < 0)
        v = 0;
    return fx_play(fx, e->patch, e->note, v, pan, e->length);
}

/* Plays effect fx at a given pan, with no position. */
unsigned char far play_effect_here(unsigned char fx, unsigned char pan, char vol)
{
    int v;
    struct Effect far *e;

    if (!fx_ok || !fx_on)
        return 0xFF;
    e = &effects[fx];
    v = e->vol + vol;
    if (v > 0x7F)
        v = 0x7F;
    else if (v < 0)
        v = 0;
    return fx_play(fx, e->patch, e->note, v, pan, e->length);
}

/* play_effect at obj's position (fine units). */
unsigned char far play_effect_on_mobile(unsigned char fx, struct Object far *obj, char vol)
{
    if (!fx_ok || !fx_on)
        return 0xFF;
    return play_effect(fx, (OBJ_HOMEX(obj) << 3) + OBJ_FINEX(obj),
                       (OBJ_HOMEY(obj) << 3) + OBJ_FINEY(obj), vol);
}

/* Silences effect slot n (0 to 3) if it is playing: on the PC speaker by an all notes
   off (controller 0x7B) on its channel, otherwise by releasing its locked channel. */
void far kill_effect(unsigned char n)
{
    if (fx_mask & (1 << n)) {
        fx_mask = fx_mask - (1 << n);
        if (sound_card == CARD_PCSPKR)
            AIL_send_channel_voice_message(music_driver, fx_midi_chan[n] + 0xAF, 0x7B, 0);
        else
            AIL_release_channel(music_driver, fx_midi_chan[n]);
        fx_channels &= ~(1 << fx_midi_chan[n]);
    }
}

/* UW2's kill_all_effects. */
void far seg014_1DC5_C7C(void)
{
    unsigned char i;

    for (i = 0; i < 4; i = i + 1) {
        if (fx_mask & (1 << i)) {
            fx_mask = fx_mask - (1 << i);
            if (sound_card == CARD_PCSPKR)
                AIL_send_channel_voice_message(music_driver, fx_midi_chan[i] + 0xAF, 0x7B, 0);
            else
                AIL_release_channel(music_driver, fx_midi_chan[i]);
            fx_channels &= ~(1 << fx_midi_chan[i]);
        }
    }
}

/* The 16 Hz effects timer: counts down each effect's ticks and silences it when they run
   out (all notes off on the PC speaker, else a note off and the channel released).
   name: FM Towns (UW2) _fx_timer. match: static, inside seg014_1DC5_C7C's row. */
static void far fx_timer(void)
{
    unsigned char i, bit;

    if (!fx_mask)
        return;
    for (i = 0, bit = 1; i < 4; i = i + 1, bit <<= 1) {
        if ((fx_mask & bit) && fx_ticks_left[i] != -1 && --fx_ticks_left[i] == 0) {
            fx_mask = fx_mask & ~bit;
            if (sound_card == CARD_PCSPKR)
                AIL_send_channel_voice_message(music_driver, fx_midi_chan[i] + 0xAF, 0x7B, 0);
            else {
                AIL_send_channel_voice_message(music_driver, fx_midi_chan[i] + 0x7F, fx_note[i], 0);
                AIL_release_channel(music_driver, fx_midi_chan[i]);
                fx_channels &= ~(1 << fx_midi_chan[i]);
            }
        }
    }
}

/* Reads SOUND\SOUNDS.DAT: a count byte, then 5 bytes an effect: patch, note, volume,
   length (high byte first). */
char far read_fx_data(void)
{
    struct Effect far *e;
    unsigned char patch;
    unsigned char note;
    unsigned char vol;
    unsigned char len_hi;
    unsigned char len_lo;
    unsigned char i;
    char path[80];
    register FILE *fp;

    strcpy(path, "SOUND\\");
    strcat(path, "sounds.dat");
    if ((fp = fopen(path, "rb")) == 0)
        return 0;
    fread(&numeffects, 1, 1, fp);
    for (i = 0; i < numeffects; i = i + 1) {
        e = &effects[i];
        fread(&patch, 1, 1, fp);
        fread(&note, 1, 1, fp);
        fread(&vol, 1, 1, fp);
        fread(&len_hi, 1, 1, fp);
        fread(&len_lo, 1, 1, fp);
        e->patch = patch;
        e->note = note;
        e->vol = vol;
        e->length = (len_hi << 8) + len_lo;
    }
    fclose(fp);
    return 1;
}

/* Starts the 16 Hz effects timer (fx_timer) and reads SOUNDS.DAT (read_fx_data). Returns 0 if either fails. */
char far init_fx(void)
{
    fx_mask = 0;
    fx_clock = AIL_register_timer(SLAVE_TIMER(fx_timer));
    if (fx_clock == -1)
        return 0;
    AIL_set_timer_frequency(fx_clock, 16);
    AIL_start_timer(fx_clock);
    if (!read_fx_data())
        return 0;
    return 1;
}

/* Plays an effect as a MIDI note on the first of four free effect slots: locks a channel
   from the music driver, selects timbre bank 1 (controller 0x72), sets the patch, resets
   the controllers, sets volume, expression and pan, and starts the note. length is in
   1/256 seconds (the timer counts it down at 16 Hz), -1 to hold. On the PC speaker (card
   1) only six effects sound, at fixed notes and lengths on channel 2. Returns the slot,
   or 0xFF. */
unsigned char far fx_play(unsigned char fx, unsigned char patch, unsigned char note,
                          unsigned char vel, unsigned char pan, register int length)
{
    unsigned char bit;
    unsigned char ch;
    unsigned char i;

    for (i = 0, bit = 1; (fx_mask & bit) && i < 4; i = i + 1, bit <<= 1)
        ;
    if (i == 4)
        return 0xFF;
    if (sound_card == CARD_PCSPKR) {
        switch (fx) {
        case 4:
        case 0x10:
            note = 0x48;
            length = 0x10;
            break;
        case 7:
        case 8:
        case 0x15:
            note = 0x42;
            length = 8;
            break;
        case 3:
        case 0x16:
            note = 0x38;
            length = 4;
            break;
        default:
            return 0xFF;
        }
        fx_mask |= bit;
        fx_midi_chan[i] = 2;
        fx_ticks_left[i] = length;
        ch = 2;
        goto note_on;
    }
    if (SND_READ(music_driver, AIL_timbre_status(music_driver, 1, patch)) == 0)
        if (!install_timbre(1, patch))
            return 0xFF;
    if ((ch = SND_READ(music_driver, AIL_lock_channel(music_driver))) == 0)
        return 0xFF;
    fx_channels |= 1 << ch;
    fx_mask |= bit;
    fx_midi_chan[i] = ch;
    fx_ticks_left[i] = length == -1 ? -1 : length / 16;
    fx_note[i] = note;
    fx_timbre[i] = patch;
    AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x72, 1);
    AIL_send_channel_voice_message(music_driver, ch + 0xBF, patch, 0);
    AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x79, 0);
    AIL_send_channel_voice_message(music_driver, ch + 0xAF, 7, 0x7F);
    AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x0B, 0x7F);
    AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x0A, pan);
note_on:
    AIL_send_channel_voice_message(music_driver, ch + 0x8F, note, vel);
    return i;
}

/* Lets the player play a musical instrument object (which: 0 or 1, patches 0x39 and 0x48
   of bank 0). Keys 1 to 9 and 0 play ten notes upward from middle C (0x3C), Alt (0x200)
   an octave higher and Ctrl (0x100) an octave lower; a note stops after a quarter of a
   second (0x40 ticks) or at the next key; Escape ends. Prints strings 0xFA and 0xFB
   around it. The last 16 notes go into hist: played on instrument 1 on level 3 within
   two tiles of 24, 45, the right tune brings up the cup of wonder (play_cup_tune), and
   then 0xFB is not printed. */
void far play_instrument(register int which)
{
    char hpos;
    char n;
    int k;
    int32 t = -1;
    unsigned char ch;
    unsigned char inst[2] = { 0x39, 0x48 };
    char notes[10] = { 0x3C, 0x3E, 0x40, 0x41, 0x43, 0x45, 0x47, 0x48, 0x4A, 0x4C };
    unsigned char last = 0xFF;
    unsigned char patch;
    unsigned char ok = 1;
    char hist[16];
    register int key;

#ifndef __TURBOC__
    port_shared_story(1);
#endif
    memset(hist, 0, 16);
    hpos = 0;
    game_sprint(0xFA);
    patch = inst[which];
    if (SND_READ(music_driver, AIL_timbre_status(music_driver, 0, patch)) == 0)
        if (!install_timbre(0, patch))
            ok = 0;
    if (ok == 1 && music_driver == -1)
        ok = 0;
    if (ok) {
        if (sound_card == CARD_PCSPKR)
            ch = 2;
        else {
            ch = SND_READ(music_driver, AIL_lock_channel(music_driver));
            fx_channels |= 1 << ch;
            AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x72, 0);
            AIL_send_channel_voice_message(music_driver, ch + 0xBF, inst[which], 0);
            AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x79, 0);
            AIL_send_channel_voice_message(music_driver, ch + 0xAF, 7, 0x7F);
            AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x0B, 0x7F);
            AIL_send_channel_voice_message(music_driver, ch + 0xAF, 0x0A, 0x40);
        }
    }
    while ((key = mouse_get_input()) != 0x1B) {
        if (key != 0) {
            k = key & 0xFCFF;
            if (k >= '0' && k <= '9') {
                n = k - '0';
                if (n == 0)
                    n = 10;
                n--;
                n = notes[n];
                if (key & 0x200)
                    n += 12;
                if (key & 0x100)
                    n -= 12;
                if (t > 0 && last != 0xFF && ok)
                    AIL_send_channel_voice_message(music_driver, ch + 0x7F, last, 0);
                hist[hpos] = n;
                hpos++;
                hpos &= 0x0F;
                if (ok)
                    AIL_send_channel_voice_message(music_driver, ch + 0x8F, n, 0x7F);
                last = n;
                t = GAME_TIME();
            }
        }
        if (t > 0 && GAME_TIME() - t > 0x40) {
            t = -1;
            if (ok)
                AIL_send_channel_voice_message(music_driver, ch + 0x7F, last, 0);
            last = 0xFF;
        }
    }
    AIL_release_channel(music_driver, ch);
#ifndef __TURBOC__
    port_shared_story(0);
#endif
    if (which == 1 && PlayerLevel == 3 && abs(OBJ_HOMEX(ThePlayer) - 0x18) <= 2
            && abs(OBJ_HOMEY(ThePlayer) - 0x2D) <= 2 && play_cup_tune(hist))
        return;
    game_sprint(0xFB);
}

/* The tune for the cup of wonder: if the cup is not yet found and the first nine notes
   of play_instrument's history are the tune, puts the cup (object 0xAE) in the player's
   hand, prints string 0x88 and marks it found. name: chosen. */
char far play_cup_tune(char *played)
{
    char tune[9] = { 0x40, 0x43, 0x41, 0x3E, 0x40, 0x47, 0x48, 0x47, 0x43 };
    char i;

    if (player->cup)
        return 0;
    for (i = 0; i < 9; i = i + 1)
        if (tune[i] != played[i])
            return 0;
    if (place_new(0L, 0xAE)) {
        game_sprint(0x88);
        player->cup = 1;
        return 1;
    }
    return 0;
}

/* Stops and releases the game clock's timer. */
void far free_timers(void)
{
    AIL_stop_timer(htimer);
    AIL_release_timer_handle(htimer);
}

/* match: the empty string is the tail of "pcspkr.adv" (-d). midi_drv, not drv_mem, is
   freed. */
void far free_sounds(void)
{
    AIL_shutdown("");
    farfree(midi_buf);
    farfree(state_table);
    farfree(timbre_cache);
    farfree(midi_drv);
}

/* Asks for theme m; change_music_maybe starts it on its next call. */
void far set_new_music(unsigned char m)
{
    newmusic = m;
}

/* A walking theme at random, 2 to 4 (UW2's set_random_walking_music). */
void far seg014_1DC5_15C5(void)
{
    newmusic = rand() % 3 + MUSIC_WALK_FIRST;
}

/* Restarts the theme when it has ended, except that theme 1 is followed by theme 4. */
void far loop_music_maybe(void)
{
    int m;

    m = curmusic;
    if (music_over()) {
        if (curmusic == MUSIC_THEME)
            m = 4;
        load_new_music(m, 1);
    }
}

#define WALKING(m)  ((m) >= MUSIC_WALK_FIRST && (m) <= MUSIC_WALK_LAST)
#define COMBAT(m)   ((m) >= MUSIC_FOE_HURT && (m) <= MUSIC_DANGER)

/* Called from the main loop to keep the right theme playing. Themes 9 and 11 play to
   their end. Ten seconds (0xA00 ticks) after the last combat a combat theme gives way to
   theme 8 if the weapon is drawn, or to a walking theme. A requested theme starts at
   once, except that one combat theme replaces another at most every eight seconds (0x800
   ticks). When a theme ends without a request, a walking theme is picked if it was one
   that does not repeat (music_repeats) or a walking theme and scrmode is 1, and theme 8
   if the weapon is drawn; otherwise it repeats. */
void far change_music_maybe(void)
{
    if (!music_ok)
        return;
    if (!music_on)
        return;
    if ((curmusic == MUSIC_VICTORY || curmusic == 0x0B) && !music_over())
        return;
    if (COMBAT(curmusic) && GAME_TIME() > lastcombattime + 0xA00) {
        if (player->drawn)
            newmusic = MUSIC_ARMED;
        else
            newmusic = rand() % 3 + MUSIC_WALK_FIRST;
    }
    if (newmusic != 0 && newmusic != curmusic) {
        if (COMBAT(curmusic) && COMBAT(newmusic)) {
            if (GAME_TIME() > theme_changed + 0x800) {
                load_new_music(newmusic, 1);
                theme_changed = GAME_TIME();
            } else
                newmusic = curmusic;
        } else
            load_new_music(newmusic, 1);
        if (COMBAT(newmusic))
            theme_changed = GAME_TIME();
    } else if (music_over()) {
        if ((music_repeats[curmusic] == 0 || WALKING(curmusic)) && scrmode == 1 || newmusic == 0)
            newmusic = rand() % 3 + MUSIC_WALK_FIRST;
        if (player->drawn)
            newmusic = MUSIC_ARMED;
        load_new_music(newmusic, 1);
        theme_changed = 0;
    }
}

/* Whether the theme has ended (or none is loaded): the sequence's status is not 1, playing. */
char far music_over(void)
{
    if (xmi_sequence == -1)
        return 1;
    return SND_READ(music_driver, AIL_sequence_status(music_driver, xmi_sequence)) != 1;
}

/* Overrides the driver's default port, IRQ and DMA with UW.CFG's (s: IRQ, port, DMA)
   where both are set (not -1). */
void far do_settings(struct DrvrDesc far *d, register int16 *s)
{
    if (d->io != -1 && s[1] != -1)
        d->io = s[1];
    if (d->irq != -1 && s[0] != -1)
        d->irq = s[0];
    if (d->dma != -1 && s[2] != -1)
        d->dma = s[2];
}

/* Loads and starts the digital driver for speech_card with UW.CFG's port, IRQ and DMA.
   Sets speechok. */
char far init_speech(void)
{
    char path[80];

    strcpy(path, "SOUND\\");
    strcat(path, speech_drivers[speech_card]);
    if ((speech_mem = load_sound_driver(path)) == 0)
        goto fail;
    if ((sphdriver = AIL_register_driver(speech_mem)) == -1)
        goto fail;
    speech_descr = AIL_describe_driver(sphdriver);
    if (speech_descr->drvr_type != DRVR_DIGITAL)
        goto fail;
    do_settings(speech_descr, speech_cfg);
    if (!SND_READ(sphdriver, AIL_detect_device(sphdriver, speech_descr->io, speech_descr->irq, speech_descr->dma, speech_descr->drq)))
        goto fail;
    AIL_init_driver(sphdriver, speech_descr->io, speech_descr->irq, speech_descr->dma, speech_descr->drq);
    speechok = 1;
    return 1;
fail:
    speechok = 0;
    return 0;
}

/* Whether there is a speech card and its driver is set up (speechok). */
char far speech_available(void)
{
    return speechok;
}

/* Gets the speech ready: the two buffers in the cutscenes' buffer (CUTS.C's free_block),
   and three critter page pairs to hold a .VOC. Without them the speech is off.
   name: chosen. */
char far init_voc(void)
{
    if (!speechok)
        return 0;
    if (dsdata[0] == 0)
        dsdata[0] = free_block();
    if (dsdata[1] == 0)
        dsdata[1] = dsdata[0] + 0x1000;
    if (ovr113_2A2(3, ems_pages) == 3)
        return 1;
    speechok = 0;
    return 0;
}

/* Plays SOUND\nn.VOC: reads it (96 KB at most) into the EMS pages, gives AIL the first
   4 KB with the .VOC header (whose 0x20 bytes are not sound), and starts playback;
   update_speech feeds the rest. A failure turns the speech off. name: chosen. */
char far play_speech(int n)
{
    char num[4];
    char path[80];
    register int i;
    register int len;

    if (!speechok)
        return 0;
    if (voc_fd != -1) {
        close(voc_fd);
        voc_fd = -1;
    }
    strcpy(path, "SOUND\\");
    if (n < 10)
        strcat(path, "0");
    strcat(path, itoa(n, num, 10));
    strcat(path, ".voc");
    if ((voc_fd = open(path, O_RDONLY | O_BINARY)) == -1)
        goto fail;
    speech_left = 0;
    for (i = 0; i < 6; i++) {
        seg012_10F(0, ems_pages[i >> 1] + (i & 1));
        speech_left += intoFarBuffer_ovr167_5DA(voc_fd, MK_FP(ems_frame, 0), 0x4000);
        if (eof(voc_fd))
            break;
    }
    seg012_10F(0, ems_pages[0]);
    len = speech_left > 0x1000 ? 0x1000 : speech_left;
    movedata(ems_frame, 0, FP_SEG(dsdata[0]), FP_OFF(dsdata[0]), len);
    if (AIL_index_VOC_block(sphdriver, dsdata[0], -1, &sp_buf[0]) == 0)
        goto fail;
    sp_buf[1] = sp_buf[0];
    speech_left -= len + 0x20;
    speech_pos = len;
    sp_buf[0].len = len - 0x20;
    AIL_register_sound_buffer(sphdriver, 0, &sp_buf[0]);
    update_speech();
    return 1;
fail:
    close(voc_fd);
    voc_fd = -1;
    speechok = 0;
    return 0;
}

/* Refills whichever speech buffer AIL has finished with (status 3) from the EMS pages,
   and keeps playback running. name: chosen. */
void far update_speech(void)
{
    int32 page;
    int32 off;
    register int i;
    register int len;

    for (i = 0; i < 2; i++) {
        if (SND_READ(sphdriver, AIL_sound_buffer_status(sphdriver, i)) == 3 && speech_left > 0) {
            page = speech_pos >> 14;
            off = speech_pos & 0x3FFF;
            seg012_10F(0, ems_pages[(int)page >> 1] + ((int)page & 1));
            len = speech_left > 0x1000 ? 0x1000 : speech_left;
            movedata(ems_frame, (unsigned)off, FP_SEG(dsdata[i]), FP_OFF(dsdata[i]), len);
            speech_left -= len;
            speech_pos += len;
            sp_buf[i].data = dsdata[i];
            sp_buf[i].len = len;
            AIL_register_sound_buffer(sphdriver, i, &sp_buf[i]);
        }
    }
    AIL_start_digital_playback(sphdriver);
}

/* Whether the speech has played out (or there is none). */
char far speech_over(void)
{
    register int s0;
    register int s1;

    if (!speechok)
        return 1;
    s0 = SND_READ(sphdriver, AIL_sound_buffer_status(sphdriver, 0));
    s1 = SND_READ(sphdriver, AIL_sound_buffer_status(sphdriver, 1));
    if (s0 == 3 && s1 == 3 && speech_left == 0)
        return 1;
    return 0;
}

/* Stops the digital playback of the speech. */
void far stop_speech(void)
{
    if (!speechok)
        return;
    AIL_stop_digital_playback(sphdriver);
}

/* Forgets the speech buffers and closes the .VOC being played. */
void far free_speech_stuff(void)
{
    dsdata[0] = dsdata[1] = 0;
    if (voc_fd != -1) {
        close(voc_fd);
        voc_fd = -1;
    }
}

/* Reads the two lines of UW.CFG that set up the sound: the music card, then the speech
   card, each with its IRQ, port (hex) and DMA. UW2's seg016_1E73_2FCB. match: the card
   numbers go straight into the byte globals with %d. */
void far seg014_1DC5_1D0D(FILE *fp)
{
    char line[100];

    fgets(line, 99, fp);
    sscanf(line, "%d %d %x %d\n", &sound_card, &m_settings[0], &m_settings[1], &m_settings[2]);
    fgets(line, 99, fp);
    sscanf(line, "%d %d %x %d\n", &speech_card, &speech_cfg[0], &speech_cfg[1], &speech_cfg[2]);  /* %d stores 2 bytes */
}
