/* UW1-only character contexts. PlayerDat stays at its original address because
   inventory routines access that storage directly, rather than through player. */
extern unsigned char UsingPole,DurCount,def_mode;
extern int16 PickDist,RightButtonThing;
extern union Link *releasePtr;
extern struct Object *newPlObj;
extern int32 lastDurCheck;
extern int32 nextSpellTime;
extern int16 BagSaveHandles[8];
extern char invArmorObj[6];
void port_shared_combat_state(int which,int save);
void port_shared_panel_state(int which,int save);
void port_shared_movement_state(int which,int save);
int port_shared_inventory_pack(void *buffer,int capacity);
int port_shared_inventory_unpack(const void *buffer,int length,int carrying);
int port_shared_inventory_valid(const void *buffer,int length,int carrying);

struct character_state {
    struct Player record;
    struct Creature creature;
    union Link inventory[NUM_INV_SLOTS];
    struct Object *cursor,*picked;
    struct Object *acting;
    ActorFn actor;
    int16 actor_arg,map_x,map_y;
    struct Tile *pick_map;
    struct Bag *bags,*bag;
    union Link *release;
    int16 handles[NUM_DISPLAY],baghandles[8],inputmode,button,pickdist;
    unsigned char slots[NUM_INV_SLOTS+21],regen[2],notice[2],effects[9];
    unsigned char missile_mana;
    signed char protection[4];
    int32 duration_time;
    int32 cast_time;
    uint32 rune_time;
};
static struct character_state characters[2];
static int character_ready,character_creating;
#define CHARACTER_IMAGE_CAP (0x63+NUM_STATIC*8)
static unsigned char character_image[CHARACTER_IMAGE_CAP];
static int character_image_length,character_image_carrying;
static int character_level,character_x,character_y,character_z,character_facing;

/* These are campaign facts, including flags which can be cleared or toggled.
   Copy the latest values; OR-ing records would lose reset/toggle semantics. */
static void campaign_copy(struct Player *to,const struct Player *from)
{
    if(!to->orb && from->orb && (PlayerLevel==LEVEL_TYBAL || to->saved_level==LEVEL_TYBAL)) {
        /* Destroying the shared orb releases each character's own reserve. */
        to->max_mana=to->saved_mana;to->play_mana=to->saved_mana;
    }
    to->moonstone=from->moonstone;to->tree=from->tree;
    to->armageddon=from->armageddon;to->orb=from->orb;to->key=from->key;to->cup=from->cup;
    to->incense=from->incense;to->talisman_ok=from->talisman_ok;to->garamon=from->garamon;
    to->quests=from->quests;memcpy(to->quest_bytes,from->quest_bytes,sizeof to->quest_bytes);
    to->talismans=from->talismans;to->dreams=from->dreams;
    memcpy(to->game_vars,from->game_vars,sizeof to->game_vars);
    to->game_clock=from->game_clock;
}

static void character_state(int which,int save)
{
    struct character_state *c=&characters[which];
#define COPY(member,global) do{if(save)memcpy(c->member,global,sizeof c->member);else memcpy(global,c->member,sizeof c->member);}while(0)
#define VALUE(member,global) do{if(save)c->member=global;else global=c->member;}while(0)
    VALUE(record,*player);VALUE(creature,*playerdat);
    COPY(inventory,Inventory);COPY(slots,SlotToDisplay);COPY(handles,SaveHandles);COPY(baghandles,BagSaveHandles);
    COPY(regen,plyregen);COPY(notice,plyNotice);COPY(protection,cmbModTH);
    VALUE(cursor,CursorObjPtr);VALUE(picked,newPlObj);VALUE(release,releasePtr);
    VALUE(acting,ObjectActing);VALUE(actor,ObjectActor);VALUE(actor_arg,ObjectActorArg);
    VALUE(map_x,MapObj_X);VALUE(map_y,MapObj_Y);VALUE(pick_map,PickMap);
    VALUE(missile_mana,mspell_mused);
    VALUE(bags,OpenBagList);VALUE(bag,OpenBag);VALUE(inputmode,GameInputMode);VALUE(button,RightButtonThing);
    VALUE(pickdist,PickDist);VALUE(duration_time,lastDurCheck);VALUE(cast_time,nextSpellTime);VALUE(rune_time,lstime);
#define EFFECT(i,g) do{if(save)c->effects[i]=(unsigned char)(g);else (g)=c->effects[i];}while(0)
    EFFECT(0,Hasted);EFFECT(1,WizEye);EFFECT(2,Blessed);EFFECT(3,TimeStop);EFFECT(4,UsingPole);
    EFFECT(5,DragonSkinBoots_dseg_5c99_1B01);EFFECT(6,DurCount);EFFECT(7,def_mode);
    EFFECT(8,ComObjData[ITEM_ADVENTURER].resist);
#undef EFFECT
#undef VALUE
#undef COPY
    port_shared_combat_state(which,save);port_shared_panel_state(which,save);
    port_shared_movement_state(which,save);
}

