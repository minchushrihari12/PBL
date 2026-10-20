/* test_deadlock.c -- unit tests for deadlock.c (no IPC needed). Run: make test */
#include <stdio.h>
#include "deadlock.h"

static int fails = 0;
#define CHECK(c) do { if (c) printf("  PASS  %s\n", #c); \
                      else { printf("  FAIL  %s\n", #c); fails++; } } while (0)

int main(void)
{
    sys_state_t s;
    int seq[MAXP], len, dead[MAXP], g[MAXP];

    printf("Scenario 1: textbook safe state\n");
    ds_load_scenario(&s, 1);
    CHECK(ds_safety(&s, seq, &len, NULL, 0) == 1);
    CHECK(len == 5);
    CHECK(seq[0] == 1 && seq[1] == 3 && seq[2] == 4 && seq[3] == 0 && seq[4] == 2);
    CHECK(ds_detect(&s, dead) == 0);
    CHECK(ds_status(&s) == ST_SAFE);

    printf("Banker's resource requests\n");
    int big[3] = {9, 0, 0}, r1[3] = {1, 0, 2}, r4[3] = {3, 3, 0}, r0[3] = {0, 2, 0};
    CHECK(ds_request_banker(&s, 1, big) == REQ_EXCEEDS_NEED);
    CHECK(ds_request_banker(&s, 1, r1) == REQ_GRANTED);
    CHECK(ds_request_banker(&s, 4, r4) == REQ_NOT_AVAILABLE);
    CHECK(ds_request_banker(&s, 0, r0) == REQ_UNSAFE);
    CHECK(s.alloc[0][1] == 1);                       /* rolled back */

    printf("Scenario 2: unsafe but not deadlocked\n");
    ds_load_scenario(&s, 2);
    CHECK(ds_safety(&s, seq, &len, NULL, 0) == 0);
    CHECK(ds_detect(&s, dead) == 0);
    CHECK(ds_status(&s) == ST_UNSAFE);

    printf("Scenario 3: deadlock and recovery\n");
    ds_load_scenario(&s, 3);
    CHECK(ds_detect(&s, dead) == 3);
    CHECK(ds_status(&s) == ST_DEADLOCK);
    CHECK(ds_choose_victim(&s, dead) == 0);
    ds_terminate(&s, 0);
    CHECK(ds_wake(&s, g) == 1 && g[0] == 2);         /* P2 gets R0 */
    CHECK(ds_detect(&s, dead) == 0);

    printf("Raw requests block instead of being refused\n");
    ds_load_scenario(&s, 1);
    int r[3] = {7, 4, 3};
    CHECK(ds_request_raw(&s, 0, r) == REQ_BLOCKED);
    CHECK(ds_request_raw(&s, 0, r1) == REQ_BAD);     /* already blocked */

    printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
