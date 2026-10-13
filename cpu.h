#ifndef CPU_H
#define CPU_H

/* A small stand-in CPU for the Core process (registers, PC, IR, memory,
   stack, queue). If you already have your own Week 1-2 CPU code, replace
   the inside of cpu.c with it and keep these function names. */

#include <stddef.h>
#include <stdint.h>
#include "ipc_defs.h"          /* NUM_REGS, TEXT_LEN */

typedef enum { CPU_OK, CPU_HALTED, CPU_ERROR } cpu_status_t;

/* Load a program from a text file (see sample.asm). Returns 0 on success,
   -1 on failure with a message in err. A successful load also resets the CPU. */
int cpu_load_file(const char *filename, char *err, size_t errlen);

/* Clear registers, PC, IR, stack, queue and data memory. Program stays loaded. */
void cpu_reset(void);

/* Execute exactly ONE instruction. */
cpu_status_t cpu_step(char *err, size_t errlen);

/* Text about the instruction executed by the last cpu_step ("" if none),
   e.g. "PC=3 ADD R1,R0". */
const char *cpu_last_desc(void);

int cpu_program_size(void);

void cpu_get_state(uint32_t *pc, uint32_t *ir, uint32_t regs[NUM_REGS],
                   uint32_t *sp, uint32_t *flags);

#endif /* CPU_H */
