/* target: seg035_31AB */
/* opts: -mm -1 -G -O -d */
/* Player input and motion: the mouse and keyboard movement controls, the clock that
   drives player and object physics, head bob, footstep and swimming sounds, the noise
   the player makes, screen shake from water, lava and quakes, and placing the 3D camera.
   The whole of DOS resident segment seg035_31AB, in original order. Function and global
   names are the originals from the FM Towns symbol table except where a comment says the
   name is ours; the source file's own name is not known.

   The clock: the 3D view's change handlers call check_physics every frame (game/UWEDIT.C
   keeps it running through change_state). It measures the time since the last call on
   the tick counter *Time (at most 0x40), adds it to the game clock and calls
   move_physics, which moves the player (move_player: PHYSICS.C's
   set_player_phys_params, MOTION.C's do_physics, PHYSICS.C's phys_affect_player), moves
   the critters and objects (critter/AI.C's move_mobile, by frames of 16 time units,
   halved under Haste), and does the screen effects and sounds. player_simple_move, the
   step keys, runs the same round for one fixed step.

   Input: PlayerInput is the movement command (PHYSICS.C's do_player_input has the list),
   set by parse_playin from the mouse in the view or the key bindings, and by
   do_player_keyboard from the keys held down (key_on). ForwInpRate and TurnInpRate are
   how fast to go forward and turn, from the mouse's position or fixed for the keys.

   Effects: playerMod[0..3] are offsets to the camera's height, heading, pitch and bank
   (get_eye adds them when doMod is set): head bob while walking, the swimmer's bob and
   roll, the jump crouch, the slow float while levitating or flying (motion_state 8),
   and three timed shakes started by set_effect (0x20 combEfflen, 0x40
   tremEfflen, 0x80 slidEfflen, bits of player->motion_state). The camera position itself
   is get_eye's.

   name: descriptive (map/filenames.tsv: player input and motion, player_mous_move,
   move_player). */

#include <stdlib.h>
#include "critter.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* This file's data, DS:073E to DS:079D. */
unsigned char pmouseHandled = 0;
unsigned char combEfflen = 0;           /* effect 0x20 */
unsigned char tremEfflen = 0;           /* effect 0x40 */
unsigned char slidEfflen = 0;           /* effect 0x80 */
unsigned char tsteps = 0;
signed char bobEffect[16] = { 1, 3, 4, 3, 1, -3, 0, 0, 1, 3, 4, 3, 1, -3, 0, 0 };
signed char sliEffect[16] = { 0, 0, -1, -2, -3, -4, -5, -6, -6, -4, -3, -2, -1, 0, 0, 0 };
signed char rorEffect[16] = { -4, -3, -2, -1, 0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3 };
int16 PlayerInput = 0;
int16 PlayerTurn = 15;
/* Nothing reads these two. */
/* name: their 3 bytes split as char and int, as in FM Towns, where _oldPlayerInput sits
   on the next even address after _IgnoringInput. */
char IgnoringInput = 0;
int16 oldPlayerInput = 0;
unsigned char water_eff = 0xFF;
char MoveCamera = 0;
int16 ForwInpRate = 0;
int16 TurnInpRate = 0;
unsigned char KeybUsed = 0;
/* name: the statics below have no FM Towns names (it addresses them from _TurnInpRate),
   so their names are ours. */
/* mouse_moves: the inputs for the three thirds of the view's bottom strip; move_keys: the
   scan codes of w s a d z c x e q; step_sfx: the footstep sounds (two feet) on floor,
   ice with grip and slippery ice; step_pan: the left and right foot's pan. */
static unsigned char mouse_moves[3] = { 9, 8, 10 };
static unsigned char move_keys[9] = { 0x11, 0x1F, 0x1E, 0x20, 0x2C, 0x2E, 0x2D, 0x12, 0x10 };
#ifndef __TURBOC__
/* --enhance modern-keys: UltimaHacks' movementKeys (John Glassmyer, MIT), the key held for each of
   move_keys' meanings (run, walk, turn left, turn right, slide left, slide right, back, fly up, fly
   down): w, x, the grey Left and Right (key_on 0x69 and 0x6A: the keyboard code puts E0 keys at
   0x60 and up), a, d, s, left Shift, left Ctrl */
