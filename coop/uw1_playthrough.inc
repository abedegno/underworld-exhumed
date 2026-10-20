/* Read-only world telemetry and raw input for a local, opt-in campaign driver.
   This interface cannot create objects, set quest flags, teleport or grant stats. */
FILE *port_bot_open(const char *name,const char *mode);
void port_bot_commit(void);
extern int port_bot_menu_count(void);
extern const char *port_bot_menu_option(int option);
static unsigned bot_serial;
static int bot_can_save;
extern unsigned char play_pow;
extern int16 pQatt;
static int16 bot_attacks[2][3];
static void bot_attack(int which)
{bot_attacks[which][0]=pQatt;bot_attacks[which][1]=weap_frame;bot_attacks[which][2]=play_pow;}
/* The original renderer's pick buffer is a read-only observation of the view.
   Export one visible pixel per object so the driver can use real mouse input. */
static void bot_targets(int which)
{
    static uint64_t next[2];uint64_t now=shared_now();FILE *fp;
    unsigned count[192]={0},sx[192]={0},sy[192]={0};int bx[192],by[192];
    if(!getenv("UW_COOP_BOT_HOME") || now<next[which] || scrnum!=0 || story_view || modal_owner>=0)return;
    next[which]=now+100;
    do_3d_grab();
    for(int y=0;y<xhgt;y++)for(int x=0;x<xwid;x++) {
        unsigned c=stdat[y*(xwid+2)+x+2];
        if(c && c<PickUp && c<192){count[c]++;sx[c]+=x;sy[c]+=y;}
    }
    for(int c=1;c<192;c++){bx[c]=-1;by[c]=-1;}
    for(int y=0;y<xhgt;y++)for(int x=0;x<xwid;x++) {
        unsigned c=stdat[y*(xwid+2)+x+2];
        if(c && c<192 && count[c]) {
            int cx=sx[c]/count[c],cy=sy[c]/count[c];
            if(bx[c]<0 || abs(x-cx)+abs(y-cy)<abs(bx[c]-cx)+abs(by[c]-cy)){bx[c]=x;by[c]=y;}
        }
    }
    fp=port_bot_open(which?"targets-client.json":"targets-host.json","wb");if(!fp)return;
    fprintf(fp,"[");int first=1;
    for(int c=1;c<192;c++)if(count[c]){
        fprintf(fp,"%s{\"index\":%d,\"x\":%d,\"y\":%d,\"pixels\":%u}",first?"":",",color_to_obj[c-1],PLeft+bx[c],199-PBot-by[c],count[c]);first=0;
    }
    fprintf(fp,"]");fclose(fp);
}
static void bot_input(void)
{
    FILE *fp;char line[128],op[16];unsigned long serial;int a,b,c;
    if(!getenv("UW_COOP_BOT_HOME"))return;
    fp=port_bot_open("command.txt","rb");if(!fp)return;
    if(!fgets(line,sizeof line,fp)){fclose(fp);return;}fclose(fp);
    /* compat.h's scanf stores DOS words. Parse into explicit 16-bit fields. */
    {char *end;serial=strtoul(line,&end,10);int16 aa=0,bb=0,cc=0;
     if(sscanf(end,"%15s %d %d %d",op,&aa,&bb,&cc)<1)return;a=aa;b=bb;c=cc;}
    /* Input is also polled from the translated renderer and blocking dialogs.
       Relocating objects there invalidates the render already in progress.
       Leave a save request pending until the next ordinary host frame. */
    if(!strcmp(op,"save") && port_coop_role()==COOP_HOST &&
       (!bot_can_save || atomic_load(&active_seat)!=0 || scrnum!=0 || story_view || modal_owner>=0))return;
    if(serial<=bot_serial)return;bot_serial=(unsigned)serial;
    /* A native dialog can block while accepting the next input command. */
    fp=port_bot_open("ack.txt","wb");if(fp){fprintf(fp,"%u",bot_serial);fclose(fp);}
    if(!strcmp(op,"shot"))bot_snapshot();
    else if(!strcmp(op,"save") && port_coop_role()==COOP_HOST) {
        static char desc[32]="Campaign checkpoint";
        if(check_save() && SaveGame(1,desc))fprintf(stderr,"SHARED PLAYTHROUGH native campaign checkpoint saved\n");
    }
    else if(!strcmp(op,"key_ext") && a>=1 && a<=127 && (b==0 || b==1)) {
        port_shared_key(0xE0);port_shared_key((uint8_t)(a|(b?0:128)));
    }
    if(!strcmp(op,"key") && a>=1 && a<=127 && (b==0 || b==1))port_shared_key((uint8_t)(a|(b?0:128)));
    else if(!strcmp(op,"pointer") && a>=0 && a<320 && b>=0 && b<200 && c>=0 && c<=3) {
        PlatPointer event;memset(&event,0,sizeof event);event.x=(float)a;event.y=(float)b;event.absolute=1;
        event.buttons=(unsigned)c;event.type=c ? PLAT_POINTER_DOWN : PLAT_POINTER_UP;port_shared_pointer(&event);
    }
    fp=port_bot_open("ack.txt","wb");if(fp){fprintf(fp,"%u",bot_serial);fclose(fp);}
}
static void bot_string(FILE *fp,const char *text)
{
    fputc('"',fp);
    for(;text && *text;text++) {
        unsigned c=(unsigned char)*text;
        if(c=='"' || c=='\\')fputc('\\',fp);
        if(c>=32 && c<127)fputc(c,fp);else fprintf(fp,"\\u%04x",c);
    }
    fputc('"',fp);
}
static void bot_state(void)
{
    static uint64_t next;static unsigned long frame;FILE *fp;unsigned char seen[1024]={0};
    uint64_t now=shared_now();
    if(!getenv("UW_COOP_BOT_HOME") || now<next || !partner || !mapdata)return;next=now+100;
    fp=port_bot_open("state.tmp","wb");if(!fp)return;
    fprintf(fp,"{\"frame\":%lu,\"command\":%u,\"level\":%d,\"screen\":%d,\"modal\":%d,\"story\":%d,\"characters\":[",++frame,bot_serial,PlayerLevel,scrnum,modal_owner,story_view);
    for(int s=0;s<2;s++) {
        const struct Player *p=s==atomic_load(&active_seat)?player:&characters[s].record;
        const struct Creature *cr=s==atomic_load(&active_seat)?playerdat:&characters[s].creature;
        struct Object *o=s?partner:critdata+1;
        fprintf(fp,"%s{\"index\":%d,\"x\":%d,\"y\":%d,\"z\":%d,\"facing\":%u,\"hp\":%u,\"max_hp\":%u,\"xp\":%lu,\"mana\":%u,\"quests\":%lu,\"talismans\":%u,\"root\":%d,\"cursor\":%d,\"inventory\":[",s?",":"",Obj_MemTPtr(o),seats[s].physics.x,seats[s].physics.y,seats[s].physics.z,(unsigned)(uint16_t)seats[s].facing,o->hp,cr->avghit,(unsigned long)p->exp,p->play_mana,(unsigned long)p->quests,p->talismans,OBJ_LINK(o),Obj_MemTPtr(s==atomic_load(&active_seat)?CursorObjPtr:characters[s].cursor));
        const union Link *inv=s==atomic_load(&active_seat)?Inventory:characters[s].inventory;
        for(int slot=0;slot<NUM_INV_SLOTS;slot++)fprintf(fp,"%s%d",slot?",":"",inv[slot].f.index);
        fprintf(fp,"],\"input_mode\":%d,\"interaction\":%d,\"pitch\":%d,\"lefty\":%u,\"hunger\":%u,\"fatigue\":%u,\"name\":",s==atomic_load(&active_seat)?GameInputMode:characters[s].inputmode,s==atomic_load(&active_seat)?RightButtonThing:characters[s].button,seats[s].pitch,p->lefty,p->hunger,p->fatigue);
        bot_string(fp,p->name);
        fprintf(fp,",\"attributes\":[%u,%u,%u],\"skills\":[",cr->attr[0],cr->attr[1],cr->attr[2]);
        for(int k=0;k<NUM_SKILLS;k++)fprintf(fp,"%s%u",k?",":"",p->skills[k]);
        fprintf(fp,"],\"runes\":[%u,%u,%u],\"quest_bytes\":[%u,%u,%u,%u],\"game_vars\":[",p->runebag[0],p->runebag[1],p->runebag[2],p->quest_bytes[0],p->quest_bytes[1],p->quest_bytes[2],p->quest_bytes[3]);
        for(int k=0;k<64;k++)fprintf(fp,"%s%u",k?",":"",p->game_vars[k]);
        const struct Phys *phys=s==atomic_load(&active_seat)?&PN:&seats[s].physics;
        fprintf(fp,"],\"motion_state\":%u,\"grounded\":%d,\"terrain\":%u,\"movement_input\":%d,\"velocity\":[%d,%d,%d],\"acceleration\":[%d,%d,%d]",p->motion_state,!(phys->terrain&FOOT_AIR),phys->terrain,s==atomic_load(&active_seat)?PlayerInput:seats[s].input,phys->vel[0],phys->vel[1],phys->vel[2],phys->acc[0],phys->acc[1],phys->acc[2]);
        fprintf(fp,",\"drawn\":%u,\"attack\":[%d,%d,%d]}",p->drawn,bot_attacks[s][0],bot_attacks[s][1],bot_attacks[s][2]);
    }
    fprintf(fp,"],\"menu\":[");for(int k=1;k<=port_bot_menu_count();k++){if(k>1)fputc(',',fp);bot_string(fp,port_bot_menu_option(k));}
    fprintf(fp,"],\"host_keyboard\":[");for(int k=0;k<128;k++)fprintf(fp,"%s%u",k?",":"",key_on[k]);
    fprintf(fp,"],\"tiles\":[");
    for(int i=0;i<4096;i++)fprintf(fp,"%s[%u,%u,%u,%u,%u]",i?",":"",mapdata[i].type,mapdata[i].height,mapdata[i].floor<TXM_FLOORS ? (unsigned)(TxmTerr[mapdata[i].floor]>>4) : 0,mapdata[i].door,mapdata[i].objects.f.index);
    fprintf(fp,"],\"objects\":[");
    /* Export linked world objects and inventories, never free pool records. */
    int first=1;unsigned queue[2048],n=0;
    for(int i=0;i<4096;i++)if(mapdata[i].objects.f.index && n<2040)queue[n++]=mapdata[i].objects.f.index;
    queue[n++]=1;queue[n++]=(unsigned)Obj_MemTPtr(partner);
    for(int s=0;s<2;s++){struct Object *cursor=s==atomic_load(&active_seat)?CursorObjPtr:characters[s].cursor;if(cursor)queue[n++]=(unsigned)Obj_MemTPtr(cursor);}
    for(unsigned q=0;q<n && q<2048;q++) {
        unsigned index=queue[q];struct Object *o;
        if(!index || index>=1024 || seen[index])continue;seen[index]=1;o=Obj_IntTMem(index);
        unsigned item=OBJ_ITEM(o);int mob=IsMobElem(o);
        fprintf(fp,"%s{\"index\":%u,\"item\":%u,\"name\":",first?"":",",index,item);first=0;
        bot_string(fp,get_string(item|STR_OBJNAMES));
        fprintf(fp,",\"npc_name\":");bot_string(fp,mob && o->whoami && o->whoami<0xF0 ? get_string((o->whoami+0x10)|STR_CONV) : "");
        fprintf(fp,",\"fate\":%u,\"solid\":%u,\"radius\":%u,\"height\":%u",ComObjData[item].fate,ComObjData[item].solid,ComObjData[item].radius,ComObjData[item].height);
        fprintf(fp,",\"x\":%u,\"y\":%u,\"z\":%u,\"next\":%u,\"link\":%u,\"owner\":%u,\"flags\":%u,\"quantity\":%u,\"who\":%u,\"hp\":%u,\"goal\":%u}",mob?OBJ_HOMEX(o):0,mob?OBJ_HOMEY(o):0,OBJ_Z(o),o->qn.f.next,OBJ_LINK(o),OBJ_OWNER(o),o->id,OBJ_ISQUANT(o),mob?o->whoami:0,mob?o->hp:OBJ_QUALITY(o),mob?OBJ_GOAL(o):0);
        if(o->qn.f.next && n<2048)queue[n++]=o->qn.f.next;
        if((OBJ_MAJOR(o)==MAJOR_CREATURE || (item>=0x80 && item<0x90)) && OBJ_LINK(o) && n<2048)queue[n++]=OBJ_LINK(o);
    }
    fprintf(fp,"]}");fclose(fp);port_bot_commit();
}
