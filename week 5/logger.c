#include <stdio.h>
#include <time.h>
#include "logger.h"

static FILE *log_file = NULL;

void logger_init(const char *path) {
    log_file = fopen(path, "a");
    if (log_file) logger_write("Logger initialized.");
}

void logger_write(const char *message) {
    if (!log_file) return;
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (local) fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
        local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
        local->tm_hour, local->tm_min, local->tm_sec, message);
    fflush(log_file);
}

void logger_close(void) {
    if (log_file) {
        logger_write("Logger closed.");
        fclose(log_file);
        log_file = NULL;
    }
}