static unsigned char modern_keys[9] = { 0x11, 0x2D, 0x69, 0x6A, 0x1E, 0x20, 0x1F, 0x2A, 0x1D };

static unsigned char port_move_key(int i)
{
    return ENHANCED(ENH_MODERN_KEYS) ? modern_keys[i] : move_keys[i];
}
#endif
static uint32 last_time = 0;
static unsigned char frame_inc = 0;
static unsigned char step_sfx[6] = { 1, 2, 0x2F, 0x30, 0x1D, 0x1D };
static unsigned char step_pan[2] = { 0x38, 0x48 };
static unsigned char step_foot = 0;
static unsigned char swim_pan = 0;
static unsigned char swim_count = 0;
static unsigned char noise_count = 0;

/* This file's _BSS, DS:33C6..33E7 (seg034's ends at 33C5; seg037's starts at 33E8). All
   FM Towns names. campos and camang are the camera when it is not on an object (UsPtr 0);
   the vort_ variables are the moongate vortex's spinning camera. */
/* match: laid out by name (tools/bssorder.py): doMod 28, vort_rad 126, vort_timer 286,
   vort_theta 406, playerMod and PlayerPitch 552, PlayerBank 576, camang 595, campos 603,
   vort_x and vort_y 1006. */
unsigned char doMod;
int16 playerMod[4];
int16 PlayerPitch;
int16 PlayerBank;
int16 campos[3];
int16 camang[3];
int16 vort_rad;
int16 vort_timer;
int16 vort_theta;
unsigned char vort_x, vort_y;

/* The mouse button is down in the 3D view (ui/INTERACT.C): moves the Wizard Eye camera,
   or reads the mouse as movement and keeps the pointer inside the view while the button
   is held (pmouseHandled). */
void far player_mous_move(void)
{
    if (WizEye)
        move_cam(0);
    else
    {
        parse_playin(-1);
        if (pmouseHandled == 0)
            mouse_constrain(PLeft, PBot, PLeft + PWid - 1, PBot + PHgt - 1);
        pmouseHandled = 2;
    }
}

/* The movement command. command -1 is the mouse: with the left button down, the
   bottom fifth of the view (y probably counts up from PBot, the bottom) picks a
   sideways or backward move by third, and elsewhere the pointer's distance from the
   centre third sets the turn rate and its height the forward rate; both buttons are a
   jump (forward if in the air or swimming). Otherwise command is a key binding's
   (game/PLAYER.C): 6 and 7 the jumps, 0 stop, the rest read through do_player_keyboard. */
void far parse_playin(int command)
{
    int16 buttons;

    if (!(KeybUsed = command >= 0))
    {
        mouse_getbut(&buttons);
        if (buttons == 1 || (buttons & 1) && player->drawn)
        {
            ForwInpRate = TurnInpRate = 0;
            if (inplist->y < PHgt / 5)
            {
                PlayerInput = mouse_moves[inplist->x * 3 / PWid];
            }
            else
            {
                if (inplist->x < PWid / 3)
                    TurnInpRate = -((PWid / 3 - inplist->x) * 0x180) / PWid;
                if (inplist->x > PWid * 2 / 3)
                    TurnInpRate = (inplist->x - PWid * 2 / 3) * 0x180 / PWid;
                if (inplist->y > PHgt * 2 / 5)
                    ForwInpRate = (inplist->y - PHgt * 2 / 5) * 0xC0 / PHgt;
                PlayerInput = 1;
            }
        }
        else if (buttons == 3)
        {
            PlayerInput = 1;
            if ((PN.terrain & 0x10) == 0 && player->motion_state != 1)
                PlayerInput = 7;
        }
    }
    else
    {
        do_player_keyboard();
        if (WizEye)
        {
            move_cam(PlayerInput + 1);
            command = 0;
        }
        switch (command)
        {
        case 6:
        case 7:
            if ((PN.terrain & 0x10) == 0 && player->motion_state != 1)
                PlayerInput = command;
            else
                PlayerInput = 1;
            break;
        case 0:
            TurnInpRate = ForwInpRate = 0;
            PlayerInput = command;
            break;
        }
    }
}

/* The movement keys held down: w forward fast, s forward slowly, a and d turn, x back,
   z and c sideways, e and q up and down when levitating or flying. Ignored with exactly
   one of Shift and Caps Lock (the capital letters are PHYSICS.C's step moves), or with
   Alt or Ctrl. */
