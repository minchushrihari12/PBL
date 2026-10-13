#include <stdio.h>
#include <string.h>
#include "scheduler.h"

static int fails = 0;
static double dabs(double x) { return x < 0 ? -x : x; }

static void logger(const char *text) { printf("    log: %s\n", text); }

/* exp_seg is a flat list: pid,start,end, pid,start,end ... */
static void run_case(const char *name, const proc_t *p, int n, algo_t algo, int q,
                     const int *exp_seg, int nseg, const int *exp_wt,
                     double avg_wt, double avg_tat, int show_log)
{
    sched_result_t r; char err[TEXT_LEN] = ""; int i, ok = 1;
    printf("%s\n", name);
    if (run_schedule(p, n, algo, q, &r, show_log ? logger : NULL, err, sizeof err) != 0) {
        printf("  FAIL: returned error: %s\n", err); fails++; return;
    }
    if (r.nseg != nseg) ok = 0;
    for (i = 0; ok && i < nseg; i++)
        if (r.segs[i].pid != exp_seg[3*i] || r.segs[i].start != exp_seg[3*i+1] || r.segs[i].end != exp_seg[3*i+2]) ok = 0;
    if (!ok) {
        printf("  FAIL: Gantt chart is wrong. Got:");
        for (i = 0; i < r.nseg; i++) printf(" [%d:%d-%d]", r.segs[i].pid, r.segs[i].start, r.segs[i].end);
        printf("\n"); fails++;
    }
    for (i = 0; i < n; i++)
        if (r.m[i].wt != exp_wt[i]) { printf("  FAIL: WT of P%d is %d, expected %d\n", r.m[i].pid, r.m[i].wt, exp_wt[i]); fails++; }
    if (dabs(r.avg_wt - avg_wt) > 0.01)   { printf("  FAIL: avg WT %.2f, expected %.2f\n", r.avg_wt, avg_wt); fails++; }
    if (dabs(r.avg_tat - avg_tat) > 0.01) { printf("  FAIL: avg TAT %.2f, expected %.2f\n", r.avg_tat, avg_tat); fails++; }
    if (ok) printf("  PASS (avg WT %.2f, avg TAT %.2f, CPU util %.1f%%)\n", r.avg_wt, r.avg_tat, r.cpu_util);
}

static void expect_error(const char *name, const proc_t *p, int n, algo_t algo, int q) {
    sched_result_t r; char err[TEXT_LEN] = "";
    printf("%s\n", name);
    if (run_schedule(p, n, algo, q, &r, NULL, err, sizeof err) == 0) { printf("  FAIL: should have returned an error\n"); fails++; }
    else printf("  PASS (error message: %s)\n", err);
}

int main(void) {
    /* The standard example: P1(arr 0, burst 5), P2(1,3), P3(2,1). Last field is priority. */
    proc_t a[3] = { {1,0,5,2}, {2,1,3,1}, {3,2,1,3} };

    { int s[] = {1,0,5, 2,5,8, 3,8,9};            int w[] = {0,4,6};
      run_case("FCFS", a, 3, ALGO_FCFS, 0, s, 3, w, 10.0/3, 19.0/3, 1); }
    { int s[] = {1,0,5, 3,5,6, 2,6,9};            int w[] = {0,5,3};
      run_case("SJF non-preemptive", a, 3, ALGO_SJF_NP, 0, s, 3, w, 8.0/3, 17.0/3, 0); }
    { int s[] = {1,0,1, 2,1,2, 3,2,3, 2,3,5, 1,5,9}; int w[] = {4,1,0};
      run_case("SJF preemptive (SRTF)", a, 3, ALGO_SJF_P, 0, s, 5, w, 5.0/3, 14.0/3, 0); }
    { int s[] = {1,0,5, 2,5,8, 3,8,9};            int w[] = {0,4,6};
      run_case("Priority non-preemptive", a, 3, ALGO_PRIO_NP, 0, s, 3, w, 10.0/3, 19.0/3, 0); }
    { int s[] = {1,0,1, 2,1,4, 1,4,8, 3,8,9};     int w[] = {3,0,6};
      run_case("Priority preemptive", a, 3, ALGO_PRIO_P, 0, s, 4, w, 3.0, 6.0, 0); }
    { int s[] = {1,0,2, 2,2,4, 3,4,5, 1,5,7, 2,7,8, 1,8,9}; int w[] = {4,4,2};
      run_case("Round Robin, quantum 2", a, 3, ALGO_RR, 2, s, 6, w, 10.0/3, 19.0/3, 0); }

    { proc_t b[1] = { {1,3,2,1} };
      int s[] = {IDLE_PID,0,3, 1,3,5};             int w[] = {0};
      run_case("Idle gap before first arrival (expect CPU util 40%)", b, 1, ALGO_FCFS, 0, s, 2, w, 0.0, 2.0, 0); }

    { proc_t bad[1] = { {1,0,0,1} };
      expect_error("Error: burst 0", bad, 1, ALGO_FCFS, 0);
      expect_error("Error: no processes", a, 0, ALGO_FCFS, 0);
      expect_error("Error: Round Robin quantum 0", a, 3, ALGO_RR, 0);
      expect_error("Error: unknown algorithm", a, 3, (algo_t)99, 0); }
    { proc_t dup[2] = { {1,0,2,1}, {1,1,2,1} };
      expect_error("Error: duplicate process ID", dup, 2, ALGO_FCFS, 0); }

    printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
