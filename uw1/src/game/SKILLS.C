/* target: ovr143 */
/* opts: -mm -1 -G -O -Y -d */
/* Skills, levelling, mantras, end-game statistics, sleep and dreams, eating, the silver
   tree, death and traps: the whole of UW1's DOS overlay ovr143, in original order.
   Seeded from UW2Decomp's src/game/SKILLS.C (UW2's ovr154).

   Skills: the player record's skills[], 0 to 30 each, each governed by an attribute
   (prime). add_to_skill is character creation's raise, get_skill a trainer's or a skill
   point's raise. player_compute derives maximum HP, mana and carrying weight; advance
   gains levels. mantra_advance is chanting at a shrine: a skill's own mantra raises that
   skill twice over, the three group mantras raise skills of a group (great_advance and
   report_advance print the outcome), and three special mantras give the cup of wonder's
   whereabouts, the key of truth and a message.
   Sleep: player_sleep passes time, heals, feeds and sobers the player, and may show one
   of Garamon's dreams (dream).
   Death: player_is_dead returns the player to life at the silver tree if one was planted
   (plant_seed, do_resurrect), otherwise ends the game (UWEDIT.C's real_death).
   The end: check_victory, run every frame, shows the end screens once the game is won
   (EndGameMode_dseg_1C8F), and once every talisman is destroyed opens the moongate that
   takes the Slasher of Veils and the player to the void.
   Traps on objects: DetectedTrap and RemoveTrap.

   UW1 against UW2: no grant_skill_advance, cs_check, player_won_game, do_gem,
   do_dreamret, punt_mouse_obj, punt_void or go_void (no ethereal void dream, no
   blackrock gem); new mantra_advance, great_advance, report_advance, check_victory,
   plant_seed and do_resurrect. The player record is UW1's (struct Player, player.h).
   Data: skill_rng, EndGameMode_dseg_1C8F.
   UW1 has no symbol-bearing build: names are UW2's (the FM Towns symbol table) where the
   routine is the same; the new functions' names are descriptive, chosen so that their
   bssorder keys reproduce the EXE's overlay stub order (see each).
   Name: descriptive (skills, sleep, dreams, eating, death: use_skill, player_sleep).

   Entry points: player_use_skill and player_key_sleep (PLAYER.C's key bindings);
   player_sleep, plant_seed and mantra_advance (USEITEMS.C, CONVERSE.C); check_victory
   (UWEDIT.C's editor_dispatch, every frame); player_is_dead (INTERACT.C); DetectedTrap
   and RemoveTrap (SPELLS.C, INTERACT.C); get_skill (BABLHACK.C, trainers); add_to_skill
   (CHARGEN.C); advance (SKILLCHK.C). */

#include <string.h>
#include <stdlib.h>
#include "combat.h"
#include "critter.h"
#include "event.h"
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* Declared in each file that uses it, its own way (no header). */
void far grfx_clear(void);
unsigned char far player_eat(int nutrition);

/* UW1: declarations that differ from the headers'. */
unsigned char far grfx_load_font(char *name);
void far Vortex_ovr143_E09(int unused);   /* PLAYER.C; this file passes it -1, which it does not read */
/* UW1: copy_visible_to_hidden is seg003_5350 (symbols.tsv's name; kin pairs it with
   UW2's). */
/* UW1: ovr145 is not matched: panel_check's place in player_sleep, advance and
   mantra_advance (kin pairs ovr145_4FB with UW2's panel_check); symbols.tsv's name.
   NightCleanCritPages is critter/CRPAGES.C's. */

/* The use-skill key: only track (12) does anything, running mdetect(8, value) to
   report nearby creatures. Traps (10) and search (11) do nothing here; any other skill
   just prints its name. */
char far use_skill(struct Object far *who, unsigned char skill, unsigned char value)
{
    if (who != ThePlayer)
        return 0;
    switch (skill)
    {
    case SKILL_TRACK:
        mdetect(8, value);
        break;
    default:
        scroll_print(get_string((skill + STRN_SKILLS) | STR_CHARGEN));
        scroll_print("\n");
        return 1;
    case SKILL_TRAPS:
    case SKILL_SEARCH:
        break;
    }
    return 1;
}

/* The use-skill key (PLAYER.C binds F9 with 2): skill counts from SKILL_TRAPS, so 2 is
   SKILL_TRACK. */
char far player_use_skill(int skill)
{
    return use_skill(ThePlayer, skill + SKILL_TRAPS, player->skills[skill + SKILL_TRAPS]);
}