void far do_player_keyboard(void)
{
    int i;

    TurnInpRate = ForwInpRate = 0;
#ifndef __TURBOC__
    if (ENHANCED(ENH_MODERN_KEYS) || ENHANCED(ENH_RUNE_KEYS)) {
        /* port only: UltimaHacks holds movement only while Ctrl and Alt are both down (the rune
           keys); modern-keys moves with Shift or Ctrl held, its fly keys */
        if (*Alt && *Ctrl)
            return;
        if (!ENHANCED(ENH_MODERN_KEYS)) {
            if ((*Shift || *CapsLock) && (*Shift == 0 || *CapsLock == 0))
                return;
            if (*Alt || *Ctrl)
                return;
        }
    } else
#endif
    {
        if ((*Shift || *CapsLock) && (*Shift == 0 || *CapsLock == 0))
            return;
        if (*Alt || *Ctrl)
            return;
    }
    for (i = 0; i < 9; i++)
    {
#ifndef __TURBOC__
        if (key_on[port_move_key(i)])   /* port only: modern-keys' keys for the same meanings */
#else
        if (key_on[move_keys[i]])
#endif
        {
            switch (move_keys[i])
            {
            case 0x11:
                ForwInpRate = 0x70;
                PlayerInput = 1;
                break;
            case 0x1F:
                ForwInpRate = 0x32;
                PlayerInput = 1;
                break;
            case 0x1E:
                TurnInpRate = -0x5A;
                PlayerInput = 1;
                break;
            case 0x20:
                TurnInpRate = 0x5A;
                PlayerInput = 1;
                break;
            case 0x2D:
                TurnInpRate = ForwInpRate = 0;
                PlayerInput = 8;
                break;
            case 0x2C:
                TurnInpRate = ForwInpRate = 0;
                PlayerInput = 9;
                break;
            case 0x2E:
                TurnInpRate = ForwInpRate = 0;
                PlayerInput = 10;
                break;
            case 0x12:
#ifndef __TURBOC__
                if ((motionbits & 0x14) == 0 && ENHANCED(ENH_MODERN_KEYS))
                    break;              /* port only: UltimaHacks' fly keys do nothing on the ground */
#endif
                if ((motionbits & 0x14) == 0)
                    PlayerInput = 0;
                else
                    PlayerInput = 12;
                break;
            case 0x10:
#ifndef __TURBOC__
                if ((motionbits & 0x14) == 0 && ENHANCED(ENH_MODERN_KEYS))
                    break;              /* port only: UltimaHacks' fly keys do nothing on the ground */
#endif
                if ((motionbits & 0x14) == 0)
                    PlayerInput = 0;
                else
                    PlayerInput = 13;
                break;
            }
        }
    }
}

/* A step key or arrow (dir is simple_fizix's turn): makes the step, then runs one
   0x40-unit round of the world's physics with it, and waits until 0x18 ticks have
   passed since the key. */
void far player_simple_move(int dir)
{
    uint32 start;
    int frames;

    start = GAME_TIME();
    if (simple_fizix(dir))
    {
        last_time = GAME_TIME();
        frame_inc = frame_inc + 4;
        PN.speed = 0;
        if (DoAnimO)
            update_animobj(1);
        player->game_clock += 0x40;
        last_time = GAME_TIME();
        frames = frame_inc;
        if (Hasted)
        {
            frames >>= 1;
            frame_inc = frame_inc & 1;
        }
        else
            frame_inc = 0;
        move_physics(0x40, frames, 1);
        editchng(10);
    }
    while (GAME_TIME() - start < 0x18)
        ;
    mouse_clearQ();
}

/* Called every frame: runs the physics for the time since the last call (see the top
   of the file). The animated objects advance once per 0x40 units of time. */
