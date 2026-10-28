/* page.c - Member 3. Generates the website (HTML + CSS + SVG chart) in C. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "page.h"

#define HTML_SIZE 200000

static char html[HTML_SIZE];
static int  pos;

static void put(const char *fmt, ...) {
    if (pos >= HTML_SIZE - 1) return;
    va_list ap; va_start(ap, fmt);
    pos += vsnprintf(html + pos, HTML_SIZE - pos, fmt, ap);
    va_end(ap);
    if (pos > HTML_SIZE - 1) pos = HTML_SIZE - 1;
}

static void put_escaped(const char *s) {          /* never print user text unescaped */
    for (; *s; s++) {
        switch (*s) {
            case '<':  put("&lt;");   break;
            case '>':  put("&gt;");   break;
            case '&':  put("&amp;");  break;
            case '"':  put("&quot;"); break;
            case '\'': put("&#39;");  break;
            default:   put("%c", *s);
        }
    }
}

static const char CSS[] =
    ":root{--bg:#eef2f7;--card:#fff;--ink:#14213d;--muted:#5b6b85;--line:#d5dde9;--accent:#1d5fa8;"
    "--fault-bg:#fbe3e6;--fault-ink:#a3243a;--hit-bg:#dcf3e8;--hit-ink:#1b6b47}"
    "@media(prefers-color-scheme:dark){:root{--bg:#0f1724;--card:#172236;--ink:#e6edf7;--muted:#93a3bd;"
    "--line:#2a3a55;--accent:#6aa9ee;--fault-bg:#4a1f29;--fault-ink:#ff9aab;--hit-bg:#17402e;--hit-ink:#8fe0b8}}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--ink);font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;line-height:1.5}"
    "main{max-width:900px;margin:0 auto;padding:32px 16px 64px}"
    "h1{margin:0;font-size:1.9rem}h2{font-size:1.15rem;margin:28px 0 10px}"
    ".sub{margin:4px 0 24px;color:var(--muted)}"
    ".panel{display:grid;grid-template-columns:1fr 1fr;gap:16px;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:20px}"
    ".wide{grid-column:1/-1}"
    "label{display:block;font-size:.9rem;margin-bottom:4px;color:var(--muted)}"
    "input,select{width:100%;padding:10px 12px;font:inherit;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:6px}"
    "input:focus,select:focus,button:focus-visible{outline:2px solid var(--accent);outline-offset:1px}"
    "button{grid-column:1/-1;padding:12px;font:inherit;font-weight:600;cursor:pointer;color:#fff;background:var(--accent);border:0;border-radius:6px}"
    "button:hover{filter:brightness(1.1)}"
    "@media(max-width:520px){.panel{grid-template-columns:1fr}}"
    ".error{margin:14px 0 0;color:var(--fault-ink);font-weight:600}"
    ".scroll{overflow-x:auto}"
    "table{width:100%;border-collapse:collapse;background:var(--card);border:1px solid var(--line)}"
    "th,td{padding:8px 12px;text-align:center;border-bottom:1px solid var(--line)}"
    "th{background:var(--bg);font-weight:600}"
    "td.empty{color:var(--muted)}"
    "td.fault{background:var(--fault-bg);color:var(--fault-ink);font-weight:600}"
    "td.hit{background:var(--hit-bg);color:var(--hit-ink);font-weight:600}"
    "tr.best td{font-weight:700}"
    ".stats{margin:8px 0 0;color:var(--muted);font-size:.95rem}"
    ".chart{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:16px;max-width:560px}"
    "svg{width:100%;height:auto}svg text{fill:var(--ink);font-size:12px}"
    ".bf{fill:var(--fault-ink)}.bh{fill:var(--hit-ink)}svg line{stroke:var(--line)}";

static void put_form(const char *refs_str, int frames, int algo) {
    static const char *labels[4] = { "FIFO", "LRU", "Optimal", "Compare all" };
    put("<form class='panel' method='get' action='/run'>"
        "<div class='wide'><label for='refs'>Page-reference string</label>"
        "<input id='refs' name='refs' type='text' placeholder='e.g. 7 0 1 2 0 3 0 4 2 3' value='");
    put_escaped(refs_str);
    put("'></div><div><label for='frames'>Page frames</label>"
        "<input id='frames' name='frames' type='number' min='1' max='10' value='%d'></div>"
        "<div><label for='algo'>Algorithm</label><select id='algo' name='algo'>", frames);
    for (int a = 1; a <= 4; a++)
        put("<option value='%d'%s>%s</option>", a, a == algo ? " selected" : "", labels[a - 1]);
    put("</select></div><button type='submit'>Run simulation</button></form>");
}

