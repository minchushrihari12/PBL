/*
 * ui.c  --  UI process.  (Owner: UI teammate)
 *
 * A tiny HTTP server (localhost only) written in C. It
 *   - serves the GUI page  gui/index.html
 *   - turns   GET /api?cmd=<command>   into a message on KEY_CORE,
 *     waits for the chunked JSON reply on KEY_UI and returns it to the browser.
 *
 * Usage: ./ui [port]      (default 8085, run from the week4 folder)
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ipc_defs.h"

#define GUI_FILE "gui/index.html"
#define OUT_MAX  32768

static int q_core = -1, q_ui = -1, q_log = -1;
static volatile sig_atomic_t running = 1;

static void on_signal(int s) { (void)s; running = 0; }

static void connect_queues(void)
{
    q_core = ipc_open(KEY_CORE);
    q_ui   = ipc_open(KEY_UI);
    q_log  = ipc_open(KEY_LOG);
}

/* ---------- talking to core ---------- */
/* 0 = ok, -1 = core not reachable, -2 = timeout */
static int ask_core(const char *cmd, char *out, size_t outsz)
{
    static int req_id = 0;
    cmd_msg_t c;
    reply_msg_t r;
    size_t len = 0;
    int waited = 0;

    req_id++;
    while (msgrcv(q_ui, &r, MSG_SIZE(reply_msg_t), 0, IPC_NOWAIT) >= 0)
        ;                                              /* drop stale replies */

    memset(&c, 0, sizeof c);
    c.mtype = 1;
    c.req_id = req_id;
    snprintf(c.text, sizeof c.text, "%s", cmd);

    if (msgsnd(q_core, &c, MSG_SIZE(cmd_msg_t), IPC_NOWAIT) < 0) {
        if (errno != EIDRM && errno != EINVAL) return -1;
        connect_queues();                              /* core restarted */
        if (msgsnd(q_core, &c, MSG_SIZE(cmd_msg_t), IPC_NOWAIT) < 0) return -1;
    }

    for (;;) {
        ssize_t n = msgrcv(q_ui, &r, MSG_SIZE(reply_msg_t), 0, IPC_NOWAIT);
        if (n < 0) {
            if (errno != ENOMSG) return -1;
            usleep(5000);
            waited += 5;
            if (waited > 3000) return -2;
            continue;
        }
        if (r.req_id != req_id) continue;
        if (r.len < 0 || r.len > CHUNK_LEN || len + (size_t)r.len + 1 > outsz) return -1;
        memcpy(out + len, r.data, (size_t)r.len);
        len += (size_t)r.len;
        if (r.last) break;
    }
    out[len] = 0;
    return 0;
}

/* ---------- HTTP helpers ---------- */
static void url_decode(char *dst, const char *src, size_t n)
{
    size_t o = 0;
    for (; *src && o + 1 < n; src++) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char h[3] = { src[1], src[2], 0 };
            dst[o++] = (char)strtol(h, NULL, 16);
            src += 2;
        } else if (*src == '+') {
            dst[o++] = ' ';
        } else {
            dst[o++] = *src;
        }
    }
    dst[o] = 0;
}

static void send_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, buf, n, 0);
        if (w <= 0) return;
        buf += w;
        n -= (size_t)w;
    }
}

static void http_send(int fd, int code, const char *text, const char *ctype,
                      const char *body, size_t len)
{
    char hdr[256];
    int h = snprintf(hdr, sizeof hdr,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     code, text, ctype, len);
    send_all(fd, hdr, (size_t)h);
    if (len) send_all(fd, body, len);
}

static void serve_gui(int fd)
{
    FILE *f = fopen(GUI_FILE, "rb");
    char *buf;
    long sz;
    if (!f) {
        const char *m = "gui/index.html not found. Start ./ui from the week4 folder.";
        http_send(fd, 404, "Not Found", "text/plain", m, strlen(m));
        return;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    buf = malloc((size_t)sz + 1);
    if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz)
        http_send(fd, 200, "OK", "text/html; charset=utf-8", buf, (size_t)sz);
    else
        http_send(fd, 500, "Error", "text/plain", "read error", 10);
    free(buf);
    fclose(f);
}

static void handle_request(int fd, const char *req)
{
    char method[8], path[2048];
    if (sscanf(req, "%7s %2047s", method, path) != 2) return;

    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        serve_gui(fd);
    } else if (!strncmp(path, "/api?cmd=", 9)) {
        static char out[OUT_MAX];
        char cmd[CMD_LEN];
        url_decode(cmd, path + 9, sizeof cmd);
        int rc = ask_core(cmd, out, sizeof out);
        if (rc != 0)
            snprintf(out, sizeof out, "{\"ok\":0,\"msg\":\"%s\"}",
                     rc == -2 ? "Core did not answer in time"
                              : "Core is not running. Start ./core first");
        http_send(fd, 200, "OK", "application/json", out, strlen(out));
        if (strcmp(cmd, "STATE") != 0)
            ipc_log(q_log, LOG_INFO, "UI", "GUI action: %.150s", cmd);
    } else if (!strcmp(path, "/favicon.ico")) {
        http_send(fd, 204, "No Content", "text/plain", "", 0);
    } else {
        http_send(fd, 404, "Not Found", "text/plain", "not found", 9);
    }
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8085;
    int srv, yes = 1;
    struct sockaddr_in addr;
    struct sigaction sa;

    signal(SIGPIPE, SIG_IGN);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    connect_queues();
    if (q_core < 0 || q_ui < 0) { perror("[ui] msgget"); return 1; }

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("[ui] socket"); return 1; }
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("[ui] bind"); return 1; }
    if (listen(srv, 8) < 0) { perror("[ui] listen"); return 1; }

    printf("[ui] GUI available at http://127.0.0.1:%d\n", port);
    fflush(stdout);
    ipc_log(q_log, LOG_INFO, "UI", "GUI server started on port %d", port);

    while (running) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("[ui] accept");
            break;
        }
        struct timeval tv = { 2, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char req[4096];
        ssize_t n = recv(fd, req, sizeof req - 1, 0);
        if (n > 0) {
            req[n] = 0;
            handle_request(fd, req);
        }
        close(fd);
    }
    close(srv);
    printf("[ui] stopped\n");
    return 0;
}
