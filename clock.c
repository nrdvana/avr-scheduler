#include "config.h"
#include "clock.h"

/*
  The clock is composed of two running counts.  The first is a "tick count" and it increments
  at a fraction of the system clock.  The second is a millisecond count that increments once
  per millisecond, or as near as can be measured.  The millisecond count operates by setting
  a timer compare register and firing an interrupt.  In the event interrupts are disabled
  when an update should have occured, special logic in the interrupt handler tries to
  determine whether it missed any compare points and adds milliseconds until it reaches the
  present.
  
  The timer used for the tick clock wraps at 16-bit.  We use a 16-bit overflow counter
  to extend the timer to 32-bit.  The millisecond count is a natural 32-bit integer.
   - The tick clock operates at F_CPU / PRESCALE Hz.
   - The tick timer overflows every PRESCALE * 2^16 / F_CPU
     Never leave interrupts disabled for longer than that!
   - The tick clock will wrap every F_CPU / PRESCALE * 2^32
   - The millisecond count will wrap every 49.7 days
   - Update the millisecond count every MSEC_INTERVAL = F_CPU / 1000 / PRESCALE
   - For the number above, invert the fractional portion to determine how often to apply a
     correction.  Every N milliseconds, we wait (MSEC_INTERVAL+1) instead of MSEC_INTERVAL.
*/

#if CLOCK_PRESCALE == 1
#define PRESCALE_BITS (BIT(CS10))
#elif CLOCK_PRESCALE == 8
#define PRESCALE_BITS (BIT(CS11))
#elif CLOCK_PRESCALE == 64
#define PRESCALE_BITS (BIT(CS10) | BIT(CS11))
#else
#error Invalid clock prescale
#endif

// CLOCK_MSEC_INTERVAL is 16.16 fractional number of clock ticks between each millisecond count
// As it accumulates in clock.nextMsec, it will very precisely decide when the milliseconds
// should be incremented next.

// The simple logic we use can't handle the case where we need to travel farther than half the timer
// (16-bit) before the next msec increment.
#if 0x7FFF < F_CPU / CLOCK_PRESCALE / 1000
#error Current clock logic requires that milliseconds be no farther than half the timer apart
#else
#define CLOCK_MSEC_INTERVAL ((uint32_t)((F_CPU * 65536ULL) / (CLOCK_PRESCALE*1000ULL)))
#endif

typedef struct clock_s {
	uint16_t overflowCount;     // number of times TCNT1 has overflowed
	uint32_t nextMsec;          // 32.32 fractional TCNT value when we will next update msecCount
	uint8_t *wakeFlagAddr;      // current "wake" flag, NULL if none is set.
} clock_t;

volatile clock_t clock= {
	.overflowCount= 0,
	.nextMsec= CLOCK_MSEC_INTERVAL,
	.wakeFlagAddr= NULL
};

// this is outside the clock struct so that we can access it from other modules
volatile msecCount32_t msecCount= 0;

void clock_init(void) {
	TCNT1= 0;
	OCR1A= (uint16_t)(clock.nextMsec >> 16);
	OCR1B= 0;
	TCCR1B|= PRESCALE_BITS; // enable with prescaler setting from above
	TIMSK1|= BIT(OCIE1A) | BIT(TOIE1); // enable interrupts for overflow and Compare-A
}

ISR(TIMER1_OVF_vect) {
	clock.overflowCount++;
}

/**
 * Combine a known recent loWord with the matching hiWord
 */
tickCount32_t clock_combineTicks_cli(uint16_t lowWord) {
	register uint16_t highWord= clock.overflowCount;
	// if the clock has rolled over since lowWord was sampled
	if ((lowWord&BIT(15)) && !(TCNT1&BIT(15))) {
		// if the interrupt flag is still set, then the high-word is still correct
		// else subtract one from the high word
		if (!(TIFR1&BIT(TOV1)))
			highWord--;
	}
	return ((tickCount32_t)highWord << 16) | lowWord;
}

/**
 * Read tick clock, while interrupts are disabled.
 */
tickCount32_t clock_readTicks_cli(void) {
	register uint16_t livecount= TCNT1;
	register uint8_t  overflow= TIFR1&BIT(TOV1);
	register uint16_t highWord= clock.overflowCount;
	// in the rare case where TCNT1 rolls over right after we read it and before we read the flag,
	//  we can ignore the flag.  Else the flag means we need to update highWord
	if (overflow && (uint8_t)~(uint8_t)(livecount>>8) /* (livecount > 0xFF00) */)
		highWord++;
	return ((tickCount32_t)highWord << 16) | livecount;
}

/**
 * Read tick clock, while interrupts are in an unknown state.
 */
tickCount32_t clock_readTicks(void) {
	uint8_t prevSreg= SREG;
	cli();
	register uint16_t livecount= TCNT1;
	register uint8_t  overflow= TIFR1&BIT(TOV1);
	register uint16_t highWord= clock.overflowCount;
	SREG= prevSreg;
	// in the rare case where TCNT1 rolls over right before we read the flag,
	//  we can ignore the flag.  Else the flag means we need to update highWord
	if (overflow && (uint8_t)~(uint8_t)(livecount>>8) /* (livecount > 0xFF00) */)
		highWord++;
	return ((tickCount32_t)highWord << 16) | livecount;
}

ISR(TIMER1_COMPA_vect) {
	// time to increment the clock.
	// we use a loop in case interrupts were disabled for more than a millisecond
	// (but that should never happen!)
	uint16_t wake_at;
	while (1) {
		msecCount++;
		clock.nextMsec+= CLOCK_MSEC_INTERVAL;
		wake_at= (uint16_t)(clock.nextMsec >> 16);

		// We increment the millisecond count up to MIN ticks (F_CPU/PRESCALE) early
		//  to give us time to update OCRA before the time arrives.
		if ((int16_t)(wake_at - TCNT1) > MINIMUM_TICK_DELAY)
			break;
		else
			log_error_code(LOG_ERR_CLOCK_MSEC_LATE);
	}
	OCR1A= wake_at;
}

// This ISR is used for the "wake" feature.
// It acts as a one-shot event that sets a flag and wakes normal execution.
ISR(TIMER1_COMPB_vect) {
	TIMSK1&= ~BIT(OCIE1B); // disable own interrupt
	if (clock.wakeFlagAddr) {
		*clock.wakeFlagAddr= 1;
		clock.wakeFlagAddr= NULL;
	}
}

/**
 * Set an interrut to wake the system up at the given time.
 * This can only be for up to 32ms into the future, by the nature
 *   of TCNT1.
 */
void clock_setWakeTime(tickCount16_t wakeTime, uint8_t *flagAddr) {
	uint8_t prevSreg= SREG;
	cli();
	clock.wakeFlagAddr= flagAddr;
	OCR1B= wakeTime;
	TIFR1|= BIT(OCF1B); // clear interrupt flag, since its probably set
	TIMSK1|= BIT(OCIE1B); // set interrupt enable
	SREG= prevSreg;
}
