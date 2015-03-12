#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <avr/interrupt.h>
#include <avr/io.h>

#ifndef BIT
#define BIT(x) (1<<(x))
#endif

#include "log.h"
#include "clock.h"
#include "sched.h"

#endif // CONFIG_H