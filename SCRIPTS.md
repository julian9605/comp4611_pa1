# Scripts and output

The complete hardware, function and grading rules are in [README.md](README.md). This document covers commands and diagnostics. Run commands from the project root directory, which contains `Makefile`, and rebuild after editing `src/hazards.c`.

## Build and inspect one function

```sh
make
make score
python3 tools/score_stalls.py --group raw
python3 tools/check_unit.py --group raw
make test-unit
```

| Command or option | Use |
|---|---|
| `make score` | Public stall-score percentage |
| `score_stalls.py --group raw\|waw\|wb\|control\|all` | Score one function or all; default `all` |
| `--source PATH` | Compile another C submission; default `src/hazards.c` |
| `--cases PATH` | Read another direct-call JSON file |
| `check_unit.py --group raw\|waw\|wb\|control\|all` | Exact stalls and unchanged input records; default `all` |
| `make test-unit` | All strict function diagnostics |
| `make test` | Function diagnostics plus public program regressions |

Both Python tools accept `--source`, `--cases` and `--group`. Public inputs and expected results are in `tests/hazard_cases.json`. When a strict case fails, read the named call and first differing field. Each data-hazard call supplies its own `state` in JSON. The strict checker verifies both the returned stall and that this input remains unchanged. Control has no record input. See the README for the distinction between scores and diagnostics.

Captured `make score` output from the unimplemented starter, excerpt:

```text
raw: 20.00% | exact 50, over 0, under 26, invalid 0; extra cycles 0; 76 calls in 42 cases
  optimal=0: 50 calls, 100.00%; optimal>0: 26 calls, 0.00% (weights 20% / 80%)
```

The starter returns zero, so it matches zero-stall calls and fails calls requiring a stall. `exact`, `over`, and `under` count returns equal to, greater than, and less than the expected stall. `invalid` counts negative or noninteger results, crashes, and timeouts. The README explains the scoring rule.

## Run a program

```sh
./sim tests/01_alu_chain.asm
./sim --forwarding none --explain tests/01_alu_chain.asm
./sim --forwarding all --trace run.csv tests/13_store_operands.asm
```

| Simulator option | Use |
|---|---|
| `--forwarding all` | Enable all paths; default |
| `--forwarding none` | Disable all paths |
| `--no-forwarding` | Alias for `--forwarding none` |
| `--explain` | Scheduling diagnostics on stderr |
| `--trace PATH` | Write cycle events as CSV |

JSON results go to stdout; redirect them with `> run.out` if needed. It includes final registers/memory, cycles, issue/completion counts and diagnostic counters. `stall_cycles` counts ID stall cycles; `control_stall_cycles` counts fetch stall cycles. These can overlap. The `raw`, `waw`, and `port` counters record the additional stalls returned by `check_raw_hazard`, `check_waw_hazard`, and `check_wb_hazard`. Their intervals are consecutive, so their sum equals `stall_cycles`. Later cycles reuse the saved plan.

`PLAN ID` shows calls made during the first ID cycle, one call per function, in order, with the advanced candidate and returned stall. Later `ID` lines report the saved completion cycle and remaining stall without calling student functions. `PLAN IF` appears once per fetch attempt and shows the saved fetch resumption cycle. For a surprising result, compare the preceding writers with the enabled paths, then inspect the instruction's ID completion and execution cycles in CSV.

These are captured results from a **completed correct implementation**, not the starter:

```sh
./sim --forwarding all tests/31_tutorial_forwarding.asm
./sim --forwarding none tests/31_tutorial_forwarding.asm
```

| Selected paths | `cycles` | `stall_cycles` | `forwarded_operands` |
|---|---:|---:|---:|
| `all` | 9 | 0 | 3 |
| `none` | 13 | 4 | 0 |

Both runs complete five instructions with identical final values. Use this comparison to check whether enabling forwarding affects timing while preserving computation.

## Read a trace

CSV columns are `cycle,stage,seq,pc,detail`. PC is a zero-based instruction index; `seq` distinguishes repeated executions of the same instruction. Events need not be listed in stage order.

