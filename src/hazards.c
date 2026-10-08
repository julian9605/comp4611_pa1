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
    (void)cycle; (void)inst; (void)src1; (void)src2; (void)dst;
    (void)forwarding; (void)state;
    return 0;
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
