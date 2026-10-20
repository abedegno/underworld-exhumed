/* keyqueue.c: replaces src/sys/KEYQUEUE.ASM and src/sys/KBDINT.ASM (seg021 modules 7 and 8),
   and the keyboard controller under them.

   KBDINT.ASM's int 9 handler stores each byte from port 60h in a 64-byte ring buffer at
   FD71:0300..033F and toggles the caps lock state on 3Ah; KEYQUEUE.ASM's key (_4A8) takes whole
   scan code sequences out of it, counts each key's presses in key_on, keeps the modifiers and
   translates through the Asc table. Here kbd_byte is the handler: the platform layer calls it
   on its own thread for each byte, as the keyboard interrupted the CPU, and key reads the same
   buffer, at the same offsets of dseg062_62a6, with the same logic, on the game's thread. The
   write pointer (KBDINT's _590, in its code segment) is a variable here, published after the
   byte is stored, as the 8086's single word store did.

   The keyboard itself repeats a held key: kbd_tick_ms, from the PIT thread, sends the last make
   code again after 500 ms and then 10.9 times a second, the BIOS's default typematic rate, so
   the game's own filtering of repeats (01E0) sees what it saw in DOS. */
#include <stdatomic.h>
#include <string.h>
#include "port.h"

#define D dseg062_62a6
#define W(o) ((uint16_t)(D[(o)] | D[(o) + 1] << 8))
#define SETW(o, v) port_setw(&D[(o)], (uint16_t)(v))

static _Atomic uint16_t write_ptr = 0x300;     /* KBDINT.ASM's _590 */
static atomic_flag lock = ATOMIC_FLAG_INIT;
static int installed;

/* typematic state */
static uint8_t held, held_e0, last_e0;
static uint32_t held_ms;

/* The int 9 handler (+62E): store, advance, wrap; caps lock toggles the lock state. The LEDs
   are the host's business. */
static void int9(uint8_t al)
{
    uint16_t di = atomic_load(&write_ptr);
    D[di] = al;
    di++;
    if (di == 0x340) di = 0x300;
    atomic_store(&write_ptr, di);
    if (al == 0x3A) D[0x1E8] ^= 1;
}

void kbd_byte(uint8_t sc)
{
    while (atomic_flag_test_and_set(&lock)) ;
    if (sc == 0xE0 || sc == 0xE1) last_e0 = sc == 0xE0;
    else {
        if (!(sc & 0x80)) { if (held != sc || held_e0 != last_e0) held_ms = 0; held = sc; held_e0 = last_e0; }
        else if ((sc & 0x7F) == held && held_e0 == last_e0) held = 0;
        last_e0 = 0;
    }
    if (installed) int9(sc);
    atomic_flag_clear(&lock);
}

void kbd_tick_ms(uint32_t ms)
{
    uint32_t before;
    while (atomic_flag_test_and_set(&lock)) ;
    if (held && installed) {
        before = held_ms;
        held_ms += ms;
        /* 500 ms, then every 92 ms (10.9 a second) */
        if (held_ms >= 500 && (before < 500 || (held_ms - 500) / 92 != (before - 500) / 92)) {
            if (held_e0) int9(0xE0);
            int9(held);
        }
    }
    atomic_flag_clear(&lock);
}

/* _598 (installkey): settle the layout (_46B), then the handler is live. */
static void layout(void)
{
    /* _46B: 2 for an enhanced keyboard (0040:0096 bit 4, set: the port's keyboard is a 101-key
       one), else 4 when FD71:0120 is set, else 0. */
    if (W(0x1E9) != 0) return;
    SETW(0x1E9, 2);
}

void seg021_22FD_598(void)
{
    layout();
    installed = 1;
}

/* _5C8 (restorekey) */
void seg021_22FD_5C8(void)
{
    installed = 0;
}

/* _552: step SI, wrapping at 0340h; 1 when it meets the write pointer. */
static int step(uint16_t *si)
{
    (*si)++;
    if (*si == 0x340) *si = 0x300;
    return *si == atomic_load(&write_ptr);
}

/* _577: the next byte, advancing the read pointer 026C. */
static uint8_t next(void)
{
    uint16_t si = W(0x26C);
    uint8_t al = D[si++];
    if (si == 0x340) si = 0x300;
    SETW(0x26C, si);
    return al;
}

