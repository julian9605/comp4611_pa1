#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

/* PROVIDED: identify actual source registers before querying RAW hazards. */
RegUse register_use(const Inst *i)
{
    RegUse u = {{-1, -1}, 0, destination(i)};
    switch (i->op) {
    case ADD: case SUB: case FADD: case FSUB: case FMUL: case FDIV:
    case SW: case FST: case BEQ: case BNE:
        u.src[0] = i->s1; u.src[1] = i->s2; u.count = 2; break;
    case ADDI: case LW: case FLD:
        u.src[0] = i->s1; u.count = 1; break;
    }
    return u;
}

static void fail(const char *s)
{
    fprintf(stderr,"simulation error: %s\n",s); exit(2);
}
static bool forwarding_options(const char *value)
{
    if (!strcmp(value,"all")) return true;
    if (!strcmp(value,"none")) return false;
    fail("--forwarding expects all or none");
    return false;
}
static double int_result(double x)
{
    if (!isfinite(x) || x < INT32_MIN || x > INT32_MAX || trunc(x) != x)
        fail("integer overflow or noninteger LW (outside the supported input domain)");
    return x;
}
static int address(double base, int offset)
{
    double a = base + offset;
    if (!isfinite(a) || a != trunc(a) || a < 0 || a >= MEM_SIZE) fail("memory address out of bounds");
    return (int)a;
}
static void trace(FILE *t, int c, const char *stage, uint64_t seq, int pc, const char *detail)
{
    if (t) fprintf(t,"%d,%s,%" PRIu64 ",%d,%s\n",c,stage,seq,pc,detail);
}
/* One check at the start of each fetch attempt, then a saved release cycle.
 * A branch's ID plan is final before its following fetch attempt starts. */
typedef struct { bool pending; int resume_cycle; } FetchPlan;
static bool fetch_wait(FetchPlan *plan, FILE *t, int cycle, int branch_id_cycle,
                       bool resolved, int next_pc, int *control_stalls, bool explain)
{
    if (!plan->pending) {
        int stall = check_control_hazard(cycle, branch_id_cycle);
        if (stall < 0 || stall > MAX_CYCLES - cycle) fail("invalid control stall count");
        plan->resume_cycle = cycle + stall;
        plan->pending = true;
        if (explain)
            fprintf(stderr, "PLAN IF %d branch_ID=%d check_control_hazard=%d resume_IF=%d\n",
                    cycle, branch_id_cycle, stall, plan->resume_cycle);
    }
    int remaining = plan->resume_cycle - cycle;
    if (remaining <= 0 && branch_id_cycle >= 0 && (!resolved || cycle <= branch_id_cycle + 1))
        fail("fetch released before the branch completed EX");
    if (remaining > 0) {
        char detail[48]; snprintf(detail, sizeof(detail), "stall=%d", remaining);
        trace(t, cycle, "IF_STALL", 0, next_pc, detail);
        ++*control_stalls;
        return true;
    }
    plan->pending = false;
    return false;
}
static int active(const View *v)
{
    int n = 0;
    for (int k=0;k<MAX_FLIGHT;++k) n += v->flight[k].valid;
    return n;
}
/* Latch bypass inputs at the stage that actually consumes them. This happens
 * before WB removes producers and before this cycle's EX/MEM results appear. */
