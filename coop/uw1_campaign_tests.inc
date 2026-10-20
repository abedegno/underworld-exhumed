/* Opt-in integration scenarios, run inside the real host engine with a connected
   client. They use original level archives and native quest/puzzle handlers.
   Setup supplies prerequisites; it is not an uninterrupted campaign playthrough. */
static void campaign_require(int ok,const char *what)
{if(!ok)port_fatal("shared campaign test: %s",what);}
static void campaign_arrive(int level,int tx,int ty)
{
    extern char ChangeLevel(int from,int to);
    int16 x,y;
    campaign_require(ChangeLevel(PlayerLevel,level),"level load failed");PlayerLevel=level;
    if(!find_good_x_and_y(ThePlayer,tx,ty,&x,&y,0)) {
        int found=0;
        for(int yy=1;yy<63 && !found;yy++)for(int xx=1;xx<63;xx++)
            if(Map_GetAddr(xx,yy)->type==TILE_OPEN && find_good_x_and_y(ThePlayer,xx,yy,&x,&y,0)){found=1;break;}
        campaign_require(found,"arrival failed");
    }
    player_setup(x,y,1);
}
static void campaign_save(void)
{
    extern char SaveLevel(int level);
    int x=PN.x/256,y=PN.y/256;
    campaign_require(SavePlayerInv("SAVE0\\"),"player save failed");
    campaign_require(SaveLevel(PlayerLevel),"world save failed");player_setup(x,y,1);
    if(access("SAVE1",0))campaign_require(mkdir("SAVE1")==0,"slot creation failed");
    campaign_require(clear_dir("SAVE1\\") && copy_dir("SAVE0\\","SAVE1\\"),"slot copy failed");
}
static void campaign_restore(void)
{
    extern char RestoreGame(char slot);
    campaign_require(RestoreGame(1),"restore failed");
    player_setup(player->saved_x/256,player->saved_y/256,1);
}
static void campaign_cursor_to_slot(int slot)
{
    struct Object *obj=CursorObjPtr;
    campaign_require(obj && AddToInventory(obj,slot),"reward could not be stowed");
    CursorObjPtr=NULL;GameInputMode=0;unforce_mouse_cursor(3);
}
static struct Object *campaign_find(int item,int who,int *tx,int *ty)
{
    for(int y=1;y<63;y++)for(int x=1;x<63;x++)
        for(struct Object *o=Obj_PtrTMem(&Map_GetAddr(x,y)->objects);o;o=Obj_PtrTMem(&o->qn.link))
            if((item<0 || OBJ_ITEM(o)==item) && (who<0 || (IsMobElem(o) && o->whoami==who))){*tx=x;*ty=y;return o;}
    return NULL;
}
static void campaign_combinations(void)
{
    unsigned short rules[30];
    extern unsigned char bltfromdrive(char *name,void *buf,unsigned size);
    campaign_require(bltfromdrive("DATA\\cmb.dat",rules,sizeof rules),"original combination rules missing");
    for(int i=0;i<10;i++)if((rules[i*3]|rules[i*3+1])&0x8000) {
        struct Object *a,*b,*c;int combo;
        seat(i&1);a=CreateObj(rules[i*3]&0x1FF,0);b=CreateObj(rules[i*3+1]&0x1FF,0);
        campaign_require(a && b,"combination prerequisites failed");
        combo=ObjsBeCombinable(a,b);campaign_require(combo==i,"native combination selection failed");
        c=CombineObjs(combo);campaign_require(c && OBJ_ITEM(c)==(rules[i*3+2]&0x1FF),"native combination result failed");
        campaign_require(RemoveAfterCombine(a,combo)==!!(rules[i*3]&0x8000) && RemoveAfterCombine(b,combo)==!!(rules[i*3+1]&0x8000),"combination source consumption failed");
        fprintf(stderr,"SHARED CAMPAIGN combination %03x + %03x -> %03x seat=%d passed\n",OBJ_ITEM(a),OBJ_ITEM(b),OBJ_ITEM(c),i&1);
        Obj_Free(a);Obj_Free(b);Obj_Free(c);
    }
    seat(0);
}
static void campaign_emerald(void)
{
    int n=0,x=40,y=40;struct Object *obj,*reward;union Link *head;
    for(int dx=-4;dx<=4;dx+=8)for(int dy=-4;dy<=4;dy+=8) {
        struct Tile *tile=Map_GetAddr(x+dx,y+dy);tile->type=TILE_OPEN;tile->height=8;
        seat(n&1);obj=CreateObj(ITEM_EMERALD,0);campaign_require(obj!=NULL,"emerald prerequisite failed");Obj_Add(&tile->objects,obj);
        if(++n==3){emerald_trap(NULL,x,y);head=&Map_GetAddr(x,y+1)->objects;campaign_require(!Obj_InList(&head,0,3,3,13),"three emeralds accepted");}
    }
    Map_GetAddr(x,y+1)->type=TILE_OPEN;Map_GetAddr(x,y+1)->height=8;
    emerald_trap(NULL,x,y);head=&Map_GetAddr(x,y+1)->objects;reward=Obj_InList(&head,0,3,3,13);
    campaign_require(reward && OBJ_ITEM(reward)==ITEM_VAS_STONE,"emerald reward missing");
    for(int dx=-4;dx<=4;dx+=8)for(int dy=-4;dy<=4;dy+=8){head=&Map_GetAddr(x+dx,y+dy)->objects;campaign_require(!Obj_InList(&head,0,2,2,7),"emerald not consumed");}
    Obj_Rem(&Map_GetAddr(x,y+1)->objects,reward);campaign_require(AddToInventory(reward,16),"emerald reward pickup failed");
    seat(0);campaign_require(!AskInventory(16),"emerald reward duplicated to host");seat(1);campaign_require(AskInventory(16) && OBJ_ITEM(AskInventory(16))==ITEM_VAS_STONE,"emerald reward lost on seat switch");seat(0);
    fprintf(stderr,"SHARED CAMPAIGN native four emerald puzzle and private reward passed\n");
}
static void campaign_land(int slot,int x,int y)
{
    int owner=atomic_load(&active_seat);
    struct Object *original=AskInventory(slot),*mobile;
    campaign_require(original!=NULL,"missing ritual talisman");
    mobile=CreateObj(OBJ_ITEM(original),1);campaign_require(mobile!=NULL,"mobile allocation failed");
    *(struct StaticObj*)mobile=*(struct StaticObj*)original;
    mobile->qn.word=0;mobile->ol.word=0;mobile->last_hit=(unsigned char)Obj_MemTPtr(ThePlayer);
    SET_HOMEX(mobile,x);SET_HOMEY(mobile,y);SET_TERRAIN(mobile,2);
    InvRemoveOneObject(original);Obj_Free(original);
    Obj_Add(&Map_GetAddr(x,y)->objects,mobile);
    /* Settled projectiles are normally processed by the host's world tick. */
    seat(0);XP=x;YP=y;
    mob_to_static(mobile);
    seat(owner);
}
static void campaign_autotalk(void)
{
    struct Object *npc;int gx=OBJ_HOMEX(partner),gy=OBJ_HOMEY(partner)-1;
    extern unsigned char critter_ai(void);
    Map_GetAddr(gx+2,gy)->type=TILE_OPEN;Map_GetAddr(gx+2,gy)->height=Map_GetAddr(gx,gy)->height;
    player_setup(gx+2,gy,1);SET_HEADING(ThePlayer,0);SET_FINEHEAD(ThePlayer,0);
    seat(1);SET_HEADING(ThePlayer,4);SET_FINEHEAD(ThePlayer,0);seat(0);
    npc=CreateObj(ITEM_FIGHTER_5D,1);campaign_require(npc!=NULL,"automatic talk setup failed");
    npc->whoami=0x1B;SET_HOMEX(npc,gx);SET_HOMEY(npc,gy);SET_FINEX(npc,3);SET_FINEY(npc,3);
    SET_Z(npc,Map_GetAddr(gx,gy)->height<<3);SET_GOAL(npc,GOAL_TALK);SET_ATTITUDE(npc,ATT_FRIENDLY);
    SET_SPEED(npc,0);SET_PITCH(npc,0x10);SET_B15_6(npc,1);
    Obj_Add(&Map_GetAddr(gx,gy)->objects,npc);
    fprintf(stderr,"SHARED CAMPAIGN automatic conversation starting nearest guest\n");
    set_critter_vars(npc);critter_ai();campaign_require(OBJ_TALKEDTO(npc),"automatic NPC ignored facing guest");
    Obj_Punt(&Map_GetAddr(gx,gy)->objects,npc,1);
    fprintf(stderr,"SHARED CAMPAIGN native automatic NPC conversation for facing guest passed\n");
}
static void campaign_test(void)
{
    extern void mantra_advance(void);
    static int stage,ids[8],count;static uint64_t began;
    const char *mode=getenv("UW_COOP_TEST_CAMPAIGN");
    uint64_t now=shared_now();
    if(!mode || !partner)return;
    if(!began)began=now;
    if(now-began<500)return;
    if(!strcmp(mode,"quests")) {
        if(stage==0) {
            struct Object *npc;
            int16 args[3]={0,1,2};
            static char work[0x10000];
            cnv_id=0x1B;campaign_require(load_script("DATA\\cnv.ark",work)>0,"original quest script load failed");
            /* A creature pursuing either companion must retain the native
               hostile dialogue gate and report that hostility to its script. */
            npc=CreateObj(ITEM_FIGHTER_5D,1);campaign_require(npc!=NULL,"hostile conversation setup failed");
            npc->whoami=0x1B;SET_ALLY(npc,0);SET_ATTITUDE(npc,ATT_FRIENDLY);
            for(int owner=0;owner<2;owner++) {
                int16 attitude=-1;seat(owner);
                SET_GTARG(npc,Obj_MemTPtr(ThePlayer));SET_GOAL(npc,GOAL_ATTACK);
                setup_converse_data(npc);bab_var_out("npc_attitude",&attitude,1);
                campaign_require(attitude==ATT_HOSTILE,"attacking companion was friendly in conversation script");
                for(int g=0;g<3;g++) {
                    SET_GOAL(npc,g==0?GOAL_ATTACK:g==1?GOAL_FLEE:GOAL_CORNERED);
                    TalkTo(npc);campaign_require(scrnum==0,"creature targeting companion accepted ordinary dialogue");
                }
            }
            seat(0);Obj_Free(npc);talking_to=NULL;
            fprintf(stderr,"SHARED CAMPAIGN hostile dialogue gates and script attitude for both companions passed\n");
            seat(1);
            for(int q=0;q<36;q++) {
                babl_setmem(0,q);babl_setmem(1,q<32?1:q-30);set_quest(args+2);
                campaign_require(get_quest(args+1)==(q<32?1:q-30),"conversation quest write/read failed");
            }
            babl_setmem(0,23);babl_setmem(1,17);x_traps(args+2);
            seat(0);campaign_require(player->quests==0xFFFFFFFFu && player->quest_bytes[3]==5 && player->game_vars[23]==17,"conversation quests not shared");
            seat(1);babl_setmem(0,QUEST_HAGBARD);babl_setmem(1,0);set_quest(args+2);seat(0);
            campaign_require(!(player->quests&QUEST_BIT(QUEST_HAGBARD)),"conversation quest clear lost");
            player->quests=0;
            npc=CreateObj(ITEM_FIGHTER_5D,1);campaign_require(npc!=NULL,"plot creature allocation failed");
            seat(1);npc->whoami=0x6E;death_check(npc,1);npc->whoami=0x8E;death_check(npc,1);npc->whoami=0x18;death_check(npc,1);seat(0);Obj_Free(npc);
            campaign_require(player->quests==(QUEST_BIT(QUEST_GAZER)|QUEST_BIT(QUEST_RODRICK)|QUEST_BIT(QUEST_LIZARDMEN)),"plot death quests not shared");
            fprintf(stderr,"SHARED CAMPAIGN conversation quest bits/bytes, clears, variables and plot deaths passed\n");
            player->max_mana=31;player->play_mana=31;seat(1);player->max_mana=19;player->play_mana=19;seat(0);
            campaign_arrive(7,23,45);stage=1;return;
        }
        if(stage==1) {
            struct Object *orb,*rock,*tybal;int ox,oy,tx,ty;unsigned hp;unsigned long exp=player->exp;
            campaign_require(player->max_mana==0,"host mana suppression failed");seat(1);
            campaign_require(player->max_mana==0,"guest mana suppression failed");
            orb=campaign_find(ITEM_ORB,-1,&ox,&oy);tybal=campaign_find(-1,0xE7,&tx,&ty);
            campaign_require(orb && tybal,"original orb or Tybal missing");hp=tybal->hp;
            rock=CreateObj(ITEM_ORB_ROCK,0);campaign_require(rock && AddToInventory(rock,11),"orb rock setup failed");
            MapObj_X=ox;MapObj_Y=oy;ObjectActing=rock;
            TybalsOrb_seg040_A12(orb,1,0);
            campaign_require(player->orb && player->max_mana==19 && player->play_mana==19 && !AskInventory(11),"guest orb use failed");
            seat(0);campaign_require(player->orb && player->max_mana==31 && player->play_mana==31,"orb did not restore private host mana");
            campaign_require(tybal->hp==hp/2+1 && !campaign_find(ITEM_ORB,-1,&ox,&oy),"orb did not change original shared world");
            fprintf(stderr,"SHARED CAMPAIGN native orb destruction restored both private mana reserves passed\n");
            {int16 x,y;extern unsigned char critter_ai(void);
             campaign_require(find_good_x_and_y(ThePlayer,tx,ty,&x,&y,0),"Tybal test position failed");player_setup(x,y,1);
             set_critter_vars(tybal);campaign_require(damage_critter(tybal,255,partner),"guest could not kill original Tybal");
             campaign_require(player->exp==exp,"Tybal guest kill credited host");
             SET_FRAME(tybal,3);set_critter_vars(tybal);campaign_require(!critter_ai(),"Tybal death animation did not finish");}
            campaign_require((player->dreams&4)!=0,"Tybal death not shared");
            fprintf(stderr,"SHARED CAMPAIGN original Tybal weakened/killed, guest reward and shared scene passed\n");
            campaign_arrive(5,54,52);stage=2;return;
        }
        if(stage==2) {
            struct Object *grave=NULL,*bones;int tx,ty;
            for(ty=1;ty<63 && !grave;ty++)for(tx=1;tx<63 && !grave;tx++)
                for(struct Object *o=Obj_PtrTMem(&Map_GetAddr(tx,ty)->objects);o;o=Obj_PtrTMem(&o->qn.link))
                    if(OBJ_ITEM(o)==ITEM_GRAVESTONE && OBJ_ISQUANT(o) && OBJ_LINK(o)==0x221){grave=o;break;}
            campaign_require(grave!=NULL,"Garamon grave missing in original level");
            seat(1);bones=CreateObj(ITEM_PILE_OF_BONES_C6,0);campaign_require(bones && AddToInventory(bones,11),"bones setup failed");
            bones->ol.f.owner=0x3E;ObjectActing=bones;
            fprintf(stderr,"SHARED CAMPAIGN Garamon conversation starting on guest\n");UseBonesOn(grave,1);
            campaign_require(player->garamon && player->talisman_ok && !AskInventory(11) && OBJ_LINK(grave)==0x222,"Garamon burial failed");
            seat(0);campaign_require(player->garamon && player->talisman_ok,"burial not shared");
            fprintf(stderr,"SHARED CAMPAIGN native Garamon burial and original conversation passed\n");
            campaign_autotalk();
            campaign_save();stage=3;return;
        }
        if(stage==3){campaign_restore();stage=4;return;}
        if(stage==4){seat(1);campaign_require(player->garamon && player->orb && player->talisman_ok,"guest quests lost on restore");seat(0);
            campaign_require(player->garamon && player->orb && player->talisman_ok,"host quests lost on restore");
            fprintf(stderr,"SHARED CAMPAIGN quest save/load passed\nSHARED CAMPAIGN quests complete\n");stage=100;}
    } else if(!strcmp(mode,"puzzles")) {
        if(stage==0){campaign_arrive(4,32,32);stage=1;return;}
        if(stage==1) {
            struct Object trap={0};
            int gx=OBJ_HOMEX(partner),gy=OBJ_HOMEY(partner),height=Map_GetAddr(gx,gy)->height+1;
            change_terrain(gx,gy,0xFF,0xFF,height,0xFF,0,0,0);
            campaign_require(seats[1].physics.z==(height<<6),"raising guest tile left physics underground");
            seat(1);work_bullfrog_tiles(4,0,0);work_bullfrog_tiles(3,0,0);work_bullfrog_tiles(2,0,0);work_bullfrog_tiles(1,0,0);seat(0);
            campaign_require(player->game_vars[24]==1 && player->game_vars[25]==1 && player->game_vars[26]==62,"bullfrog controls not shared");
            fprintf(stderr,"SHARED CAMPAIGN bullfrog heights selected=%d neighbour=%d\n",Map_GetAddr(49,49)->height,Map_GetAddr(48,48)->height);
            campaign_require(Map_GetAddr(49,49)->height==2 && Map_GetAddr(48,48)->height==3,"bullfrog geometry failed");
            trap.qn.f.quality=3;SET_Z(&trap,8);eight_pos_switch(3,&trap,32,32);
            campaign_require(Map_GetAddr(32,32)->height==4,"eight position switch geometry failed");
            seat(1);campaign_require(player->game_vars[26]==62 && Map_GetAddr(49,49)->height==2,"host geometry not visible to guest");seat(0);
            campaign_save();stage=2;return;
        }
        if(stage==2){campaign_restore();stage=3;return;}
        if(stage==3) {
            campaign_require(Map_GetAddr(49,49)->height==2 && player->game_vars[26]==62,"puzzle state lost on restore");
            fprintf(stderr,"SHARED CAMPAIGN bullfrog/platform controls, shared geometry and save/load passed\n");
            campaign_combinations();campaign_emerald();
            campaign_arrive(3,24,45);stage=4;return;
        }
        if(stage==4) {
            char wrong[16]={0};seat(1);
            campaign_require(!play_cup_tune(wrong) && !player->cup,"wrong tune granted cup");
            fprintf(stderr,"SHARED CAMPAIGN play flute starting on guest\n");play_instrument(1);
            campaign_require(player->cup && CursorObjPtr && OBJ_ITEM(CursorObjPtr)==ITEM_SHINY_CUP,"flute tune failed to grant cup");
            campaign_cursor_to_slot(11);seat(0);
            campaign_require(player->cup && !AskInventory(11),"cup duplicated into host inventory");
            seat(1);{char tune[9]={0x40,0x43,0x41,0x3E,0x40,0x47,0x48,0x47,0x43};campaign_require(!play_cup_tune(tune),"cup reward duplicated");}seat(0);
            fprintf(stderr,"SHARED CAMPAIGN native flute puzzle, private cup and duplicate prevention passed\n");
            fprintf(stderr,"SHARED CAMPAIGN key mantra is %s\n",get_string((STRN_MANTRAS+NUM_SKILLS+1)|STR_CHARGEN));
            stage=5;
        }
        if(stage==5){seat(1);mantra_advance();
            campaign_require(player->key && CursorObjPtr && OBJ_ITEM(CursorObjPtr)==ITEM_KEY_OF_TRUTH,"key mantra failed");
            campaign_cursor_to_slot(12);seat(0);campaign_require(player->key && !AskInventory(12),"key duplicated into host inventory");
            fprintf(stderr,"SHARED CAMPAIGN native key mantra and private reward passed\nSHARED CAMPAIGN puzzles complete\n");stage=100;}
    } else if(!strcmp(mode,"ritual")) {
        if(stage==0){campaign_arrive(8,32,32);stage=1;return;}
        if(stage==1) {
            /* Lit and unlit taper are two forms of the same talisman. */
            for(int id=0;id<512;id++)if(ComObjData[id].fate==10 && id!=ITEM_LIT_TAPER){campaign_require(count<8,"too many distinct talismans");ids[count++]=id;}
            campaign_require(count==8,"original talisman table is incomplete");
            for(int i=0;i<8;i++){struct Object *o;seat(i&1);o=CreateObj(ids[i],0);campaign_require(o && AddToInventory(o,11+i/2),"split talisman inventory setup failed");fprintf(stderr,"SHARED CAMPAIGN talisman %d item=%03x seat=%d\n",i,ids[i],i&1);}seat(0);
            player->talisman_ok=0;player->talismans=8;
            /* Before burial and outside the central lava, no talisman is counted. */
            seat(1);{struct Object *o=CreateObj(ids[0],1);SET_TERRAIN(o,2);Obj_Add(&Map_GetAddr(32,32)->objects,o);XP=YP=32;
                struct Object *s=mob_to_static(o);campaign_require(s && player->talismans==8 && (player->dreams&8),"premature ritual accepted");Obj_Rem(&Map_GetAddr(32,32)->objects,s);Obj_Free(s);}
            player->talisman_ok=1;
            {struct Object *o=CreateObj(ids[0],1);SET_TERRAIN(o,2);Obj_Add(&Map_GetAddr(10,10)->objects,o);XP=YP=10;
             struct Object *s=mob_to_static(o);campaign_require(s && player->talismans==8,"wrong lava tile accepted");Obj_Rem(&Map_GetAddr(10,10)->objects,s);Obj_Free(s);}
            seat(0);
            for(int i=0;i<4;i++){
                unsigned hp=critdata[1].hp;seat(i&1);
                if(i==1)critdata[1].hp=0;
                campaign_land(11+i/2,32,32);campaign_require(player->talismans==7-i,"talisman count failed");
                if(i==1)critdata[1].hp=(unsigned char)hp;
            }seat(0);
            campaign_save();stage=2;return;
        }
        if(stage==2){campaign_restore();stage=3;return;}
        if(stage==3) {
            campaign_require(player->talismans==4 && player->talisman_ok,"mid ritual save lost progress");
            for(int i=4;i<8;i++){seat(i&1);campaign_require(AskInventory(11+i/2) && OBJ_ITEM(AskInventory(11+i/2))==ids[i],"mid ritual save lost talisman");
                campaign_land(11+i/2,32,32);campaign_require(player->talismans==7-i,"restored talisman count failed");}seat(0);
            fprintf(stderr,"SHARED CAMPAIGN all eight native talisman destructions and mid ritual save/load passed\n");
            stage=4;return;
        }
        if(stage==4 && PlayerLevel==9){seat(1);campaign_require(player->talismans==0xFF && !AskInventory(14),"guest did not follow ritual portal");seat(0);
            campaign_require(player->talismans==0xFF && !AskInventory(14),"host did not follow ritual portal");
            fprintf(stderr,"SHARED CAMPAIGN native Slasher banishment and both avatars in the void passed\nSHARED CAMPAIGN ritual complete\n");stage=100;}
    } else if(!strcmp(mode,"terrain")) {
        static uint64_t phase;
        extern unsigned char tsteps,combEfflen,tremEfflen;
        if(stage==0) {
            int dry=0;
            for(int i=0;i<TXM_FLOORS;i++)if(((TxmTerr[i]>>4)&3)==0){dry=i;break;}
            for(int y=38;y<=44;y++)for(int x=38;x<=45;x++) {
                struct Tile *t=Map_GetAddr(x,y);t->type=TILE_OPEN;t->height=7;t->floor=dry;
            }
            Map_GetAddr(42,40)->type=TILE_SLOPE_E;
            for(int owner=0;owner<2;owner++) {
                seat(owner);player_setup(42,40,1);PlayerFacing=0x4000;
                int startx=PN.x;
                for(int i=0;i<6;i++) {
                    PlayerInput=PIN_FORWARD;ForwInpRate=0x30;move_player(16);
                    fprintf(stderr,"SHARED TERRAIN slope seat=%d step=%d x=%d z=%d expected=%d velz=%d hits=%x\n",owner,i,PN.x,PN.z,7*64+((PN.x&255)>>2),PN.vel[2],Ppd.hits0);
                    campaign_require(PN.x/256==42 && abs(PN.z-(7*64+((PN.x&255)>>2)))<=1,"avatar failed to follow native slope height");
                }
                campaign_require(PN.x-startx>8,"slope test did not move the avatar");
                Ppd.index=Obj_MemTPtr(ThePlayer);
                campaign_require(set_resterr(0x0D)==FOOT_SHORE,"avatar did not receive player shore footing");
                player_setup(40+owner,40,1);
            }
            fprintf(stderr,"SHARED CAMPAIGN native slope following and shore footing for both avatars passed\n");
            seat(0);tsteps=17;combEfflen=13;tremEfflen=7;
            seat(1);tsteps=2;combEfflen=3;tremEfflen=4;
            seat(0);campaign_require(tsteps==17 && combEfflen==13 && tremEfflen==7,"guest overwrote host movement clocks");
            tsteps=combEfflen=tremEfflen=0;
            seat(1);campaign_require(tsteps==2 && combEfflen==3 && tremEfflen==4,"host overwrote guest movement clocks");
            tsteps=combEfflen=tremEfflen=0;seat(0);
            fprintf(stderr,"SHARED CAMPAIGN private movement and shake clocks passed\n");
            {extern int frp_check(int,int);extern int16 askill,hitobj,hitloc;extern unsigned char hitangle;
            askill=99;hitobj=Obj_MemTPtr(partner);hitloc=0;hitangle=4;
            campaign_require(frp_check(1,Obj_MemTPtr(partner))==1,"friendly attacks reached player armor rolls");}
            fprintf(stderr,"SHARED CAMPAIGN friendly attacks suppress armor damage rolls passed\n");
            campaign_arrive(8,2,2);stage=1;return;
        }
        if(stage==1) {
            int dry=-1,lava=-1;
            for(int i=0;i<TXM_FLOORS;i++) {
                if(((TxmTerr[i]>>4)&3)==0)dry=i;
                if(((TxmTerr[i]>>4)&3)==2)lava=i;
            }
            campaign_require(dry>=0 && lava>=0,"original level eight terrain textures missing");
            /* A quiet fixture isolates lava from Slasher attacks. The terrain
               and all damage/equipment handlers remain the native game code. */
            for(int y=2;y<=8;y++)for(int x=2;x<=8;x++) {
                struct Tile *t=Map_GetAddr(x,y);t->type=TILE_OPEN;t->height=4;t->floor=dry;
            }
            Map_GetAddr(5,3)->floor=lava;
            seat(0);DragonSkinBoots_dseg_5c99_1B01=0;player_setup(3,3,1);playerdat->avghit=40;ThePlayer->hp=30;
            seat(1);DragonSkinBoots_dseg_5c99_1B01=0;player_setup(5,3,1);playerdat->avghit=40;ThePlayer->hp=30;
            campaign_require(player->motion_state&MS_LAVA,"guest was not standing on native lava");
            seat(0);phase=now;stage=2;return;
        }
        if(stage==2 && now-phase>=500) {
            seat(1);campaign_require(ThePlayer->hp<30,"unprotected guest was immune to lava");
            ThePlayer->hp=30;DragonSkinBoots_dseg_5c99_1B01=1;seat(0);
            campaign_require(ThePlayer->hp>=30 && !DragonSkinBoots_dseg_5c99_1B01,"guest lava damage or boots leaked into host");
            phase=now;stage=3;return;
        }
        if(stage==3 && now-phase>=600) {
            seat(1);campaign_require(ThePlayer->hp>=30 && DragonSkinBoots_dseg_5c99_1B01,"guest boots did not prevent native lava damage");
            DragonSkinBoots_dseg_5c99_1B01=0;player_setup(4,3,1);seat(0);
            campaign_require(ThePlayer->hp>=30 && !DragonSkinBoots_dseg_5c99_1B01,"guest protection changed host state");
            fprintf(stderr,"SHARED CAMPAIGN native guest lava damage, private boot protection and host health passed\nSHARED CAMPAIGN terrain complete\n");stage=100;
        }
    } else if(!strcmp(mode,"ending")) {
        if(stage==0){campaign_arrive(9,27,23);stage=1;return;}
        if(stage==1) {
            struct Object *exit=NULL;
            for(int y=1;y<63 && !exit;y++)for(int x=1;x<63 && !exit;x++)
                for(struct Object *o=Obj_PtrTMem(&Map_GetAddr(x,y)->objects);o;o=Obj_PtrTMem(&o->qn.link))
                    if(OBJ_ITEM(o)==ITEM_DO_TRAP && OBJ_QUALITY(o)==0x3F){exit=o;break;}
            campaign_require(exit!=NULL,"original ending trap missing");
            seat(1);UseTrap(exit,OBJ_HOMEX(ThePlayer),OBJ_HOMEY(ThePlayer));seat(0);
            campaign_require(EndGameMode_dseg_1C8F!=0,"guest exit did not arm ending");
            fprintf(stderr,"SHARED CAMPAIGN guest triggered original ending trap\n");stage=100;
        }
    }
}