/* The derived maxima: HP 30 + level * strength / 5, mana (mana skill + 1) *
   intelligence / 8 (kept aside on level 7, where it is held at nothing; see GAMEWRAP.C's
   do_level_hacks), carrying weight strength * 20. With restore, mana is filled. */
void far player_compute(char restore)
{
    register int mana;

    playerdat->avghit = 30 + player->level * playerdat->attr[ATTR_STR] / 5;
    mana = (player->skills[SKILL_MANA] + 1) * playerdat->attr[ATTR_INT] >> 3;
    if (PlayerLevel == LEVEL_TYBAL)
        player->saved_mana = mana;
    else
        player->max_mana = mana;
    player->max_weight = playerdat->attr[ATTR_STR] * 2 * 10;
    if (restore)
        player->play_mana = player->max_mana;
}

/* Gains levels: prints the new level, gives one skill point a level, recomputes the
   maxima. */
void far advance(char levels)
{
    register char *s = WRITABLE_STR(" 0\n");

    player->level = player->level + levels;
    if (player->level >= 10)
        s[0] = player->level / 10 + '0';
    else
        s[0] = ' ';
    s[1] = player->level % 10 + '0';
    game_sprint(0x93);                  /* "You have attained experience level " */
    scroll_print(s);
    player->skill_points = player->skill_points + levels;
    player_compute(0);
    ovr145_4FB();
}

/* The attribute that governs a skill: 0 strength, 1 dexterity, 2 intelligence. */
int far prime(int skill)
{
    if (skill < SKILL_MANA)
        return 0;
    if (skill < SKILL_TRAPS)
        return 2;
    return 1;
}

/* Character creation's raise. A new skill gets 3 + attribute / 9 + 0 to 2 at random,
   plus three skill_checks of the attribute against 20 (each -1 to 2); a skill already
   held gets 1 + attribute / 13 + 0 or 1, plus two checks. At most 30. */
void far add_to_skill(int skill)
{
    int rolls;
    int base;
    int divisor;
    int stat;

    if (player->skills[skill] != 0)
    {
        base = 1;
        divisor = 13;
        rolls = 2;
    }
    else
    {
        base = 3;
        divisor = 9;
        rolls = 3;
    }
    stat = playerdat->attr[prime(skill)];
    player->skills[skill] += base;
    player->skills[skill] += stat / divisor;
    player->skills[skill] += (int32)rand() * rolls / 0x8000L;
    for (; rolls > 0; rolls--)
        player->skills[skill] += skill_check(stat, 20);
    if (player->skills[skill] > 30)
        player->skills[skill] = 30;
}

/* The divisor for the random bonus when a skill rises, by governing attribute. */
static unsigned char skill_rng[3] = { 25, 40, 10 };
/* Set when the game is won: check_victory then shows the end. name: the listing's. */
char EndGameMode_dseg_1C8F = 0;

/* Raises a skill by spending a skill point (from a trainer, via BABLHACK.C's x_skills,
   or grant_skill_advance). Fails (returns 0) if the skill is already above twice its
   attribute or at 30. Otherwise +1; +1 more if the attribute is not strength and half
   the attribute is still above the skill; and +1 more with chance (attribute - skill) /
   skill_rng[attribute] while below the attribute. Raising lore also resets the lore
   tries and records the lore skill for this level. */
char far get_skill(char skill)
{
    int stat_rng;
    char result = 1;
    int attribute;
    int stat;

    stat = prime(skill);
    stat_rng = skill_rng[stat];
    attribute = playerdat->attr[stat];
    if (attribute * 2 < player->skills[skill] || player->skills[skill] >= 30)
        result = 0;
    else
    {
        player->skills[skill] = player->skills[skill] + 1;
        if (stat != 0 && attribute / 2 > player->skills[skill])
            player->skills[skill] = player->skills[skill] + 1;
        if (player->skills[skill] < attribute
            && rand() % stat_rng < attribute - player->skills[skill])
            player->skills[skill] = player->skills[skill] + 1;
        if (player->skills[skill] > 30)
            player->skills[skill] = 30;
    }
    if (skill == SKILL_LORE)
    {
        clear_all_loretries();
        if (PlayerLevel <= 8)
            player->lore[PlayerLevel] = player->skills[SKILL_LORE];
    }
    return result;
}

/* UW1: a skill's own mantra's outcome: "You have advanced greatly in <skill>." or "You
   cannot advance any further in that skill." */
