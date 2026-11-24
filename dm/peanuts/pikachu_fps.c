// SPDX-License-Identifier: GPL-3.0-or-later
// Exact-build overworld timing adaptation. The profile supplies guarded offline
// observation gates; all other builds and old packs keep their original timing.
#include "dm.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define LGP_BUILD "1c375692de4f4b879abe88742dc0ca60"
enum { FIELD=1, SPECIAL=2, STEP=3, MOVE_ENTER=4, MOVE_EXIT=5, TURN=6, SYNC_CAMERA_STEP=7, UI_STEP=8, FIELD_AI_TICK=12, PROJECTILE_STEP=13, PROJECTILE_RATE=14, MODEL_TIME=15, RING_STEP=17, RNG_COMPARE=18, RNG_RESTORE=19, CAPTURE_HIT_STOP=20 };
enum { CAPTURE_DISPATCH=21, CAPTURE_EVENT_TICK=22, CAPTURE_DODGE_DURATION=23, CAPTURE_FLIGHT_POSITION=24 };
enum { CAPTURE_LAUNCH_STEP=25, CAPTURE_LAUNCH_POSITION=26, CAPTURE_ROLL_DURATION=27 };
static _Atomic uint64_t capture_event_manager;
extern int nxhost_fps_limit_get(void);
static _Atomic int build_checked, build_matches, world, field_ran, special;
static _Atomic int scene_test;
static _Atomic uint64_t present_serial;
static _Atomic uint64_t main_base;
static _Thread_local struct { uint64_t component; float original[3]; int scaled; } moves[16];
static _Thread_local unsigned move_depth, move_overflow;
static _Thread_local uint64_t held_scene_object, held_scene_serial, rng_object, rng_saved_r8;
static _Thread_local uint32_t held_scene_value;
static _Thread_local int rng_masked;
static int supported(void) {
    if (!atomic_load(&build_checked)) {
        int match=0, found=0;
        for(int i=0;i<g_nmod;++i)
            if(!strcmp(g_mod[i].name,"main")) { found=1; if(!strcmp(g_mod[i].build_id,LGP_BUILD)) { match=1; atomic_store(&main_base,(uint64_t)(uintptr_t)g_mod[i].base); } }
        if(!found) return 0;
        atomic_store(&build_matches,match);
        atomic_store(&build_checked,1);
    }
    return atomic_load(&build_matches) && gate_obs_present(OBS_LGP_FPS);
}
static int enabled(void) { return supported() && nxhost_fps_limit_get()>=60; }
static int world60(void) { return enabled() && atomic_load(&world) && !atomic_load(&special); }
static int scene60_test(void) { return enabled() && atomic_load(&scene_test); }
static int clock60(void) { return world60() || scene60_test(); }
static float fp_scalar(uint64_t* r,unsigned q) { float v; memcpy(&v,(uint8_t*)r+256+16*q,4); return v; }
static void fp_scalar_set(uint64_t* r,unsigned q,float v) { memcpy((uint8_t*)r+256+16*q,&v,4); }

static void scene_counter_adjust(uint64_t* r) {
    held_scene_object=0;
    if(!scene60_test() || !(atomic_load(&present_serial)&1)) return;
    if(r[19]>UINT64_MAX-0x7c) return;
    uint64_t address=r[19]+0x78;
    uint32_t old=0;
    if(address>=0x10000 && !plat_mem_read(address,&old,4) && (uint32_t)r[8]==old+1) {
        r[8]=old; // Preserve explicit resets; event consumers need their own held-tick checks.
        held_scene_object=r[19]; held_scene_value=old; held_scene_serial=atomic_load(&present_serial);
    }
}
static void ui_animation_step(uint64_t* r) {
    if(!scene60_test() || atomic_load(&world) ||
       r[0]<0x10000 || r[0]>UINT64_MAX-0x34) return;
    float speed=0,delta=fp_scalar(r,0);
    if(!plat_mem_read(r[0]+0x30,&speed,4) && isfinite(speed) && speed!=0 && isfinite(delta) &&
       fabsf(delta-speed)<=fmaxf(0.00001f,fabsf(speed)*0.00001f)) {
        fp_scalar_set(r,0,delta*0.5f); // Do not halve an already half-step layout ratio again.
    }
}
static void projectile_step(uint64_t* r) {
    if(!scene60_test() || !atomic_load(&special) || (uint32_t)r[8]!=UINT32_C(0x3d088889)) return;
    float gravity[4]; memcpy(gravity,(uint8_t*)r+272,sizeof gravity);
    if(gravity[0]!=0 || fabsf(gravity[1]+32.66666794f)>0.00001f || gravity[2]!=0 || gravity[3]!=0) return;
    for(unsigned i=0;i<4;++i) gravity[i]*=0.5f;
    memcpy((uint8_t*)r+272,gravity,sizeof gravity);
    r[8]=UINT32_C(0x3c888889); // Velocity is in cm/s: halve dt, not initial velocity.
}
static void sync_camera_step(uint64_t* r) {
    if(!scene60_test() || !atomic_load(&special)) return;
    float angular=fp_scalar(r,8);
    if(!isfinite(angular) || (uint32_t)r[22]>UINT32_MAX/2 || (uint32_t)r[23]>UINT32_MAX/2) return;
    // This camera controller counts integer frames. Scale its loaded frame
    // durations, not its integer clock, and preserve the stored configuration.
    r[22]=(uint32_t)r[22]*2u; r[23]=(uint32_t)r[23]*2u;
    fp_scalar_set(r,8,angular*0.5f);
}
static void ring_step(uint64_t* r) {
    float delta=fp_scalar(r,0);
    if(scene60_test() && atomic_load(&special) && isfinite(delta)) fp_scalar_set(r,0,delta*0.5f);
}

