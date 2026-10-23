/*
 * signal_utils.h
 * --------------
 * Two small building blocks used by the firmware, written in plain C
 * so they can also be tested on a laptop (see test/test_signal_utils.c).
 *
 *   1. Circular buffer  - passes samples from the ISR to the main loop
 *   2. Moving average   - simple digital filter (average of last 8 samples)
 */

#ifndef SIGNAL_UTILS_H
#define SIGNAL_UTILS_H

#include <stdint.h>

/* ============================================================
 * 1. CIRCULAR BUFFER
 * ============================================================
 * A fixed-size array used like a ring:
 *   - head = where the next sample is WRITTEN (by the ISR)
 *   - tail = where the next sample is READ    (by the main loop)
 * When an index reaches the end, it wraps back to 0.
 *
 * BUF_SIZE is a power of 2 (64), so instead of using "% 64" we can
 * wrap the index with a bit mask: (index + 1) & 63.  This is faster.
 *
 * 'volatile' tells the compiler these values can change inside an
 * interrupt, so it must always read them from memory.
 */
#define BUF_SIZE 64                    /* must be a power of 2 */
#define BUF_MASK (BUF_SIZE - 1)        /* 63 = 0b111111 */

typedef struct {
    volatile uint16_t data[BUF_SIZE];
    volatile uint8_t  head;
    volatile uint8_t  tail;
} CircularBuffer;

static inline void cb_init(CircularBuffer *b)
{
    b->head = 0;
    b->tail = 0;
}

/* Add a sample. Returns 1 if added, 0 if the buffer was full (sample dropped). */
static inline uint8_t cb_push(CircularBuffer *b, uint16_t value)
{
    uint8_t next = (b->head + 1) & BUF_MASK;
    if (next == b->tail) {
        return 0;                      /* full: one slot is always kept empty */
    }
    b->data[b->head] = value;          /* write data first ...            */
    b->head = next;                    /* ... then move head (order matters) */
    return 1;
}

/* Take the oldest sample. Returns 1 if a sample was read, 0 if empty. */
static inline uint8_t cb_pop(CircularBuffer *b, uint16_t *value)
{
    if (b->head == b->tail) {
        return 0;                      /* empty */
    }
    *value = b->data[b->tail];
    b->tail = (b->tail + 1) & BUF_MASK;
    return 1;
}

/* ============================================================
 * 2. MOVING-AVERAGE FILTER
 * ============================================================
 * output = average of the last 8 samples.
 *
 * Trick 1: keep a running sum. For each new sample, subtract the
 *          oldest one and add the newest one (2 operations, not 8).
 * Trick 2: 8 = 2^3, so "divide by 8" is the same as "shift right by 3".
 *          Shifting is much faster than dividing on a small chip.
 */
#define MA_SHIFT 3
#define MA_LEN   (1 << MA_SHIFT)       /* 8 */

typedef struct {
    uint16_t history[MA_LEN];          /* last 8 samples */
    uint8_t  index;                    /* position of the oldest sample */
    uint32_t sum;                      /* running sum of the 8 samples */
} MovingAverage;

static inline void ma_init(MovingAverage *f)
{
    for (uint8_t i = 0; i < MA_LEN; i++) {
        f->history[i] = 0;
    }
    f->index = 0;
    f->sum = 0;
}

static inline uint16_t ma_update(MovingAverage *f, uint16_t x)
{
    f->sum -= f->history[f->index];    /* remove oldest sample */
    f->history[f->index] = x;          /* store newest sample  */
    f->sum += x;                       /* add newest sample    */
    f->index = (f->index + 1) & (MA_LEN - 1);
    return (uint16_t)(f->sum >> MA_SHIFT);   /* divide by 8 */
}

#endif /* SIGNAL_UTILS_H */
