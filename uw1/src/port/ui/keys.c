/* keys.c: --enhance modern-keys' and rune-keys' actions (docs/ENHANCEMENTS.md), UltimaHacks'
   keyAndMouseBindings in our names (John Glassmyer, MIT). Each key that stands for a click sets
   what that click sets (the position in inplist, as the region's handler reads it) and calls the
   handler, so the game decides everything as it does for the click. Port only; bound in
   game/PLAYER.C's init_player and ui/AUTOMAP.C. */
#include "compat.h"
#include "gfx.h"
#include "ui.h"
#include "player.h"
#include "combat.h"
#include "sys.h"
#include <stdlib.h>
#include "view3d.h"
#include "inv.h"
#include "sound.h"
#include "object.h"

/* ui/INTERACT.C (and UW2's GAMESCR.C for the flasks and compass): no header declares these */
void far flask_info(void);
void far print_info(void);
struct Object far * far pick_3d(int how);
void far look_nothing(unsigned char how, int txt);
void far player_3dlook(void);
void far player_3duse(void);
void far player_3dtalk(void);
unsigned char far IsMobElem(struct Object far *obj);
extern int16 pTxtId;                    /* the texture a pick found, for look_nothing */
extern struct Object far *newPlObj;     /* the object picked, for player_3dlook and _3duse */

extern int16 attackKey;                 /* combat/COMBAT.C: the key held to charge a blow */
extern int16 m3dx, m3dy, m3dw, m3dh;    /* ui/MOUSE.C: the 3D view (m3dy its bottom edge) */

static int last_attack = 6;             /* slash, until . ; or p choose another */

/* Space (type 0): attack with the last attack type; . ; p (3, 6, 9) also choose it */
void far port_key_attack(int type)
{
    if (type)
        last_attack = type;
    player_attack(type ? type : last_attack);
    if (!type)
        attackKey = 0x39;               /* Space's scan code, held to charge, as UltimaHacks */
}

/* h: as a click on the left flask, then on the right (UltimaHacks' clickFlasks) */
void far port_key_flasks(void)
{
    inplist->x = 20;
    inplist->y = 11;
    flask_info();
    inplist->x = 60;
    inplist->y = 11;
    flask_info();
}

/* g: as a click on the compass */
void far port_key_compass(void)
{
    print_info();
}

/* r, f: to the rune bag (1) or the statistics (2), or back to the inventory when it is showing
   (UltimaHacks' flipToPanel) */
void far port_key_panel(int panel)
{
    set_screen_frame(SCR_PANEL, RightPanel == panel ? 0 : panel);
}

/* z: the map (UltimaHacks' transitionToInterfaceMode(MAP)) */
void far port_key_open_map(void)
{
    newscr(2);
}

/* 1, 3, the grey Up and Down: the pitch as the original 1 and 3 (the other way round); the
   keypad's 5 levels it */
void far port_key_pitch(int dir)
{
    chg_plyp(dir);
}

/* c, v, b: close the open container, scroll the inventory down, up */
void far port_key_bag(int how)
{
    if (how == 0)
        CloseTheBag();
    else if (how == 1)
        ScrollItemsDown();
    else
        ScrollItemsUp();
}

/* q looks (0), e uses (1) at the pointer, as a click there in look or use would (UltimaHacks'
   interactAtCursor): only in the 3D view, and no use while carrying something or aiming */
void far port_key_interact(int use)
{
    int16 x, y;

    if (GameInputMode != 0 && use)
        return;
    mouse_getxy(&x, &y);
    if (x < m3dx || x > m3dx + m3dw || y > m3dy || y < m3dy - m3dh)
        return;
    inplist->x = x - m3dx;              /* relative to the view, as pick_3d reads a click */
    inplist->y = y - (m3dy - m3dh);
    newPlObj = pick_3d(2);
    if (getenv("UW_COOP_TEST_TRACE")) fprintf(stderr,"SHARED PICK seat=%d x=%d y=%d item=%d\n",port_shared_guest(),x,y,newPlObj ? newPlObj->id & 0x1FF : -1);
    if (newPlObj == 0) {                /* nothing but terrain: a look describes it */
        if (!use)
            look_nothing(2, pTxtId);
        return;
    }
    if (port_shared_avatar(newPlObj)) { scroll_print("Your companion.\n"); return; }
    if (getenv("UW_COOP_TEST_TRACE")) fprintf(stderr,"SHARED USE seat=%d item=%u\n",port_shared_guest(),newPlObj->id & 0x1FF);
    if (!use)
        player_3dlook();
    else if ((IsMobElem(newPlObj) && OBJ_MAJOR(newPlObj) == 1) || (newPlObj->id & 0x1FF) == 0x1CD)
        player_3dtalk();                /* a character (or item 0x1CD): talk, as UltimaHacks */
    else
        player_3duse();
}

/* Ctrl+Alt+letter, Backspace, Space (rune-keys, UltimaHacks' runeKey): a rune the player has goes
   on the shelf as a click on it in the rune bag would; one it lacks is refused with a sound */
void far port_key_rune(int ch)
{
    int rune, col, row;
    int16 mode;

    {  /* a test switch, UW1PORT_GRANT_RUNES (Exhume's tools/enhcheck.py keys): every rune, given at the
           first rune key, since the staged saved game's player has none */
        static int granted;
        if (!granted && getenv("UW1PORT_GRANT_RUNES")) {
            granted = 1;
            player->runebag[0] = player->runebag[1] = player->runebag[2] = 0xFF;
        }
    }
    if (ch == ' ') {
        try_cast(1);
        return;
    }
    if (ch == KEY_BACKSPACE)
        rune = -1;
    else {
        if (ch < 'a' || ch > 'y' || ch == 'x')
            return;
        rune = ch == 'y' ? 23 : ch - 'a';   /* no X rune: Y is Ylem, 23 */
        if (!(player->runebag[rune >> 3] >> (7 - (rune & 7)) & 1)) {
            /* refused silently: UltimaHacks plays effect 45 (a file it shares with UW2, which has
               49 effects); UW1 has 24 (sound/SOUND.C), so there is no effect 45 to play */
            return;
        }
    }
    play_effect_here(rune < 0 ? 4 : 3, 64, 0);   /* UltimaHacks: 4 clearing, 3 a rune taken */
    col = rune & 3;                         /* mous_in_rune's slot, inverted: four to a row, */
    row = 5 - (rune >> 2);                  /* 20 to 23 at the top */
    mode = GameInputMode;
    inplist->x = rune < 0 ? 40 : 3 + col * 0x12 + 9;
    inplist->y = rune < 0 ? 10 : 0x12 + row * 0xF + 7;   /* y < 0x12 clears the shelf */
    inplist->cmd &= ~2;                     /* the left button: add, not look */
    GameInputMode = 0;
    mous_in_rune();
    GameInputMode = mode;
}
