# PEANUTS: exercise the production present scheduling block with controlled queue delays.
"""A present deadline must use readiness after GPU and queue waits."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class PresentDeadline(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cc'), 'C compiler required')
    def test_post_submit_deadline_and_error_paths(self):
        source = (ROOT / 'dm/gfx/vk_android/vka_present.c').read_text()
        timing_at = source.index('uint64_t want = 0, period =', source.index('static int present_one('))
        timing_end = source.index('// The frame done on the GPU', timing_at) if timing_at < source.index('// The frame done on the GPU') else 0
        submit_at = source.index('void vka_qhold_add(int k, uint64_t ns); uint64_t th0 = now_ns();')
        end = source.index('    g_fr[k].held = im;', submit_at)
        # Include an early scheduling block when testing the old ordering, so
        # the same harness reproduces its stale deadline rather than duplicating
        # the corrected algorithm in the test.
        early = source[timing_at:timing_end] if timing_at < submit_at else ''
        tail = source[submit_at:end]
        preamble = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include "dm/gfx/vk_android/vka_panel.h"
typedef int VkResult;
enum { VK_ERROR_DEVICE_LOST = -4, VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE = 1,
       VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 2 };
typedef struct { uint32_t presentID; uint64_t desiredPresentTime; } VkPresentTimeGOOGLE;
typedef struct { int sType; const void *pNext; unsigned swapchainCount; const VkPresentTimeGOOGLE *pTimes; } VkPresentTimesInfoGOOGLE;
typedef struct { int sType; const VkPresentTimesInfoGOOGLE *pNext; unsigned waitSemaphoreCount;
    const int *pWaitSemaphores; unsigned swapchainCount; const int *pSwapchains; const uint32_t *pImageIndices; } VkPresentInfoKHR;
static struct { int q, qmu, display_timing; } V = {1, 1, 1};
static struct { int fence; } g_fr[1];
static int g_sdone[1], g_swap, g_fg_on;
static uint64_t g_refresh_ns = 8437500, g_last_desired;
static vkpanel g_panel;
static vkgrid g_grid;
static _Atomic uint32_t g_grid_timed, g_grid_late, g_grid_presents;
static _Atomic uint64_t g_presentation_deadline; // PEANUTS: current display metadata
static uint64_t test_now, queue_delay, wait_delay, deadline;
static int submit_error, hung, submitted, presented, unlocked, want_future = 1;
static uint32_t want_id = 7;
int nxhost_fps_limit_get(void);
static uint64_t now_ns(void) { return test_now; }
static int vka_hung(void) { return hung; }
static int fg_grid_env = 1;   /* DM_VK_FG_GRID as the test sets it; every other switch at its default */
static int vka_env(const char *k, int fallback) { return strcmp(k, "DM_VK_FG_GRID") ? fallback : fg_grid_env; }
#define VKA_ENV(k, fallback) vka_env(k, fallback)
#define VLOG(...) ((void)0) /* PEANUTS: diagnostic sink absent from this clock fixture. */
static void pthread_mutex_unlock(int *mu) { assert(mu == &V.qmu); unlocked++; }
void vka_qhold_add(int k, uint64_t ns) { (void)k; (void)ns; }
static VkResult submit(int q, int n, const void *si, int fence) {
    (void)q; (void)n; (void)si; (void)fence;
    assert(!unlocked); submitted++; test_now += queue_delay; return submit_error;
}
static VkResult present(int q, const VkPresentInfoKHR *pi) {
    (void)q; assert(submitted && !unlocked && pi->pNext);
    const VkPresentTimeGOOGLE *pt = pi->pNext->pTimes;
    assert(pt->presentID == want_id); deadline = pt->desiredPresentTime;
    assert(!want_future || deadline >= test_now); presented++; return 0;
}
static struct { VkResult (*vkQueueSubmit)(int, int, const void *, int);
    VkResult (*vkQueuePresentKHR)(int, const VkPresentInfoKHR *); } vk = {submit, present};
typedef struct { double period, hold; uint64_t ticket; const void *src; int fg; } item;   /* pq_item's fields that the block reads */
static item value;
static void exercise(void) {
    const item *it = &value;
    uint32_t idx = 0; unsigned k = 0; int si = 0; VkResult r;
'''
        body = early + '\n    test_now += wait_delay;\n' + tail + '\n    (void)pr;\n}\n'
        cases = r'''
int nxhost_fps_limit_get(void) { return 30; }
static void reset(void) {
    const uint64_t anchor = 1000000000;
    g_panel.period = g_refresh_ns; g_panel.last = anchor;
    g_grid.last = anchor; g_last_desired = 0;
    g_grid_timed = g_grid_late = g_grid_presents = 0;
    value = (item){1. / 120, 0, 7, NULL, 0}; g_fg_on = 0; fg_grid_env = 1; want_id = 7; want_future = 1;
    test_now = anchor + 2000000; queue_delay = wait_delay = deadline = 0;
    submit_error = hung = submitted = presented = unlocked = 0;
}
int main(void) {
    // The next four-refresh target is 33.75 ms after the anchor. A wait
    // consumes its half-refresh submission margin; the old code gave the
    // compositor an already elapsed deadline and still counted zero late.
    for (int phase = 0; phase < 3; phase++) {
        reset();
        wait_delay = phase == 0 ? 30000000 : phase == 2 ? 20000000 : 0;
        queue_delay = phase == 1 ? 30000000 : phase == 2 ? 10000000 : 0;
        exercise();
        assert(submitted == 1 && presented == 1 && unlocked == 1);
        assert(g_grid_timed == 1 && g_grid_presents == 1 && g_grid_late == 1);
    }
    reset(); exercise();
    assert(g_grid_late == 0 && deadline == 1000000000 + 4 * g_refresh_ns - g_refresh_ns / 2);
    // PEANUTS: the API timestamp is still future, but Android's 11.333 ms queue deadline has elapsed for the old target.
    reset(); g_presentation_deadline = 11333333; test_now = 1000000000 + 27000000; exercise();
    assert(g_grid_late == 1 && g_grid.last >= test_now + g_presentation_deadline);
    g_presentation_deadline = 0;
    reset(); submit_error = -1; exercise();
    assert(submitted == 1 && !presented && unlocked == 1 && !g_grid_presents);
    reset(); hung = 1; exercise();
    assert(!submitted && !presented && unlocked == 1 && !g_grid_presents);
    // Frame generation (DM_VK_FG_GRID): a generated frame held two refreshes takes the grid two refreshes on, not the 30 fps limit's four; it has no ticket, so the
    // game frame counters stay as they were. The game frame after it (fg, ticket 7) takes the grid too and is counted.
    static int marker;
    reset(); g_fg_on = 1; value = (item){1. / 60, 2 * g_refresh_ns / 1e9, 0, &marker, 1}; want_id = 0; exercise();
    assert(presented == 1 && deadline == 1000000000 + 2 * g_refresh_ns - g_refresh_ns / 2);
    assert(!g_grid_timed && !g_grid_presents && g_grid.last == 1000000000 + 2 * g_refresh_ns);
    reset(); g_fg_on = 1; value = (item){1. / 60, 2 * g_refresh_ns / 1e9, 7, NULL, 1}; exercise();
    assert(deadline == 1000000000 + 2 * g_refresh_ns - g_refresh_ns / 2 && g_grid_timed == 1 && g_grid_presents == 1 && !g_grid_late);
    // DM_VK_FG_GRID=0: the chain from the last desired time as before the switch (the first image of a chain is "now"), the grid dropped
    reset(); g_fg_on = 1; fg_grid_env = 0; want_future = 0; value = (item){1. / 60, 2 * g_refresh_ns / 1e9, 7, NULL, 1}; exercise();
    assert(presented == 1 && !g_grid_timed && g_grid.last == 0 && deadline == test_now - g_refresh_ns / 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            src, exe = Path(td) / 'present.c', Path(td) / 'present'
            src.write_text(preamble + body + cases)
            compiled = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == '__main__':
    unittest.main()