/* name: descriptive, chosen for its bssorder key (191, between moveto 141 and do_mstone
   276, as the EXE's stub order has it). */
void far great_advance(int skill, char ok)
{
    if (ok) {
        game_sprint(0x1C);
        scroll_print(get_string((skill + STRN_SKILLS) | STR_CHARGEN));
        scroll_print(".\n");
    } else
        game_sprint(0x1B);
}

/* UW1: a group mantra's outcome: the skills raised (list, 0xFF-terminated, at most four),
   as "You have advanced in a, b and c.", or "None of your skills improved." */
/* name: descriptive, chosen for its bssorder key (10, between mantra_advance 5 and
   moveto 141). */
void far report_advance(register unsigned char *list)
{
    register int i = 0;

    if (list[0] == 0xFF) {
        game_sprint(0x1E);
        return;
    }
    game_sprint(0x1D);
    for (; list[i] != 0xFF && i < 4; i++) {
        if (i == 3 || (i != 0 && list[i + 1] == 0xFF))
            scroll_print(" and ");
        else if (i != 0)
            scroll_print(", ");
        scroll_print(get_string((list[i] + STRN_SKILLS) | STR_CHARGEN));
    }
    scroll_print(".\n");
}

/* UW1: chanting at a shrine. The mantra typed is looked up among string block 2's
   strings 0x33 to 0x4C: the first twenty are the skills' own mantras, each spending a
   skill point on two get_skill raises of that skill; then the cup of wonder's
   whereabouts (until it is found), the key of truth (once), a message, and the three
   group mantras, which spend a skill point on raises of up to three of the combat
   skills, two of the magic skills (mana favoured while below 8) or four of the others. */
/* name: descriptive, chosen for its bssorder key (5, the first of the EXE's stub order). */
void far mantra_advance(void)
{
    char mantra;
    char skill;
    char count;
    char text[50];
    register int tries;

    text[0] = 0;
    wdialog("Chant the mantra: ", 0, text, 1, 0xA);
    scroll_print("\n");
    for (mantra = STRN_MANTRAS; mantra < STRN_MANTRAS + NUM_MANTRAS; mantra++)
        if (str_cmp(get_string(mantra | STR_CHARGEN), strupr(text)) == 0)
            break;
    if (mantra == STRN_MANTRAS + NUM_MANTRAS)
        game_sprint(0x19);              /* "That is not a mantra." */
    else {
        mantra -= STRN_MANTRAS;
        if (mantra < NUM_SKILLS) {
            char r1, r2;

            if (player->skill_points == 0)
                game_sprint(0x18);      /* "You are not ready to advance." */
            else {
                r1 = get_skill(mantra);
                r2 = get_skill(mantra);
                if (r1 || r2) {
                    game_sprint(0x1A);
                    player->skill_points--;
                }
                great_advance(mantra, r1 || r2);
            }
        } else {
            int base;
            int size;
            unsigned char list[4];
            unsigned char found;

            switch (mantra - NUM_SKILLS) {
            case 0:
                if (!player->cup)
                    print_path_to(get_string(0x223), OBJ_HOMEX(ThePlayer),
                                  OBJ_HOMEY(ThePlayer), PlayerLevel, 0x18, 0x2D, 3, 4);
                seg041_35D7_E9(0x20);
                return;
            case 1:
                if (!player->key && place_new(0, ITEM_KEY_OF_TRUTH)) {
                    game_sprint(0x1E);  /* "None of your skills improved.": so the shipped strings say after the key appears */
                    player->key = 1;
                }
                seg041_35D7_E9(0x20);
                return;
            case 2:
                game_sprint(0x1F);  /* string 0x1F of block 1 is empty in the shipped STRINGS.PAK */
                seg041_35D7_E9(0x20);
                return;
            case 3: base = SKILL_ATTACK; size = 7; tries = 3; break;
            case 4: base = SKILL_MANA; size = 3; tries = 2; break;
            case 5: base = SKILL_TRAPS; size = 10; tries = 4; break;
            default:
                seg041_35D7_E9(0x20);
                return;
            }
            if (player->skill_points == 0)
                game_sprint(0x18);
            else {
                found = 0;
                memset(list, 0xFF, 4);
                for (count = size; tries != 0 && count--; tries--) {
                    if (base == SKILL_MANA && player->skills[SKILL_MANA] < 8 && (rand() & 2))
                        skill = SKILL_MANA;
                    else
                        skill = base + (int)(((int32)rand() * size) / 0x8000L);
                    if (get_skill(skill)) {
                        list[found] = skill;
                        found++;
                    }
                }
                report_advance(list);
                player->skill_points--;
            }
        }
        player_compute(0);
        ovr145_4FB();
    }
    FixPlayerEquips();
    seg041_35D7_E9(0x20);
    mouse_clearQ();
    flush_keys();
}

