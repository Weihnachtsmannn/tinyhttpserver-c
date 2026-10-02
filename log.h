/* log.h */
#ifndef LOG_H
#define LOG_H

#include <pthread.h>
#include <stdio.h>

typedef enum {
        LOG_DEBUG,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
} log_level;

void log_init(const char *filename, int max_lines);
void log_write(log_level level, const char *format, ...);
void log_close(void);

#define LOG_DEBUG(fmt, ...) log_write(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_write(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_write(LOG_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif