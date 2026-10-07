#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIZE 256
#define PROGRAM_SIZE 100

/* CPU structure */
typedef struct
{
    int PC;          /* Program Counter */
    int IR;          /* Instruction Register */
    int ACC;         /* Accumulator */
    int MAR;         /* Memory Address Register */
    int MDR;         /* Memory Data Register */
    int running;     /* CPU running status */

} CPU;

/* Instruction structure */
typedef struct
{
    char opcode[10];
    int operand;

} Instruction;

/* Global memory */
int memory[MEMORY_SIZE];

/* Program memory */
Instruction program[PROGRAM_SIZE];

int programSize = 0;


/* Function declarations */
void initializeCPU(CPU *cpu);
void displayHeader(void);
void displayRegisters(CPU *cpu);
void displayMemory(void);
void loadProgram(CPU *cpu);
void executeInstruction(CPU *cpu);
void runProgram(CPU *cpu);
void resetCPU(CPU *cpu);
void waitForEnter(void);


/* Initialize CPU */
void initializeCPU(CPU *cpu)
{
    cpu->PC = 0;
    cpu->IR = 0;
    cpu->ACC = 0;
    cpu->MAR = 0;
    cpu->MDR = 0;
    cpu->running = 1;

    memset(memory, 0, sizeof(memory));
    memset(program, 0, sizeof(program));

    programSize = 0;
}


/* Display title */
void displayHeader(void)
{
    system("cls");

    printf("\n");
    printf("==============================================\n");
    printf("             TOF CPU SIMULATOR               \n");
    printf("==============================================\n");
}


/* Display CPU registers */
void displayRegisters(CPU *cpu)
{
    printf("\n--------------- CPU REGISTERS ---------------\n");

    printf("Program Counter (PC)       : %03d\n", cpu->PC);
    printf("Instruction Register (IR)  : %03d\n", cpu->IR);
    printf("Accumulator (ACC)          : %03d\n", cpu->ACC);
    printf("Memory Address Register    : %03d\n", cpu->MAR);
    printf("Memory Data Register       : %03d\n", cpu->MDR);

    printf("CPU Status                 : ");

    if (cpu->running)
        printf("RUNNING\n");
    else
        printf("STOPPED\n");

    printf("----------------------------------------------\n");
}


/* Display memory */
void displayMemory(void)
{
    int i;

    printf("\n---------------- MEMORY ----------------------\n");

    for (i = 0; i < MEMORY_SIZE; i++)
    {
        if (memory[i] != 0)
        {
            printf("Memory[%03d] = %d\n", i, memory[i]);
        }
    }

    printf("----------------------------------------------\n");
}


/* Load a program */
void loadProgram(CPU *cpu)
{
    int i;
    int choice;

    displayHeader();

    printf("\nPROGRAM LOADING\n");
    printf("-------------------------------\n");

    printf("\nEnter number of instructions (1-%d): ",
           PROGRAM_SIZE);

    scanf_s("%d", &programSize);

    if (programSize < 1 || programSize > PROGRAM_SIZE)
    {
        printf("\nInvalid program size!\n");
        programSize = 0;
        waitForEnter();
        return;
    }

    printf("\nAvailable instructions:\n");
    printf("LOAD address\n");
    printf("STORE address\n");
    printf("ADD address\n");
    printf("SUB address\n");
    printf("JMP address\n");
    printf("JZ address\n");
    printf("INPUT\n");
    printf("OUTPUT\n");
    printf("HALT\n");

    printf("\n");

    for (i = 0; i < programSize; i++)
    {
        printf("Instruction %d: ", i);

        scanf_s("%9s", program[i].opcode,
                (unsigned)_countof(program[i].opcode));

        program[i].operand = 0;

        if (strcmp(program[i].opcode, "LOAD") == 0 ||
            strcmp(program[i].opcode, "STORE") == 0 ||
            strcmp(program[i].opcode, "ADD") == 0 ||
            strcmp(program[i].opcode, "SUB") == 0 ||
            strcmp(program[i].opcode, "JMP") == 0 ||
            strcmp(program[i].opcode, "JZ") == 0)
        {
            scanf_s("%d", &program[i].operand);
        }
    }

    cpu->PC = 0;
    cpu->ACC = 0;
    cpu->IR = 0;
    cpu->running = 1;

    printf("\nProgram loaded successfully!\n");

    waitForEnter();
}