/* The end-of-game screen: the name, "A level N <class>", "Banished the Slasher of
   Veils", "after D days in the Abyss", the attributes, HP, mana and experience
   (exp / 10: exp is kept in tenths), and the twenty skills. */
void far game_stats(void)
{
    char far *str;
    char numbuf[10];
    int x;
    int days;
    int hours;
    int value;
    char text[50];
    register int i;
    register int y;

    grfx_load_font("fontchar.sys");
    *foreground_color = *background_color = 0x5C;
    str = get_string(player_name_handle);
    y = 0xB4;
    x = 0xA0 - string_width(str) / 2;
    string_to_screen(str, x, y);

    str = str_copy(text, get_string(0x2BB));    /* "A level " */
    i = strlen(text);
    if (player->level > 9)
        text[i++] = player->level / 10 + '0';
    text[i++] = player->level % 10 + '0';
    text[i++] = ' ';
    text[i++] = 0;
    str_cat(text, get_string((player->pclass + STRN_CLASSES) | STR_CHARGEN));
    y -= cur_font->height;
    x = 0xA0 - string_width(text) / 2;
    string_to_screen(text, x, y);

    str = get_string(0x2BC);            /* "Banished the Slasher of Veils" */
    y -= cur_font->height;
    x = 0xA0 - string_width(str) / 2;
    string_to_screen(str, x, y);

    hours = player->game_clock / 0x1C2000L;
    days = hours / 12;
    str_copy(text, get_string(0x2BD));  /* "after " */
    str_cat(text, itoa(days, numbuf, 10));
    str_cat(text, get_string(0x2BE));   /* " days in the Abyss" */
    x = 0xA0 - string_width(text) / 2;
    y -= cur_font->height;
    string_to_screen(text, x, y);
    y -= cur_font->height;

    for (i = 0; i < 6; i++)
    {
        x = i / 3 ? 0xBE : 0x50;
        value = i % 3 * cur_font->height;
        str = get_string((i + STRN_STATS) | STR_CHARGEN);
        switch (i)
        {
        case 0:
        case 1:
        case 2:
            itoa(playerdat->attr[i], text, 10);
            break;
        case 3:
            itoa(playerdat->avghit, text, 10);
            break;
        case 4:
            itoa(player->max_mana, text, 10);
            break;
        case 5:
            ltoa(player->exp / 10, text, 10);
            break;
        }
        string_to_screen(str, x, y - value);
        string_to_screen(text, x + 0x2D, y - value);
    }

    y -= cur_font->height * 2;
    for (i = 0; i < NUM_SKILLS; i++)
    {
        value = player->skills[i];
        str = get_string((i + STRN_SKILLS) | STR_CHARGEN);
        text[0] = value > 9 ? value / 10 + '0' : value + '0';
        text[1] = value > 9 ? value % 10 + '0' : 0;
        text[2] = 0;
        x = i % 3 * 0x4A + 0x32;
        if (i % 3 == 0)
            y -= cur_font->height;
        string_to_screen(str, x, y);
        string_to_screen(text, x + 0x46 - string_width(text), y);
    }
}

/* Garamon's dreams after a night's sleep: dream 0 until seen, then dream 1 once off
   level 1, then dreams 2 and 3 while their bits are set; with none due, a 1 in 4 + 4 *
   sleepfactor chance of one of dreams 4 to 9 not yet seen. Dream n is cutscene 0x18 + n,
   and its bit of the record's dream word is flipped once shown. None once Garamon is
   buried. Returns 1 if a cutscene played, otherwise waits a moment and returns 0. */
char far dream(int sleepfactor)
{
    int found = -1;
    uint32 timer;
    register int dreams;

    dreams = player->dreams;
    if ((dreams & 1) != 1)
        found = 0;
    else if (PlayerLevel > 1 && (dreams & 2) != 2)
        found = 1;
    else if ((dreams & 4) == 4)
        found = 2;
    else if ((dreams & 8) == 8)
        found = 3;
    if (found < 0)
    {
        if (rand() % (4 + (sleepfactor << 2)) == 0)
        {
            found = 4 + rand() % 6;
            if ((1 << found) & dreams)
                found = -1;
        }
    }
    if (found >= 0 && !player->garamon)
    {
        runcutscene(found + 0x18);
        player->dreams ^= 1 << found;
        return 1;
    }
    timer = GAME_TIME();
    while (timer + 0x180 > GAME_TIME())
        ;
    return 0;
}

