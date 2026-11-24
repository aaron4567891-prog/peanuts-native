// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real adapter with register frames and bounded guest-memory reads.
#include "pikachu_fps_stub/dm.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
module_t g_mod[1];
int g_nmod = 1;
unsigned timing_test_logs;
static int observed = 1, cap = 60;
static void (*callback)(uint64_t *, int);
static uint64_t durations[2] = {16666667, 33333334};
static float components[18][32];
static uint64_t capture_object[64],capture_config[208];
static uint64_t capture_event_fixture[0xf98/8];
static uint64_t gate_stack[2];
int nxhost_fps_limit_get(void) { return cap; }
int gate_obs_present(int id) { assert(id == OBS_LGP_FPS); return observed; }
void gate_obs_set(int id, void (*fn)(uint64_t *, int)) { assert(id == OBS_LGP_FPS); callback = fn; }
int plat_mem_read(uint64_t address, void *out, size_t size) {
    uintptr_t p = (uintptr_t)address, begin = (uintptr_t)durations, end = begin + sizeof durations;
    if (p >= begin && p <= end && size <= end - p) { memcpy(out, (void *)p, size); return 0; }
    begin = (uintptr_t)components; end = begin + sizeof components;
    if (p >= begin && p <= end && size <= end - p) { memcpy(out, (void *)p, size); return 0; }
    begin = (uintptr_t)capture_object; end = begin + sizeof capture_object;
    if (p >= begin && p <= end && size <= end - p) { memcpy(out, (void *)p, size); return 0; }
    begin = (uintptr_t)capture_config; end = begin + sizeof capture_config;
    if (p >= begin && p <= end && size <= end - p) { memcpy(out, (void *)p, size); return 0; }
    const struct { void *pointer; size_t size; } extra[] = {
        {gate_stack,sizeof gate_stack},
        {capture_event_fixture,sizeof capture_event_fixture}};
    for(unsigned i=0;i<sizeof extra/sizeof extra[0];++i) {
        begin=(uintptr_t)extra[i].pointer; end=begin+extra[i].size;
        if(p>=begin && p<=end && size<=end-p) { memcpy(out,(void*)p,size); return 0; }
    }
    return -1;
}
#include "../../../dm/peanuts/pikachu_fps.c"
static uint64_t regs[100]; // q0 occupies bytes 256..271 in entry.S's preserved frame.
static void gate(int kind) { assert(callback); callback(regs, kind); }
static float turn(float value) {
    memcpy((uint8_t *)regs + 256, &value, sizeof value); gate(TURN);
    memcpy(&value, (uint8_t *)regs + 256, sizeof value); return value;
}
static void component(unsigned i) {
    components[i][28] = 2; components[i][29] = -4; components[i][30] = 8;
    regs[0] = (uintptr_t)components[i];
}
static float adjusted(int kind,unsigned q,float value) {
    fp_scalar_set(regs,q,value); gate(kind); return fp_scalar(regs,q);
}
static void stored_counter(uint32_t *memory,uint32_t next) {
    regs[19]=(uintptr_t)memory-0x78;
    regs[8]=next;
    gate(FIELD_AI_TICK);
    *memory=(uint32_t)regs[8];
}
static void all_scenes(void) {
    pn_lgp_fps_present(); assert(!world && pn_lgp_fps_interval()==1);
    regs[2]=1; regs[9]=(uintptr_t)durations; gate(STEP); assert(regs[9]==(uintptr_t)durations-8);
    component(0); components[0][12]=2; // title UI wrapper rate +0x30
    assert(adjusted(UI_STEP,0,2)==1 && adjusted(MODEL_TIME,1,1.f/30)==1.f/60);
    cap=30; assert(pn_lgp_fps_interval()==0);
    regs[9]=(uintptr_t)durations; gate(STEP); assert(regs[9]==(uintptr_t)durations);
    assert(adjusted(UI_STEP,0,2)==2);
    cap=60;
    gate(FIELD); pn_lgp_fps_present(); pn_lgp_fps_present();
    assert(scene_test && !world && pn_lgp_fps_interval()==1); // menu without a heartbeat
    regs[2]=1; regs[9]=(uintptr_t)durations; gate(STEP); assert(regs[9]==(uintptr_t)durations-8);
    component(0); components[0][12]=2; // wrapper rate +0x30
    assert(adjusted(UI_STEP,0,2)==1 && adjusted(UI_STEP,0,1)==1);
    assert(adjusted(UI_STEP,0,.75f)==.75f); // explicit playback scales are not guessed
    assert(adjusted(MODEL_TIME,1,1.f/30)==1.f/60 && adjusted(MODEL_TIME,1,1.f/60)==1.f/60);
    // Raw model time is authoritative; after the animation multiplies by speed, 2x must stay 2x.
    float raw=adjusted(MODEL_TIME,1,1.f/30), fast=raw*2;
    assert(raw==1.f/60 && fast==1.f/30 );
    double time30=0,time60=0;
    for(int i=0;i<30;i++) time30+=1.f/30;
    for(int i=0;i<60;i++) time60+=adjusted(MODEL_TIME,1,1.f/30);
    assert(fabs(time30-time60)<1e-6 && fabs(time60-1)<1e-6);
    gate(SPECIAL); pn_lgp_fps_present(); assert(pn_lgp_fps_interval()==1);
    regs[2]=1; regs[9]=(uintptr_t)durations; gate(STEP); assert(regs[9]==(uintptr_t)durations-8);
    {
        uint32_t *memory=(uint32_t*)&components[1][0]; *memory=0;
        for(int f=0;f<60;f++) { stored_counter(memory,*memory+1); pn_lgp_fps_present(); }
        assert(*memory==30u);
        if(!(present_serial&1)) pn_lgp_fps_present();
        *memory=7; stored_counter(memory,0); assert(*memory==0);
        *memory=7; stored_counter(memory,42); assert(*memory==42);
    }
    // Known heuristic boundary: old0 -> literal1 one-shot cannot be distinguished from ++.
    if(!(present_serial&1)) pn_lgp_fps_present();
    uint32_t *one_shot=(uint32_t*)&components[1][0]; *one_shot=0;
    stored_counter(one_shot,1); assert(*one_shot==0);
    // The held integer frame 30 gets two render opportunities but exactly one RNG equality hit.
    if(present_serial&1) pn_lgp_fps_present();
    uint32_t *clock=(uint32_t*)&components[2][0]; *clock=0; unsigned random_hits=0,seen_thirty=0;
    for(int f=0;f<60;f++) {
        stored_counter(clock,*clock+1);
        regs[8]=UINT64_C(0xdeadbeef00000000)|*clock;
        uint64_t original[100]; float guest[18][32]; memcpy(original,regs,sizeof original); memcpy(guest,components,sizeof guest);
        gate(RNG_COMPARE);
        if((uint32_t)regs[8]==30) ++random_hits;
        if(*clock==30) ++seen_thirty;
        uint64_t masked=regs[8]; regs[8]=original[8];
        assert(!memcmp(original,regs,sizeof original) && !memcmp(guest,components,sizeof guest)); regs[8]=masked;
        gate(RNG_RESTORE); assert(!memcmp(original,regs,sizeof original)); // timeout sees original clock, full register restored
        pn_lgp_fps_present();
    }
    assert(*clock==30 && seen_thirty==2 && random_hits==1);
    // A real/non-held frame 30 must remain eligible; an in-flight masked frame restores even after cap changes.
    if(present_serial&1) pn_lgp_fps_present();
    *clock=29; stored_counter(clock,30); regs[8]=30; gate(RNG_COMPARE); assert(regs[8]==30); gate(RNG_RESTORE);
    pn_lgp_fps_present(); stored_counter(clock,31); assert(*clock==30);
    regs[8]=UINT64_C(0x123456780000001e); gate(RNG_COMPARE); assert(regs[8]==UINT64_C(0x123456780000001f));
    cap=30; gate(RNG_RESTORE); assert(regs[8]==UINT64_C(0x123456780000001e)); cap=60;
    float phase30=0,phase60=0;
    for(int i=0;i<30;i++) phase30+=.125f;
    for(int i=0;i<60;i++) phase60+=adjusted(RING_STEP,0,.125f);
    assert(phase30==phase60);
    unsigned cycles30=0,cycles60=0; phase30=phase60=0;
    for(int i=0;i<120;i++) { phase30+=.125f; if(phase30>=1) { phase30-=1; ++cycles30; } }
    for(int i=0;i<240;i++) { phase60+=adjusted(RING_STEP,0,.125f); if(phase60>=1) { phase60-=1; ++cycles60; } }
    assert(cycles30==15 && cycles60==cycles30 && phase30==phase60);
    // Constant-acceleration semi-implicit Euler: same velocity/second; bounded positional discretization difference.
    double y30=0,y60=0,v30=1000,v60=1000; const float gravity=-32.66666794f;
    for(int i=0;i<30;i++) { v30+=gravity; y30+=v30*(1.f/30); }
    for(int i=0;i<60;i++) {
        float g[4]={0,gravity,0,0}; memcpy((uint8_t*)regs+272,g,sizeof g); regs[8]=0x3d088889;
        gate(PROJECTILE_STEP); memcpy(g,(uint8_t*)regs+272,sizeof g);
        assert(regs[8]==0x3c888889 && g[1]==gravity*.5f);
        float dt; uint32_t bits=(uint32_t)regs[8]; memcpy(&dt,&bits,4); v60+=g[1];
        gate(PROJECTILE_STEP); float again[4]; memcpy(again,(uint8_t*)regs+272,sizeof again);
        assert(regs[8]==0x3c888889 && !memcmp(g,again,sizeof g));
        float vertical=(float)v60;
        memcpy((uint8_t*)regs+260,&vertical,sizeof vertical);
        fp_scalar_set(regs,1,dt);
        gate(CAPTURE_FLIGHT_POSITION);
        memcpy(&vertical,(uint8_t*)regs+260,sizeof vertical);
        y60+=vertical*dt;
    }
    assert(fabs(v30-v60)<.001 && fabs(y30-y60)<.001);
    assert(adjusted(PROJECTILE_RATE,9,30)==60 && adjusted(PROJECTILE_RATE,9,60)==60);
    // The comparison includes frame zero at both rates.
    for(unsigned limit=0;limit<5;limit++) {
        regs[9]=limit;
        regs[10]=limit;
        gate(CAPTURE_HIT_STOP);
        assert(regs[9]==limit && (regs[10]+1)*30==(limit+1)*60);
    }
    regs[10]=UINT32_MAX;
    gate(CAPTURE_HIT_STOP);
    assert(regs[10]==UINT32_MAX);
    atomic_store(&capture_event_manager,(uintptr_t)capture_event_fixture);
    regs[8]=UINT32_C(0x3d088889);
    gate(CAPTURE_LAUNCH_STEP);
    assert(regs[8]==UINT32_C(0x3c888889));
    fp_scalar_set(regs,0,20);
    fp_scalar_set(regs,1,1.0f/60.0f);
    fp_scalar_set(regs,2,-4);
    gate(CAPTURE_LAUNCH_POSITION);
    assert(fp_scalar(regs,0)==18 && fp_scalar(regs,2)==-4);
    assert(adjusted(CAPTURE_ROLL_DURATION,0,40)==80);
    capture_event_fixture[0x3c0/8]=(uintptr_t)capture_object;
    gate_stack[1]=(uintptr_t)g_mod[0].base+0x80f6e8;
    regs[29]=(uintptr_t)gate_stack;
    regs[19]=(uintptr_t)capture_object;
    uint32_t timeline_ticks=0;
    for(unsigned frame=0;frame<60;frame++) {
        atomic_store(&present_serial,frame);
        regs[8]=timeline_ticks;
        gate(CAPTURE_EVENT_TICK);
        timeline_ticks=(uint32_t)regs[8]+1u;
    }
    assert(timeline_ticks==30);
    gate_stack[1]++;
    regs[8]=30;
    gate(CAPTURE_EVENT_TICK);
    assert(regs[8]==30);
    assert(adjusted(CAPTURE_DODGE_DURATION,1,15.5f)==31.f);
    assert(adjusted(CAPTURE_DODGE_DURATION,1,0)==0);
    assert(isnan(adjusted(CAPTURE_DODGE_DURATION,1,NAN)));
    // Cooperative sync camera consumes loaded frame counts and an angle/update; its stored config is untouched.
    regs[22]=150; regs[23]=40;
    float angular[4]={6,7,8,9}; memcpy((uint8_t*)regs+256+8*16,angular,sizeof angular);
    uint64_t expected[100],config_before[208],object_before[64];
    memcpy(expected,regs,sizeof expected); memcpy(config_before,capture_config,sizeof config_before);
    memcpy(object_before,capture_object,sizeof object_before);
    expected[22]=300; expected[23]=80; float half_angle=3;
    memcpy((uint8_t*)expected+256+8*16,&half_angle,4);
    gate(SYNC_CAMERA_STEP);
    assert(!memcmp(expected,regs,sizeof expected)); // only W22/W23 and low S8 changed; Q8's other lanes retained
    assert(!memcmp(config_before,capture_config,sizeof config_before) && !memcmp(object_before,capture_object,sizeof object_before));
    assert(150.0/30==regs[22]/60.0 && 40.0/30==regs[23]/60.0);
    assert(6*30==fp_scalar(regs,8)*60); // 180 degrees/second at either cadence
    for(unsigned bad=22;bad<=23;bad++) {
        regs[22]=150; regs[23]=40; regs[bad]=UINT32_MAX/2u+1u; fp_scalar_set(regs,8,6);
        memcpy(expected,regs,sizeof expected); gate(SYNC_CAMERA_STEP); assert(!memcmp(expected,regs,sizeof expected));
    }
    regs[22]=150; regs[23]=40; fp_scalar_set(regs,8,NAN);
    memcpy(expected,regs,sizeof expected); gate(SYNC_CAMERA_STEP); assert(!memcmp(expected,regs,sizeof expected));
    for(int ceiling=90;ceiling<=120;ceiling+=30) {
        cap=ceiling; assert(pn_lgp_fps_interval()==1);
        assert(adjusted(MODEL_TIME,1,1.f/30)==1.f/60); // high ceilings keep a fixed native 60
    }
    cap=30; assert(pn_lgp_fps_interval()==0);
    regs[10]=2; gate(CAPTURE_HIT_STOP); assert(regs[10]==2);
    regs[22]=150; regs[23]=40; fp_scalar_set(regs,8,6);
    memcpy(expected,regs,sizeof expected); gate(SYNC_CAMERA_STEP); assert(!memcmp(expected,regs,sizeof expected));
    assert(adjusted(MODEL_TIME,1,1.f/30)==1.f/30 && adjusted(RING_STEP,0,.125f)==.125f);
    regs[2]=1; regs[9]=(uintptr_t)durations; gate(STEP); assert(regs[9]==(uintptr_t)durations);
    *one_shot=0; stored_counter(one_shot,1); assert(*one_shot==1);
    puts("Pikachu scene prototype clocks/counters/ring/physics passed; one-shot ambiguity reproduced");
}
int main(int argc, char **argv) {
    assert(argc == 2);
    g_mod[0] = (module_t){"main", LGP_BUILD, (void *)((uintptr_t)durations - 0xf7eeb8)};
    if (!strcmp(argv[1], "unknown")) g_mod[0].build_id = "00000000000000000000000000000000";
    if (!strcmp(argv[1], "oldpack")) observed = 0;
    if (!strcmp(argv[1], "scenes")) { all_scenes(); return 0; }
    if (!strcmp(argv[1], "unknown") || !strcmp(argv[1], "oldpack")) {
        gate(FIELD); assert(pn_lgp_fps_interval() == 0);
        regs[2] = 1; regs[9] = (uintptr_t)durations; gate(STEP); assert(regs[9] == (uintptr_t)durations);
        component(0); gate(MOVE_ENTER); assert(components[0][28] == 2 && turn(.5f) == .5f); gate(MOVE_EXIT);
        pn_lgp_fps_present(); assert(pn_lgp_fps_interval() == 0); return 0;
    }
    assert(!strcmp(argv[1], "supported"));
    for (unsigned i = 0; i < 5; ++i) {
        static const int caps[] = {0,30,60,90,120}; cap = caps[i];
        gate(FIELD); pn_lgp_fps_present(); assert(pn_lgp_fps_interval() == (cap >= 60 ? 1 : 0));
        regs[1] = 99; regs[2] = 1; regs[9] = (uintptr_t)durations; gate(STEP);
        assert(regs[9] == (uintptr_t)durations - (cap >= 60 ? 8 : 0)); assert(regs[1] == 99 && regs[2] == 1);
        component(0); gate(MOVE_ENTER); assert(components[0][28] == (cap >= 60 ? 1 : 2));
        assert(fabsf(turn(.5f) - (cap >= 60 ? 1 - sqrtf(.5f) : .5f)) < 1e-6f);
        gate(MOVE_EXIT); assert(components[0][28] == 2 && components[0][29] == -4 && components[0][30] == 8);
        pn_lgp_fps_present(); assert(pn_lgp_fps_interval() == (cap >= 60 ? 2 : 0)); // no world update: menu
        regs[2] = 1; regs[9] = (uintptr_t)durations; gate(STEP); assert(regs[9] == (uintptr_t)durations);
    }
    cap = 60; gate(FIELD); assert(pn_lgp_fps_interval() == 1);
    // Correct register and table guards: x1 is not the duration index.
    regs[1] = 1; regs[2] = 0; regs[9] = (uintptr_t)durations; gate(STEP); assert(regs[9] == (uintptr_t)durations);
    regs[2] = 1; regs[9] = (uintptr_t)durations + 8; gate(STEP); assert(regs[9] == (uintptr_t)durations + 8);
    durations[1] = 42; regs[9] = (uintptr_t)durations; gate(STEP); assert(regs[9] == (uintptr_t)durations); durations[1] = 33333334;
    component(0); gate(MOVE_ENTER); cap = 30; gate(SPECIAL); gate(MOVE_EXIT); // restoration cannot depend on enabled/world state
    assert(components[0][28] == 2 && pn_lgp_fps_interval() == 0);
    cap = 60; assert(pn_lgp_fps_interval() == 2); pn_lgp_fps_present(); assert(pn_lgp_fps_interval() == 2);
    gate(FIELD); pn_lgp_fps_present(); assert(pn_lgp_fps_interval() == 1); gate(SPECIAL); assert(pn_lgp_fps_interval() == 2);
    pn_lgp_fps_present(); assert(pn_lgp_fps_interval() == 2); gate(FIELD); assert(pn_lgp_fps_interval() == 1);
    component(0); components[0][28] = NAN; gate(MOVE_ENTER); assert(turn(.5f) == .5f); gate(MOVE_EXIT);
    for (unsigned i = 0; i < 18; ++i) { component(i); gate(MOVE_ENTER); }
    for (unsigned i = 18; i-- > 0;) { gate(MOVE_EXIT); assert(components[i][28] == 2); }
    assert(move_depth == 0 && move_overflow == 0);
    assert(turn(0) == 0 && turn(1) == 1); gate(MOVE_EXIT); // unmatched exit is harmless
    gate(SPECIAL); // new experimental mutations remain no-ops in the normal/default mode
    assert(adjusted(CAPTURE_DODGE_DURATION,1,15.5f)==15.5f);
    regs[10]=2; gate(CAPTURE_HIT_STOP); assert(regs[10]==2);
    assert(adjusted(MODEL_TIME,1,1.f/30)==1.f/30);
    assert(adjusted(RING_STEP,0,.125f)==.125f && adjusted(PROJECTILE_RATE,9,30)==30);
    uint32_t *counter=(uint32_t*)&components[1][0]; *counter=0;
    stored_counter(counter,1); assert(*counter==1);
    puts("Pikachu timing: register guards, all caps, scene cadence and movement restoration passed");
    return 0;
}