static void put_steps(const char *name, const Result *r, const int *refs) {
    put("<h2>%s step by step</h2><div class='scroll'><table><thead><tr><th>Page</th>", name);
    for (int j = 1; j <= r->frames; j++) put("<th>Frame %d</th>", j);
    put("<th>Status</th></tr></thead><tbody>");
    for (int i = 0; i < r->n; i++) {
        put("<tr><td>%d</td>", refs[i]);
        for (int j = 0; j < r->frames; j++) {
            if (r->snapshot[i][j] < 0) put("<td class='empty'>-</td>");
            else put("<td>%d</td>", r->snapshot[i][j]);
        }
        put(r->fault[i] ? "<td class='fault'>Fault</td></tr>" : "<td class='hit'>Hit</td></tr>");
    }
    put("</tbody></table></div><p class='stats'>Hits: %d &middot; Faults: %d</p>", r->hits, r->faults);
}

static void put_summary(const Result *res, const char **names, int count, int n) {
    int best = res[0].faults;
    for (int i = 1; i < count; i++) if (res[i].faults < best) best = res[i].faults;
    put("<h2>Comparison</h2><div class='scroll'><table><thead><tr><th>Algorithm</th><th>Page hits</th>"
        "<th>Page faults</th><th>Hit ratio</th><th>Fault ratio</th></tr></thead><tbody>");
    for (int i = 0; i < count; i++) {
        int isbest = res[i].faults == best;
        put("<tr%s><td>%s%s</td><td>%d</td><td>%d</td><td>%.1f%%</td><td>%.1f%%</td></tr>",
            isbest ? " class='best'" : "", names[i], isbest ? " (fewest faults)" : "",
            res[i].hits, res[i].faults, 100.0 * res[i].hits / n, 100.0 * res[i].faults / n);
    }
    put("</tbody></table></div>");
}

static void put_chart(const Result *res, const char **names, int count) {
    const int W = 560, H = 260, L = 40, B = 40, T = 28;
    double plotH = H - B - T, plotW = W - L - 10;
    int maxv = 1;
    for (int i = 0; i < count; i++) {
        if (res[i].faults > maxv) maxv = res[i].faults;
        if (res[i].hits > maxv) maxv = res[i].hits;
    }
    put("<h2>Faults and hits</h2><div class='chart'><svg viewBox='0 0 %d %d' role='img' "
        "aria-label='Bar chart of page faults and hits per algorithm'>", W, H);
    put("<line x1='%d' y1='%.0f' x2='%d' y2='%.0f'/>", L, T + plotH, W - 10, T + plotH);
    put("<text x='%d' y='%d'>0</text><text x='%d' y='%d'>%d</text>", L - 18, (int)(T + plotH + 4), L - 18, T + 4, maxv);
    put("<rect class='bf' x='%d' y='6' width='10' height='10'/><text x='%d' y='15'>Faults</text>", L, L + 14);
    put("<rect class='bh' x='%d' y='6' width='10' height='10'/><text x='%d' y='15'>Hits</text>", L + 80, L + 94);
    for (int i = 0; i < count; i++) {
        double gw = plotW / count, gx = L + i * gw, mid = gx + gw / 2, bw = gw * 0.26;
        double fh = res[i].faults * plotH / maxv, hh = res[i].hits * plotH / maxv;
        put("<rect class='bf' x='%.1f' y='%.1f' width='%.1f' height='%.1f'/>", mid - bw - 3, T + plotH - fh, bw, fh);
        put("<rect class='bh' x='%.1f' y='%.1f' width='%.1f' height='%.1f'/>", mid + 3, T + plotH - hh, bw, hh);
        put("<text x='%.1f' y='%.1f' text-anchor='middle'>%d</text>", mid - bw / 2 - 3, T + plotH - fh - 4, res[i].faults);
        put("<text x='%.1f' y='%.1f' text-anchor='middle'>%d</text>", mid + 3 + bw / 2, T + plotH - hh - 4, res[i].hits);
        put("<text x='%.1f' y='%d' text-anchor='middle'>%s</text>", mid, H - B + 20, names[i]);
    }
    put("</svg></div>");
}

const char *page_render(const char *refs_str, int frames, int algo, const char *error,
                        const int *refs, int n, const Result *results, const char **names, int count) {
    pos = 0;
    put("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Page Replacement Simulator</title><style>%s</style></head><body><main>", CSS);
    put("<h1>Page replacement simulator</h1>"
        "<p class='sub'>Compare FIFO, LRU and Optimal on the same reference string.</p>");
    put_form(refs_str, frames, algo);

    if (error) {
        put("<p class='error' role='alert'>");
        put_escaped(error);
        put("</p>");
    } else if (count > 0) {
        if (count > 1) { put_summary(results, names, count, n); put_chart(results, names, count); }
        for (int i = 0; i < count; i++) put_steps(names[i], &results[i], refs);
        if (count == 1)
            put("<p class='stats'>Total references: %d &middot; Hit ratio: %.1f%% &middot; Fault ratio: %.1f%%</p>",
                n, 100.0 * results[0].hits / n, 100.0 * results[0].faults / n);
    }
    put("</main></body></html>");
    return html;
}