/* Passing out drunk where the player stands: in motion states 1 and 2 (bits 0 and 1;
   probably swimming or similar) the player dies. With state bit 3 (probably falling)
   and no slow fall, levitate or fly (motionbits 0x16), the fall does 12 to 62 damage. */
void far drop_drunk_player(void)
{
    unsigned char damage;

    if (player->motion_state & (MS_SWIM | MS_LAVA))
        damage_item(ThePlayer, 0L, 0, 0, 0xFF, 0);
    FixPlayerEquips();
    finish_player();
    if ((player->motion_state & MS_FLOAT) && (motionbits & (MB_SLOW_FALL | MB_LEVITATE | MB_FLY)) == 0)
    {
        damage = rand() % 6 * 10 + 12;
        damage_item(ThePlayer, 0L, 0, 0, damage, DMG_POISON);
    }
    if (player->motion_state & (MS_SWIM | MS_LAVA))
        damage_item(ThePlayer, 0L, 0, 0, 0xFF, 0);
}

/* Sleeps. how: 0 camping, 1 a bedroll, a negative value passing out (drunk). A chosen
   sleep is refused while moving or swimming, on level 9 or with hostile creatures near.
   2 to 6 hours pass at once: active spells and mushrooms end, lights burn 180 steps an
   hour, and poison strength n does n(n + 1) / 2 damage and is cured; passing out also
   runs drop_drunk_player. wandering_monster_check may interrupt: fatigue drops by 0x20,
   hunger by 12 to 27, drunkenness by 16. Otherwise the rest of a 7 to 10 hour night
   passes, HP and mana come back, hunger drops by 24 to 55 and drunkenness by 32, and a
   chosen sleep may bring a dream. */
void far player_sleep(register int how)
{
    int comfort;
    int regen;
    char fade = 1;
    register int hours;

    if (how >= 0)
    {
        if ((player->motion_state & 0x1B) || PN.acc[2] != 0 || PlayerLevel == LEVEL_VOID)
        {
            game_sprint(0x14);          /* "You can't go to sleep here!" */
            return;
        }
        if (hostile_creatures_near())
        {
            game_sprint(0xE);           /* "There are hostile creatures near!" */
            return;
        }
        game_sprint(0xF);               /* "You make camp." */
    }
    render_FB();
    set_new_music(MUSIC_MAPS);
    change_music_maybe();
    fadeout3d(5);
    if (how >= 0)
        game_sprint(0x10);              /* "You go to sleep." */
    DoClosingDoors(0);
    Obj_GarbageCollect(1, 0x14);
    hours = rand() % 5 + 2;
    player->game_clock += hours * 0xE1000L;
    player->active_spells = 0;
    player->shrooms = 0;
    DegradeLights(hours * 180, 0);
    if (player->poison)
    {
        comfort = (player->poison + 1) * player->poison >> 1;
        damage_item(ThePlayer, 0L, 0, 0, comfort, DMG_POISON);
        player->poison = 0;
    }
    if (how < 0)
        drop_drunk_player();
    if (ThePlayer->hp == 0)
    {
        seg014_1DC5_15C5();
        return;
    }
    if (wandering_monster_check())
    {
        if (player->fatigue > 0x20)
            player->fatigue -= 0x20;
        else
            player->fatigue = 0;
        game_sprint(0x15);              /* "Your sleep is interrupted!" */
        player_eat(-12 - (rand() & 0xF));
        if (player->drunk < 0x10)
            player->drunk = 0;
        else
            player->drunk -= 0x10;
    }
    else
    {
        update_all_critters_whilst_player_snoozes();
        DoWanderingMonsters(0);
        hours = rand() % 4 + 7 - hours;
        if (ThePlayer->hp < 10)
            hours += rand() % 2 + 1;
        player->game_clock += hours * 0xE1000L;
        DegradeLights(hours * 180, 0);
        comfort = player->hunger > 0x40 && how > 0;
        regen = player->fatigue / 2 + 2;
        if (regen > 5)
            regen = 5;
        player->fatigue = 0;
        if (player->hunger != 0)
        {
            restore_hp(ThePlayer, regen + regen * comfort - 1);
            restore_mana(ThePlayer, -6);
            restore_mana(ThePlayer, regen + (regen + 1) * comfort - 1);
        }
        else
        {
            game_sprint(0x11);          /* "You are starving." */
            damage_item(ThePlayer, 0L, 0, 0, 2, 0);
        }
        player_eat(-24 - (rand() & 0x1F));
        if (player->drunk < 0x20)
            player->drunk = 0;
        else
            player->drunk -= 0x20;
        if (how >= 0)
            fade = !dream(comfort);
        game_sprint(0x13 - comfort);    /* "Your sleep is uneasy." or "You feel rested." */
    }
    FixPlayerEquips();
    NightCleanCritPages();
    PN.speed = 0;
    PN.acc[0] = PN.acc[1] = PN.acc[2] = 0;
    PN.vel[0] = PN.vel[1] = PN.vel[2] = 0;
    ovr145_4FB();
    render_FB();
    seg014_1DC5_15C5();
    if (fade)
        fadein3d(5);
    else
        send_FB();
}

