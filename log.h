#ifndef LOG_H
#define LOG_H

#define LOG_ERR_CLOCK_MSEC_LATE 1
#define LOG_ERR_SCHEDULER_TIMESTAMP_WRAP 2
extern void log_error_code(uint8_t code);

#endif // LOG_H
