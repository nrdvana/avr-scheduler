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
  The tick clock operates at F_CPU/8 Hz  (8/F_CPU sec) (.00000050000625 sec)
  The tick timer overflows every 32ms.  We must never leave interrupts disabled for
  longer than this!!
  The tick clock will wrap every 2^32 * 8/F_CPU = 2147.5 sec = 35.79 minutes
  The millisecond count will wrap every 49.7 days
  
  Calculating the millisecond compare points:
     Nominally, 2000 counts is one millisecond, however
       (8*1000/F_CPU) * 2000 = 1.0000125 ms
     So, every 10 million ms, we are ahead by 125 ms
       (10 000 000 / 125) = 80000 / 1, so every 80000 counts we
     subtract 1 (by pushing the deadline forward)
*/

#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay_basic.h>
#include "clock.h"

#define CLOCK_MSEC_INTERVAL 2000
#define CLOCK_CORRECTION_INTERVAL 40

typedef struct clock_s {
	int16_t overflowCount;      // number of times TCNT1 has overflowed
	int16_t nextMsec;           // TCNT value when we will next update msecCount
	int8_t nextMsecCorrection;  // ms remaining until we adjust the nextMsec point
	uint8_t *wakeFlagAddr;      // current "wake" flag, NULL if none is set.
} clock_t;

volatile clock_t clock= {
	.overflowCount= 0,
	.nextMsec= CLOCK_MSEC_INTERVAL,
	.nextMsecCorrection= 0,
	.wakeFlagAddr= NULL
};

// this is outside the clock struct so that we can access it from other modules
volatile msecCount32_t msecCount= 0;

void clock_init(void) {
	TCNT1= 0;
	OCR1A= clock.nextMsec;
	OCR1B= 0;
	TCCR1B|= BIT(CS11); // enable with prescaler of 8
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
		if (!(TIFR1|BIT(TOV1)))
			highWord--;
	}
	return ((tickCount32_t)highWord << 16) | lowWord;
}

/**
 * Read tick clock, while interrupts are disabled.
 */
tickCount32_t clock_readTicks_cli(void) {
	register uint16_t livecount= TCNT1;
	register uint8_t overflowFlag= TIFR1|BIT(TOV1);
	register uint16_t highWord= clock.overflowCount;
	// in the rare case where TCNT1 rolls over right after we read it and before we read the flag,
	//  we can ignore the flag.  Else the flag means we need to update highWord
	if (overflowFlag && ~livecount /* (livecount != 0xFFFF) */)
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
	register uint8_t overflowFlag= TIFR1|BIT(TOV1);
	register uint16_t highWord= clock.overflowCount;
	SREG= prevSreg;
	// in the rare case where TCNT1 rolls over right before we read the flag,
	//  we can ignore the flag.  Else the flag means we need to update highWord
	if (overflowFlag && ~livecount /* (livecount != 0xFFFF) */)
		highWord++;
	return ((tickCount32_t)highWord << 16) | livecount;
}

ISR(TIMER1_COMPA_vect) {
	// time to increment the clock.
	// we use a loop in case interrupts were disabled for more than a millisecond
	// (but that should never happen!)
	while (1) {
		msecCount++;
		
		clock.nextMsec+= CLOCK_MSEC_INTERVAL;
		if (clock.nextMsecCorrection <= 0) {
			clock.nextMsec++;
			clock.nextMsecCorrection+= CLOCK_CORRECTION_INTERVAL;
			//PORTC^= 4;
		}
		else
			--clock.nextMsecCorrection;
		// We increment the millisecond count up to MIN ticks (F_CPU/8) early
		//  to give us time to update OCRA before the time arrives.
		if ((int16_t)(clock.nextMsec - TCNT1) > MINIMUM_TICK_DELAY)
			break;
		else
			FLAG_ERR(ERR_MSEC_COUNT_LATE);
	}
	OCR1A= clock.nextMsec;
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
void setWakeTime(tickCount16_t wakeTime, uint8_t *flagAddr) {
	uint8_t prevSreg= SREG;
	cli();
	clock.wakeFlagAddr= flagAddr;
	OCR1B= wakeTime;
	TIFR1|= BIT(OCF1B); // clear interrupt flag, since its probably set
	TIMSK1|= BIT(OCIE1B); // set interrupt enable
	SREG= prevSreg;
}