/* The sleep key: sleeps in a bedroll if the player carries one (FindObj(4, 2, 1, 4)),
   else camps. */
void far player_key_sleep(int bedroll)
{
    int16 where;

    if (FindObj(4, 2, 1, 4, &where) == 0)
        bedroll = 0;
    else
        bedroll = 1;
    player_sleep(bedroll);
}

/* Changes hunger by nutrition (higher is fuller). Returns 0, eating nothing, if it
   would go past 255 (too full). Eating also heals food_heal / 8 HP (at most 8) and
   resets food_heal. PLAYTIME.C and player_sleep call it with negative values. */
unsigned char far player_eat(int nutrition)
{
    int value;

    value = player->hunger;
    value += nutrition;
    if (value > 0xFF)
        return 0;
    if (value < 0)
        player->hunger = 0;
    else
        player->hunger = value;
    if (nutrition > 0)
    {
        value = player->food_heal / 8;
        if (value > 8)
            value = 8;
        get_hp_back(ThePlayer, value);
        player->food_heal = 0;
    }
    return 1;
}

/* UW1, run every frame (UWEDIT.C's editor_dispatch): once the game is won
   (EndGameMode_dseg_1C8F) the ending cutscene, the pictures DATA\WIN1.BYT and WIN2.BYT
   with game_stats, and back to the start menu; once the last talisman is destroyed, a
   moongate appears at the centre of the level, the Slasher of Veils is dragged through
   it, and the player follows to level 9 (27, 23). */
/* name: descriptive, chosen for its bssorder key (819, between prime 792 and
   DetectedTrap 844). */
void far check_victory(void)
{
    struct Object far *gate;
    union Link far *head;

#ifndef __TURBOC__
    int shared_scene = EndGameMode_dseg_1C8F || player->talismans == 0;
    if (port_shared_guest()) return;
    if (shared_scene) port_shared_story(1);
#endif

    if (EndGameMode_dseg_1C8F)
    {
        inplist->mode = 0;
        LeftPanel = 2;
        runcutscene(1);
        mouse_hide();
        grfx_clear();
        LoadBitMap_ovr141_0(7, "DATA\\win1.byt");
#ifndef __TURBOC__
        if (getenv("UW_COOP_TEST_CAMPAIGN")) fprintf(stderr, "SHARED ENDING win1 loaded\n");
#endif
        while (mouse_get_input() < 0)
            ;
        grSoftPageFlip();
        LoadBitMap_ovr141_0(-1, "DATA\\win2.byt");
        game_stats();
        grSoftPageFlip();
        copy_visible_to_hidden();
#ifndef __TURBOC__
        if (getenv("UW_COOP_TEST_CAMPAIGN")) fprintf(stderr, "SHARED ENDING win2 with character stats loaded\n");
#endif
        while (mouse_get_input() < 0)
            ;
        real_death(0);
        EndGameMode_dseg_1C8F = 0;
    }
    else if (player->talismans == 0)
    {
        gate = 0;
        if ((gate = CreateObj(ITEM_MOONGATE, 0)) != 0)
        {
            SET_ISQUANT(gate, 1);
            gate->ol.f.link = 0x2C0;
            head = &Map_GetAddr(0x20, 0x20)->objects;
            Obj_AddEnd(head, gate);
        }
        game_sprint(0x117);             /* "The Slasher of Veils is dragged from this world." */
        Vortex_ovr143_E09(-1);
        fadeout3d(5);
        if (gate)
        {
            Obj_Rem(head, gate);
            Obj_Free(gate);
        }
        do_teleport(ThePlayer, 0x1B, 0x17, 9);
        player->talismans = 0xFF;
        game_sprint(0x118);             /* "You are sucked through the moongate . . ." */
        NewPlyFade &= 0xFE;
        new_player_pos();
        NewPlyFade |= 1;
    }
#ifndef __TURBOC__
    if (shared_scene) port_shared_story(0);
#endif
}

