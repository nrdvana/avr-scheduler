#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
//#include <util/delay.h>
#include <util/delay_basic.h>
//#include "usb_rawhid.h"
#include "clock.h"
#include "sched.h"
#include "main.h"

#define TASK_WAIT_TICK_THRESHOLD 4

static task_t *volatile pending_tick= NULL;
static task_t *volatile pending_msec= NULL;
static task_t *volatile ready= NULL;
static task_t *volatile* ready_tail= &ready;

// insert-sort, for now.  Might use a heap later.
static void insert_task(task_t *volatile *dest, task_t *newTask) {
	while (*dest && (int32_t)(newTask->wakeTime - (*dest)->wakeTime) > 0)
		dest= &(*dest)->next;
	newTask->next= *dest;
	*dest= newTask;
}

// append an item to the end of the list
static inline void move_to_ready(task_t *t) {
	*ready_tail= t;
	ready_tail= &t->next;
	t->next= NULL;
	t->isReady= true;
}

void sched_queue(task_t *t, int8_t flags, uint32_t wake_spec) {
	int32_t timeOfs;
	uint8_t oldSreg= SREG;
	cli();
	if (t->isScheduled)
		sched_cancel(t);

	t->isScheduled= true;
	t->isReady= false;
	if (flags & SCHED_WAKE_FLAG_MSEC) {
		t->waitForMsec= true;
		t->waitForTick= false;
		msecCount32_t now_msec= clock_readMsec();
		if (flags & SCHED_WAKE_FLAG_FROM_NOW) {
			t->wakeTime= now_msec + wake_spec;
			timeOfs= (int32_t) wake_spec;
		}
		else {
			t->wakeTime= wake_spec;
			timeOfs= (int32_t)(wake_spec - now_msec);
		}
		// If the timestamp is farther than 31-bits, (i.e. negative when signed)
		//  we consider it a event intended for the past and schedule it immediately.
		if (timeOfs > 0) {
			insert_task(&pending_msec, t);
			return;
		}
	}
	else {
		t->waitForMsec= false;
		t->waitForTick= true;
		tickCount32_t now_tick= clock_readTicks();
		if (flags & SCHED_WAKE_FLAG_FROM_NOW) {
			t->wakeTime= now_tick + wake_spec;
			timeOfs= (int32_t) wake_spec;
		}
		else {
			t->wakeTime= wake_spec;
			timeOfs= (int32_t)(wake_spec - now_tick - (uint32_t)TASK_WAIT_TICK_THRESHOLD);
		}
		if (timeOfs > 0) {
			insert_task(&pending_tick, t);
			return;
		}
	}
	// If not added to one of those queues, then it needs to run now

	// If the timestamp is *way* in the past, we flag an error
	if (timeOfs < -0xFFFFFFL)
		FLAG_ERR(ERR_SCHED_TIMESTAMP_WRAP);

	move_to_ready(t);
	SREG= oldSreg;
}

static inline task_t** find_in_list(task_t **list, task_t *item) {
	while (*list) {
		if (*list == item) return list;
		list= &(*list)->next;
	}
	return NULL;
}

void sched_cancel(task_t *t) {
	uint8_t oldSreg= SREG;
	cli();
	task_t **tp= find_in_list((task_t**) &pending_msec, t);
	if (!tp) tp= find_in_list((task_t**) &pending_tick, t);
	if (!tp) tp= find_in_list((task_t**) &ready, t);

	if (tp) {
		*tp= t->next;
		if (ready_tail == &t->next)
			ready_tail= tp;
	}
		
	t->next= NULL;
	t->isScheduled= false;
	t->isReady= false;

	SREG= oldSreg;
}

/*
tickCount32_t ticksTilNextTask(void) {
	if (ready) return 0;
	tickCount32_t next= (tickCount32_t)-1;
	if (pending_tick) {
		tickCount32_t now_tick= clock_readTicks();
		next= (pending_tick->wakeTime - now_tick);
	}
	if (pending_msec) {
		msecCount32_t now_msec= clock_readMsec();
		// if we're beyond the range of tickCount32_t, we just return -1
		tickCount32 diff= pending_msec->wakeTime - now_msec
		if (diff < TICK_ROLLOVER_PERIOD_MS_APPROX) {
			diff*= CLOCK_TICK_PER_MSEC;
			if (diff < next) return diff;
		}
	}
	return next;
}
*/

void sched_run_one(void) {
	cli();
	int32_t delay= CLOCK_TICK_PER_MSEC;
	// Move every task whose tick-time has arrived into the ready list
	if (pending_tick) {
		tickCount32_t now_tick= clock_readTicks();
		while (pending_tick && (delay= (pending_tick->wakeTime - now_tick)) < TASK_WAIT_TICK_THRESHOLD) {
			task_t *t= pending_tick;
			pending_tick= t->next;
			move_to_ready(t);
		}
	}
	// move every task whose msec-time has arrived into the ready list
	if (pending_msec) {
		msecCount32_t now_msec= clock_readMsec();
		while (pending_msec && (int32_t)(pending_msec->wakeTime - now_msec) <= 0) {
			task_t *t= pending_msec;
			pending_msec= t->next;
			move_to_ready(t);
		}
	}
	
	if (ready) {
		task_t *t= ready;
		ready= t->next;
		if (ready_tail == &t->next)
			ready_tail= &ready;
		t->next= NULL;
		t->isScheduled= false;
		
		// Capture the function and arguments we're going to call BEFORE re-enabling
		// interrupts, in case an interrupt handler changes the task object.
		void (*fn)(void*)= t->fn;
		void *arg= t->closure;
		sei();
		(*fn)(arg);
	}
	else if (pending_tick && delay < CLOCK_TICK_PER_MSEC) {
		// TODO: set a timer interrupt close to the time when we want to wake up.
		// Meanwhile, just busy-loop.
		sei();
	}
	else {
		// Nothing ready, and the next event isn't until the next millisecond.
		// Enter idle mode and let the clock millisecond interrupt wake us up.
		set_sleep_mode(SLEEP_MODE_IDLE);
		sleep_enable();
		sei();
		sleep_cpu();
		sleep_disable();
	}
}

void sched_run(void) {
	while (1) sched_run_one();
}
