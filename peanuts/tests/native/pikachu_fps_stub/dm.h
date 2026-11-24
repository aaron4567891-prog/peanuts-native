// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { const char *name, *build_id; void *base; } module_t;
extern module_t g_mod[1];
extern int g_nmod;
enum { OBS_LGP_FPS = 35 };
int gate_obs_present(int id);
void gate_obs_set(int id, void (*fn)(uint64_t *, int));
int plat_mem_read(uint64_t address, void *out, size_t size);
extern unsigned timing_test_logs;
static inline void timing_test_log(const char *format, ...) { (void)format; ++timing_test_logs; }
static inline uint64_t now_ns(void) { return 1230000000; }
#define LOG(...) timing_test_log(__VA_ARGS__)
