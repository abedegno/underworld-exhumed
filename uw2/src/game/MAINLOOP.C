/* target: seg012_1D31 */
/* opts: -mm -1 -G -O -d */
/* The main loop and the per-screen change dispatcher.
   mainloop runs until notdone is cleared: each pass runs do_changes, then hands the
   input list (inplist) to input_dispatch, which reads the mouse and keyboard and calls
   the handlers of the current screen. do_changes is how the game redraws: anything that
   needs work on the next pass sets a bit in the global changed with editchng, and
   do_changes calls editor_dispatch[scrnum][bit] for each set bit (15 bits, 0 to 14),
   clearing the bit first. scrnum picks one of three screens' tables (UWEDIT.C owns
   editor_dispatch, change_state and scrnum). change_state[scrnum] is or-ed back in after
   every pass, so a screen can keep some bits always set: the 3D view's physics, display
   and screen update (0x3800) and the automap's redraw (0x1000). It also keeps digital sound effects playing, and counts down
   pmouseHandled to give the mouse back with mouse_freereign.
   Entry points: mainloop (called from UWEDIT.C's main), editchng (called all over the
   game), do_changes.
   Data: dsfx_playing, set while a digital sample is playing.
   Name: inferred (mainloop is its first function, and System Shock's main loop file is
   MAINLOOP.C). The function names are the originals from the FM Towns symbol table. */

#include "motion.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

unsigned char dsfx_playing = 0;      /* this file's _DATA: DS:010A */


void far mainloop(void)
{
    while (notdone)
    {
#ifndef __TURBOC__
        port_cheat_point();     /* port only: a test's cheats (Exhume's sys/cheat.c), between passes */
        if (port_shared_step()) continue;
        /* Native co-op sessions are pumped on the game thread, including on the map. */
        if (!port_shared_on()) port_coop_tick();
#endif
        do_changes();
        input_dispatch(inplist);
    }
}

void far do_changes(void)
{
    unsigned i;
    int bit;

    if (dsfx_playing)
        update_digi_playback();
    if (changed)
    {
        for (i = 0, bit = 1; i < 15; i++, bit <<= 1)
        {
            if (changed & bit)
            {
                changed = changed & ~bit;
                if (editor_dispatch[scrnum][i])
                    editor_dispatch[scrnum][i]();
            }
        }
        changed |= change_state[scrnum];
        if (pmouseHandled > 0)
        {
            if (--pmouseHandled == 0)
                mouse_freereign();
        }
    }
}

void far editchng(int bits)
{
    changed |= bits;
}