/* UW1: plants the silver seed in the square ahead of the player. Not on level 9 (-1).
   It takes root (1) only in an open square whose floor texture is 5 to 11, 18 to 22,
   27 to 31 or 35 to 40, with room for the tree; the tree's level is recorded for
   player_is_dead. */
/* name: descriptive, chosen for its bssorder key (536, between player_eat 504 and
   player_sleep 664). */
char far plant_seed(void)
{
    int16 x, y;
    struct Object far *tree;
    struct Tile far *tile;
    register int terrain;
    register int z;

    if (PlayerLevel == LEVEL_VOID)
        return -1;
    x = PN.x >> 5;
    y = PN.y >> 5;
    move_along(PlayerFacing >> 8, 0xB, &x, &y);
    tile = Map_GetAddr(x >> 3, y >> 3);
    if (tile->type != TILE_OPEN)
        return 0;
    terrain = floor_IDs[tile->floor];
    if ((terrain < 5 || terrain > 11) && (terrain < 18 || terrain > 22)
        && (terrain < 27 || terrain > 31) && (terrain < 35 || terrain > 40))
        return 0;
    z = tile->height << 3;
    if (can_place(ITEM_SILVER_TREE, 0, x, y, z, 0, 0))
    {
        tree = CreateObj(ITEM_SILVER_TREE, 0);
        SET_Z(tree, z);
        SET_FINEX_UNSIGNED(tree, x & 7);
        SET_FINEY(tree, y);
        SET_DOORDIR(tree, 1);
        if (add_animobj(Obj_MemTPtr(tree), -1, 0, x >> 3, y >> 3))
        {
            player->tree = PlayerLevel;
            Obj_Add(&tile->objects, tree);
            return 1;
        }
        Obj_Free(tree);
    }
    return 0;
}

/* Sets the pending teleport to the square of the first item of the given id on the
   current level; returns 0 if level is not the current one. */
char far moveto(int level, register int item)
{
    int16 x = 0;
    int16 y = 0;

    npp_func = 0;
    if (level == PlayerLevel)
    {
        Obj_FindInMap(item >> 6, (item & ID_MINOR) >> 4, item & ID_INCLASS, &x, &y);
        dprintf("At %d %d\n", x, y);
        NewPlayerX = x;
        NewPlayerY = y;
        return 1;
    }
    return 0;
}

/* UW1, npp_func after death with a silver tree planted: arrive at the tree, with HP 2 to
   4 below the maximum and mana down by an eighth plus 2, poison and spells gone. */
/* name: descriptive, chosen for its bssorder key (380, between advance 313 and
   drop_drunk_player 460, where UW2 has do_dreamret). */
void far do_resurrect(void)
{
    if (moveto(player->tree, ITEM_SILVER_TREE)) {
        if (playerdat->avghit > 8)
            ThePlayer->hp = playerdat->avghit - 2 - rand() * 3L / 0x8000L;
        else
            ThePlayer->hp = playerdat->avghit;
        player->play_mana = player->max_mana;
        if (player->max_mana > 8)
            player->play_mana -= player->max_mana / 8 + 2;
        SET_SEQ(ThePlayer, 0x2C);
        player->poison = 0;
        player->active_spells = 0;
        FixPlayerEquips();
        set_new_music(4);
    }
}

/* npp_func of the moonstone spells: arrive at the moonstone (item 0x126) on its level. */
void far do_mstone(void)
{
    moveto(player->moonstone, ITEM_MOONSTONE);
}

/* HP has reached 0. While talismans remain to be destroyed: the effects stop, death
   costs an eighth of the experience, anything held on the cursor is dropped, and a
   bones object (0xC2 to 0xC6) is left where the player fell. With a silver tree planted
   (and not on level 9) the player comes back at the tree (do_resurrect); otherwise it
   is the end (real_death(1)). Once the last talisman is gone the player cannot die and
   keeps 4 HP. */
