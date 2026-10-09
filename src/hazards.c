/* COMP4611: Pipeline Hazards
 * Name: KWAN Bo Cheong Julian
 * ITSC email: bcjkwan@connect.ust.hk
 * Acknowledgements:
 * Complete three read-only ID checks and one IF check. Submit only this file.
 * Infer instruction timing from inst and the hardware rules in the handout.
 * You may add private helpers for classification and latency.
 */
#include "hazards.h"

int check_raw_hazard(int cycle, Op inst, int src1, int src2, int dst,
                     bool forwarding, const Scoreboard *state)
{
    /* TODO 1: Find the earliest ID cycle where BOTH old source values can
     * arrive through the RF or enabled forwarding paths. Distinguish store
     * base/data and exact path cycles. Return the required stall without
     * changing any register record or WB reservation. */
    // (void)cycle; (void)inst; (void)src1; (void)src2; (void)dst;
    // (void)forwarding; (void)state;
    
    // cycle: candidate cycle for ID    

    // first check src1 and src2, if they had been written earlier
    // *_last_written: their WB cycles
    int src1_last_written = state->writers[src1].write_cycle;
    int src2_last_written = state->writers[src2].write_cycle;
    int later_cycle; // the scheduled WB cycle
    Op prev_op;

    if (src1_last_written > src2_last_written) {
        later_cycle = src1_last_written;
        prev_op = state->writers[src1].writer_op;
    }
    else {
        later_cycle = src2_last_written;
        prev_op = state->writers[src2].writer_op;
    }

    int ex_cycles = 1;
    if (prev_op == FADD || prev_op == FSUB) {
        ex_cycles = 4;
    }
    else if (prev_op == FMUL) {
        ex_cycles = 7;
    }
    else if (prev_op == FDIV) {
        ex_cycles = 24;
    }

    int stalls = 0;
    if (forwarding) {
        // forwarding enabled
        // WB-to-EX
        if (inst == LW || inst == SW || inst == FLD || inst == FST) {
            
        }
        // MEM-to-EX
        else {

        }
        // WB-to-MEM
    }
    else {

    }
        
    return stalls;
}

int check_waw_hazard(int cycle, Op inst, int src1, int src2, int dst,
                     bool forwarding, const Scoreboard *state)
{
    /* TODO 2: Derive candidate WB and compare with the old destination writer.
     * Return the minimum stall without changing the records. */
    (void)cycle; (void)inst; (void)src1; (void)src2; (void)dst;
    (void)forwarding; (void)state;
    return 0;
}

int check_wb_hazard(int cycle, Op inst, int src1, int src2, int dst,
                    bool forwarding, const Scoreboard *state)
{
    /* TODO 3: Derive candidate WB and find its first free shared-path slot.
     * Return the minimum stall; the framework reserves the final slot.
     * Every instruction uses this path, including stores and branches. */
    (void)cycle; (void)inst; (void)src1; (void)src2; (void)dst;
    (void)forwarding; (void)state;
    return 0;
}

int check_control_hazard(int cycle, int branch_id_cycle)
{
    /* TODO 4: Derive when the pending EX-resolved branch releases IF.
     * Return the IF stall, or zero when fetching is allowed.
     * A branch_id_cycle of -1 means no branch is pending. */
    (void)cycle; (void)branch_id_cycle;
    return 0;
}