static void character_art(void)
{
    /* The native paperdoll and weapon colour map caches are global. */
    static int body=-1;
    int next=player->female*5+player->body;
    if(next!=body){load_inventory_pix();load_weapcm();body=next;}
    memset(invArmorObj,0,6);
    inv_refresh=1;
    if(!character_creating && scrnum==0 && RightPanel!=PANEL_TURNING)pretty_panelagain();
}

static void character_prepare(void)
{
    state(&seats[0],1);
    if(!character_ready) {
        characters[1]=characters[0];
        memset(&characters[1].record,0,sizeof characters[1].record);
        memset(characters[1].inventory,0,sizeof characters[1].inventory);
        memset(characters[1].baghandles,0,sizeof characters[1].baghandles);
        characters[1].cursor=characters[1].picked=NULL;characters[1].release=NULL;
        characters[1].acting=NULL;characters[1].actor=NULL;characters[1].actor_arg=0;characters[1].pick_map=NULL;
        characters[1].bags=characters[1].bag=NULL;characters[1].inputmode=characters[1].button=0;
        memset(characters[1].regen,0,sizeof characters[1].regen);
        campaign_copy(&characters[1].record,&characters[0].record);
    }
}

static void character_join(void)
{
    int level=PlayerLevel;
    if(!character_ready) {
        character_creating=1;
        atomic_store(&paired,1);
        fprintf(stderr,"uw1port: player two is creating an independent character\n");
        if(port_shared_test_start() && !getenv("UW_COOP_TEST_CHARGEN")){init_char(0);strcpy(player->name,"Player Two");}
        else if(!create_player()){init_char(0);strcpy(player->name,"Player Two");}
        character_creating=0;
        PlayerLevel=level;
        campaign_copy(player,&characters[0].record);
        character_ready=1;companion_health=-1;
        port_shared_level_hacks(PlayerLevel,0);
        ThePlayer->hp=playerdat->avghit;
        load_inventory_pix();FixPlayerEquips();
        /* Restore the adventure frame after chargen's fullscreen pictures. */
        if(!port_shared_test_start())display_scr();
        fprintf(stderr,"uw1port: independent character created: %s\n",player->name);
    } else if(character_image_length) {
        if(!port_shared_inventory_unpack(character_image,character_image_length,character_image_carrying))
            port_fatal("The co-op inventory cannot fit in this level's object pool");
        ThePlayer->whoami=0xFD;ThePlayer->qn.f.next=0;
        character_image_length=0;
        if(character_level==PlayerLevel){PN.x=character_x;PN.y=character_y;PN.z=character_z;PlayerFacing=character_facing;}
        FixPlayerEquips();
        fprintf(stderr,"uw1port: independent character restored: %s\n",player->name);
    }
}

