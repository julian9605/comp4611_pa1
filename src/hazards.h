#ifndef HAZARDS_H
#define HAZARDS_H
#include <stdbool.h>

#define NUM_REGS 64 /* R0..R31: 0..31; F0..F31: 32..63. -1: absent. */
#define NUM_CYCLES 100064

/* Parsed instruction name only. Derive operation class and EX latency from
 * the documented hardware rules. Register operands are already identified. */
typedef enum { ADD, SUB, ADDI, LW, SW, FADD, FSUB, FMUL, FDIV,
               FLD, FST, BEQ, BNE } Op;

typedef struct {
    Op writer_op;     /* Opcode of the latest scheduled writer. */
    int write_cycle;  /* Its WB cycle; zero means the initial RF value is ready. */
} RegisterWriter;

typedef struct {
    RegisterWriter writers[NUM_REGS];
    bool wb_reserved[NUM_CYCLES];
} Scoreboard;

/* The three ID functions only read state and return a minimum stall >= 0.
 * Each checks its own constraint. Do not change records or reservations.
 * The framework calls them only during the instruction's first ID cycle.
 * Each is called ONCE: check_raw_hazard, then check_waw_hazard, then
 * check_wb_hazard. Each returned stall advances the cycle passed to the next
 * function. Records stay unchanged throughout these three calls.
 * Once all constraints are satisfied, the framework records the destination
 * writer/WB cycle and reserves the final WB slot exactly once.
 *
 * cycle is the candidate ID completion cycle, not necessarily the current
 * simulator cycle. A returned stall s advances that candidate to cycle+s.
 * forwarding is true to enable MEM->EX, WB->EX and WB->MEM together,
 * or false to disable all three paths.
 * All execution units, including the 24-cycle divider, are fully pipelined.
 * All instructions spend their opcode-specific latency in EX, then one cycle
 * in MEM and one in WB. Same-cycle WB writes before ID reads.
 * Ignore -1/R0 as sources or destinations; F0 (index 32) is a normal register.
 * Old write_cycle values remain stored after WB. Never clear/decrement them.
 */

/* Find the earliest ID cycle where both sources can arrive in time.
 * SW/FST use src1 in EX and src2 in MEM; store data can be captured earlier.
 * With all paths enabled or all disabled, a source that is available remains
 * available at later ID cycles. The later of the two source limits suffices. */
int check_raw_hazard(int cycle, Op inst, int src1, int src2, int dst,
                     bool forwarding, const Scoreboard *state);

/* The destination must write strictly after its previous writer.
 * Derive this instruction's WB cycle from its candidate ID and EX latency. */
int check_waw_hazard(int cycle, Op inst, int src1, int src2, int dst,
                     bool forwarding, const Scoreboard *state);

/* Find the first free WB slot at/after this instruction's candidate WB.
 * Every instruction uses the shared path, including stores and branches.
 * Only inspect reservations; the framework reserves the final slot. */
int check_wb_hazard(int cycle, Op inst, int src1, int src2, int dst,
                    bool forwarding, const Scoreboard *state);

/* Called once when a fetch attempt starts, before IF can proceed. The
 * framework saves cycle+stall and resumes IF then without calling again.
 * cycle is the first attempted IF cycle. branch_id_cycle is the pending
 * branch's final planned ID completion cycle; -1 means no pending branch.
 * Branches resolve at the END of their one-cycle EX; the next instruction
 * may start IF in the following cycle. Return the required fetch stall.
 * This stall is separate from the branch's own ID stall. Do not inspect
 * branch operands, forwarding, taken/not-taken, or register records. */
int check_control_hazard(int cycle, int branch_id_cycle);
#endif
