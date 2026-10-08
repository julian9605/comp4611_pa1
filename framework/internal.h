#ifndef INTERNAL_H
#define INTERNAL_H
/* PROVIDED implementation details. Not part of the student API. */
#include "hazards.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NREG NUM_REGS /* R0..R31 = 0..31; F0..F31 = 32..63 */
#define MEM_SIZE 256
#define MAX_CODE 1024
#define MAX_FLIGHT 64
#define MAX_CYCLES 100000

/* Parsed instruction used inside the provided framework. */
typedef struct {
    Op op;
    int dst, s1, s2, imm, target, line;
    /* Register indexes use the combined R/F numbering above.
     * Stores: s1 = integer base, s2 = data; unused register fields are -1. */
} Inst;
typedef struct {
    Inst code[MAX_CODE];
    int count;
    double initial_rf[NREG], initial_mem[MEM_SIZE];
} Program;
typedef struct {
    int src[2], count, dst; /* -1 means no architectural destination */
} RegUse;
typedef enum { FROM_RF, FROM_MEM_EX, FROM_WB_EX, FROM_WB_MEM, NOT_READY } OperandKind;
typedef struct {
    OperandKind kind;
    double value; /* RF value is latched at ID; bypass values are latched later. */
    uint64_t producer; /* Exact dynamic writer; never reselect by register name. */
} Operand;
/* An issued instruction awaiting WB; maintained by the framework. */
typedef struct {
    bool valid; /* Ignore a slot unless valid is true. */
    uint64_t seq; /* Dynamic age: larger is younger; slot index is NOT age. */
    int pc, issue, ex_start, ex_end, mem_cycle, wb_cycle, ready_cycle;
    /* ex_end: last EX cycle; wb_cycle: reserved register-write cycle.
     * ready_cycle: result exists at the END of this cycle. EX or store MEM
     * can consume it only in a later cycle through an enabled physical path. */
    Inst inst;
    Operand operand[2]; /* Selected bypasses are consumed in first EX or store MEM. */
    double a, b, result; /* Keep FP values as double, without integer casts. */
    int address;
} Flight;
/* Internal processor state; bridge.c exposes only scalar/array inputs. */
typedef struct {
    int cycle;
    bool forwarding; /* All forwarding paths on or all off. */
    bool explain; /* Optional ID input diagnostics. */
    Scoreboard scoreboard; /* Scheduling records maintained by the framework. */
    bool pending;
    int planned_issue;
    int raw_end, waw_end; /* End of each additional stall interval. */
    double rf[NREG]; /* R registers contain exact signed 32-bit integers */
    Flight flight[MAX_FLIGHT];
} View;
enum { H_RAW=1, H_WAW=2, H_PORT=4 };
typedef struct {
    unsigned hazards; /* constraints responsible for this cached planning delay */
    int stall_cycles; /* Remaining stall from the saved ID completion cycle. */
    Operand operand[2];
} Decision;

/* Supplied helpers. ID in c -> EX starts c+1 -> WB in c+latency+2. */
int ex_latency(Op op);
int destination(const Inst *i);
const char *op_name(Op op);
void load_program(FILE *f, Program *p);

/* Supplied instruction classification; no student implementation needed. */
RegUse register_use(const Inst *i);

/* Supplied adapter: combines read-only checks during the first ID cycle,
 * and captures RF operands or binds bypass producers on the issue cycle. */
Decision decide_issue(View *v, const Inst *i);
#endif