static void character_detach(void)
{
    int old=atomic_load(&active_seat);
    if(old!=1)seat(1);
    /* Cancel pending blows/aiming before object pointers are relocated. */
    clear_fight_state();
    if(GameInputMode!=1){GameInputMode=0;CursorObjPtr=NULL;}
    ObjectActing=NULL;ObjectActor=NULL;ObjectActorArg=0;newPlObj=NULL;releasePtr=NULL;
    character_image_carrying=GameInputMode==1 && CursorObjPtr!=NULL;
    character_image_length=port_shared_inventory_pack(character_image,sizeof character_image);
    if(!character_image_length)port_fatal("Cannot preserve the co-op character inventory");
    character_level=PlayerLevel;character_x=PN.x;character_y=PN.y;character_z=PN.z;character_facing=PlayerFacing;
    player->saved_x=PN.x;player->saved_y=PN.y;player->saved_z=PN.z;player->saved_facing=PlayerFacing;player->saved_level=PlayerLevel;
    player->health=ThePlayer->hp;player->maxhealth=playerdat->avghit;
    player->strength=playerdat->attr[0];player->dexterity=playerdat->attr[1];player->intelligence=playerdat->attr[2];
    FreePlayerInv(&ThePlayer->ol.link);
    if(character_image_carrying){union Link link;link.word=0;link.f.index=Obj_MemTPtr(CursorObjPtr);Obj_FreeChain(&link);}
    CursorObjPtr=NULL;ClearInventory();GameInputMode=0;
    state(&seats[1],1);
    seat(0);
}

