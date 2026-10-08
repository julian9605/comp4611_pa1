/* PROVIDED: plan once when an instruction first reaches ID. Student functions
 * only read prior records. This file commits the final schedule and later
 * captures operands without calling student functions again. */
#include "internal.h"
#include <stdlib.h>

static void invalid(const char *message)
{
    fprintf(stderr, "simulation error: %s\n", message);
    exit(2);
}
static int checked_stall(int stall, int cycle)
{
    if (stall < 0 || stall > MAX_CYCLES - cycle)
        invalid("invalid stall count or cycle limit exceeded");
    return stall;
}

static int plan_instruction(View *v, const Inst *inst, RegUse u)
{
    int src1 = u.count > 0 ? u.src[0] : -1;
    int src2 = u.count > 1 ? u.src[1] : -1;
    int candidate = v->cycle;
    /* Earlier instructions' records do not change during these three calls.
     * Only the candidate ID cycle advances. With all/no forwarding, later
     * cycles preserve source readiness and destination write order. */
    int raw = checked_stall(check_raw_hazard(candidate, inst->op, src1, src2,
                           u.dst, v->forwarding, &v->scoreboard), candidate);
    candidate += raw;
    v->raw_end = candidate;
    int waw = checked_stall(check_waw_hazard(candidate, inst->op, src1, src2,
                           u.dst, v->forwarding, &v->scoreboard), candidate);
    candidate += waw;
    v->waw_end = candidate;
    int port = checked_stall(check_wb_hazard(candidate, inst->op, src1, src2,
                            u.dst, v->forwarding, &v->scoreboard), candidate);
    candidate += port;
    if (v->explain)
        fprintf(stderr, "PLAN ID %d check_raw_hazard(%d)=%d check_waw_hazard(%d)=%d check_wb_hazard(%d)=%d complete_ID=%d\n",
                v->cycle, v->cycle, raw, v->raw_end, waw, v->waw_end, port, candidate);
    int wb = candidate + ex_latency(inst->op) + 2;
    if (wb >= NUM_CYCLES) invalid("writeback cycle exceeds record table");
    if (u.dst > 0) v->scoreboard.writers[u.dst] = (RegisterWriter){inst->op, wb};
    v->scoreboard.wb_reserved[wb] = true;
    return candidate;
}

Decision decide_issue(View *v, const Inst *i)
{
    RegUse u = register_use(i);
    if (!v->pending) {
        v->planned_issue = plan_instruction(v, i, u);
        v->pending = true;
    }
    unsigned reason = v->cycle < v->raw_end ? H_RAW :
                      v->cycle < v->waw_end ? H_WAW :
                      v->cycle < v->planned_issue ? H_PORT : 0;
    Decision d = {reason, v->planned_issue - v->cycle,
                  {{FROM_RF, 0, 0}, {FROM_RF, 0, 0}}};
    if (v->explain)
        fprintf(stderr, "ID %d %s complete_ID=%d remaining_stall=%d\n",
                v->cycle, op_name(i->op), v->planned_issue, d.stall_cycles);
    if (d.stall_cycles > 0) return d;
    /* Capture the actual values/routes only when ID completes. The plan has
     * already established source availability; retain physical route checks
     * so an incorrect student answer cannot silently use unavailable data. */
    for (int k = 0; k < u.count; ++k) {
        int r = u.src[k];
        if (r <= 0) continue;
        const Flight *writer = NULL;
        for (int n = 0; n < MAX_FLIGHT; ++n) {
            const Flight *f = &v->flight[n];
            if (f->valid && destination(&f->inst) == r &&
                (!writer || f->seq > writer->seq)) writer = f;
        }
        if (writer) {
            /* Choose the exact physical route, then retain the producer's
             * identity. Its result may not exist yet at this ID cycle. */
            bool load = writer->inst.op == LW || writer->inst.op == FLD;
            bool store_data = k == 1 && (i->op == SW || i->op == FST);
            if (v->forwarding && !load && writer->mem_cycle == v->cycle + 1)
                d.operand[k].kind = FROM_MEM_EX;
            else if (v->forwarding && writer->wb_cycle == v->cycle + 1)
                d.operand[k].kind = FROM_WB_EX;
            else if (v->forwarding && store_data && writer->wb_cycle == v->cycle + 2)
                d.operand[k].kind = FROM_WB_MEM;
            else d.operand[k].kind = NOT_READY;
            d.operand[k].producer = writer->seq;
        } else d.operand[k].value = v->rf[r];
    }
    v->pending = false;
    return d;
}
