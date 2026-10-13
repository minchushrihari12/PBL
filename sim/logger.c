/* logger.c : the Logger process.
   Receives log messages from Core (queue Q_LOG) and writes them to
   simulator.log (everything) and errors.log (errors only).

   Build: gcc -Wall -Wextra logger.c -o logger -lrt -pthread

   Threads:  receiver thread : mq_receive -> puts messages in a ring buffer
             main thread     : writer, takes messages out and writes to disk
   So slow disk writes never stop the queue from being emptied. */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ipc_defs.h"

#define LBUF_SIZE 256

static mqd_t q;
static log_msg_t lbuf[LBUF_SIZE];
static int lb_head, lb_tail, lb_count;
static pthread_mutex_t lb_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  lb_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  lb_not_full  = PTHREAD_COND_INITIALIZER;

static void *receiver_thread(void *arg) {
    log_msg_t m;
    (void)arg;
    for (;;) {
        memset(&m, 0, sizeof m);
        if (mq_receive(q, (char *)&m, sizeof m, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("logger: mq_receive");
            memset(&m, 0, sizeof m);
            m.level = LOG_QUIT;                 /* fatal: shut down */
        }
        pthread_mutex_lock(&lb_lock);
        while (lb_count == LBUF_SIZE) pthread_cond_wait(&lb_not_full, &lb_lock);
        lbuf[lb_tail] = m;
        lb_tail = (lb_tail + 1) % LBUF_SIZE;
        lb_count++;
        pthread_cond_signal(&lb_not_empty);
        pthread_mutex_unlock(&lb_lock);
        if (m.level == LOG_QUIT) break;
    }
    return NULL;
}

int main(void) {
    FILE *fl, *fe;
    pthread_t rt;
    long total = 0, errors = 0;
    char ts[32];
    time_t now;
    struct tm tmv;

    ipc_print_info("logger");
    q = IPC_OPEN_LOG(O_RDONLY);

    fl = fopen("simulator.log", "a");
    if (!fl) { perror("logger: cannot open simulator.log"); return 1; }
    fe = fopen("errors.log", "a");
    if (!fe) { perror("logger: cannot open errors.log"); fclose(fl); return 1; }

    if (pthread_create(&rt, NULL, receiver_thread, NULL) != 0) { perror("pthread_create"); return 1; }

    for (;;) {
        log_msg_t m;
        const char *lvl;

        pthread_mutex_lock(&lb_lock);
        while (lb_count == 0) pthread_cond_wait(&lb_not_empty, &lb_lock);
        m = lbuf[lb_head];
        lb_head = (lb_head + 1) % LBUF_SIZE;
        lb_count--;
        pthread_cond_signal(&lb_not_full);
        pthread_mutex_unlock(&lb_lock);

        if (m.level == LOG_QUIT) break;

        lvl = (m.level == LOG_ERROR) ? "ERROR" : "INFO";
        localtime_r(&m.timestamp, &tmv);
        strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
        m.text[TEXT_LEN - 1] = '\0';
        fprintf(fl, "[%s] %s: %s\n", ts, lvl, m.text);
        fflush(fl);
        total++;
        if (m.level == LOG_ERROR) {
            fprintf(fe, "[%s] ERROR: %s\n", ts, m.text);
            fflush(fe);
            errors++;
        }
    }

    now = time(NULL);
    localtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
    fprintf(fl, "[%s] INFO: logger summary: %ld messages, %ld errors\n", ts, total, errors);
    fclose(fl);
    fclose(fe);
    pthread_join(rt, NULL);
    mq_close(q);
    return 0;
}
