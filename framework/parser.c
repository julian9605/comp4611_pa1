#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

const char *op_name(Op op)
{
    static const char *names[] = {"ADD","SUB","ADDI","LW","SW","ADD.D",
        "SUB.D","MUL.D","DIV.D","L.D","S.D","BEQ","BNE"};
    return names[op];
}
int ex_latency(Op op)
{
    if (op == FADD || op == FSUB) return 4;
    if (op == FMUL) return 7;
    if (op == FDIV) return 24;
    return 1;
}
int destination(const Inst *i)
{
    switch (i->op) {
    case ADD: case SUB: case ADDI: case LW: case FADD: case FSUB:
    case FMUL: case FDIV: case FLD: return i->dst == 0 ? -1 : i->dst;
    default: return -1;
    }
}
static void bad(int line, const char *why)
{
    fprintf(stderr, "input line %d: %s\n", line, why); exit(2);
}
static int reg(const char *s, char bank, int line)
{
    if (*s == '$') ++s;
    if (toupper((unsigned char)*s++) != bank || !isdigit((unsigned char)*s))
        bad(line, "expected register R0..R31 or F0..F31 in correct bank");
    char *end;
    long n = strtol(s, &end, 10);
    if (*end || n < 0 || n > 31) bad(line, "invalid register");
    return (int)n + (bank == 'F' ? 32 : 0);
}
static int integer(const char *s, int line)
{
    char *end; errno = 0;
    long n = strtol(s, &end, 10);
    if (errno || end == s || *end || n < INT_MIN || n > INT_MAX)
        bad(line, "invalid decimal integer");
    return (int)n;
}
static double number(const char *s, int line)
{
    char *end; errno = 0;
    double n = strtod(s, &end);
    if (errno || end == s || *end || !isfinite(n)) bad(line, "invalid number");
    return n;
}
void load_program(FILE *f, Program *p)
{
    struct Label { char name[80]; int pc; } labels[MAX_CODE];
    char targets[MAX_CODE][80];
    int nl = 0, line = 0;
    char buf[512];
    memset(p, 0, sizeof(*p)); memset(targets, 0, sizeof(targets));
    while (fgets(buf, sizeof(buf), f)) {
        ++line;
        if (!strchr(buf, '\n')) {
            int next = fgetc(f);
            if (next != EOF && next != '\n') bad(line, "line too long");
        }
        char *comment = strchr(buf, '#'); if (comment) *comment = 0;
        for (char *c = buf; *c; ++c) if (*c == ',' || *c == '(' || *c == ')') *c = ' ';
        char *tok[8]; int nt = 0;
        for (char *t = strtok(buf, " \t\r\n"); t; t = strtok(NULL, " \t\r\n")) {
            if (nt == 8) bad(line, "too many tokens");
            tok[nt++] = t;
        }
        if (!nt) continue;
        size_t len = strlen(tok[0]);
        if (tok[0][len-1] == ':') {
            if (len < 2 || len > 80 || nl == MAX_CODE) bad(line, "invalid label");
            tok[0][len-1] = 0;
            for (int j = 0; j < nl; ++j) if (!strcmp(labels[j].name,tok[0])) bad(line,"duplicate label");
            strcpy(labels[nl].name,tok[0]); labels[nl++].pc = p->count;
            for (int j = 1; j < nt; ++j) tok[j-1] = tok[j];
            if (!--nt) continue;
        }
        for (char *c = tok[0]; *c; ++c) *c = (char)toupper((unsigned char)*c);
        if (tok[0][0] == '.') {
            if (p->count || nt != 3) bad(line,"directives must precede instructions and have two arguments");
            double v = number(tok[2], line);
            if (!strcmp(tok[0],".REG")) {
                int r = reg(tok[1], 'R', line);
                if (v < INT32_MIN || v > INT32_MAX || v != trunc(v) || (r == 0 && v != 0)) bad(line,"invalid R initial value");
                p->initial_rf[r] = v;
            } else if (!strcmp(tok[0],".FREG")) p->initial_rf[reg(tok[1],'F',line)] = v;
            else if (!strcmp(tok[0],".MEM")) {
                int a = integer(tok[1],line);
                if (a < 0 || a >= MEM_SIZE) bad(line,"memory index out of range");
                p->initial_mem[a] = v;
            } else bad(line,"unknown directive");
            continue;
        }
        if (p->count == MAX_CODE) bad(line,"program too large");
        Inst i = {ADD, -1, -1, -1, 0, -1, line};
        int op;
        for (op = ADD; op <= BNE; ++op) if (!strcmp(tok[0],op_name((Op)op))) break;
        if (op > BNE) bad(line,"unknown opcode");
        i.op = (Op)op;
        if (nt != 4) bad(line,"incorrect operand count");
        switch(i.op) {
        case ADD: case SUB: case FADD: case FSUB: case FMUL: case FDIV: {
            char bank = i.op >= FADD ? 'F' : 'R';
            i.dst = reg(tok[1],bank,line); i.s1 = reg(tok[2],bank,line); i.s2 = reg(tok[3],bank,line); break;
        }
        case ADDI: i.dst=reg(tok[1],'R',line); i.s1=reg(tok[2],'R',line); i.imm=integer(tok[3],line); break;
        case LW: case FLD:
            i.dst=reg(tok[1],i.op==LW?'R':'F',line); i.imm=integer(tok[2],line); i.s1=reg(tok[3],'R',line); break;
        case SW: case FST:
            i.s2=reg(tok[1],i.op==SW?'R':'F',line); i.imm=integer(tok[2],line); i.s1=reg(tok[3],'R',line); break;
        case BEQ: case BNE:
            i.s1=reg(tok[1],'R',line); i.s2=reg(tok[2],'R',line);
            if (strlen(tok[3]) >= 80) bad(line,"target too long");
            strcpy(targets[p->count],tok[3]); break;
        }
        p->code[p->count++] = i;
    }
    if (ferror(f)) bad(line,"read failure");
    for (int k = 0; k < p->count; ++k) if (targets[k][0]) {
        for (int j = 0; j < nl; ++j) if (!strcmp(targets[k],labels[j].name)) p->code[k].target=labels[j].pc;
        if (p->code[k].target < 0) bad(p->code[k].line,"unknown label");
    }
}