void far player_is_dead(void)
{
    struct Object far *bones;
    char ok;

#ifndef __TURBOC__
    if (port_shared_death()) return;
#endif

    if (player->talismans == 0)
    {
        ThePlayer->hp = 4;
        return;
    }
    seg014_1DC5_C7C();
    load_new_music(MUSIC_DEATH, 1);
    player_get_exp(-(int)(player->exp >> 3));
    render_FB();
    fadeout3d(5);
    clear_fight_state();
    if (CursorObjPtr != 0)
    {
        if (GameInputMode == 1 || GameInputMode == 0)
        {
            near_mob_put_at(ThePlayer, CursorObjPtr, 6, 0);
            CursorObjPtr = 0;
            GameInputMode = 0;
            unforce_mouse_cursor(3);
        }
        else if (GameInputMode == 2)
        {
            CursorObjPtr = 0;
            GameInputMode = 0;
            unforce_mouse_cursor(3);
        }
    }
    bones = CreateObj(rand() % 5 + ITEM_SKULL_C2, 0);
    if (put_at(PN.x >> 5, PN.y >> 5, PN.z >> 3, bones, 0, 1))
    {
        SET_Z(bones, OBJ_Z(ThePlayer));
        bones->ol.f.owner = 0x3F;
        SET_FINEX(bones, OBJ_FINEX(ThePlayer));
        SET_FINEY(bones, OBJ_FINEY(ThePlayer));
        obj_deal(bones, PN.x >> 8, PN.y >> 8, 1);
    }
    if (player->tree && PlayerLevel != LEVEL_VOID)
    {
        do_teleport(ThePlayer, 0x3F, 0x3F, player->tree);
        npp_func = do_resurrect;
        NewPlyFade = 0;
        ok = new_player_pos();
        NewPlyFade = 3;
        if (ok)
        {
            runcutscene(0x102);
            cFillFB(0xF1);
            scroll_clear(1);
        }
        else
            real_death(1);
    }
    else
        real_death(1);
}

/* Tests for a trap on obj: a trap object linked from it, or the trap a trigger points
   at, of type 0 to 2. Returns the skill_check of skill against 8, or 0 when there is no
   trap. */
int far DetectedTrap(struct Object far *obj, int skill)
{
    union Link far *head;
    struct Object far *trap;

    if (OBJ_ISQUANT(obj) || OBJ_LINK(obj) == 0)
        return 0;
    head = &obj->ol.link;
    trap = Obj_InList(&head, 0, MAJOR_TRAP, -1, -1);
    if (trap != 0)
    {
        if (OBJ_MINOR(trap) >= MINOR_TRIGGER)
            trap = Obj_PtrTMem(&trap->ol.link);
        if (OBJ_INMAJOR(trap) <= 2)
            return skill_check(skill, 8);
    }
    return 0;
}

/* Tries to disarm the trap on obj: skill_check of skill against 8. Success removes the
   trap; failure (-1) sets it off; 0 is "Unable to defuse trap." */
int far RemoveTrap(struct Object far *obj, int skill)
{
    union Link far *head;
    struct Object far *trap;
    struct Object far *trig;
    char name[20];
    register int result = 0;

    if (OBJ_ISQUANT(obj) || OBJ_LINK(obj) == 0)
        return 0;
    head = &obj->ol.link;
    trig = Obj_InList(&head, 0, MAJOR_TRAP, -1, -1);
    if (trig != 0)
    {
        if (OBJ_MINOR(trig) >= MINOR_TRIGGER)
            trap = Obj_PtrTMem(&trig->ol.link);
        else
        {
            trap = trig;
            trig = 0;
        }
        if (OBJ_INMAJOR(trap) <= 2)
        {
            result = skill_check(skill, 8);
            if (result > 0)
            {
                if (!get_name(name, trap, 0, 0))
                    strcpy(name, "UNNAMED");
                scroll_print("The ");
                scroll_print(name);
                scroll_print(" on the ");
                if (!get_name(name, obj, 0, 0))
                    strcpy(name, "UNNAMED");
                scroll_print(name);
                scroll_print(" was successfully dearmed.\n");
                Obj_FreeChain(head);
            }
            else if (result < 0)
            {
                scroll_print("Your bumbling attempts have set off the ");
                if (!get_name(name, trap, 0, 0))
                    strcpy(name, "UNNAMED");
                scroll_print(name);
                scroll_print(".\n");
                if (trig)
                    UseTrigger(ThePlayer, obj, trig, -1);
                else
                {
                    SetOffTrap(ThePlayer, obj, trap, MapObj_X, MapObj_Y);
                    delete_trap(head, trap);
                }
            }
            else
                scroll_print("Unable to defuse trap.\n");
        }
    }
    return result;
}