/* Execute one instruction */
void executeInstruction(CPU *cpu)
{
    Instruction *instruction;

    if (programSize == 0)
    {
        printf("\nNo program loaded!\n");
        waitForEnter();
        return;
    }

    if (cpu->PC < 0 || cpu->PC >= programSize)
    {
        printf("\nProgram Counter out of range!\n");
        cpu->running = 0;
        waitForEnter();
        return;
    }

    instruction = &program[cpu->PC];

    cpu->IR = cpu->PC;

    printf("\n");
    printf("Executing: %s", instruction->opcode);

    if (instruction->operand != 0 ||
        strcmp(instruction->opcode, "LOAD") == 0 ||
        strcmp(instruction->opcode, "STORE") == 0 ||
        strcmp(instruction->opcode, "ADD") == 0 ||
        strcmp(instruction->opcode, "SUB") == 0 ||
        strcmp(instruction->opcode, "JMP") == 0 ||
        strcmp(instruction->opcode, "JZ") == 0)
    {
        printf(" %d", instruction->operand);
    }

    printf("\n");

    /* LOAD */
    if (strcmp(instruction->opcode, "LOAD") == 0)
    {
        if (instruction->operand >= 0 &&
            instruction->operand < MEMORY_SIZE)
        {
            cpu->MAR = instruction->operand;
            cpu->MDR = memory[cpu->MAR];
            cpu->ACC = cpu->MDR;
        }
        else
        {
            printf("Invalid memory address!\n");
            cpu->running = 0;
            return;
        }

        cpu->PC++;
    }

    /* STORE */
    else if (strcmp(instruction->opcode, "STORE") == 0)
    {
        if (instruction->operand >= 0 &&
            instruction->operand < MEMORY_SIZE)
        {
            cpu->MAR = instruction->operand;
            cpu->MDR = cpu->ACC;
            memory[cpu->MAR] = cpu->MDR;
        }
        else
        {
            printf("Invalid memory address!\n");
            cpu->running = 0;
            return;
        }

        cpu->PC++;
    }

    /* ADD */
    else if (strcmp(instruction->opcode, "ADD") == 0)
    {
        if (instruction->operand >= 0 &&
            instruction->operand < MEMORY_SIZE)
        {
            cpu->MAR = instruction->operand;
            cpu->MDR = memory[cpu->MAR];
            cpu->ACC = cpu->ACC + cpu->MDR;
        }
        else
        {
            printf("Invalid memory address!\n");
            cpu->running = 0;
            return;
        }

        cpu->PC++;
    }

    /* SUB */
    else if (strcmp(instruction->opcode, "SUB") == 0)
    {
        if (instruction->operand >= 0 &&
            instruction->operand < MEMORY_SIZE)
        {
            cpu->MAR = instruction->operand;
            cpu->MDR = memory[cpu->MAR];
            cpu->ACC = cpu->ACC - cpu->MDR;
        }
        else
        {
            printf("Invalid memory address!\n");
            cpu->running = 0;
            return;
        }

        cpu->PC++;
    }

    /* JMP */
    else if (strcmp(instruction->opcode, "JMP") == 0)
    {
        if (instruction->operand >= 0 &&
            instruction->operand < programSize)
        {
            cpu->PC = instruction->operand;
        }
        else
        {
            printf("Invalid jump address!\n");
            cpu->running = 0;
        }
    }

    /* JZ */
    else if (strcmp(instruction->opcode, "JZ") == 0)
    {
        if (cpu->ACC == 0)
        {
            if (instruction->operand >= 0 &&
                instruction->operand < programSize)
            {
                cpu->PC = instruction->operand;
            }
            else
            {
                printf("Invalid jump address!\n");
                cpu->running = 0;
            }
        }
        else
        {
            cpu->PC++;
        }
    }

    /* INPUT */
    else if (strcmp(instruction->opcode, "INPUT") == 0)
    {
        printf("Enter value for ACC: ");
        scanf_s("%d", &cpu->ACC);

        cpu->PC++;
    }

    /* OUTPUT */
    else if (strcmp(instruction->opcode, "OUTPUT") == 0)
    {
        printf("\nOUTPUT = %d\n", cpu->ACC);

        cpu->PC++;
    }

    /* HALT */
    else if (strcmp(instruction->opcode, "HALT") == 0)
    {
        printf("\nCPU HALTED.\n");
        cpu->running = 0;
    }

    /* Invalid instruction */
    else
    {
        printf("\nUnknown instruction: %s\n",
               instruction->opcode);

        cpu->running = 0;
    }

    printf("\nACC = %d", cpu->ACC);
    printf("\nPC  = %d\n", cpu->PC);
}


/* Run entire program */
void runProgram(CPU *cpu)
{
    int count = 0;

    if (programSize == 0)
    {
        printf("\nNo program loaded!\n");
        waitForEnter();
        return;
    }

    cpu->running = 1;

    printf("\n========== PROGRAM EXECUTION ==========\n");

    while (cpu->running)
    {
        executeInstruction(cpu);

        count++;

        /*
         * Safety limit to prevent an infinite loop
         */
        if (count > 1000)
        {
            printf("\nExecution stopped: possible infinite loop.\n");
            cpu->running = 0;
            break;
        }

        if (cpu->running)
        {
            printf("\nPress ENTER for next instruction...");
            getchar();
            getchar();
        }
    }

    printf("\n========== EXECUTION COMPLETE ==========\n");

    waitForEnter();
}


/* Reset CPU */
void resetCPU(CPU *cpu)
{
    initializeCPU(cpu);

    printf("\nCPU and memory have been reset.\n");

    waitForEnter();
}


/* Wait for user */
void waitForEnter(void)
{
    printf("\nPress ENTER to continue...");
    getchar();
    getchar();
}


/* Main function */
int main(void)
{
    CPU cpu;
    int choice;

    initializeCPU(&cpu);

    while (1)
    {
        displayHeader();

        displayRegisters(&cpu);

        printf("\n");
        printf("=============== MAIN MENU ===============\n");
        printf("1. Load Program\n");
        printf("2. View Registers\n");
        printf("3. View Memory\n");
        printf("4. Execute Next Instruction\n");
        printf("5. Run Program\n");
        printf("6. Reset CPU\n");
        printf("7. Exit\n");
        printf("==========================================\n");

        printf("\nEnter your choice: ");
        scanf_s("%d", &choice);

        switch (choice)
        {
        case 1:
            loadProgram(&cpu);
            break;

        case 2:
            displayHeader();
            displayRegisters(&cpu);
            waitForEnter();
            break;

        case 3:
            displayHeader();
            displayMemory();
            waitForEnter();
            break;

        case 4:
            displayHeader();
            executeInstruction(&cpu);
            waitForEnter();
            break;

        case 5:
            displayHeader();
            runProgram(&cpu);
            break;

        case 6:
            resetCPU(&cpu);
            break;

        case 7:
            printf("\nThank you for using TOF CPU Simulator!\n");
            return 0;

        default:
            printf("\nInvalid choice!\n");
            waitForEnter();
        }
    }

    return 0;
}