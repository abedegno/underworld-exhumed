/* One authoritative engine, two physical avatars and two host-rendered views.
   UW1 switches independent character/inventory records and shares campaign facts.
   UW2 retains its party character. The client never
   simulates the world or sends positions, objects, files or executable code. */
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
/* Platform structs use the host ABI. compat.h then enables the DOS game's
   packed layout; loading plat.h afterwards would misread PlatConfig fields. */
#include "plat.h"
#include "compat.h"
#include "port.h"
#include "player.h"
#include "motion.h"
#include "sys.h"
#include "ui.h"
#include "gfx.h"
#include "view3d.h"
#include "critter.h"
#include "shared.h"
#include "shared_port.h"
#include "coop.h"
#include "sys/inscript.h"
#include "ui/settings.h"
#include "inv.h"
#include "combat.h"
#include "file.h"
#if COOP_GAME == 1
#include "conv.h"
#include "event.h"
#include "sound.h"
#endif
#ifndef PIN_NONE
#define PIN_NONE 0
#endif

static int enabled;
static _Atomic int worker_started;
static _Atomic int connected, stop_worker, active_seat, paired;
static _Atomic int game_finished;
static uint64_t stop_deadline;
static pthread_t worker;
static pthread_mutex_t shared_lock=PTHREAD_MUTEX_INITIALIZER;
static struct shared_net transport;
static struct {
    unsigned char keys[512];unsigned count;
    int x,y,dx,dy,buttons,absolute;
    struct { int x,y,buttons; } clicks[32];unsigned nclicks;
} inputs[2];
static unsigned char pictures[2][SHARED_FRAME];
static int picture_valid[2];
static uint64_t picture_generation[2];
/* A bounded stereo ring. Old audio is discarded when a peer cannot keep up. */
static int16_t sound_ring[16384];
static unsigned sound_read,sound_write,sound_count;
static unsigned sound_nonzero;
static unsigned char client_held[256];static int client_prefix;
static struct Object * _Atomic partner;
static int partner_level;
static struct Object *test_door;
static uint32_t guest_time;
static int in_step;
static int modal_owner=-1,story_view;
extern char KeybUsed;
extern int16 pTurn,MaxPlayerAccel,lastTerr,plyMoType;
extern unsigned char doMod;
extern unsigned char DoAnimO;
extern char in_game;
unsigned char IsMobElem(struct Object *object);
#if COOP_GAME != 1
unsigned char find_good_x_and_y(struct Object *obj,int x,int y,int16 *outx,int16 *outy,int loose);
#endif
void player_setup(int x,int y,int how);
struct seat_state {
    struct Phys physics;
    struct Handler handler;
    int16 sq,facing,heading,pitch,bank,input,turn,forward,turnrate,pturn,maxacc,lastterr,motype,fps[3],mod[4];
    void *eye;int16 camera_pos[3],camera_angle[3];
    unsigned char motion,update,mouse,movement,fpsmode,swim,bob;
    char keyboard;
#if COOP_GAME == 1
    int16 destination_x,destination_y,destination_level;
    char destination_fade;
    void (*destination_fn)(void);
#endif
};
static struct seat_state seats[2];
static struct seat_state saved_guest;
static struct Object saved_guest_object;
static int resume_guest,resume_level,resume_eye;
static int companion_health=-1;

static void seat(int next);
static void state(struct seat_state *s,int save);
static uint64_t shared_now(void);
#if COOP_GAME == 1
static void bot_snapshot(void);
#include "uw1_characters.inc"
#include "uw1_campaign_tests.inc"
#include "uw1_playthrough.inc"
#endif

