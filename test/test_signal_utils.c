/*
 * test_signal_utils.c
 * -------------------
 * Tests the circular buffer and moving-average filter on your laptop,
 * without needing an Arduino.
 *
 * Run from the project root:
 *   gcc -Wall test/test_signal_utils.c -o test_utils
 *   ./test_utils            (Windows: test_utils.exe)
 */

#include <stdio.h>
#include "../firmware/signal_analyzer/signal_utils.h"

static int failures = 0;

static void check(const char *name, int condition)
{
    printf("%-50s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) failures++;
}

int main(void)
{
    CircularBuffer buf;
    MovingAverage  ma;
    uint16_t value;

    /* ---- Test 1: empty buffer returns nothing ---- */
    cb_init(&buf);
    check("Empty buffer: pop returns 0", cb_pop(&buf, &value) == 0);

    /* ---- Test 2: first in, first out ---- */
    cb_push(&buf, 10);
    cb_push(&buf, 20);
    cb_push(&buf, 30);
    int ok = cb_pop(&buf, &value) && value == 10
          && cb_pop(&buf, &value) && value == 20
          && cb_pop(&buf, &value) && value == 30;
    check("Samples come out in the same order (FIFO)", ok);

    /* ---- Test 3: buffer holds 63 samples, then reports full ---- */
    cb_init(&buf);
    int added = 0;
    for (int i = 0; i < 100; i++) {
        added += cb_push(&buf, (uint16_t)i);
    }
    check("Full buffer stores 63 samples and drops the rest", added == BUF_SIZE - 1);

    /* ---- Test 4: wrap-around works (index goes 63 -> 0) ---- */
    cb_init(&buf);
    ok = 1;
    for (int i = 0; i < 500; i++) {        /* many push/pop cycles */
        cb_push(&buf, (uint16_t)i);
        if (!cb_pop(&buf, &value) || value != i) ok = 0;
    }
    check("Wrap-around: 500 push/pop cycles are correct", ok);

    /* ---- Test 5: moving average of a constant value ---- */
    ma_init(&ma);
    uint16_t out = 0;
    for (int i = 0; i < 20; i++) out = ma_update(&ma, 800);
    check("Moving average of constant 800 gives 800", out == 800);

    /* ---- Test 6: moving average rises slowly after a step ---- */
    ma_init(&ma);
    uint16_t step[8];
    for (int i = 0; i < 8; i++) step[i] = ma_update(&ma, 1000);
    /* expected: 125, 250, 375, ... 1000  (1000*k/8) */
    ok = 1;
    for (int i = 0; i < 8; i++) {
        if (step[i] != (uint16_t)(1000 * (i + 1) / 8)) ok = 0;
    }
    check("Step input: output rises 125, 250, ..., 1000", ok);

    /* ---- Test 7: filter reduces noise ---- */
    ma_init(&ma);
    int max_dev = 0;
    for (int i = 0; i < 200; i++) {
        uint16_t noisy = (i % 2 == 0) ? 600 : 400;   /* jumps +-100 around 500 */
        out = ma_update(&ma, noisy);
        if (i >= 8) {
            int dev = out > 500 ? out - 500 : 500 - out;
            if (dev > max_dev) max_dev = dev;
        }
    }
    check("Fast noise (+-100) is removed by the filter", max_dev <= 1);

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures;
}
