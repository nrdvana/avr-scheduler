#ifndef CLOCK_H
#define CLOCK_H

#ifndef F_CPU
  #error clock.h requires F_CPU value.  This should be defined in your Makefile.
#endif
#ifndef CLOCK_PRESCALE
  #define CLOCK_PRESCALE 8
#endif

#define CLOCK_TICK_PER_SEC (F_CPU/CLOCK_PRESCALE)
#define CLOCK_TICK_PER_MSEC (F_CPU/CLOCK_PRESCALE/1000)
#define TICK_ROLLOVER_PERIOD_MS_APPROX ((msecCount32_t)(1000.0*65535*65536/CLOCK_TICK_PER_SEC))
#define MINIMUM_TICK_DELAY (31/CLOCK_PRESCALE+1)

typedef uint16_t tickCount16_t; // 0 - 0.033 seconds @ F_CPU=16Mhz
typedef uint32_t tickCount32_t; // 0 - 2147 seconds @ F_CPU=16Mhz
typedef uint32_t msecCount32_t; // 0 - 4bil ms = 0 - 49.7 days

extern void clock_init(void);

extern volatile tickCount32_t msecCount;

extern tickCount32_t clock_readTicks_cli(void);
extern tickCount32_t clock_readTicks(void);
extern tickCount32_t clock_combineTicks_cli(uint16_t lowWord);

static inline tickCount16_t clock_readTicks16(void) {
	uint8_t prevSreg= SREG;
	cli();
	tickCount16_t result= TCNT1;
	SREG= prevSreg;
	return result;
}

static inline msecCount32_t clock_readMsec(void) {
	uint8_t prevSreg= SREG;
	cli();
	msecCount32_t result= msecCount;
	SREG= prevSreg;
	return result;
}

static inline msecCount32_t clock_readMsec_cli(void) {
	return msecCount;
}

extern void clock_setWakeTime(tickCount16_t wakeTime, uint8_t *flagAddr);

#endif //CLOCK_H
