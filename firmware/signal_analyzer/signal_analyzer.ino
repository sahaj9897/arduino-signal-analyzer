/*
 * signal_analyzer.ino
 * -------------------
 * Real-Time Signal Analyzer on Arduino Uno (ATmega328P)
 *
 * What it does:
 *   1. Timer1 fires an interrupt 500 times per second (500 Hz sampling rate)
 *   2. The interrupt (ISR) reads the ADC (analog pin A0) and stores the
 *      sample in a circular buffer. The ISR is kept very short.
 *   3. The main loop takes samples from the buffer, applies a
 *      moving-average filter, and sends "raw,filtered" over UART.
 *   4. A Python script on the laptop plots the signal and its FFT live.
 *
 * Timer, ADC and UART are set up by writing to the chip's registers
 * directly with bit manipulation (no analogRead() or Serial library).
 *
 * Test signal: pin 8 outputs a 40 Hz square wave. Connect pin 8 to A0
 * with a jumper wire to test the system with a known frequency.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include "signal_utils.h"

/* ---------------- Settings ---------------- */
#define SAMPLE_RATE_HZ  500UL     /* samples per second */
#define BAUD_RATE       115200UL  /* UART speed */
#define TEST_TONE_PIN   8         /* test signal output pin */
#define TEST_TONE_HZ    40        /* test signal frequency */

/* ---------------- Global data ---------------- */
static CircularBuffer adc_buffer;   /* shared between ISR and main loop */
static MovingAverage  filter;

/* ============================================================
 * TIMER1: generate an interrupt at SAMPLE_RATE_HZ
 * ============================================================
 * CPU clock = 16 MHz. With prescaler 64, the timer counts at
 *   16,000,000 / 64 = 250,000 counts per second.
 * In CTC mode the timer counts from 0 up to OCR1A, fires an interrupt,
 * and restarts from 0. For 500 Hz:
 *   OCR1A = 250,000 / 500 - 1 = 499
 */
void timer1_init(void)
{
    TCCR1A = 0;                               /* clear control registers */
    TCCR1B = 0;
    TCNT1  = 0;                               /* reset counter */

    TCCR1B |= (1 << WGM12);                   /* CTC mode (Clear Timer on Compare) */
    TCCR1B |= (1 << CS11) | (1 << CS10);      /* prescaler = 64 */
    OCR1A   = (F_CPU / 64 / SAMPLE_RATE_HZ) - 1;  /* = 499 */

    TIMSK1 |= (1 << OCIE1A);                  /* enable compare-match interrupt */
}

/* ============================================================
 * ADC: read analog voltage on pin A0
 * ============================================================
 * Result is 10 bits: 0 (0 V) to 1023 (5 V).
 * ADC clock must be 50-200 kHz: 16 MHz / 128 = 125 kHz.
 * One conversion takes 13 ADC clocks = 104 microseconds,
 * which is much less than our 2 ms sample period.
 */
void adc_init(void)
{
    ADMUX  = (1 << REFS0);                    /* reference = 5 V (AVcc), channel = A0 */
    ADCSRA = (1 << ADEN);                     /* enable ADC */
    ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  /* prescaler = 128 */
    ADCSRA |= (1 << ADSC);                    /* start the first conversion */
}

/* ============================================================
 * UART: send data to the laptop over USB
 * ============================================================
 * Format: 8 data bits, no parity, 1 stop bit (8N1).
 * Double-speed mode (U2X0) gives a more accurate 115200 baud:
 *   UBRR0 = 16,000,000 / (8 x 115200) - 1 = 16
 */
void uart_init(void)
{
    UCSR0A = (1 << U2X0);                     /* double-speed mode */
    UBRR0  = (F_CPU / 8 / BAUD_RATE) - 1;     /* = 16 */
    UCSR0B = (1 << TXEN0);                    /* enable transmitter */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   /* 8 data bits */
}

void uart_send_char(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) {
        /* wait until the transmit register is empty */
    }
    UDR0 = c;                                 /* send the character */
}

/* Send a number as text, e.g. 512 -> '5' '1' '2' */
void uart_send_number(uint16_t n)
{
    char digits[5];
    uint8_t count = 0;

    if (n == 0) {
        uart_send_char('0');
        return;
    }
    while (n > 0) {
        digits[count++] = '0' + (n % 10);     /* take the last digit */
        n /= 10;
    }
    while (count > 0) {
        uart_send_char(digits[--count]);      /* send digits in correct order */
    }
}

/* ============================================================
 * INTERRUPT SERVICE ROUTINE (ISR)
 * ============================================================
 * Runs automatically 500 times per second.
 * It is kept SHORT: read one value, start the next conversion,
 * store the value, and exit. All slow work (filtering, sending)
 * is done in the main loop, so sampling always stays on time.
 */
ISR(TIMER1_COMPA_vect)
{
    uint16_t sample = ADC;                    /* result of the conversion started last time */
    ADCSRA |= (1 << ADSC);                    /* start the next conversion */
    cb_push(&adc_buffer, sample);             /* store in circular buffer */
}

/* ============================================================
 * SETUP: runs once at power-on
 * ============================================================ */
void setup()
{
    cli();                                    /* disable interrupts during setup */

    cb_init(&adc_buffer);
    ma_init(&filter);
    uart_init();
    adc_init();
    timer1_init();

    /* Test signal: 40 Hz square wave on pin 8 (uses Timer2, not Timer1) */
    tone(TEST_TONE_PIN, TEST_TONE_HZ);

    sei();                                    /* enable interrupts: sampling starts */
}

/* ============================================================
 * MAIN LOOP: runs forever
 * ============================================================ */
void loop()
{
    uint16_t raw;

    /* process every sample waiting in the buffer */
    while (cb_pop(&adc_buffer, &raw)) {
        uint16_t filtered = ma_update(&filter, raw);

        /* send one line: raw,filtered\n  (example: 1023,895) */
        uart_send_number(raw);
        uart_send_char(',');
        uart_send_number(filtered);
        uart_send_char('\n');
    }
}
