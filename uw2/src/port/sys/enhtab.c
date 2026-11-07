/* UW2's enhancements: changes the original game does not have, each off unless the player
   turns it on (docs/ENHANCEMENTS.md). The fixes (ENH_FIX) are among them: changes to play that
   correct a fault of the original, listed and shown on a settings tab of their own. In the
   order of portgame.h's ENH_ enum. */
#include "portgame.h"
#include "sys/enhance.h"

#define UH "UltimaHacks (John Glassmyer, MIT)"

const struct enhance_flag enhance_table[ENH_COUNT] = {
    { "skip-intro", "Starts at the main menu, without the title or the introduction", ENH_TIMING, UH, NULL },
    { "wrap-menu", "The start menu and the saved games wrap from the last item to the first", ENH_PRESENTATION, UH, NULL },
    { "fast-panels", "The right-hand panel slides twice as fast", ENH_TIMING, UH, NULL },
    { "free-heading", "Sliding along a wall no longer turns your view", ENH_GAMEPLAY, UH, NULL },
    { "subtitles", "Cutscenes show their text while the speech plays", ENH_PRESENTATION, UH, NULL },
    { "skill-messages", "Says which skill a skill point or a trainer raised, and to what", ENH_PRESENTATION, UH, NULL },
    { "perspective", "Floors and ceilings stay straight when the view pitches (walls keep the original's columns)", ENH_PRESENTATION, "uwpatch (SiENcE, MIT)", NULL },
    { "full-sprites", "Creatures and objects keep their size when the view pitches", ENH_PRESENTATION, UH, NULL },
    { "wide-pitch", "Look three times as far up and down, with what is behind you drawn", ENH_GAMEPLAY, UH, NULL },
    { "mouse-look", "The ` key toggles mouse-look: the mouse turns the view, clicks act at the crosshair", ENH_TIMING, UH, NULL },
    { "invert-look", "Mouse-look's up and down inverted", ENH_PRESENTATION, UH, NULL },
    { "modern-keys", "UltimaHacks' keys: WASD, arrows to turn and look, Space attacks, Q looks and E uses at the cursor, Z the map, R and F the panels", ENH_TIMING, UH, NULL },
    { "rune-keys", "Ctrl+Alt+letter picks a rune you have, Ctrl+Alt+Backspace clears, Ctrl+Alt+Space casts", ENH_PRESENTATION, UH, NULL },
    { "tidy-objects", "A stack too heavy to take part of no longer loses an object slot, and lost slots are freed on arrival", ENH_FIX, NULL, "UW1" },
    { "restore-at-rest", "A restored game starts at rest, without the motion or fall it was restored in", ENH_FIX, NULL, NULL },
    /* combat/DAMAGE.C's remove_object turns a broken object into debris keeping the spell it
       carried, so the debris casts it again and again for nothing; this takes the spell away */
    { "plain-debris", "An enchanted object that breaks loses its spell, so its debris casts nothing", ENH_FIX, NULL, NULL },
    /* UW2's DoPlayerWeapon (combat/COMBAT.C) adds a melee weapon's Accuracy enchantment to the
       damage and its Damage enchantment to the chance to hit; this names each for what it does
       (obj/LOOK.C's do_of), the game unchanged. UW1's work as named */
    { "true-enchants", "A weapon's enchantment is named for what it does: Damage when it adds damage, Accuracy when it adds to hit (the original names them the other way round)", ENH_FIX, NULL, NULL },
};
