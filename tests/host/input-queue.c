/* GPL-3.0-or-later WITH the Madeira Converter Exception, version 1. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define WINIOS_RING_SIZE 256
#define WINIOS_EV_MOUSE 0
#define WINIOS_EV_KEY 1
#define KEYEVENTF_KEYUP 2
typedef struct { unsigned type; int x,y; unsigned flags,data; } winios_input_event_t;
static struct { winios_input_event_t buf[256]; unsigned head,tail; int lock; } g_input_q;
static double now;
static const char *mouse_setting, *key_setting;
static unsigned count, checks;
static int locked, keys[256], left;
static winios_input_event_t delivered[512];
static double delivered_at[512];
static const char *mock_getenv(const char *name) { return strcmp(name,"MADEIRA_KEY_HOLD_MS") ? mouse_setting : key_setting; }
static void pthread_mutex_lock(int *lock) { assert(lock==&g_input_q.lock && !locked); locked=1; }
static void pthread_mutex_unlock(int *lock) { assert(lock==&g_input_q.lock && locked); locked=0; }
static double CACurrentMediaTime(void) { return now; }
static long mock_write(int fd,const void *p,unsigned long n) { assert(fd==2 && p && !locked); return (long)n; }
static void record(winios_input_event_t event) { assert(!locked && count<512); delivered[count]=event; delivered_at[count++]=now; }
static void winios_drv_post_key(unsigned short vk,unsigned flags) {
    record((winios_input_event_t){1,vk,0,flags,0}); if(vk<256)keys[vk]=!(flags&2);
}
static void winios_drv_post_mouse(int x,int y,unsigned flags,unsigned data,void *window) {
    assert(!window); record((winios_input_event_t){0,x,y,flags,data});
    if(flags&2)left=1; if(flags&4)left=0;
}
#define getenv mock_getenv
#define write mock_write
#include "MadeiraClickQueue.h"

static void reset(const char *mouse,const char *key) {
    assert(!locked); memset(&g_input_q,0,sizeof(g_input_q)); memset(&click_state,0,sizeof(click_state));
    memset(keys,0,sizeof(keys)); count=0; now=100; left=0; mouse_setting=mouse; key_setting=key;
}
static void add(unsigned type,int value,unsigned flags) {
    unsigned h=g_input_q.head; assert((h+1)%256!=g_input_q.tail);
    g_input_q.buf[h]=(winios_input_event_t){type,value,0,flags,0}; g_input_q.head=(h+1)%256;
}
static void poll(double seconds) { now+=seconds; madeira_click_drain(); assert(!locked); }
#define CHECK(condition) do { assert(condition); checks++; } while(0)
int main(void) {
    reset(0,0);add(0,0,2);add(0,0,4);poll(0);
    CHECK(count==1 && left);poll(.079);CHECK(count==1);poll(.002);CHECK(count==2 && !left);
    reset(0,0);add(1,65,0);add(1,65,2);poll(0);CHECK(count==1 && keys[65]);
    poll(.079);CHECK(count==1);poll(.002);CHECK(count==2 && !keys[65]);
    CHECK(delivered_at[1]-delivered_at[0]>=.080);
    reset(0,0);add(1,16,0);add(1,65,0);add(1,65,2);add(1,16,2);poll(0);
    CHECK(count==2 && keys[16] && keys[65]);poll(.081);
    CHECK(count==4 && !keys[16] && !keys[65] && delivered[2].x==65 && delivered[3].x==16);
    reset(0,0);for(int i=0;i<2;i++){add(1,27,0);add(1,27,2);}poll(0);poll(.081);
    CHECK(count==2 && !keys[27]);poll(.020);CHECK(count==2);poll(.011);CHECK(count==3 && keys[27]);
    poll(.081);CHECK(count==4 && !keys[27]);
    reset(0,0);add(1,87,0);poll(0);add(1,87,2);poll(.5);CHECK(count==2 && !keys[87]);
    reset(0,0);add(1,65,0);poll(0);add(1,65,0);poll(.07);add(1,65,2);poll(.02);CHECK(count==3 && !keys[65]);
    reset(0,0);add(1,38,1);add(1,38,3);poll(0);poll(.081);CHECK(count==2 && delivered[1].flags==3);
    reset(0,0);add(1,65,2);poll(0);CHECK(count==1);add(1,256,0);poll(0);CHECK(count==2);
    reset("0",0);add(0,0,2);add(0,0,4);add(1,65,0);add(1,65,2);poll(0);CHECK(count==3 && keys[65] && !left);
    reset(0,"0");add(1,65,0);add(1,65,2);poll(0);CHECK(count==2 && !keys[65]);
    const char *settings[]={"16","120","250","bad","99999999999999999","-1"};
    const double delays[]={.016,.120,.250,.080,.080,.080};
    for(unsigned i=0;i<6;i++) {
        reset(0,settings[i]);add(1,65,0);add(1,65,2);poll(0);CHECK(count==1);
        poll(delays[i]+.001);CHECK(count==2 && !keys[65]);
    }
    reset(0,0);g_input_q.head=g_input_q.tail=255;add(1,65,0);add(1,65,2);poll(0);poll(.081);
    CHECK(count==2 && g_input_q.tail==1);
    reset(0,0);click_state.draining=1;add(1,65,0);poll(0);CHECK(count==0 && click_state.draining);
    click_state.draining=0;poll(0);CHECK(count==1);
    reset(0,0);add(1,65,0);add(1,65,2);poll(0);poll(60);CHECK(count==2 && !keys[65]);
    for(unsigned fps=15;fps<=120;fps*=2) {
        reset(0,0);add(1,65,0);add(1,65,2);int seen=0;
        for(unsigned frame=0;frame<40;frame++){poll(frame?1.0/fps:0);seen|=keys[65];}
        CHECK(seen && !keys[65] && count==2);
    }
    reset(0,0);for(int vk=65;vk<70;vk++){add(1,vk,0);add(1,vk,2);}
    for(int i=0;i<80;i++)poll(1.0/120);CHECK(count==10);
    for(unsigned i=0;i<10;i++)CHECK(delivered[i].x==65+(int)(i/2) && delivered[i].flags==(i%2?2u:0u));
    printf("PASS: %u source queue checks (mock clock/OS, not an iPhone test)\n",checks);
    return 0;
}
