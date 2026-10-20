/* target: ovr106 */
/* opts: -mm -1 -G -O -Y -d */
/* Handing variables to a conversation and taking them back. Converse (CONVERSE.C) calls
   setup_converse_data after loading the script and before running it, which copies the
   NPC's and the player's state into the script's imported variables (bab_var, BABL.C);
   after babl_run it calls update_converse_data, which copies back the ones a
   conversation may change (bab_var_out). Only variables the script imports are touched:
   bab_var and bab_var_out look the name up in the script's import table and do nothing
   when it is missing. The file owns no data. The whole of DOS overlay ovr106, in
   original order.

   What goes in: npc_whoami, npc_hunger (0x10 fed, 0xC0 not), npc_health (hp * 256 /
   the class's average hit points, 0x80 when that is 0), npc_hp, npc_arms (the class's
   first attack chance), npc_power (attribute 0 plus the caster value), npc_goal,
   npc_gtarg, npc_talkedto, npc_level (the class's), npc_xhome and npc_yhome (the NPC's
   quality and owner fields, which hold its home tile), npc_name (a string id: block 7
   string whoami + 16, or the item's name for whoami 0), npc_attitude (0 when it is
   attacking the player, goal 5 with target 1; 6 for an ally; else its attitude 0..3),
   then play_hunger, play_health, play_hp, play_arms (attack skill + strength),
   play_power (dexterity + mana + missile skill), play_mana, play_level, dungeon_level,
   game_time (game_clock / 0x3BC4), game_mins (that modulo 1440), game_days (game_clock
   / 0x1502E80, which is 0x3BC4 * 1440), new_player_exp (0), play_sex, play_poison,
   play_drawn and play_name (the player's name string id).

   What comes out: npc_hunger (fed when below 0x20), npc_hp, npc_xhome, npc_yhome,
   npc_goal with npc_gtarg (through change_critter_goal), npc_attitude (above 3 means
   attitude 3 and an ally), the talked-to bit set, play_hunger, play_hp, play_mana,
   play_poison, and new_player_exp, which when non-zero is given to player_get_exp.

   Name: descriptive (map/filenames.tsv: conversation variables in and out). */
/* name: Function and global names are the originals from the FM Towns symbol table; the
   source file's own name is not known. */

#include "conv.h"
#include "critter.h"
#include "object.h"
#include "player.h"
#include "ui.h"

void far setup_converse_data(struct Object far *npc)
{
    int16 val;
    struct Creature near *crit;
    int who;

    crit = &Creature[OBJ_INMAJOR(npc)];
    val = npc->whoami;
    bab_var("npc_whoami", &val, 1);
    val = OBJ_FED(npc) ? 0x10 : 0xC0;
    bab_var("npc_hunger", &val, 1);
    /* a 16-bit int (shl ax,8, then cwd before the idiv): an NPC with 128 hit points or
       more gets a health below 0, where the host's int kept 32 bits */
    if (crit->avghit != 0)
        val = (int16)(npc->hp << 8) / crit->avghit;
    else
        val = 0x80;
    bab_var("npc_health", &val, 1);
    val = npc->hp;
    bab_var("npc_hp", &val, 1);
    val = crit->attacks[0].chance;
    bab_var("npc_arms", &val, 1);
    val = crit->attr[0] + crit->caster;
    bab_var("npc_power", &val, 1);
    val = OBJ_GOAL(npc);
    bab_var("npc_goal", &val, 1);
    val = OBJ_GTARG(npc);
    bab_var("npc_gtarg", &val, 1);
    val = OBJ_TALKEDTO(npc);
    bab_var("npc_talkedto", &val, 1);
    val = crit->level;
    bab_var("npc_level", &val, 1);
    val = OBJ_QUALITY(npc);
    bab_var("npc_xhome", &val, 1);
    val = OBJ_OWNER(npc);
    bab_var("npc_yhome", &val, 1);
    who = npc->whoami;
    val = who ? (who + 0x10) | STR_CONV : OBJ_ITEM(npc) | STR_OBJNAMES;
    bab_var("npc_name", &val, 1);
    if (OBJ_GOAL(npc) == 5 && OBJ_GTARG(npc) == 1)
        val = 0;
    else if (OBJ_ALLY(npc))
        val = 6;
    else
        val = OBJ_ATTITUDE(npc);
    bab_var("npc_attitude", &val, 1);

    crit = &Creature[OBJ_INMAJOR(ThePlayer)];
    val = player->hunger;
    bab_var("play_hunger", &val, 1);
    if (crit->avghit != 0)
        val = (int16)(ThePlayer->hp << 8) / crit->avghit;   /* 16-bit, as npc_health */
    else
        val = 0x80;
    bab_var("play_health", &val, 1);
    val = ThePlayer->hp;
    bab_var("play_hp", &val, 1);
    val = player->skills[SKILL_ATTACK] + player->strength;
    bab_var("play_arms", &val, 1);
    val = player->dexterity + player->play_mana + player->skills[SKILL_MISSILE];
    bab_var("play_power", &val, 1);
    val = player->play_mana;
    bab_var("play_mana", &val, 1);
    val = player->level;
    bab_var("play_level", &val, 1);
    val = PlayerLevel;
    bab_var("dungeon_level", &val, 1);
    val = player->game_clock / 0x3BC4L;
    bab_var("game_time", &val, 1);
    val = player->game_clock / 0x3BC4L % 0x5A0L;
    bab_var("game_mins", &val, 1);
    val = player->game_clock / 0x1502E80L;
    bab_var("game_days", &val, 1);
    val = 0;
    bab_var("new_player_exp", &val, 1);
    val = player->female;
    bab_var("play_sex", &val, 1);
    val = player->poison;
    bab_var("play_poison", &val, 1);
    val = player->drawn;
    bab_var("play_drawn", &val, 1);
    val = player_name_handle;
    bab_var("play_name", &val, 1);
}

/* Copy back what a conversation may change. Returns 1 when the script left npc_attitude
   at 0 (hostile); Converse then skips its closing pause. The local is called killed,
   though nothing here kills anything. */
char far update_converse_data(struct Object far *npc)
{
    int16 val;
    int16 gtarg;
    char killed;

    killed = 0;
    bab_var_out("npc_hunger", &val, 1);
    SET_FED(npc, val < 0x20 ? 1 : 0);
    bab_var_out("npc_hp", &val, 1);
    npc->hp = val;
    bab_var_out("npc_xhome", &val, 1);
    npc->qn.f.quality = val;
    bab_var_out("npc_yhome", &val, 1);
    npc->ol.f.owner = val;
    bab_var_out("npc_goal", &val, 1);
    bab_var_out("npc_gtarg", &gtarg, 1);
    change_critter_goal(npc, val, gtarg);
    SET_TALKEDTO(npc, 1);
    bab_var_out("npc_attitude", &val, 1);
    if (val > 3) {
        SET_ATTITUDE(npc, 3);
        SET_ALLY(npc, 1);
    } else
        SET_ATTITUDE(npc, val);
    if (val == 0)
        killed = 1;
    SET_TALKEDTO(npc, 1);
    bab_var_out("play_hunger", &val, 1);
    player->hunger = val;
    bab_var_out("play_hp", &val, 1);
    ThePlayer->hp = val;
    bab_var_out("play_mana", &val, 1);
    player->play_mana = val;
    bab_var_out("play_poison", &val, 1);
    player->poison = val;
    bab_var_out("new_player_exp", &val, 1);
    if (val != 0)
        player_get_exp(val);
    return killed;
}