static void capture_dispatch(uint64_t* r) {
    if(!scene60_test() || !atomic_load(&special) || r[19]<0x10000 ||
       r[19]>UINT64_MAX-0x24 || r[20]!=r[19]+0x10 || (uint32_t)r[8]>8) return;
    uint64_t scene,manager;
    if(plat_mem_read(r[19]+8,&scene,sizeof scene) || scene<0x10000 ||
       scene>UINT64_MAX-0x240 || plat_mem_read(scene+0x238,&manager,sizeof manager)) return;
    atomic_store(&capture_event_manager,manager);
}

static void capture_event_tick(uint64_t* r) {
    if(!scene60_test() || !atomic_load(&special)) return;
    const uint64_t manager=atomic_load(&capture_event_manager);
    if(manager<0x10000 || manager>UINT64_MAX-0x1230 || r[29]<0x10000 ||
       r[29]>UINT64_MAX-16) return;
    uint64_t caller;
    if(plat_mem_read(r[29]+8,&caller,sizeof caller) ||
       caller!=atomic_load(&main_base)+0x80f6e8) return;
    uint32_t slot;
    uint64_t timeline;
    if(plat_mem_read(manager+0xf90,&slot,sizeof slot) ||
       slot>=(0xf90-0x3c0)/0xb0 ||
       plat_mem_read(manager+0x3c0+0xb0*slot,&timeline,sizeof timeline) ||
       timeline!=r[19]) return;
    // The script consumes each current-frame event before reaching this add.
    // Hold the increment on alternate presents to preserve authored timestamps.
    if(atomic_load(&present_serial)&1)
        r[8]=(uint32_t)r[8]-1u;
}

