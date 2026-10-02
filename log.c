/* log.c */
#include "log.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *g_log_fp = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_log_max_lines = 0;
static int g_log_cur_lines = 0;

static const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void log_init(const char *filename, int max_lines) {
        g_log_fp = fopen(filename, "a");
        g_log_max_lines = max_lines;
}

void log_write(log_level level, const char *format, ...) {
        if (!g_log_fp)
                return;

        pthread_mutex_lock(&g_log_mutex);

        if (g_log_max_lines > 0 && g_log_cur_lines >= g_log_max_lines) {
                fclose(g_log_fp);
                g_log_fp = fopen("logfile", "w"); /* 覆盖重写 */
                g_log_cur_lines = 0;
        }

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        fprintf(g_log_fp, "[%s] [%s] ", time_buf, level_str[level]);

        va_list args;
        va_start(args, format);
        vfprintf(g_log_fp, format, args);
        va_end(args);

        fprintf(g_log_fp, "\n");
        fflush(g_log_fp);
        g_log_cur_lines++;

        pthread_mutex_unlock(&g_log_mutex);
}

void log_close(void) {
        if (g_log_fp) {
                fclose(g_log_fp);
                g_log_fp = NULL;
        }
}