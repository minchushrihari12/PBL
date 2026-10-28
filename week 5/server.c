/* server.c - Web server written in C.
 * GET /     -> the page with an empty form (built by page.c)
 * GET /run  -> runs ./mqclient, which asks the core process (through message queues)
 *              for the finished HTML page, and sends it to the browser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include "page.h"

#define PORT      8000
#define MAX_JSON  262144
#define REQ_BUF   8192

static void send_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w <= 0) return;
        buf += w; n -= (size_t)w;
    }
}

static void respond(int fd, int code, const char *status, const char *type, const char *body, size_t len) {
    char head[256];
    int h = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        code, status, type, len);
    send_all(fd, head, (size_t)h);
    send_all(fd, body, len);
}

static void not_found(int fd) {
    const char *m = "404 Not Found";
    respond(fd, 404, "Not Found", "text/plain", m, strlen(m));
}

static void url_decode(const char *src, char *dst, size_t size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < size; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = { src[i + 1], src[i + 2], 0 };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = 0;
}

/* copies the decoded value of ?name=value into out */
static void get_param(const char *query, const char *name, char *out, size_t size) {
    size_t nl = strlen(name);
    const char *p = query;
    out[0] = 0;
    while (p && *p) {
        if (strncmp(p, name, nl) == 0 && p[nl] == '=') {
            const char *v = p + nl + 1;
            const char *end = strchr(v, '&');
            size_t len = end ? (size_t)(end - v) : strlen(v);
            char raw[6000];
            if (len >= sizeof raw) len = sizeof raw - 1;
            memcpy(raw, v, len); raw[len] = 0;
            url_decode(raw, out, size);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static void handle_run(int fd, const char *query) {
    char refs[1024], frames[16], algo[16];
    get_param(query, "refs", refs, sizeof refs);
    get_param(query, "frames", frames, sizeof frames);
    get_param(query, "algo", algo, sizeof algo);

    int pfd[2];
    if (pipe(pfd) < 0) { not_found(fd); return; }
    pid_t pid = fork();
    if (pid == 0) {                                   /* child: run mqclient, output into the pipe */
        close(pfd[0]);
        dup2(pfd[1], 1);
        close(pfd[1]);
        execl("./mqclient", "mqclient", refs, frames, algo, (char *)NULL);
        _exit(1);
    }
    close(pfd[1]);
    char *buf = malloc(MAX_JSON);
    size_t used = 0;
    ssize_t r;
    while (buf && used < MAX_JSON - 1 && (r = read(pfd[0], buf + used, MAX_JSON - 1 - used)) > 0)
        used += (size_t)r;
    close(pfd[0]);
    waitpid(pid, NULL, 0);

    if (!buf || used == 0) {
        const char *e = "<p>Empty reply from core. Is the core process running?</p>";
        respond(fd, 200, "OK", "text/html; charset=utf-8", e, strlen(e));
    } else {
        respond(fd, 200, "OK", "text/html; charset=utf-8", buf, used);
    }
    free(buf);
}

static void serve(int fd) {
    char req[REQ_BUF];
    size_t n = 0;
    while (n < sizeof req - 1) {
        ssize_t r = read(fd, req + n, sizeof req - 1 - n);
        if (r <= 0) break;
        n += (size_t)r;
        req[n] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[n] = 0;

    char method[8], path[2048];
    if (sscanf(req, "%7s %2047s", method, path) != 2 || strcmp(method, "GET") != 0) { not_found(fd); return; }

    if (strncmp(path, "/run?", 5) == 0) { handle_run(fd, path + 5); return; }
    if (strcmp(path, "/") == 0) {                     /* empty form with the sample filled in */
        const char *html = page_render("7 0 1 2 0 3 0 4 2 3", 3, 4, NULL, NULL, 0, NULL, NULL, 0);
        respond(fd, 200, "OK", "text/html; charset=utf-8", html, strlen(html));
        return;
    }
    not_found(fd);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(PORT);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("server: bind"); return 1; }
    if (listen(s, 16) < 0) { perror("server: listen"); return 1; }
    printf("server: open http://localhost:%d\n", PORT);
    fflush(stdout);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        if (fork() == 0) {                            /* one child per connection */
            close(s);
            signal(SIGCHLD, SIG_DFL);
            serve(c);
            close(c);
            _exit(0);
        }
        close(c);
    }
}
