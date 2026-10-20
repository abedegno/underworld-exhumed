/* target: seg011_1CA3 */
/* opts: -mm -1 -G -O -d */
/* The main loop and the per-screen change dispatcher.
   mainloop runs until notdone is cleared: each pass runs do_changes when any change bit
   is set, then hands the input list (inplist) to input_dispatch, which reads the mouse
   and keyboard and calls the handlers of the current screen. do_changes is how the game
   redraws: anything that needs work on the next pass sets a bit in the global changed
   with editchng, and do_changes calls editor_dispatch[scrnum][bit] for each set bit
   (15 bits, 0 to 14), clearing the bit first. scrnum picks one screen's table (in UW2
   UWEDIT.C owns editor_dispatch, change_state and scrnum). change_state[scrnum] is or-ed
   back in after every pass, so a screen can keep some bits always set. It also counts
   down pmouseHandled to give the mouse back with mouse_freereign.
   UW1 differences from UW2's MAINLOOP.C: mainloop tests changed itself before calling
   do_changes, and there is no digital sound effect playback (no dsfx_playing, no call
   to update_digi_playback), so this file has no _DATA.
   Entry points: mainloop, editchng (called all over the game), do_changes.
   The whole of UW1's DOS resident segment seg011_1CA3, in original order.
   Name: inferred (mainloop is its first function, and System Shock's main loop file is
   MAINLOOP.C). UW1 has no symbol-bearing build: the function names are UW2's, from the
   FM Towns symbol table, the routines being the same. */

#include "motion.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* The game's main loop: until notdone is cleared, runs the change handlers when any change bit is set, then reads and dispatches input. */
void far mainloop(void)
{
    while (notdone)
    {
#ifndef __TURBOC__
        if (port_shared_step()) continue;
        /* Native co-op sessions are pumped on the game thread, including on the map. */
        if (!port_shared_on()) port_coop_tick();
#endif
        if (changed)
            do_changes();
        input_dispatch(inplist);
    }
}

/* Runs editor_dispatch[scrnum][bit] for each change bit set (0 to 14), clearing the
   bit first, then sets the screen's always-on bits again (change_state) and counts
   pmouseHandled down, freeing the mouse when it reaches 0. */
void far do_changes(void)
{
    unsigned i;
    int bit;

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

/* Asks for work on the next pass of the main loop: sets bits in changed. */
void far editchng(int bits)
{
    changed |= bits;
}