void far check_physics(void)
{
    uint32 delta;
    unsigned char frames;

#ifndef __TURBOC__
    if (port_shared_guest()) { port_shared_guest_physics(); return; }
#endif

    delta = GAME_TIME() - last_time;
    if (delta > 0x40)
    {
        delta = 0x40;
        frame_inc = frame_inc + 4;
        frames = 1;
    }
    else
    {
        frame_inc = frame_inc + (unsigned char)((GAME_TIME() >> 4) - (last_time >> 4));
        frames = (GAME_TIME() >> 6) - (last_time >> 6);
    }
    if (delta != 0)
    {
        if (frames != 0 && DoAnimO)
            update_animobj(frames);
        player->game_clock += delta;
        last_time = GAME_TIME();
        frames = frame_inc;
        if (Hasted)
        {
            frames >>= 1;
            frame_inc = frame_inc & 1;
        }
        else
            frame_inc = 0;
        move_physics(delta, frames, 0);
    }
}

/* One round: the player (unless easy, a step move, or nothing is moving), the mobile
   objects for frames (unless critters are stopped or time is stopped), the screen
   effects, and the player's noise and footsteps. */
void far move_physics(int incr, int frames, unsigned char easy)
{
    doMod = 0;
    playerMod[0] = 0;
    tsteps += incr;
    if (PlayerInput == 0 && !WizEye)
        do_player_keyboard();
    if ((PlayerInput != 0 || PN.speed != 0 || PN.vel[2] != 0 || PN.acc[2] != 0 || PN.acc[1] != 0
         || PN.acc[0] != 0 || fiz_update) && !easy)
        move_player(incr);
    if (MoveCrits && !TimeStop && frames != 0)
        move_mobile(frames);
    if (player->motion_state)
        parse_effect();
    set_sound(easy);
    make_noise(easy);
}

/* Runs the player's physics until the player is at rest (game/SKILLS.C). */
void far finish_player(void)
{
    PlayerInput = 0;
    while (PN.speed != 0 || PN.vel[2] != 0 || PN.acc[2] != 0 || PN.acc[1] != 0 || PN.acc[0] != 0
           || fiz_update)
        move_player(0x40);
}

/* One step of the player's physics, then the head bob for walking, the crouch of a
   jump and the sway of a sideways move. */
void far move_player(int incr)
{
    PN.radius = ComObjData[OBJ_ITEM(ThePlayer)].radius;
    PN.height = ComObjData[OBJ_ITEM(ThePlayer)].height;
    playerMod[1] = playerMod[2] = playerMod[3] = 0;
    set_player_phys_params(incr);
    do_physics(&PN, &PT);
    phys_affect_player();
    editchng(10);
    if ((PN.terrain & 0x10) == 0)
    {
        if (PN.speed > pFPS[0] >> 2 && PlayerInput == 1)
        {
            char bob;

            if ((bob = PN.speed * 4 / (pFPS[0] >> 1) - 1) < 2)
                bob = 2;
            doMod = 1;
            playerMod[0] = bobEffect[tsteps >> 4] * bob;
        }
        if (PlayerInput == 7)
        {
            playerMod[0] = -0x20;
            playerMod[2] = -0x100;
            doMod = 1;
            PlayerInput = 0;
        }
        if (PlayerInput == 9 || PlayerInput == 10)
        {
            doMod = 1;
            playerMod[0] = sliEffect[tsteps >> 4] * 2;
        }
    }
    PlayerInput = 0;
}

/* The player's sounds: a looping water sound while swimming (restarted every 0x1800
   ticks) with strokes panned left and right, and footsteps (by floor and ice, alternating
   feet) at a rate that rises with speed. */
void far make_noise(char easy)
{
    unsigned char ice;
    unsigned delay;

    if (player->motion_state & 1)
    {
        if (water_eff != 0xFF && watertime + 0x1800 <= GAME_TIME())
        {
            kill_effect(water_eff);
            water_eff = 0xFF;
        }
        if (water_eff == 0xFF)
        {
            watertime = GAME_TIME();
            water_eff = play_effect_here(0, 0x40, 0);
        }
        swim_count++;
        swim_count = swim_count % 15;
        if (!swim_count)
        {
            swim_pan = !swim_pan;
            if (PN.speed != 0 && GAME_TIME() > nextstep)
                play_effect_here(0x1A, step_pan[swim_pan], (PN.speed >> 5) - 0x10);
        }
    }
    else
    {
        if (water_eff != 0xFF)
        {
            kill_effect(water_eff);
            water_eff = 0xFF;
        }
        if (player->motion_state & 8)
            return;
        if (player->motion_state & 4)
        {
            if (frictionless)
                ice = 2;
            else
                ice = 1;
        }
        else
            ice = 0;
        if ((PN.terrain & 0x10) == 0)
        {
            if (easy != 0)
            {
                play_effect_here(step_sfx[ice * 2 + step_foot], step_pan[step_foot],
                                 (PN.speed >> 5) - 0x10);
                step_foot = !step_foot;
                nextstep = GAME_TIME() + 100;
            }
            else if (PN.speed > 0x2F && GAME_TIME() > nextstep)
            {
                play_effect_here(step_sfx[ice * 2 + step_foot], step_pan[step_foot],
                                 (PN.speed >> 5) - 0x10);
                step_foot = !step_foot;
                delay = 6000 / ((PN.speed >> 2) + 1) + 0x40;
                if (delay > 200)
                    delay = 200;
                nextstep = GAME_TIME() + delay;
            }
        }
    }
}