static void lgp_gate(uint64_t* r,int kind) {
    if(!supported()) return;
    if(kind==FIELD) {
        atomic_store(&capture_event_manager,0);
        atomic_store(&field_ran,1); atomic_store(&special,0); atomic_store(&world,1);
    } else if(kind==SPECIAL) {
        atomic_store(&capture_event_manager,0);
        atomic_store(&special,1); atomic_store(&world,0);
    } else if(kind==STEP) {
        // Filled by the exact-build profile: redirect this one duration-table
        // load to G[0], keeping the destination timer slot and the table intact.
        if(clock60() && (uint32_t)r[2]==1 && r[9]==atomic_load(&main_base)+0xf7eeb8) {
            uint64_t durations[2];
            if(!plat_mem_read(r[9],durations,sizeof durations) && durations[0]==16666667 && durations[1]==33333334)
                r[9]-=8;
        }
    } else if(kind==MOVE_ENTER) {
        if(move_depth==16) { ++move_overflow; return; }
        unsigned at=move_depth++;
        moves[at].scaled=0;
        if(!world60() || r[0]<0x10000 || r[0]>UINT64_MAX-0x7c) return;
        float original[3];
        if(plat_mem_read(r[0]+0x70,original,sizeof original)) return;
        for(unsigned i=0;i<3;++i) if(!isfinite(original[i])) return;
        moves[at].component=r[0]; memcpy(moves[at].original,original,sizeof original);
        float half[3]; for(unsigned i=0;i<3;++i) half[i]=original[i]*0.5f;
        memcpy((void*)(uintptr_t)(r[0]+0x70),half,sizeof half);
        moves[at].scaled=1;
    } else if(kind==MOVE_EXIT) {
        if(move_overflow) { --move_overflow; return; }
        if(!move_depth) return;
        unsigned at=--move_depth;
        if(moves[at].scaled)
            memcpy((void*)(uintptr_t)(moves[at].component+0x70),moves[at].original,sizeof moves[at].original);
    } else if(kind==TURN && move_depth && !move_overflow && moves[move_depth-1].scaled) {
        // entry.S: q0 begins 256 bytes into the preserved gate frame.
        float t; memcpy(&t,(uint8_t*)r+256,sizeof t);
        if(t>0.0f && t<1.0f) { t=1.0f-sqrtf(1.0f-t); memcpy((uint8_t*)r+256,&t,sizeof t); }
    } else if(kind==SYNC_CAMERA_STEP) {
        sync_camera_step(r);
    } else if(kind==UI_STEP) {
        ui_animation_step(r);
    } else if(kind==FIELD_AI_TICK) {
        scene_counter_adjust(r);
    } else if(kind==PROJECTILE_STEP) {
        projectile_step(r);
    } else if(kind==PROJECTILE_RATE) {
        if(scene60_test() && atomic_load(&special) && fp_scalar(r,9)==30.0f) { fp_scalar_set(r,9,60.0f); }
    } else if(kind==MODEL_TIME) {
        if(scene60_test() && !world60() && fabsf(fp_scalar(r,1)-(1.0f/30.0f))<0.000001f) {
            fp_scalar_set(r,1,1.0f/60.0f);
        }
    } else if(kind==RING_STEP) {
        ring_step(r);
    } else if(kind==RNG_COMPARE) {
        // A held frame 30 must not draw the random branch twice. Temporarily
        // miss only this equality, then restore before the timeout comparison.
        if(scene60_test() && held_scene_object==r[19] &&
           held_scene_serial==atomic_load(&present_serial) && held_scene_value==30 && (uint32_t)r[8]==30) {
            rng_saved_r8=r[8]; rng_object=r[19]; rng_masked=1;
            r[8]=(r[8]&UINT64_C(0xffffffff00000000))|31;
        }
    } else if(kind==RNG_RESTORE) {
        if(rng_masked && rng_object==r[19]) { r[8]=rng_saved_r8; rng_masked=0; }
    } else if(kind==CAPTURE_HIT_STOP) {
        // The state waits while elapsed <= hitStopFrames, including frame zero.
        // Double the number of waiting updates and leave the config untouched.
        if(scene60_test() && atomic_load(&special)) {
            const uint32_t limit=(uint32_t)r[10];
            if(limit<=UINT32_MAX/2u) {
                r[10]=limit*2u+1u;
            }
        }
    } else if(kind==CAPTURE_DISPATCH) {
        capture_dispatch(r);
    } else if(kind==CAPTURE_EVENT_TICK) {
        capture_event_tick(r);
    } else if(kind==CAPTURE_DODGE_DURATION) {
        // Only the authored dodge arc's loaded frame duration is scaled.
        // The same value controls the transition and the position interpolation.
        if(scene60_test() && atomic_load(&special)) {
            const float frames=fp_scalar(r,1);
            if(isfinite(frames) && frames>0 && frames<0x1p23f) {
                fp_scalar_set(r,1,frames*2);
            }
        }
    } else if(kind==CAPTURE_FLIGHT_POSITION) {
        if(scene60_test() && atomic_load(&special) && fp_scalar(r,1)==1.0f/60.0f) {
            // The velocity is already stored. Correct the vertical displacement
            // so two 60 Hz Euler steps land on the original 30 Hz trajectory.
            float vertical;
            memcpy(&vertical,(uint8_t*)r+260,sizeof vertical);
            vertical-=980.0f/120.0f;
            memcpy((uint8_t*)r+260,&vertical,sizeof vertical);
        }
    } else if(kind>=CAPTURE_LAUNCH_STEP && kind<=CAPTURE_ROLL_DURATION) {
        // These ball updates also have a fixed 30 Hz step, independently of
        // the later capture controller's flight and event script.
        if(scene60_test() && atomic_load(&special) && atomic_load(&capture_event_manager)) {
            if(kind==CAPTURE_LAUNCH_STEP && (uint32_t)r[8]==UINT32_C(0x3d088889)) {
                r[8]=UINT32_C(0x3c888889);
            } else if(kind==CAPTURE_LAUNCH_POSITION && fp_scalar(r,1)==1.0f/60.0f) {
                float velocity[4], delta[4];
                memcpy(velocity,(uint8_t*)r+256,sizeof velocity);
                memcpy(delta,(uint8_t*)r+288,sizeof delta);
                for(unsigned i=0;i<4;i++) velocity[i]+=0.5f*delta[i];
                memcpy((uint8_t*)r+256,velocity,sizeof velocity);
            } else if(kind==CAPTURE_ROLL_DURATION) {
                const float duration=fp_scalar(r,0);
                if(duration>0 && duration<0x1p23f) fp_scalar_set(r,0,duration*2);
            }
        }
    }
}

// One completed frame decides the next interval. Missing field updates denote
// menus; capture/battle entry gates immediately restore their original cadence.
void pn_lgp_fps_present(void) {
    if(!supported()) return;
    const char* test=getenv("PN_LGP_SCENE60_TEST");
    atomic_store(&scene_test,test && !strcmp(test,"1"));
    atomic_fetch_add(&present_serial,1);
    const int ran=atomic_exchange(&field_ran,0);
    atomic_store(&world,ran && !atomic_load(&special));
    static int last=-1;
    const int high=clock60();
    if(high!=last) { LOG("Pikachu timing: %s",high?(scene60_test()?"experimental scene 60 FPS timing":"overworld 60 FPS, half-step movement and corrected turning"):"native 30 FPS scene"); last=high; }
}
int pn_lgp_fps_interval(void) {
    if(!enabled()) return 0;
    return clock60()?1:2;
}
__attribute__((constructor)) static void lgp_attach(void) { gate_obs_set(OBS_LGP_FPS,lgp_gate); }
