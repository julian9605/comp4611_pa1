/* COMP4611: Pipeline Hazards
 * Name: KWAN Bo Cheong Julian
 * ITSC email: bcjkwan@connect.ust.hk
 * Acknowledgements:
 * Complete three read-only ID checks and one IF check. Submit only this file.
 * Infer instruction timing from inst and the hardware rules in the handout.
 * You may add private helpers for classification and latency.
 */
#include "hazards.h"

/* Return the later of two candidate cycles. */
static int max_int(int a, int b)
{
    return a > b ? a : b;
}

/* Return the number of EX cycles occupied by an instruction. */
static int ex_cycles(Op op)
{
    if (op == FADD || op == FSUB) return 4;
    if (op == FMUL) return 7;
    if (op == FDIV) return 24;
    return 1;
}

/* Loads produce their value at the end of MEM, not at the end of EX. */
static bool is_load(Op op)
{
    return op == LW || op == FLD;
}

/* -1 means no operand; integer R0 is constant and cannot create a hazard. */
static bool is_ignored_register(int reg)
{
    return reg < 0 || reg == 0;
}

static int source_ready_cycle(int cycle, int source, bool store_data,
                              bool forwarding, const Scoreboard *state)
{
    if (is_ignored_register(source)) return cycle;

    RegisterWriter writer = state->writers[source];
    if (writer.write_cycle == 0) return cycle;
    if (!forwarding) return writer.write_cycle;

    if (store_data) {
        /* Store data is consumed in MEM, so WB->MEM is available two
         * cycles after the consumer completes ID. */
        return writer.write_cycle - 2;
    }

    /* All non-store operands are consumed at the beginning of EX. */
    return writer.write_cycle - (is_load(writer.writer_op) ? 1 : 2);
}

int check_raw_hazard(int cycle, Op inst, int src1, int src2, int dst,
                     bool forwarding, const Scoreboard *state)
{
    /* TODO 1: Find the earliest ID cycle where BOTH old source values can
     * arrive through the RF or enabled forwarding paths. Distinguish store
     * base/data and exact path cycles. Return the required stall without
     * changing any register record or WB reservation. */
    (void)dst;
    int required = source_ready_cycle(cycle, src1, false, forwarding, state);
    if (inst == SW || inst == FST)
        required = max_int(required, source_ready_cycle(cycle, src2, true,
                                                        forwarding, state));
    else
        required = max_int(required, source_ready_cycle(cycle, src2, false,
                                                        forwarding, state));
    return required > cycle ? required - cycle : 0;
}

int check_waw_hazard(int cycle, Op inst, int src1, int src2, int dst,
                     bool forwarding, const Scoreboard *state)
{
    /* TODO 2: Derive candidate WB and compare with the old destination writer.
     * Return the minimum stall without changing the records. */
    (void)src1;
    (void)src2;
    (void)forwarding;
    if (is_ignored_register(dst)) return 0;

    int candidate_wb = cycle + ex_cycles(inst) + 2;
    int previous_wb = state->writers[dst].write_cycle;
    return candidate_wb <= previous_wb ? previous_wb + 1 - candidate_wb : 0;
}

int check_wb_hazard(int cycle, Op inst, int src1, int src2, int dst,
                    bool forwarding, const Scoreboard *state)
{
    /* TODO 3: Derive candidate WB and find its first free shared-path slot.
     * Return the minimum stall; the framework reserves the final slot.
     * Every instruction uses this path, including stores and branches. */
    (void)src1;
    (void)src2;
    (void)dst;
    (void)forwarding;
    int stall = 0;
    int candidate_wb = cycle + ex_cycles(inst) + 2;
    while (candidate_wb < NUM_CYCLES && state->wb_reserved[candidate_wb]) {
        ++stall;
        ++candidate_wb;
    }
    return stall;
}

int check_control_hazard(int cycle, int branch_id_cycle)
{
    /* TODO 4: Derive when the pending EX-resolved branch releases IF.
     * Return the IF stall, or zero when fetching is allowed.
     * A branch_id_cycle of -1 means no branch is pending. */
    if (branch_id_cycle < 0) return 0;
    int release_cycle = branch_id_cycle + 2;
    return release_cycle > cycle ? release_cycle - cycle : 0;
}