| Entry | Meaning |
|---|---|
| `ID` / `issue` | Instruction leaves ID this cycle |
| `ID` / `stall_mask=N` | Cached planning reasons: `check_raw_hazard`=1, `check_waw_hazard`=2, `check_wb_hazard`=4; one reason per ID stall cycle |
| `FWD_MEM_EX`, `FWD_WB_EX`, `FWD_WB_MEM` | Path actually used in the event's cycle |
| EX, A1–A4, M1–M7, D1–D24, MEM, WB | Occupied stage; A/M/D number the FP execution cycles |
| `IF_HOLD` | Fetch held by pipeline occupancy |
| `IF_STALL` / `stall=N` | Fetch held for a pending branch |
| `BRANCH_EX` | Branch outcome determined in EX |

With all paths enabled, the same completed implementation produces these lines for program 31:

```csv
5,FWD_WB_MEM,2,1,src2_from=1
5,FWD_WB_EX,3,2,src1_from=1
6,FWD_MEM_EX,4,3,src1_from=3
```

For example, the first line records the store's second source arriving during cycle 5. The last field identifies the supplying dynamic instruction.

For control, run `./sim --trace branch.csv tests/28_independent_branch.asm`. Captured excerpt from a correct run:

```csv
2,IF_STALL,0,1,stall=2
3,BRANCH_EX,1,0,taken
3,IF_STALL,0,3,stall=1
4,IF,2,3,ADDI
```

Fetch stalls in cycles 2–3 and starts the target in 4. The JSON reports `stall_cycles: 0` and `control_stall_cycles: 2`: this stall belongs to IF, not ID.

## Run regression tests

```sh
python3 tools/check.py ./sim --case 01
python3 tools/check.py ./sim --stage multicycle
python3 tools/check.py ./sim --case 31 --forwarding none
python3 tools/check.py ./sim --all-forwarding-configs
make test
```

For the `--case 31 --forwarding none` command above, captured output from a completed correct implementation is:

```text
PASS 31_tutorial_forwarding.nofwd
1/1 passed
```

This confirms the selected program and path configuration matched its expected result and trace; it does not mean every program was tested.

The runner checks final values, counters and cycle events. It reports the first differing numeric cycle; ordering within that cycle does not matter, but duplicate events do. Failures save actual output, trace and stderr under `test-results/`; `--results-dir PATH` changes the location. Expected baseline files are `<case>.out/.csv` for all paths and `<case>.nofwd.out/.csv` for none.

The runner defaults to both `all` and `none`. Use `--forwarding` to select one configuration, with the simulator's syntax, or `--all-forwarding-configs` for both settings. Each configuration is compared with its own expected files, not another path configuration's baseline.

Useful starting programs are `01_alu_chain`, `02_load_use`, `04_waw`, `05_wb_collision`, `13_store_operands` and `28_independent_branch`. You need not hand-trace every program.

## Common mistakes

- Rebuild before rerunning: otherwise `./sim` still contains the previous code.
- Check API argument order: sources precede destination, unlike assembly.
- For state mismatches, remove writes to the input records or reservations; the framework owns those updates.
- For store-data failures, distinguish the address base used in EX from the value used in MEM.
- Keep JSON stdout separate from `--explain` stderr when comparing output.

## Optional assembly tests

Registers and memory initially contain zero. Put initializers before instructions:

```asm
.reg R2 0
.reg R3 1
.mem 0 9
LW R1, 0(R2)
SW R1, 0(R3)
```

| Syntax | Operation |
|---|---|
| `ADD/SUB Rd, Rs, Rt` | Integer addition/subtraction |
| `ADDI Rd, Rs, imm` | Add signed decimal immediate |
| `LW Rd, offset(Rs)` / `SW Rt, offset(Rs)` | Integer load/store |
| `ADD.D/SUB.D/MUL.D/DIV.D Fd, Fs, Ft` | FP arithmetic |
| `L.D Fd, offset(Rs)` / `S.D Ft, offset(Rs)` | FP load/store |
| `BEQ/BNE Rs, Rt, label` | Branch if equal/not equal |
| `.freg F2 3.5` | Initialize an FP register |

Memory contains 256 numeric cells; offsets count cells, not bytes. Use signed 32-bit integers, valid addresses, finite FP values/results and nonzero divisors. Opcode/register spelling is case-insensitive, labels case-sensitive. Optional `$` register prefixes, blank lines and `#` comments are accepted. Labels can precede instructions or stand alone, including at end-of-file. Limits are 1024 static instructions, 100,000 cycles and 511 characters per line excluding newline.