static void latch_forwarded(View *v, FILE *t, int *forwarded)
{
    for (int k=0;k<MAX_FLIGHT;++k) {
        Flight *consumer=&v->flight[k];
        if (!consumer->valid) continue;
        for (int j=0;j<2;++j) {
            Operand *operand=&consumer->operand[j];
            if (operand->kind==FROM_RF) continue;
            int use_cycle=operand->kind==FROM_WB_MEM ? consumer->mem_cycle : consumer->ex_start;
            if (use_cycle!=v->cycle) continue;
            const Flight *producer=NULL;
            for (int n=0;n<MAX_FLIGHT;++n)
                if (v->flight[n].valid && v->flight[n].seq==operand->producer)
                    producer=&v->flight[n];
            if (!producer || producer->ready_cycle>=v->cycle)
                fail("forwarding source is unavailable at its consumption stage");
            const char *route=NULL;
            if (operand->kind==FROM_MEM_EX && v->forwarding &&
                producer->inst.op!=LW && producer->inst.op!=FLD && producer->mem_cycle==v->cycle)
                route="FWD_MEM_EX";
            else if (operand->kind==FROM_WB_EX && v->forwarding && producer->wb_cycle==v->cycle)
                route="FWD_WB_EX";
            else if (operand->kind==FROM_WB_MEM && v->forwarding &&
                     j==1 && (consumer->inst.op==SW || consumer->inst.op==FST) && producer->wb_cycle==v->cycle)
                route="FWD_WB_MEM";
            if (!route) fail("forwarding route is unavailable in this cycle");
            operand->value=producer->result;
            if (j==0) consumer->a=operand->value;
            else consumer->b=operand->value;
            char detail[80];
            snprintf(detail,sizeof(detail),"src%d_from=%" PRIu64,j+1,operand->producer);
            trace(t,v->cycle,route,consumer->seq,consumer->pc,detail);
            ++*forwarded;
            operand->kind=FROM_RF; /* Value is now held in this instruction's latch. */
        }
    }
}
/* Supplied execution datapath. Timing visibility is controlled by ready_cycle. */
static void execute(Flight *f)
{
    if (f->operand[0].kind!=FROM_RF ||
        ((f->inst.op!=SW && f->inst.op!=FST) && f->operand[1].kind!=FROM_RF))
        fail("instruction reached EX without its required operands");
    switch(f->inst.op) {
    case ADD: f->result=int_result(f->a+f->b); break;
    case SUB: f->result=int_result(f->a-f->b); break;
    case ADDI: f->result=int_result(f->a+f->inst.imm); break;
    case FADD: f->result=f->a+f->b; break;
    case FSUB: f->result=f->a-f->b; break;
    case FMUL: f->result=f->a*f->b; break;
    case FDIV:
        if (f->b == 0) fail("division by zero (outside the supported input domain)");
        f->result=f->a/f->b; break;
    case LW: case SW: case FLD: case FST: f->address=address(f->a,f->inst.imm); break;
    case BEQ: case BNE: break;
    }
    if (!isfinite(f->result)) fail("nonfinite floating-point result");
}
int main(int argc, char **argv)
{
    const char *input=NULL, *tracepath=NULL;
    bool forwarding=true;
    bool explain=false;
    for (int a=1;a<argc;++a) {
        if (!strcmp(argv[a],"--no-forwarding")) forwarding=false;
        else if (!strcmp(argv[a],"--forwarding") && a+1<argc) forwarding=forwarding_options(argv[++a]);
        else if (!strcmp(argv[a],"--explain")) explain=true;
        else if (!strcmp(argv[a],"--trace") && a+1<argc) tracepath=argv[++a];
        else if (argv[a][0] != '-' && !input) input=argv[a];
        else fail("usage: sim [--forwarding all|none] [--no-forwarding] [--explain] [--trace file.csv] program.asm");
    }
    if (!input) fail("usage: sim [--forwarding all|none] [--no-forwarding] [--explain] [--trace file.csv] program.asm");
    if (tracepath && !strcmp(input,tracepath)) fail("trace path must differ from input path");
    FILE *f=fopen(input,"r"); if (!f) { perror(input); return 2; }
    Program p; load_program(f,&p); fclose(f);
    FILE *t=NULL;
    if (tracepath) { t=fopen(tracepath,"w"); if (!t) { perror(tracepath); return 2; } fprintf(t,"cycle,stage,seq,pc,detail\n"); }
    View v={0};
    v.forwarding=forwarding;
    v.explain=explain;
    memcpy(v.rf,p.initial_rf,sizeof(v.rf));
    double mem[MEM_SIZE]; memcpy(mem,p.initial_mem,sizeof(mem));
    int pc=0, id=-1, cycles=0, issued=0, completed=0, stalls=0;
    int reasons[3]={0}, fwd=0, control_stalls=0;
    FetchPlan fetch_plan = {false, 0};
    int branch_id_cycle=-1;
    bool branch_resolved=false;
    uint64_t next_seq=1,id_seq=0;
    while (id>=0 || pc<p.count || active(&v) || branch_id_cycle>=0) {
        if (++cycles>MAX_CYCLES) fail("cycle limit reached: check hazards or termination");
        v.cycle=cycles;
        int wb_count=0, mem_count=0;
        /* Read start-of-cycle occupancy for the trace. Physical slot order is stable. */
        for (int k=0;k<MAX_FLIGHT;++k) {
            Flight *x=&v.flight[k]; if (!x->valid) continue;
            char stage[20];
            if (cycles>=x->ex_start && cycles<=x->ex_end) {
                int n=cycles-x->ex_start+1;
                if (x->inst.op==FADD || x->inst.op==FSUB) snprintf(stage,sizeof(stage),"A%d",n);
                else if (x->inst.op==FMUL) snprintf(stage,sizeof(stage),"M%d",n);
                else if (x->inst.op==FDIV) snprintf(stage,sizeof(stage),"D%d",n);
                else strcpy(stage,"EX");
                trace(t,cycles,stage,x->seq,x->pc,op_name(x->inst.op));
            } else if (cycles==x->mem_cycle) { ++mem_count; trace(t,cycles,"MEM",x->seq,x->pc,op_name(x->inst.op)); }
            else if (cycles==x->wb_cycle) { ++wb_count; trace(t,cycles,"WB",x->seq,x->pc,op_name(x->inst.op)); }
        }
        if (wb_count>1 || mem_count>1) fail("unhandled shared MEM/WB structural hazard");
        latch_forwarded(&v,t,&fwd);
        /* WB is first-half-of-cycle; these writes are visible to ID in this cycle. */
        for (int k=0;k<MAX_FLIGHT;++k) {
            Flight *x=&v.flight[k];
            if (x->valid && x->wb_cycle==cycles) {
                int d=destination(&x->inst);
                if (d>=0) v.rf[d]=x->result;
                x->valid=false; ++completed;
            }
        }
        /* Materialize this edge's EX and MEM outputs, available to NEXT cycle's EX. */
        for (int k=0;k<MAX_FLIGHT;++k) {
            Flight *x=&v.flight[k]; if (!x->valid) continue;
            if (x->ex_end==cycles) {
                execute(x);
                if (x->inst.op==BEQ || x->inst.op==BNE) {
                    bool taken=x->inst.op==BEQ ? x->a==x->b : x->a!=x->b;
                    if (taken) pc=x->inst.target;
                    branch_resolved=true;
                    trace(t,cycles,"BRANCH_EX",x->seq,x->pc,taken ? "taken" : "not_taken");
                    if (explain)
                        fprintf(stderr,"cycle %d: branch resolves in EX: %s; next IF in a later cycle\n",
                                cycles,taken ? "taken" : "not taken");
                }
            }
            if (x->mem_cycle==cycles) {
                if (x->inst.op==LW) x->result=int_result(mem[x->address]);
                else if (x->inst.op==FLD) x->result=mem[x->address];
                else if (x->inst.op==SW || x->inst.op==FST) {
                    if (x->operand[1].kind!=FROM_RF) fail("store reached MEM without its data");
                    mem[x->address]=x->b;
                }
            }
        }
        if (id>=0) {
            Inst *i=&p.code[id];
            Decision d=decide_issue(&v,i);
            if (i->op==BEQ || i->op==BNE) branch_id_cycle=v.planned_issue;
            if (d.stall_cycles > 0) {
                if (d.hazards & ~7u) fail("unknown hazard bit");
                ++stalls;
                for (int b=0;b<3;++b) if (d.hazards & (1u<<b)) ++reasons[b];
                char detail[48]; snprintf(detail,sizeof(detail),"stall_mask=%u",d.hazards);
                trace(t,cycles,"ID",id_seq,id,detail);
                if (branch_id_cycle>=0)
                    (void)fetch_wait(&fetch_plan,t,cycles,branch_id_cycle,branch_resolved,pc,&control_stalls,explain);
                else if (pc<p.count) trace(t,cycles,"IF_HOLD",0,pc,"held");
                continue; /* Older instructions have already advanced. */
            }
            trace(t,cycles,"ID",id_seq,id,"issue");
            for (int j=0;j<2;++j)
                if (d.operand[j].kind==NOT_READY) fail("issued with NOT_READY operand");
            int slot;
            for (slot=0;slot<MAX_FLIGHT && v.flight[slot].valid;++slot) {}
            if (slot==MAX_FLIGHT) fail("internal in-flight capacity exceeded");
            int latency=ex_latency(i->op);
            Flight x={0}; x.valid=true; x.seq=id_seq; x.pc=id; x.inst=*i;
            x.issue=cycles; x.ex_start=cycles+1; x.ex_end=cycles+latency;
            x.mem_cycle=x.ex_end+1; x.wb_cycle=x.ex_end+2;
            x.ready_cycle=x.ex_end+((i->op==LW || i->op==FLD)?1:0);
            x.operand[0]=d.operand[0]; x.operand[1]=d.operand[1];
            x.a=d.operand[0].value; x.b=d.operand[1].value;
            v.flight[slot]=x; ++issued; id=-1;
        }
        if ((pc < p.count || branch_id_cycle >= 0 || fetch_plan.pending) &&
            fetch_wait(&fetch_plan,t,cycles,branch_id_cycle,branch_resolved,pc,&control_stalls,explain))
            continue;
        branch_id_cycle=-1;
        branch_resolved=false;
        if (pc<p.count) {
            id=pc++; id_seq=next_seq++;
            trace(t,cycles,"IF",id_seq,id,op_name(p.code[id].op));
            if (p.code[id].op==BEQ || p.code[id].op==BNE)
                branch_id_cycle=cycles+1; /* Refined by the ID plan next cycle. */
        }
    }
    if (t && fclose(t)) fail("trace write failure");
    printf("{\n  \"cycles\": %d, \"issued\": %d, \"completed\": %d,\n",cycles,issued,completed);
    printf("  \"stall_cycles\": %d, \"raw\": %d, \"waw\": %d, \"port\": %d,\n",stalls,reasons[0],reasons[1],reasons[2]);
    printf("  \"control_stall_cycles\": %d, \"forwarded_operands\": %d,\n  \"R\": [",control_stalls,fwd);
    for (int k=0;k<32;++k) printf("%s%.0f",k?", ":"",v.rf[k]);
    printf("],\n  \"F\": [");
    for (int k=32;k<NREG;++k) printf("%s%.17g",k>32?", ":"",v.rf[k]==0?0:v.rf[k]);
    printf("],\n  \"memory\": [");
    for (int k=0;k<MEM_SIZE;++k) printf("%s%.17g",k?", ":"",mem[k]==0?0:mem[k]);
    printf("]\n}\n");
    return 0;
}