/* _562: is a whole sequence waiting? 0 if not, else 1 with its first byte in *al. */
static int whole(uint8_t *al)
{
    uint16_t si = W(0x26C);
    if (si == atomic_load(&write_ptr)) return 0;
    if (D[si] == 0xE0) {
        if (step(&si)) return 0;
    } else if (D[si] == 0xE1) {
        if (step(&si)) return 0;
        if (step(&si)) return 0;
    }
    *al = next();
    return 1;
}

/* _454: layout 4's remapping through the 13 entries at 026E. */
static uint8_t remap(uint8_t al)
{
    uint8_t hi = al & 0x80;
    int i;
    al &= 0x7F;
    for (i = 0; i < 13; i++)
        if (D[0x26E + i] == al) { al = D[0x26E + i + 13]; break; }
    return al | hi;
}

/* _51A: the six modifier states at 01E2, each down if either of its two keys is. */
static void modifiers(void)
{
    uint16_t bx = W(0x1E9);
    int i;
    for (i = 0; i < 6; i++, bx += 0x10)
        D[0x1E2 + i] = D[(uint16_t)(W(0x299 + bx) + 0x1EB)] | D[(uint16_t)(W(0x2A1 + bx) + 0x1EB)];
}

/* _4A8 (key): the next key event. 0 for none; else the character in the low byte and the scan
   code in the high byte. */
int key(void)
{
    port_shared_poll_input();
    uint8_t al;
    uint16_t ax, bx;
    int i;
    for (;;) {
        if (!whole(&al)) return 0;
        if (al == 0xE1) {
            next();
            next();
            al = 0xE0;
        } else if (al == 0xE0) {
            al = next();
            D[0x26B] = al;
            if ((al & 0x7F) == 0x2A || (al & 0x7F) == 0x36) continue;
            for (i = 0; i < 16; i++)
                if (D[0x288 + i] == (al & 0x7F)) break;
            if (i == 16) continue;
            al = (uint8_t)((D[0x26B] & 0x80) | (0x60 + i));
        }
        if (W(0x1E9) == 4) al = remap(al);
        ax = al;
        bx = al & 0x7F;
        if (al & 0x80) {
            D[0x1EB + bx] = 0;
            modifiers();
            continue;
        }
        if (W(0x1E0) == 0 && D[0x1EB + bx] != 0) continue;
        D[0x1EB + bx]++;
        if (D[0x1E2]) bx |= 0x80;
        ax = (uint16_t)(D[0x10 + bx] << 8 | (ax & 0xFF));
        modifiers();
        return (uint16_t)(ax << 8 | ax >> 8);
    }
}

/* Switch only the keyboard controller state; the game world is never copied. */
void port_shared_keyboard_seat(int seat)
{
    static struct { unsigned char bytes[0x160]; uint16_t write; uint8_t held,e0,last; uint32_t ms; } saved[2];
    static int current, ready;
    while (atomic_flag_test_and_set(&lock)) ;
    if (!ready) {
        memcpy(saved[1].bytes,D+0x1E0,sizeof saved[1].bytes);
        memset(saved[1].bytes+2,0,7);memset(saved[1].bytes+0xB,0,128);
        memset(saved[1].bytes+0x120,0,64);
        saved[1].bytes[0x8C]=0;saved[1].bytes[0x8D]=3;saved[1].write=0x300;ready=1;
    }
    if (seat<0) {
        memset(saved[1].bytes+2,0,7);memset(saved[1].bytes+0xB,0,128);
        memset(saved[1].bytes+0x120,0,64);saved[1].bytes[0x8C]=0;saved[1].bytes[0x8D]=3;
        saved[1].write=0x300;saved[1].held=saved[1].e0=saved[1].last=0;saved[1].ms=0;
        atomic_flag_clear(&lock);return;
    }
    if (seat!=current) {
        memcpy(saved[current].bytes,D+0x1E0,sizeof saved[current].bytes);
        saved[current].write=atomic_load(&write_ptr);saved[current].held=held;
        saved[current].e0=held_e0;saved[current].last=last_e0;saved[current].ms=held_ms;
        memcpy(D+0x1E0,saved[seat].bytes,sizeof saved[seat].bytes);
        atomic_store(&write_ptr,saved[seat].write);held=saved[seat].held;held_e0=saved[seat].e0;
        last_e0=saved[seat].last;held_ms=saved[seat].ms;current=seat;
    }
    atomic_flag_clear(&lock);
}