static uint64_t shared_now(void)
{ uint64_t t=plat_counter(),hz=plat_counter_hz();return t/hz*1000+t%hz*1000/hz; }
static void put16(unsigned char *p,int value) { p[0]=(unsigned char)(value>>8);p[1]=(unsigned char)value; }
static int get16(const unsigned char *p) { return p[0]*256+p[1]; }
void port_shared_enable(void) { enabled=1; }
int port_shared_on(void) { return enabled; }
int port_shared_guest(void) { return enabled && atomic_load(&active_seat)==1; }
int port_shared_avatar(const void *o)
{ return enabled && partner && (o==partner || o==critdata+1); }
int port_shared_party_alive(void)
{return ThePlayer->hp || (partner && (partner->hp || critdata[1].hp));}
int port_shared_friendly_source(const void *o)
{
    const struct Object *from=o;
    if(port_shared_avatar(o))return 1;
    return enabled && partner && from && IsMobElem((struct Object*)from) && OBJ_MAJOR(from)!=MAJOR_CREATURE &&
        (from->last_hit==1 || from->last_hit==Obj_MemTPtr(partner));
}
int port_shared_art(const void *o,int item)
{
    if(!port_shared_avatar(o))return item;
#if COOP_GAME == 1
    return o==partner ? ITEM_FIGHTER_5E : ITEM_FIGHTER_5D;
#else
    return o==partner ? ITEM_HUMAN_71 : ITEM_HUMAN_70;
#endif
}
const void *port_shared_target(const void *actor,const void *target)
{
    const struct Object *a=actor,*t=target,*p=partner,*h=critdata ? critdata+1 : NULL;
    int hx,hy,px,py;
    if(!p || (t!=p && t!=h))return target;
    hx=(int)OBJ_HOMEX(a)-(int)OBJ_HOMEX(h);hy=(int)OBJ_HOMEY(a)-(int)OBJ_HOMEY(h);
    px=(int)OBJ_HOMEX(a)-(int)OBJ_HOMEX(p);py=(int)OBJ_HOMEY(a)-(int)OBJ_HOMEY(p);
    return p->hp && px*px+py*py<hx*hx+hy*hy ? p : h;
}
static void state(struct seat_state *s,int save)
{
#define FIELD(member,global) if(save)s->member=(global);else (global)=s->member
    if(save){s->physics=PN;s->handler=PT;s->eye=UsPtr;memcpy(s->fps,pFPS,sizeof s->fps);memcpy(s->mod,playerMod,sizeof s->mod);
        memcpy(s->camera_pos,campos,sizeof s->camera_pos);memcpy(s->camera_angle,camang,sizeof s->camera_angle);}
    else{PN=s->physics;PT=s->handler;UsPtr=s->eye;memcpy(pFPS,s->fps,sizeof s->fps);memcpy(playerMod,s->mod,sizeof s->mod);
        memcpy(campos,s->camera_pos,sizeof s->camera_pos);memcpy(camang,s->camera_angle,sizeof s->camera_angle);}
    FIELD(sq,GrSq);FIELD(facing,PlayerFacing);FIELD(heading,PlayerHeading);FIELD(pitch,PlayerPitch);FIELD(bank,PlayerBank);
    FIELD(input,PlayerInput);FIELD(turn,PlayerTurn);FIELD(forward,ForwInpRate);FIELD(turnrate,TurnInpRate);
    FIELD(pturn,pTurn);FIELD(maxacc,MaxPlayerAccel);FIELD(lastterr,lastTerr);FIELD(motype,plyMoType);
    FIELD(motion,motionbits);FIELD(update,fiz_update);FIELD(mouse,pmouseHandled);FIELD(keyboard,KeybUsed);FIELD(bob,doMod);
    FIELD(movement,player->motion_state);FIELD(fpsmode,player->fps);FIELD(swim,player->swim_count);
#if COOP_GAME == 1
    FIELD(destination_x,NewPlayerX);FIELD(destination_y,NewPlayerY);FIELD(destination_level,NewPlayerLevel);
    FIELD(destination_fade,NewPlyFade);FIELD(destination_fn,npp_func);
    character_state(s==&seats[1],save);
#endif
#undef FIELD
}
static void seat(int next)
{
    int old=atomic_load(&active_seat);if(next==old)return;
    state(&seats[old],1);
    /* UW2 still uses its original party character. UW1 owns two inventories. */
#if COOP_GAME != 1
    if(partner){
        if(old==1)critdata[1].ol=partner->ol;
        else partner->ol=critdata[1].ol;
    }
#else
    campaign_copy(&characters[next].record,&characters[old].record);
#endif
    ThePlayer=next==1 ? partner : critdata+1;UsPtr=ThePlayer;
    state(&seats[next],0);PN.index=Obj_MemTPtr(ThePlayer);
    atomic_store(&active_seat,next);
    port_shared_keyboard_seat(next);port_shared_mouse_seat(next);port_shared_input_seat(next);
    { int x,y;pthread_mutex_lock(&shared_lock);x=inputs[next].x;y=inputs[next].y;pthread_mutex_unlock(&shared_lock);
      mouse_putxy(x,199-y); }
    editchng(0x7FFE);
#if COOP_GAME == 1
    character_art();
#endif
}
void port_shared_reset(void)
{
    unsigned char *mob;
    resume_guest=0;
    if(!enabled || !atomic_load(&connected) || !in_game)companion_health=-1;
    if(!enabled || !partner)return;
#if COOP_GAME == 1
    character_detach();
#endif
    if(atomic_load(&connected) && in_game)companion_health=partner->hp ? partner->hp : -1;
    if(atomic_load(&active_seat))seat(0);
    if(partner_level==PlayerLevel && mapdata && seats[1].sq>=0 && seats[1].sq<MAP_TILES)
        Obj_Rem(&mapdata[seats[1].sq].objects,partner);
    for(mob=ActiveMob;mob<LastActiveMob;mob++)
        if(OBJ_GTARG(&critdata[*mob])==Obj_MemTPtr(partner))SET_GTARG(&critdata[*mob],1);
    partner->ol.word=0;Obj_Free(partner);partner=NULL;test_door=NULL;
    fprintf(stderr,"%s: shared-world partner removed\n",PORT_NAME);
}
void port_shared_before_save(void)
{
    int preserve=enabled && partner!=NULL;
    if(preserve) {
        if(atomic_load(&active_seat)==1)state(&seats[1],1);
        saved_guest=seats[1];saved_guest_object=*partner;
        resume_level=PlayerLevel;resume_eye=saved_guest.eye==partner;
    }
    port_shared_reset();resume_guest=preserve;
#if COOP_GAME == 1
    if(enabled && character_ready && !character_write())port_fatal("Cannot write the co-op character save");
#endif
}
static int spawn(void)
{
    int16 x,y;
    int resume=resume_guest && resume_level==PlayerLevel;
#if COOP_GAME == 1
    int keep_position=resume || (character_image_length && character_level==PlayerLevel);
    struct Phys placement;int16 facing;
#endif
    port_shared_keyboard_seat(-1);port_shared_mouse_seat(-1);port_shared_input_seat(-1);
    state(&seats[0],1);partner=Obj_Alloc(1);if(!partner)return 0;
    *partner=resume ? saved_guest_object : *ThePlayer;partner->qn.word=0;partner->ol.word=0;
#if COOP_GAME != 1
    if(companion_health>=0)partner->hp=(unsigned char)(companion_health>playerdat->avghit ? playerdat->avghit : companion_health);
#else
    character_prepare();
#endif
    free_critter(Obj_MemTPtr(partner));
    seats[1]=resume ? saved_guest : seats[0];if(!resume || resume_eye)seats[1].eye=partner;
    seats[1].sq=-1;seats[1].input=PIN_NONE;seats[1].forward=seats[1].turnrate=0;
    seats[1].physics.speed=0;memset(seats[1].physics.vel,0,sizeof seats[1].physics.vel);
    partner_level=PlayerLevel;seat(1);
#if COOP_GAME == 1
    character_join();
    placement=PN;facing=PlayerFacing;
#endif
    if(!find_good_x_and_y(partner,PN.x/256,PN.y/256+
#if COOP_GAME == 1
       (keep_position ? 0 : 1),
#else
       (resume ? 0 : 1),
#endif
       &x,&y,0) &&
       !find_good_x_and_y(partner,PN.x/256,PN.y/256,&x,&y,1)) {seat(0);Obj_Free(partner);partner=NULL;return 0;}
    player_setup(x,y,1);PN.index=Obj_MemTPtr(partner);
#if COOP_GAME == 1
    if(keep_position && x==placement.x/256 && y==placement.y/256) {
        PN=placement;PN.index=Obj_MemTPtr(partner);
#else
    if(resume && x==saved_guest.physics.x/256 && y==saved_guest.physics.y/256) {
        PN=saved_guest.physics;PN.index=Obj_MemTPtr(partner);
#endif
        SET_FINEX_UNSIGNED(partner,(PN.x>>5)&7);SET_FINEY(partner,(PN.y>>5)&7);SET_Z(partner,PN.z>>3);
    }
    PlayerFacing=
#if COOP_GAME == 1
        keep_position ? facing :
#else
        resume ? saved_guest.facing :
#endif
        (int16)(seats[0].facing+0x8000);PlayerHeading=PlayerFacing;
    resume_guest=0;
    guest_time=*Time;state(&seats[1],1);seat(0);
    if(getenv("UW_COOP_TEST_INTERACTION")) {
        static int installed;
        if(!installed) {
            struct Tile *tile=&mapdata[32+4*64];
            extern struct Object *CreateObj(int item,char mobile);
            tile->type=1;tile->height=mapdata[32+3*64].height;
            test_door=CreateObj(0x140,0);
            if(test_door) { SET_Z(test_door,tile->height<<3);SET_HEADING(test_door,0);Obj_Add(&tile->objects,test_door);tile->door|=2; }
            seats[1].facing=seats[1].heading=0;installed=1;
            fprintf(stderr,"SHARED TEST DOOR index=%d tile=32,4 floor=%u type=%u\n",Obj_MemTPtr(test_door),tile->height,tile->type);
        }
    }
    fprintf(stderr,"%s: shared-world second avatar %d spawned on level %d at %d,%d\n",PORT_NAME,Obj_MemTPtr(partner),PlayerLevel,x,y);
    return 1;
}
void port_shared_guest_physics(void)
{
#if COOP_GAME == 1
    extern unsigned char tsteps;
#endif
    uint32_t delta=*Time-guest_time;guest_time=*Time;if(delta>64)delta=64;
    if(!delta)return;
#if COOP_GAME == 1
    doMod=0;playerMod[0]=0;tsteps+=(unsigned char)delta;
#endif
    if(PlayerInput==PIN_NONE)do_player_keyboard();
    if(PlayerInput!=PIN_NONE || PN.speed || PN.vel[2] || PN.acc[2] || fiz_update)move_player((int)delta);
#if COOP_GAME == 1
    if(player->motion_state)parse_effect();
    make_noise(0);
#endif
}
static void capture(int which)
{
    unsigned char frame[SHARED_FRAME],pixels[640*480];int w,h;
    vga_scanout_now(pixels,&w,&h,frame+8);
    if(w!=320 || h!=200)return;
    memcpy(frame+8+768,pixels,SHARED_PIXELS);
    put16(frame,w);put16(frame+2,h);frame[4]=(unsigned char)port_shared_mouse_look();
    frame[5]=(unsigned char)(partner!=NULL);frame[6]=(unsigned char)(story_view>0 || modal_owner>=0);frame[7]=0;
    pthread_mutex_lock(&shared_lock);memcpy(pictures[which],frame,sizeof frame);
    picture_valid[which]=1;picture_generation[which]++;pthread_mutex_unlock(&shared_lock);
}
#if COOP_GAME == 1
static void bot_snapshot(void)
{
    const char *home=getenv("UW_COOP_BOT_HOME");char path[1024];unsigned char frame[SHARED_FRAME];
    if(!home)return;
    pthread_mutex_lock(&shared_lock);memcpy(frame,pictures[0],sizeof frame);pthread_mutex_unlock(&shared_lock);
    snprintf(path,sizeof path,"%s/view.png",home);plat_write_png(path,frame+8+768,320,200,frame+8);
}
#endif
void port_shared_flip(void)
{
    if(enabled && worker_started && !atomic_load(&stop_worker) && port_coop_role()==COOP_HOST) {
        int which=atomic_load(&active_seat);capture(which);
        if(story_view || modal_owner>=0){
            pthread_mutex_lock(&shared_lock);memcpy(pictures[1-which],pictures[which],SHARED_FRAME);
            picture_valid[1-which]=picture_valid[which];picture_generation[1-which]++;pthread_mutex_unlock(&shared_lock);
        }
    }
}
void port_shared_modal(int mode)
{
    if(!enabled)return;
    modal_owner=mode==4 ? atomic_load(&active_seat) : -1;
    if(mode==4 && getenv("UW_COOP_TEST_TRACE"))fprintf(stderr,"SHARED MODAL owner=%d\n",modal_owner);
}
void port_shared_story(int showing)
{
    if(!enabled)return;
    /* The ending wraps a cutscene and the win pictures. Nested scenes must not
       turn off the outer shared view when their own animation finishes. */
    if(showing)story_view++;
    else if(story_view)story_view--;
}
int port_shared_step(void)
{
    if(!enabled || port_coop_role()!=COOP_HOST || in_step)return 0;
    if(atomic_load(&stop_worker)){notdone=0;return 1;}
    if(!atomic_load(&connected)){port_shared_reset();return 0;}
    if(!player || GrSq<0 || scrnum<0 || scrnum>2)return 0;
    if(!partner && !spawn())return 0;
    if(modal_owner>=0 && scrnum==2) {
        int owner=modal_owner;in_step=1;atomic_store(&paired,1);seat(owner);
        port_shared_poll_input();do_changes();input_dispatch(inplist);capture(owner);
        pthread_mutex_lock(&shared_lock);memcpy(pictures[1-owner],pictures[owner],SHARED_FRAME);
        picture_valid[1-owner]=picture_valid[owner];picture_generation[1-owner]++;pthread_mutex_unlock(&shared_lock);
        seat(0);atomic_store(&paired,0);in_step=0;return 1;
    }
    in_step=1;atomic_store(&paired,1);seat(0);
#if COOP_GAME == 1
    bot_can_save=1;bot_input();bot_can_save=0;
#endif
    port_shared_poll_input();do_changes();input_dispatch(inplist);capture(0);
#if COOP_GAME == 1
    bot_attack(0);
    bot_targets(0);
#endif
    if(partner && partner_level==PlayerLevel && (ThePlayer->hp || partner->hp)) {
        mouse_hide();seat(1);port_shared_poll_input();
        do_changes();input_dispatch(inplist);capture(1);
#if COOP_GAME == 1
        bot_attack(1);
        bot_targets(1);
#endif
        seat(0);mouse_show();
#if COOP_GAME == 1
        character_travel();
#endif
    } else {
        /* Party menus and maps are shared; the owner of a modal action keeps control. */
        pthread_mutex_lock(&shared_lock);memcpy(pictures[1],pictures[0],SHARED_FRAME);
        picture_valid[1]=picture_valid[0];picture_generation[1]++;pthread_mutex_unlock(&shared_lock);
    }
    if(getenv("UW_COOP_TEST_TRACE") && partner) {
        static uint64_t next;
        uint64_t now=shared_now();
        if(now>=next){next=now+1000;fprintf(stderr,"SHARED TRACE clock=%lu host=%d,%d,%d guest=%d,%d,%d hp=%u,%u\n",
            (unsigned long)player->game_clock,seats[0].physics.x,seats[0].physics.y,seats[0].facing,
            seats[1].physics.x,seats[1].physics.y,seats[1].facing,critdata[1].hp,partner->hp);
            if(test_door)fprintf(stderr,"SHARED TEST DOOR item=%u\n",OBJ_ITEM(test_door));}
    }
#if COOP_GAME == 1
    character_test();
    campaign_test();
    bot_state();
#endif
    if(getenv("UW_COOP_TEST_TRANSITIONS")) {
        static uint64_t began;static int saved,travelled;
        uint64_t now=shared_now();
        extern char SaveLevel(int level),ChangeLevel(int from,int to);
        if(!began)began=now;
        if(!saved && now-began>=2500) {
            int x=PN.x/256,y=PN.y/256;
            if(partner)partner->hp=12;
            if(!SaveLevel(PlayerLevel))port_fatal("shared test: save failed");
            player_setup(x,y,1);saved=1;fprintf(stderr,"SHARED TEST SAVE passed\n");
        }
        if(!travelled && now-began>=4500) {
            int16 x,y;int tx,ty,found=0;
            if(!partner || partner->hp!=12)port_fatal("shared test: save lost companion health");
            fprintf(stderr,"SHARED TEST SAVE companion health preserved\n");
            if(!ChangeLevel(PlayerLevel,2))port_fatal("shared test: transition failed");
            PlayerLevel=2;
            for(ty=1;ty<63 && !found;ty++)for(tx=1;tx<63;tx++)
                if(mapdata[tx+ty*64].type==1 && find_good_x_and_y(ThePlayer,tx,ty,&x,&y,0)){found=1;break;}
            if(!found)port_fatal("shared test: no arrival tile");
            player_setup(x,y,1);travelled=1;fprintf(stderr,"SHARED TEST TRANSITION level=2 passed\n");
        }
    }
    atomic_store(&paired,0);in_step=0;return 1;
}
void port_shared_key(uint8_t code)
{
    if(!enabled){kbd_byte(code);return;}
    pthread_mutex_lock(&shared_lock);
    if(port_coop_role()==COOP_JOIN) {
        if(code==0xE0 || code==0xE1)client_prefix=code==0xE0;
        else{client_held[(code&127)+(client_prefix ? 128 : 0)]=(code&128)==0;client_prefix=0;}
    }
    if(inputs[0].count<sizeof inputs[0].keys)inputs[0].keys[inputs[0].count++]=code;
    pthread_mutex_unlock(&shared_lock);
}
void port_shared_pointer(const PlatPointer *e)
{
    if(!enabled){mouse_event(e);return;}
    pthread_mutex_lock(&shared_lock);
    inputs[0].x=(int)e->x;inputs[0].y=(int)e->y;inputs[0].dx+=(int)e->dx;inputs[0].dy+=(int)e->dy;
    inputs[0].buttons=(int)(e->buttons&3);inputs[0].absolute=e->absolute;
    if(e->type==PLAT_POINTER_DOWN && inputs[0].nclicks<32) {
        unsigned n=inputs[0].nclicks++;inputs[0].clicks[n].x=(int)e->x;inputs[0].clicks[n].y=(int)e->y;
        inputs[0].clicks[n].buttons=(int)(e->buttons&3);
    }
    pthread_mutex_unlock(&shared_lock);
}
void port_shared_poll_input(void)
{
    PlatPointer e;unsigned char keys[512];unsigned count,i;int s;
    if(!enabled || port_coop_role()!=COOP_HOST)return;
#if COOP_GAME == 1
    bot_input();
#endif
#if COOP_GAME == 1
    if(atomic_load(&stop_worker) || (character_creating && !atomic_load(&connected))){kbd_byte(0x01);kbd_byte(0x81);}
#endif
    s=atomic_load(&active_seat);memset(&e,0,sizeof e);
    pthread_mutex_lock(&shared_lock);count=inputs[s].count>16 ? 16 : inputs[s].count;
    memcpy(keys,inputs[s].keys,count);memmove(inputs[s].keys,inputs[s].keys+count,inputs[s].count-count);inputs[s].count-=count;
    e.x=(float)inputs[s].x;e.y=(float)inputs[s].y;e.dx=(float)inputs[s].dx;e.dy=(float)inputs[s].dy;
    e.buttons=(unsigned)inputs[s].buttons;e.absolute=inputs[s].absolute;
    inputs[s].dx=inputs[s].dy=0;pthread_mutex_unlock(&shared_lock);
    for(i=0;i<count;i++)kbd_byte(keys[i]);mouse_event(&e);
#if COOP_GAME == 1
    if(story_view || modal_owner>=0)bot_state();
#endif
    if(story_view || modal_owner>=0) {
        static uint64_t next;
        uint64_t now=shared_now();
        /* Some native menus and win pictures draw directly or use soft page
           flips. Capture on the game thread while they wait for input, too. */
        if(now>=next){next=now+30;port_shared_flip();}
    }
}
int port_shared_click(int16_t *x,int16_t *y)
{
    int s,code=-1;if(!enabled || port_coop_role()!=COOP_HOST)return -1;
    s=atomic_load(&active_seat);pthread_mutex_lock(&shared_lock);
    if(inputs[s].nclicks) {
        code=inputs[s].clicks[0].buttons;*x=(int16_t)inputs[s].clicks[0].x;*y=(int16_t)(199-inputs[s].clicks[0].y);
        inputs[s].nclicks--;memmove(inputs[s].clicks,inputs[s].clicks+1,inputs[s].nclicks*sizeof inputs[s].clicks[0]);
    }
    pthread_mutex_unlock(&shared_lock);return code;
}
static void received(int type,const unsigned char *p,size_t length,void *unused)
{
    (void)unused;(void)length;
    pthread_mutex_lock(&shared_lock);
    if(type==SHARED_PICTURE) {
        if(get16(p)==320 && get16(p+2)==200 && p[4]<=1 && p[5]<=1 && p[6]<=1 && !p[7]) {
            memcpy(pictures[0],p,SHARED_FRAME);picture_valid[0]=1;picture_generation[0]++;
            if(getenv("UW_COOP_TEST_TRACE") && p[6]) {
                static unsigned story_frames;
                story_frames++;
                if(story_frames==1 || story_frames%100==0)fprintf(stderr,"SHARED STORY displayed frames=%u\n",story_frames);
            }
        }
    } else if(type==SHARED_AUDIO && get16(p)==44100 && get16(p+2)<=1024) {
        unsigned i,count=(unsigned)get16(p+2)*2;
        for(i=0;i<count;i++) {
            if(sound_count==16384){sound_read=(sound_read+1)%16384;sound_count--;}
            sound_ring[sound_write]=(int16_t)get16(p+4+i*2);sound_write=(sound_write+1)%16384;sound_count++;
            if((int16_t)get16(p+4+i*2)!=0)sound_nonzero++;
        }
        if(getenv("UW_COOP_TEST_AUDIO") && sound_nonzero && sound_nonzero<=count)
            fprintf(stderr,"SHARED AUDIO received nonzero=%u\n",sound_nonzero);
    } else if(type==SHARED_KEYS && p[268]<=1 && p[269]<=3 && p[270]<=3 && !p[271] && !p[264] && !p[265] && !p[266] && !p[267]) {
        unsigned count=p[0];
        if(count<=sizeof inputs[1].keys-inputs[1].count) {
            memcpy(inputs[1].keys+inputs[1].count,p+1,count);inputs[1].count+=count;
        }
        inputs[1].x=get16(p+256);inputs[1].y=get16(p+258);
        inputs[1].dx+=(int16_t)get16(p+260);inputs[1].dy+=(int16_t)get16(p+262);
        inputs[1].absolute=p[268];inputs[1].buttons=p[269];
        if(inputs[1].x>319)inputs[1].x=319;if(inputs[1].y>199)inputs[1].y=199;
        if(p[270] && inputs[1].nclicks<32) {
            unsigned n=inputs[1].nclicks++;inputs[1].clicks[n].x=inputs[1].x;inputs[1].clicks[n].y=inputs[1].y;
            inputs[1].clicks[n].buttons=p[270];
        }
    }
    pthread_mutex_unlock(&shared_lock);
}
static void *network(void *unused)
{
    uint64_t last=0,serial=0;int before=0;(void)unused;
    while(!atomic_load(&stop_worker)) {
        uint64_t now=shared_now();shared_tick(&transport,now,received,NULL);
        atomic_store(&connected,transport.ready);
        if(before!=transport.ready) {
            fprintf(stderr,"%s: shared world %s\n",PORT_NAME,transport.ready ? "CONNECTED" : "DISCONNECTED - waiting for host/partner");
            if(!transport.ready){
                pthread_mutex_lock(&shared_lock);memset(&inputs[1],0,sizeof inputs[1]);
                if(transport.role==COOP_JOIN){inputs[0].count=inputs[0].nclicks=0;inputs[0].buttons=inputs[0].dx=inputs[0].dy=0;}
                picture_valid[1]=0;sound_read=sound_write=sound_count=0;pthread_mutex_unlock(&shared_lock);serial=0;
            }
            before=transport.ready;
        }
        if(transport.ready && !transport.queued && transport.role==COOP_HOST) {
            pthread_mutex_lock(&shared_lock);
            if(sound_count>=2048) {
                unsigned char data[SHARED_SOUND];unsigned i;
                put16(data,44100);put16(data+2,1024);
                for(i=0;i<2048;i++){put16(data+4+i*2,sound_ring[sound_read]);sound_read=(sound_read+1)%16384;sound_count--;}
                shared_send(&transport,SHARED_AUDIO,data,sizeof data);
            }
            pthread_mutex_unlock(&shared_lock);
        }
        if(transport.ready && !transport.queued && now-last>=30) {
            if(transport.role==COOP_HOST) {
                pthread_mutex_lock(&shared_lock);
                if(picture_valid[1] && serial!=picture_generation[1]) {
                    if(shared_send(&transport,SHARED_PICTURE,pictures[1],SHARED_FRAME)>0){serial=picture_generation[1];last=now;}
                }
                pthread_mutex_unlock(&shared_lock);
            } else {
                unsigned char input[SHARED_INPUT];unsigned count;
                memset(input,0,sizeof input);pthread_mutex_lock(&shared_lock);
                count=inputs[0].count>255 ? 255 : inputs[0].count;input[0]=(unsigned char)count;memcpy(input+1,inputs[0].keys,count);
                memmove(inputs[0].keys,inputs[0].keys+count,inputs[0].count-count);inputs[0].count-=count;
                put16(input+256,inputs[0].x);put16(input+258,inputs[0].y);put16(input+260,inputs[0].dx);put16(input+262,inputs[0].dy);
                input[268]=(unsigned char)inputs[0].absolute;input[269]=(unsigned char)inputs[0].buttons;
                if(inputs[0].nclicks) {
                    put16(input+256,inputs[0].clicks[0].x);put16(input+258,inputs[0].clicks[0].y);
                    input[270]=(unsigned char)inputs[0].clicks[0].buttons;inputs[0].nclicks--;
                    memmove(inputs[0].clicks,inputs[0].clicks+1,inputs[0].nclicks*sizeof inputs[0].clicks[0]);
                }
                inputs[0].dx=inputs[0].dy=0;pthread_mutex_unlock(&shared_lock);
                shared_send(&transport,SHARED_KEYS,input,sizeof input);last=now;
            }
        }
        plat_sleep_ns(2000000);
    }
    shared_close(&transport);return NULL;
}
static void shutdown_shared(void)
{
    if(!worker_started)return;atomic_store(&stop_worker,1);pthread_join(worker,NULL);worker_started=0;
}
void port_shared_config(const PlatConfig *config)
{
    if(enabled && config->exit_after_ms>0)stop_deadline=shared_now()+(uint64_t)config->exit_after_ms-100;
    if(getenv("UW_COOP_TEST_TRACE"))fprintf(stderr,"SHARED STOP configured %ld ms\n",config->exit_after_ms);
}
void port_shared_finished(void) {atomic_store(&game_finished,1);}
void port_shared_quit(void)
{
    uint64_t end;
    if(!enabled)return;
    if(getenv("UW_COOP_TEST_TRACE"))fprintf(stderr,"SHARED STOP requested\n");
    atomic_store(&stop_worker,1);end=shared_now()+1500;
    while(!atomic_load(&game_finished) && shared_now()<end)plat_sleep_ns(2000000);
    shutdown_shared();
    if(getenv("UW_COOP_TEST_TRACE"))fprintf(stderr,"SHARED STOP completed game=%d\n",atomic_load(&game_finished));
}
void port_shared_tick(void)
{
    inscript_tick();
    if(stop_deadline && shared_now()>=stop_deadline){stop_deadline=0;port_shared_quit();}
}
int port_shared_start(void)
{
#if COOP_GAME == 1
    extern void port_shared_crash_install(void);
    port_shared_crash_install();
#endif
#ifdef __EMSCRIPTEN__
    return -1;
#else
    if(shared_open(&transport,port_coop_role(),COOP_GAME,port_coop_endpoint()))return -1;
    inputs[0].x=inputs[1].x=160;inputs[0].y=inputs[1].y=100;
    inputs[0].absolute=inputs[1].absolute=1;
    if(pthread_create(&worker,NULL,network,NULL)){shared_close(&transport);return -1;}
    worker_started=1;atexit(shutdown_shared);
#if COOP_GAME == 1
    fprintf(stderr,"%s: shared world: independent characters and inventories; campaign quests are shared\n",PORT_NAME);
#else
    fprintf(stderr,"%s: shared world: host owns simulation; party inventory and quests are shared\n",PORT_NAME);
#endif
    return 0;
#endif
}
void port_shared_scanout(unsigned char *pixels,int *w,int *h,unsigned char palette[768])
{
    int world_ready=0;
    if(port_coop_role()==COOP_HOST) {
        if(!atomic_load(&paired) && !atomic_load(&stop_worker)){vga_scanout(pixels,w,h,palette);capture(0);if(!partner){
            pthread_mutex_lock(&shared_lock);memcpy(pictures[1],pictures[0],SHARED_FRAME);picture_valid[1]=picture_valid[0];picture_generation[1]++;pthread_mutex_unlock(&shared_lock);
        }port_coop_banner(pixels,*w,*h,palette,atomic_load(&connected) ? "COOP SHARED WORLD - HOST" : "COOP SHARED WORLD - WAITING FOR PARTNER");return;}
    }
    pthread_mutex_lock(&shared_lock);
    if(picture_valid[0]){world_ready=pictures[0][5];*w=320;*h=200;memcpy(palette,pictures[0]+8,768);memcpy(pixels,pictures[0]+8+768,SHARED_PIXELS);}
    else{*w=320;*h=200;memset(pixels,0,SHARED_PIXELS);memset(palette,0,768);}
    pthread_mutex_unlock(&shared_lock);
    port_coop_banner(pixels,*w,*h,palette,port_coop_role()==COOP_HOST ? "COOP SHARED WORLD - HOST" :
        !atomic_load(&connected) ? "COOP SHARED WORLD - CONNECTING TO HOST" :
        world_ready ? "COOP SHARED WORLD - PLAYER TWO" : "COOP CONNECTED - WAITING FOR HOST TO START GAME");
}
static int remote_game(void *unused)
{
    (void)unused;
#ifdef _WIN32
    { extern int __real_plat_audio_open(int,void (*)(int16_t*,int));
      extern void port_shared_audio_play(int16_t*,int);
      __real_plat_audio_open(44100,port_shared_audio_play); }
#endif
    while(!atomic_load(&stop_worker)) {
        plat_sleep_ns(20000000);
    }
    port_shared_finished();
    return 0;
}
static void remote_lifecycle(int event) { if(event==PLAT_QUIT_REQUEST)port_shared_quit(); }
static void remote_tick(void)
{
    static int settings_was;
    int showing=settings_open(),look;
    if(showing && !settings_was) {
        unsigned char releases[512];unsigned count=0,i;
        pthread_mutex_lock(&shared_lock);
        for(i=0;i<256;i++)if(client_held[i]){if(i>=128)releases[count++]=0xE0;releases[count++]=(unsigned char)((i&127)|128);}
        inputs[0].buttons=0;inputs[0].nclicks=0;pthread_mutex_unlock(&shared_lock);
        for(i=0;i<count;i++)port_shared_key(releases[i]);
    }
    settings_was=showing;
#if COOP_GAME == 1
    bot_input();
#endif
    pthread_mutex_lock(&shared_lock);look=!showing && atomic_load(&connected) && picture_valid[0] && pictures[0][4];pthread_mutex_unlock(&shared_lock);
    plat_pointer_capture(look);port_shared_tick();
}
int port_shared_client_run(const PlatConfig *config)
{
    PlatHooks hooks;int result;port_shared_config(config);if(port_shared_start())return 2;
    memset(&hooks,0,sizeof hooks);hooks.scanout=port_shared_scanout;hooks.key=port_shared_key;
    hooks.pointer=port_shared_pointer;hooks.lifecycle=remote_lifecycle;hooks.tick=remote_tick;
    pit_start();
    result=plat_run(config,&hooks,remote_game,NULL);shutdown_shared();return result;
}
int port_shared_test_start(void) { return enabled && getenv("UW_COOP_TEST_START")!=NULL; }

void port_shared_audio_play(int16_t *samples,int frames)
{
    int i;pthread_mutex_lock(&shared_lock);
    for(i=0;i<frames*2;i++) {
        samples[i]=sound_count ? sound_ring[sound_read] : 0;
        if(sound_count){sound_read=(sound_read+1)%16384;sound_count--;}
    }
    pthread_mutex_unlock(&shared_lock);
}
#ifdef _WIN32
static void (*sound_source)(int16_t*,int);
int __real_plat_audio_open(int rate,void (*fill)(int16_t*,int));
static void sound_tee(int16_t *samples,int frames)
{
    int i;sound_source(samples,frames);
    if(!atomic_load(&connected))return;
    pthread_mutex_lock(&shared_lock);
    for(i=0;i<frames*2;i++) {
        if(sound_count==16384){sound_read=(sound_read+1)%16384;sound_count--;}
        sound_ring[sound_write]=samples[i];sound_write=(sound_write+1)%16384;sound_count++;
    }
    pthread_mutex_unlock(&shared_lock);
}
int __wrap_plat_audio_open(int rate,void (*fill)(int16_t*,int))
{
    if(enabled && port_coop_role()==COOP_HOST && rate==44100){sound_source=fill;return __real_plat_audio_open(rate,sound_tee);}
    return __real_plat_audio_open(rate,fill);
}
#endif
