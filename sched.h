#ifndef SCHED_H
#define SCHED_H

#include <string.h>
#include "clock.h"

typedef struct task_s {
	bool isScheduled:1, isReady: 1, waitForMsec:1, waitForTick: 1;
	uint32_t wakeTime;
	struct task_s *next;
	void* closure;
	void (*fn)(void *closure);
} task_t;

inline void sched_init(void) {}

#define SCHED_WAKE_FLAG_MSEC 1
#define SCHED_WAKE_FLAG_FROM_NOW 2
extern void sched_queue(task_t *t, int8_t flags, uint32_t wakeTime);

inline void sched_now(task_t *t, void (*fn)(void *), void *arg) {
	uint8_t oldSreg= SREG;
	cli();
	t->fn= fn;
	t->closure= arg;
	sched_queue(t, SCHED_WAKE_FLAG_FROM_NOW, 0);
	SREG= oldSreg;
}
inline void sched_at_msec(task_t *t, msecCount32_t cnt, void (*fn)(void *), void *arg) {
	uint8_t oldSreg= SREG;
	cli();
	t->fn= fn;
	t->closure= arg;
	sched_queue(t, SCHED_WAKE_FLAG_MSEC, cnt);
	SREG= oldSreg;
}
inline void sched_msec_from_now(task_t *t, msecCount32_t ofs, void (*fn)(void *), void *arg) {
	uint8_t oldSreg= SREG;
	cli();
	t->fn= fn;
	t->closure= arg;
	sched_queue(t, SCHED_WAKE_FLAG_MSEC|SCHED_WAKE_FLAG_FROM_NOW, ofs);
	SREG= oldSreg;
}
inline void sched_at_tick(task_t *t, tickCount32_t cnt, void (*fn)(void *), void *arg) {
	uint8_t oldSreg= SREG;
	cli();
	t->fn= fn;
	t->closure= arg;
	sched_queue(t, 0, cnt);
	SREG= oldSreg;
}
inline void sched_ticks_from_now(task_t *t, tickCount32_t ofs, void (*fn)(void *), void *arg) {
	uint8_t oldSreg= SREG;
	cli();
	t->fn= fn;
	t->closure= arg;
	sched_queue(t, SCHED_WAKE_FLAG_FROM_NOW, ofs);
	SREG= oldSreg;
}

inline void sched_again_now(task_t *t) {
	sched_queue(t, SCHED_WAKE_FLAG_FROM_NOW, 0);
}
inline void sched_again_at_msec(task_t *t, msecCount32_t cnt) {
	sched_queue(t, SCHED_WAKE_FLAG_MSEC, cnt);
}
inline void sched_again_msec_from_now(task_t *t, msecCount32_t ofs) {
	sched_queue(t, SCHED_WAKE_FLAG_MSEC|SCHED_WAKE_FLAG_FROM_NOW, ofs);
}
inline void sched_again_at_tick(task_t *t, tickCount32_t cnt) {
	sched_queue(t, 0, cnt);
}
inline void sched_again_ticks_from_now(task_t *t, tickCount32_t ofs) {
	sched_queue(t, SCHED_WAKE_FLAG_FROM_NOW, ofs);
}

extern void sched_cancel(task_t *t);

extern void sched_run_one(void);

extern void sched_run(void);

#endif //SCHED_H