static uint32_t character_hash(const unsigned char *data,size_t size)
{
    uint32_t hash=2166136261u;while(size--)hash=(hash^*data++)*16777619u;return hash;
}
static void character_u32(unsigned char *p,uint32_t value)
{p[0]=(unsigned char)(value>>24);p[1]=(unsigned char)(value>>16);p[2]=(unsigned char)(value>>8);p[3]=(unsigned char)value;}
static uint32_t character_get32(const unsigned char *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

static int character_write(void)
{
    unsigned char data[32+sizeof(struct Player)+sizeof(struct Creature)+CHARACTER_IMAGE_CAP];
    size_t size=32+sizeof(struct Player)+sizeof(struct Creature)+(size_t)character_image_length;
    FILE *fp;int ok;
    if(!character_image_length)return 0;
    memset(data,0,32);memcpy(data,"UWC1",4);character_u32(data+4,1);
    character_u32(data+8,sizeof(struct Player));character_u32(data+12,sizeof(struct Creature));
    character_u32(data+16,(uint32_t)character_image_length);data[24]=(unsigned char)character_level;
    data[25]=(unsigned char)character_image_carrying;
    memcpy(data+32,&characters[1].record,sizeof(struct Player));
    memcpy(data+32+sizeof(struct Player),&characters[1].creature,sizeof(struct Creature));
    memcpy(data+32+sizeof(struct Player)+sizeof(struct Creature),character_image,(size_t)character_image_length);
    character_u32(data+20,character_hash(data+24,size-24));
    fp=fopen("SAVE0\\coop2.tmp","wb");if(!fp)return 0;
    ok=fwrite(data,1,size,fp)==size;if(fclose(fp))ok=0;
    if(ok) {
        int had_previous=access("SAVE0\\coop2.dat",0)==0;
        /* The virtual DOS filesystem refuses rename-over-existing. Keep a
           recoverable previous image until the replacement is in place. */
        unlink("SAVE0\\coop2.bak");
        if(had_previous && rename("SAVE0\\coop2.dat","SAVE0\\coop2.bak"))return 0;
        ok=rename("SAVE0\\coop2.tmp","SAVE0\\coop2.dat")==0;
        if(ok)unlink("SAVE0\\coop2.bak");
        else if(had_previous)rename("SAVE0\\coop2.bak","SAVE0\\coop2.dat");
    }
    return ok;
}

void port_shared_campaign_reset(void)
{
    if(!enabled)return;
    port_shared_reset();character_ready=character_image_length=0;resume_guest=0;
    memset(characters,0,sizeof characters);
    port_shared_combat_state(-1,0);port_shared_panel_state(-1,0);
    port_shared_movement_state(-1,0);
    modal_owner=-1;
}

void port_shared_campaign_load(void)
{
    unsigned char data[32+sizeof(struct Player)+sizeof(struct Creature)+CHARACTER_IMAGE_CAP];
    struct Player rec;size_t size,wanted;unsigned image;int phase=1;
    FILE *fp;
    if(!enabled)return;
    fp=fopen("SAVE0\\coop2.dat","rb");
    if(!fp)fp=fopen("SAVE0\\coop2.bak","rb");
    if(!fp)return; /* Solo save: create a new guest. */
    /* Exhume's DOS fread uses the file descriptor and does not set stdio's
       feof flag. Ask its matching fgetc for an extra byte to detect oversize. */
    size=fread(data,1,sizeof data,fp);if(fgetc(fp)!=EOF){fclose(fp);goto corrupt;}fclose(fp);
    if(size<32 || memcmp(data,"UWC1",4) || character_get32(data+4)!=1 ||
       character_get32(data+8)!=sizeof(struct Player) || character_get32(data+12)!=sizeof(struct Creature))goto corrupt;
    phase=2;image=character_get32(data+16);wanted=32+sizeof(struct Player)+sizeof(struct Creature)+(size_t)image;
    if(image>CHARACTER_IMAGE_CAP || wanted!=size || data[24]<1 || data[24]>9 || data[25]>1 ||
       data[26] || data[27] || data[28] || data[29] || data[30] || data[31] ||
       character_get32(data+20)!=character_hash(data+24,size-24))goto corrupt;
    memcpy(&rec,data+32,sizeof rec);
    phase=3;
    if(!memchr(rec.name,0,sizeof rec.name) || rec.active_spells>3 || rec.body>4 || rec.pclass>7 || rec.level<1 || rec.level>16)goto corrupt;
    phase=4;if(!port_shared_inventory_valid(data+32+sizeof rec+sizeof(struct Creature),(int)image,data[25]))goto corrupt;
    state(&seats[0],1);characters[1]=characters[0];characters[1].record=rec;
    memcpy(&characters[1].creature,data+32+sizeof rec,sizeof(struct Creature));
    memset(characters[1].inventory,0,sizeof characters[1].inventory);
    characters[1].cursor=NULL;characters[1].bags=characters[1].bag=NULL;
    characters[1].inputmode=0;memset(characters[1].baghandles,0,sizeof characters[1].baghandles);
    memcpy(character_image,data+32+sizeof rec+sizeof(struct Creature),image);
    character_image_length=(int)image;character_image_carrying=data[25];character_level=data[24];
    character_x=rec.saved_x;character_y=rec.saved_y;character_z=rec.saved_z;character_facing=rec.saved_facing;
    character_ready=1;fprintf(stderr,"uw1port: co-op campaign character loaded: %s\n",rec.name);return;
corrupt:
    fprintf(stderr,"uw1port: co-op character validation phase %d (%lu bytes)\n",phase,(unsigned long)size);
    port_fatal("The co-op character save is damaged or incompatible; load a different campaign save");
}

int port_shared_player_index(int index)
{return index==1 || (enabled && partner && index==Obj_MemTPtr(partner));}
int port_shared_context(int index)
{
    int old=atomic_load(&active_seat),next;
    if(!enabled || !partner || character_creating || !port_shared_player_index(index))return -1;
    next=index==1 ? 0 : 1;if(next!=old)seat(next);return old;
}
void port_shared_context_end(int previous) {if(previous>=0)seat(previous);}
int port_shared_near_players(int x,int y,int distance)
{
    if(!enabled || !partner)return 0;
    return abs(x-(int)OBJ_HOMEX(partner))+abs(y-(int)OBJ_HOMEY(partner))<=distance ||
        abs(x-(int)OBJ_HOMEX(critdata+1))+abs(y-(int)OBJ_HOMEY(critdata+1))<=distance;
}
void port_shared_level_hacks(int level,int mode)
{
    struct Player *p;
    if(!enabled || !character_ready || level!=LEVEL_TYBAL)return;
    p=atomic_load(&active_seat)==1 ? player : &characters[1].record;
    if(!player->orb) {
        if(mode==0){p->saved_mana=p->max_mana;p->max_mana=p->play_mana=0;}
        else if(mode==1){p->max_mana=p->saved_mana;p->play_mana=p->saved_mana>>2;}
    }
}
int port_shared_world_time_stop(void)
{
    if(!enabled || !character_ready)return TimeStop;
    return TimeStop || characters[1-atomic_load(&active_seat)].effects[3];
}
void port_shared_killed(int index,struct Object *npc)
{int old=port_shared_context(index);player_killed_a(npc);port_shared_context_end(old);}
void port_shared_terrain(struct Object *avatar,int height,int rising)
{
    int old=port_shared_context(Obj_MemTPtr(avatar));
    if(rising)PN.z=height<<6;
    else parse_player_terr(0x10,1);
    port_shared_context_end(old);
}
char port_shared_death_check(struct Object *npc,char mode)
{
    int old=port_shared_context(npc->last_hit);
    char result=death_check(npc,mode);
    port_shared_context_end(old);return result;
}

int port_shared_death(void)
{
    struct Object *other;
    int16 x,y;
    if(!enabled || !partner)return 0;
    other=atomic_load(&active_seat) ? critdata+1 : partner;
    if(!other->hp)return 0;
    /* A live companion keeps the adventure going; no items are created or copied. */
    player_get_exp(-(int)(player->exp>>3));player->poison=0;
    ThePlayer->hp=playerdat->avghit/4+1;
    if(find_good_x_and_y(ThePlayer,OBJ_HOMEX(other),OBJ_HOMEY(other),&x,&y,1))player_setup(x,y,1);
    scroll_print("Your companion brings you back to your feet.\n");editchng(0x7FFE);return 1;
}

static struct {int pending;int16 x,y,level;char fade;void (*fn)(void);} party_travel;
int port_shared_guest_travel(void)
{
    if(!port_shared_guest() || NewPlayerX<=0 || NewPlayerLevel==PlayerLevel)return 0;
    party_travel.pending=1;party_travel.x=NewPlayerX;party_travel.y=NewPlayerY;party_travel.level=NewPlayerLevel;
    party_travel.fade=NewPlyFade;party_travel.fn=npp_func;NewPlayerX=0;return 1;
}
static void character_travel(void)
{
    if(!party_travel.pending)return;
    NewPlayerX=party_travel.x;NewPlayerY=party_travel.y;NewPlayerLevel=party_travel.level;
    NewPlyFade=party_travel.fade;npp_func=party_travel.fn;party_travel.pending=0;editchng(8);
}

/* Opt-in integration fixture. It calls the native inventory, equipment, upkeep,
   reward, save and restore routines, then checks what each seat actually reads. */
static void character_test_require(int ok,const char *what)
{if(!ok)port_fatal("shared character test: %s",what);}
static void character_test_inventory(void)
{
    struct Object *bag,*inside,*cursor;
    character_test_require(!strcmp(player->name,"Player Two"),"guest name lost");
    character_test_require(playerdat->attr[0]==11 && player->skills[SKILL_ATTACK]==19,"guest stats lost");
    character_test_require(AskInventory(11) && OBJ_ITEM(AskInventory(11))==ITEM_TORCH,"guest item lost");
    bag=AskInventory(12);character_test_require(bag && OBJ_ITEM(bag)==ITEM_PACK,"guest bag lost");
    inside=Obj_PtrTMem(&bag->ol.link);
    character_test_require(inside && OBJ_ITEM(inside)==ITEM_PIECE_OF_CHEESE && OBJ_LINK(inside)==3,"nested stack lost");
    cursor=CursorObjPtr;character_test_require(GameInputMode==1 && cursor && OBJ_ITEM(cursor)==ITEM_GOLD_COIN && OBJ_LINK(cursor)==7,"guest cursor lost");
}
static void character_test(void)
{
    static int stage;static uint64_t began;uint64_t now=shared_now();
    extern char SaveLevel(int level),RestoreGame(char slot),ChangeLevel(int from,int to);
    if(!getenv("UW_COOP_TEST_CHARACTERS") || !partner)return;
    if(getenv("UW_COOP_TEST_RESUME")) {
        if(!stage){seat(1);character_test_inventory();seat(0);
            character_test_require(playerdat->attr[0]==23 && player->quests==QUEST_BIT(QUEST_RODRICK),"restart lost host/world state");
            fprintf(stderr,"SHARED CHARACTERS fresh process resumed both saved characters\n");stage=100;}
        return;
    }
    if(!began)began=now;
    if(!stage) {
        struct Object *item,*bag,*inside,*enemy;
        unsigned long host_exp;
        playerdat->attr[0]=23;player->skills[SKILL_ATTACK]=7;player->play_mana=9;
        strcpy(player->name,"Host Tester");player->quests=QUEST_BIT(QUEST_DR_OWL);player->game_vars[23]=41;
        item=CreateObj(ITEM_LOAF_OF_BREAD_B1,0);character_test_require(item && AddToInventory(item,11),"host pickup failed");
        host_exp=player->exp;
        seat(1);
        character_test_require(!AskInventory(11) && CursorObjPtr==NULL,"host inventory leaked to guest");
        character_test_require(player->quests==QUEST_BIT(QUEST_DR_OWL) && player->game_vars[23]==41,"host quest failed to sync");
        playerdat->attr[0]=11;player->skills[SKILL_ATTACK]=19;player->play_mana=4;
        player->quests^=QUEST_BIT(QUEST_DR_OWL);player->quests|=QUEST_BIT(QUEST_RODRICK);player->game_vars[23]=12;
        item=CreateObj(ITEM_TORCH,0);character_test_require(item && AddToInventory(item,11),"guest pickup failed");
        bag=CreateObj(ITEM_PACK,0);character_test_require(bag && AddToInventory(bag,12),"guest bag pickup failed");
        inside=CreateObj(ITEM_PIECE_OF_CHEESE,0);character_test_require(inside!=NULL,"stack allocation failed");
        SET_ISQUANT(inside,1);inside->ol.f.link=3;Obj_AddEnd(&bag->ol.link,inside);
        CursorObjPtr=CreateObj(ITEM_GOLD_COIN,0);character_test_require(CursorObjPtr!=NULL,"cursor allocation failed");
        SET_ISQUANT(CursorObjPtr,1);CursorObjPtr->ol.f.link=7;GameInputMode=1;
        player->active_spells=1;player->spells[0]=0x0300;FixPlayerEquips();
        duration_check();character_test_require(ASPELL_STAB(player->spells[0])==2,"guest upkeep failed");
        character_test_inventory();seat(0);
        character_test_require(playerdat->attr[0]==23 && player->skills[SKILL_ATTACK]==7 && player->play_mana==9,"guest stats leaked to host");
        character_test_require(player->active_spells==0 && !CursorObjPtr,"guest spells/cursor leaked to host");
        character_test_require(OBJ_ITEM(AskInventory(11))==ITEM_LOAF_OF_BREAD_B1,"guest inventory replaced host");
        character_test_require(player->quests==QUEST_BIT(QUEST_RODRICK) && player->game_vars[23]==12,"guest quest toggle failed to sync");
        enemy=CreateObj(ITEM_GIANT_RAT_43,1);character_test_require(enemy!=NULL,"enemy allocation failed");
        port_shared_killed(Obj_MemTPtr(partner),enemy);Obj_Free(enemy);
        character_test_require(player->exp==host_exp,"guest kill XP went to host");
        seat(1);character_test_require(player->exp>0,"guest got no kill XP");seat(0);
        {unsigned hp=partner->hp;damage_item(partner,ThePlayer,32,3,5,4);character_test_require(partner->hp==hp,"friendly fire");}
        {unsigned hp=ThePlayer->hp;partner->hp=1;healing(partner,15);
         character_test_require(partner->hp==characters[1].creature.avghit && ThePlayer->hp==hp,"healing used the wrong character");}
        fprintf(stderr,"SHARED CHARACTERS individual inventories, stats, mana, spells, quests, rewards passed\n");stage=1;
    }
    if(stage==1 && now-began>=1500) {
        character_test_require(SavePlayerInv("SAVE0\\"),"host save failed");
        character_test_require(SaveLevel(PlayerLevel),"world save failed");player_setup(32,2,1);
        if(access("SAVE1",0))character_test_require(mkdir("SAVE1")==0,"save slot create failed");
        character_test_require(clear_dir("SAVE1\\") && copy_dir("SAVE0\\","SAVE1\\"),"campaign copy failed");
        fprintf(stderr,"SHARED CHARACTERS campaign saved both characters\n");stage=2;
    }
    if(stage==2 && now-began>=3000) {
        seat(1);character_test_inventory();playerdat->attr[0]=2;player->play_mana=0;seat(0);
        character_test_require(RestoreGame(1),"campaign restore failed");player_setup(32,2,1);
        fprintf(stderr,"SHARED CHARACTERS campaign restore requested\n");stage=3;return;
    }
    if(stage==3) {
        seat(1);character_test_inventory();seat(0);
        character_test_require(playerdat->attr[0]==23 && player->play_mana==9 && player->quests==QUEST_BIT(QUEST_RODRICK),"host campaign restore lost state");
        fprintf(stderr,"SHARED CHARACTERS save/load restored both characters and world\n");stage=4;
    }
    if(stage==4 && now-began>=5000) {
        /* Travel begins on the guest using the real pending-position hook. */
        seat(1);NewPlayerX=38;NewPlayerY=1;NewPlayerLevel=2;NewPlyFade=0;npp_func=NULL;
        character_test_require(port_shared_guest_travel(),"guest travel was not handed to host");seat(0);character_travel();
        stage=5;
    }
    if(stage==5 && PlayerLevel==2) {
        seat(1);character_test_inventory();seat(0);
        character_test_require(AskInventory(11) && OBJ_ITEM(AskInventory(11))==ITEM_LOAF_OF_BREAD_B1,"travel lost host inventory");
        fprintf(stderr,"SHARED CHARACTERS guest travel preserved both inventories\n");
        seat(1);ThePlayer->hp=0;character_test_require(port_shared_death(),"guest death ended living host's game");
        character_test_require(ThePlayer->hp>0,"guest revival failed");seat(0);
        fprintf(stderr,"SHARED CHARACTERS companion revival passed\n");stage=6;
    }
    if(stage==6) {
        unsigned host_mana=player->max_mana,guest_mana=characters[1].record.max_mana;
        port_shared_level_hacks(LEVEL_TYBAL,0);
        character_test_require(characters[1].record.max_mana==0 && player->max_mana==host_mana,"Tybal restriction crossed characters");
        port_shared_level_hacks(LEVEL_TYBAL,1);
        character_test_require(characters[1].record.max_mana==guest_mana,"Tybal restriction lost guest mana");
        fprintf(stderr,"SHARED CHARACTERS Tybal mana rules passed\n");stage=7;
        {struct Player isolated=*player,changed=*player;
         isolated.orb=0;isolated.max_mana=0;isolated.saved_mana=35;isolated.saved_level=LEVEL_TYBAL;changed.orb=1;
         campaign_copy(&isolated,&changed);
         character_test_require(isolated.max_mana==35 && isolated.play_mana==35,"shared orb lost private mana reserve");
         fprintf(stderr,"SHARED CHARACTERS shared orb restored private mana passed\n");}
    }
    if(stage>=7 && stage<=14) {
        int to=stage==14 ? 1 : stage-4,found=0,tx,ty;int16 x,y;
        /* Exercise each native level archive and the per-level campaign hooks.
           This checks infrastructure; it does not skip/complete real quests. */
        character_test_require(ChangeLevel(PlayerLevel,to),"level sweep travel failed");PlayerLevel=to;
        for(ty=1;ty<63 && !found;ty++)for(tx=1;tx<63;tx++)
            if(mapdata[tx+ty*64].type==1 && find_good_x_and_y(ThePlayer,tx,ty,&x,&y,0)){found=1;break;}
        character_test_require(found,"level sweep arrival not found");player_setup(x,y,1);stage+=20;return;
    }
    if(stage>=27 && stage<=34) {
        seat(1);character_test_inventory();seat(0);
        character_test_require(player->quests==QUEST_BIT(QUEST_RODRICK) && player->game_vars[23]==12,"level sweep lost shared quests");
        fprintf(stderr,"SHARED CHARACTERS level sweep %d inventories and quests passed\n",PlayerLevel);
        stage-=19;
        if(stage==15)fprintf(stderr,"SHARED CHARACTERS all nine levels and return passed\n");
    }
}