/* name: IDA's ApplyPlayerSneakScore. FM Towns has set_sound_ at this position, between
   make_noise_ and parse_effect_, and its code is the same: the player's noise and
   visibility nibbles at playerdat+1Dh from _plyNotice. */
/* The noise rises at once with speed and decays one step every eight rounds. */
void far set_sound(char easy)
{
    char n;

    n = plyNotice[0];
    if (easy != 0)
        n = n + 4;
    else if (PN.speed == 0)
        n = 0;
    else
        n = PN.speed * 10 / pFPS[0] + n - 5;
    if (player->motion_state)
        n = n + 4;
    if (n < 0)
        n = 0;
    else if (n > 15)
        n = 15;
    if (playerdat->noise > n)
    {
        if (noise_count == 0)
            playerdat->noise = playerdat->noise - 1;
    }
    else
        playerdat->noise = n;
    noise_count = (noise_count + 1) % 8;
    playerdat->visibility = plyNotice[1];
}

/* The camera offsets from the movement state: swimming (0x11; newFPS never sets 0x10)
   dips the view
   by swim_count and, once under, rolls and sways it; lava burns 1 point one time in
   five, and the djinn capture at step 3 moves to step 4 with string 0x14E; the float
   of levitating or flying; and the three timed shakes, which count down and clear their
   motion_state bits. */
void far parse_effect(void)
{
    signed char amp = 1;
    signed char phase = 1;

    if (player->motion_state == 0)
    {
        doMod = 0;
        playerMod[0] = 0;
        return;
    }
    doMod = 1;
    editchng(2);
    playerMod[1] = playerMod[2] = playerMod[3] = 0;
    if (player->motion_state & 0x11)
    {
        playerMod[0] = -player->swim_count;
        if (player->swim_count > 0x50)
        {
            amp = PN.speed * 4 / (pFPS[0] >> 1) - 3;
            if (amp < 1)
                amp = 1;
            phase = tsteps >> 4;
            if (PN.speed == 0)
                playerMod[3] = (rand() & 0x1FF) - 0x100;
            else
                playerMod[3] = amp * rorEffect[phase] << 6;
            playerMod[0] += rorEffect[(phase + 2) & 0xF] * 2 * amp;
            playerMod[1] = amp * ((rand() & 0x7F) - 0x40);
            playerMod[2] = amp * ((rand() & 0x7F) - 0x40);
        }
    }
    if (player->motion_state & 2)
    {
        if (rand() % 5 == 0)
        {
            damage_item(ThePlayer, 0L, 0, 0, 1, 8);
            if (player->xclock[XC_DJINN] == 3)
            {
                player->xclock[XC_DJINN] = 4;
                game_sprint(0x14E);  /* 'The oily mud bakes on your skin.' */
            }
        }
    }
    if (player->motion_state & 8)
        playerMod[0] = abs(0x10 - (tsteps >> 3)) * 3;
    if (player->motion_state & 0xE0)
    {
        if (player->motion_state & 0x40)
        {
            if (tremEfflen-- == 0)
            {
                player->motion_state = player->motion_state ^ 0x40;
                editchng(2);
            }
            amp = tremEfflen / 10;
            if (amp > 8)
                amp = 8;
        }
        if (player->motion_state & 0x20)
        {
            if (combEfflen-- == 0)
            {
                player->motion_state = player->motion_state ^ 0x20;
                editchng(2);
            }
            phase = combEfflen / 8;
            if (phase > 3)
                phase = 3;
        }
        if (player->motion_state & 0x80)
        {
            if (slidEfflen-- == 0)
            {
                player->motion_state = player->motion_state ^ 0x80;
                editchng(2);
            }
            phase = 0;
            playerMod[1] += ((rand() & 0x1FF) - 0x100) << 2;
        }
        amp += phase;
        playerMod[1] += amp * ((rand() & 0xFF) - 0x80);
        playerMod[2] += amp * ((rand() & 0x7F) - 0x40);
        playerMod[3] += amp * ((rand() & 0x1FF) - 0x100);
    }
}

