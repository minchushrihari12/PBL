#ifndef LOGGER_H
#define LOGGER_H
void logger_init(const char *path);
void logger_write(const char *message);
void logger_close(void);
#endif