/* Starts a timed shake: 0x20 (combat), 0x40 (a tremor) or 0x80 (sliding on ice), for
   amount rounds. The names are from the variables (combEfflen, tremEfflen,
   slidEfflen); PHYSICS.C uses 0x80 for ice and 0x40 in an unreferenced function. */
void far set_effect(unsigned char which, char amount)
{
    switch (which)
    {
    case 0x20:
        combEfflen = amount;
        break;
    case 0x40:
        tremEfflen = amount;
        break;
    case 0x80:
        slidEfflen = amount;
        break;
    default:
        return;
    }
    player->motion_state |= which;
}

/* name: IDA's PositionCameraAtObject. FM Towns has get_eye_ at this position, after
   set_effect_, and its code is the same. */
/* Places the 3D camera (cPlayer) by UsPtr: the player's eye (0xA4 above the feet) with
   the effect offsets, a fixed camera (0, campos and camang), another mobile object
   (above critdata), a view from behind the player (critdata - 1), or the moongate
   vortex (critdata - 2). */
void far get_eye(void)
{
    int16 x, y;

    if (UsPtr == ThePlayer)
    {
        cPlayer->x = PN.x;
        cPlayer->y = PN.y;
        cPlayer->z = PN.z + 0xA4;
        cPlayer->heading = PlayerFacing;
        cPlayer->pitch = PlayerPitch;
        cPlayer->bank = PlayerBank;
        if (doMod)
        {
            cPlayer->z += playerMod[0];
            if (cPlayer->z > 1000)
                cPlayer->z = 1000;
            cPlayer->heading += playerMod[1];
            cPlayer->pitch += playerMod[2];
            cPlayer->bank += playerMod[3];
        }
    }
    else if (UsPtr == 0)
    {
        cPlayer->x = campos[0];
        cPlayer->z = campos[2];
        cPlayer->y = campos[1];
        cPlayer->heading = camang[0];
        cPlayer->pitch = camang[1];
        cPlayer->bank = camang[2];
    }
    else if (UsPtr > critdata)
    {
        cPlayer->x = (OBJ_HOMEX(UsPtr) << 8) + (OBJ_FINEX(UsPtr) << 5);
        cPlayer->y = (OBJ_HOMEY(UsPtr) << 8) + (OBJ_FINEY(UsPtr) << 5);
        cPlayer->z = (OBJ_Z(UsPtr) << 3) + 0xB0;
        cPlayer->heading = (OBJ_HEADING(UsPtr) << 13) + (OBJ_FINEHEAD(UsPtr) << 8);
    }
    else if (UsPtr == critdata - 1)
    {
        cSinCos(PlayerFacing, &x, &y);
        cPlayer->x = PN.x - (x >> 7);
        cPlayer->y = PN.y - (y >> 7);
        cPlayer->z = PN.z + 0x148;
        cPlayer->heading = PlayerFacing;
        cPlayer->pitch = PlayerPitch;
        cPlayer->bank = PlayerBank;
    }
    else if (UsPtr == critdata - 2)
    {
        cSinCos(vort_theta, &x, &y);
        x = x / 0x100;
        y = y / 0x100;
        x = x * (0x40 - vort_timer) / 0x40;
        y = y * (0x40 - vort_timer) / 0x40;
        cPlayer->x = (vort_x << 8) + x * vort_rad / 2 + 0x80;
        cPlayer->y = (vort_y << 8) + y * vort_rad / 2 + 0x80;
        cPlayer->z = PN.z + 0xA4 - vort_timer * 2;
        cPlayer->heading = vort_theta + 0x7FFF;
        cPlayer->pitch = 0;
        cPlayer->bank = vort_timer << 11;
    }
}
