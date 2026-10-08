#!/usr/bin/env python3
"""Check exact stall counts and verify that input records remain unchanged."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


GROUPS = ('raw', 'waw', 'wb', 'control')
ROOT = Path(__file__).resolve().parent.parent
NUM_REGS = 64
NUM_CYCLES = 100064
REGISTER_FIELDS = ('writer_op', 'write_cycle')
OPS = ('ADD', 'SUB', 'ADDI', 'LW', 'SW', 'FADD', 'FSUB', 'FMUL', 'FDIV', 'FLD', 'FST', 'BEQ', 'BNE')


class BuildError(RuntimeError):
    pass


def number(value, name, minimum=0, maximum=NUM_CYCLES - 1):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f'{name}: expected integer in [{minimum}, {maximum}]')
    return value


def boolean(value, name):
    if type(value) is not bool:
        raise ValueError(f'{name}: expected true or false')
    return 'true' if value else 'false'


def normalized_state(state):
    """Expand omitted fields as zero/empty; expected states are full snapshots."""
    if not isinstance(state, dict):
        raise ValueError('state must be an object')
    entries = state.get('writers', {})
    if not isinstance(entries, dict):
        raise ValueError('writers must map register numbers to writer records')
    registers = {}
    for key, value in entries.items():
        reg = number(int(key), 'register', 0, NUM_REGS - 1)
        if not isinstance(value, dict):
            raise ValueError('register writer record must be an object')
        writer = value.get('writer_op', 'ADD')
        if writer not in OPS:
            raise ValueError('unknown writer opcode: ' + str(writer))
        record = {'writer_op': writer, 'write_cycle': number(value.get('write_cycle', 0), 'write_cycle')}
        if writer != 'ADD' or record['write_cycle']:
            registers[str(reg)] = record
    return {
        'writers': registers,
        'wb_reserved': sorted(set(number(c, 'wb_reserved cycle') for c in state.get('wb_reserved', []))),
    }


def state_setup(initial):
    state = normalized_state(initial)
    lines = ['static Scoreboard state;']
    for reg, timing in state['writers'].items():
        lines.extend(f'state.writers[{reg}].{field} = {value};' for field, value in timing.items())
    lines.extend(f'state.wb_reserved[{cycle}] = true;' for cycle in state['wb_reserved'])
    return lines


def forwarding_literal(value):
    return boolean(value, 'forwarding')


def invocation(group, step):
    cycle = number(step['cycle'], 'cycle', 1)
    number(step['expected_stall'], 'expected_stall')
    if group == 'control':
        branch_id = number(step['branch_id_cycle'], 'branch_id_cycle', -1)
        if branch_id == 0:
            raise ValueError('branch_id_cycle must be -1 or a positive cycle')
        return f'check_control_hazard({cycle}, {branch_id})'
    src1 = number(step.get('src1', -1), 'src1', -1, 63)
    src2 = number(step.get('src2', -1), 'src2', -1, 63)
    dst = number(step.get('dst', -1), 'dst', -1, 63)
    inst = step['inst']
    if inst not in OPS:
        raise ValueError('unknown instruction: ' + str(inst))
    forwarding = forwarding_literal(step['forwarding'])
    if inst in ('BEQ', 'BNE') and dst != -1:
        raise ValueError('branches have no destination register')
    normalized_state(step['state'])
    return f'check_{group}_hazard({cycle}, {inst}, {src1}, {src2}, {dst}, {forwarding}, &state)'


def case_body(group, case):
    if not isinstance(case['name'], str) or not case['name']:
        raise ValueError('each case needs a nonempty name')
    if not isinstance(case['steps'], list) or not case['steps']:
        raise ValueError('each case needs one or more steps')
    lines = ['printf("[");']
    for index, step in enumerate(case['steps']):
        setup = [] if group == 'control' else state_setup(step['state'])
        lines += ['{', *setup, f'int stall = {invocation(group, step)};',
                  ('printf(",");' if index else ''),
                  'printf("{\\\"stall\\\":%d", stall);']
        if group != 'control':
            lines += ['printf(",\\\"state\\\":");', 'print_state(&state);']
        lines += ['printf("}");', '}']
    return lines + ['printf("]\\n");']


STATE_PRINTER = r'''
static void print_state(const Scoreboard *state) {
    static const char *const names[] = {"ADD", "SUB", "ADDI", "LW", "SW", "FADD", "FSUB", "FMUL", "FDIV", "FLD", "FST", "BEQ", "BNE"};
    printf("{\"writers\":{");
    int comma = 0;
    for (int r = 0; r < NUM_REGS; ++r) {
        const RegisterWriter *reg = &state->writers[r];
        if (reg->writer_op != ADD || reg->write_cycle) {
            const char *op = reg->writer_op >= ADD && reg->writer_op <= BNE ? names[reg->writer_op] : "INVALID";
            printf("%s\"%d\":{\"writer_op\":\"%s\",\"write_cycle\":%d}",
                   comma ? "," : "", r, op, reg->write_cycle);
            comma = 1;
        }
    }
    printf("},\"wb_reserved\":[");
    comma = 0;
    for (int c = 0; c < NUM_CYCLES; ++c) {
        if (state->wb_reserved[c]) {
            printf("%s%d", comma ? "," : "", c);
            comma = 1;
        }
    }
    printf("]}");
}
'''


def harness(cases):
    printer = STATE_PRINTER if any(group != 'control' for group, _ in cases) else ''
    lines = ['#include "hazards.h"', '#include <stdio.h>', '#include <stdlib.h>', printer,
             'int main(int argc, char **argv) {', 'if (argc != 2) return 2;',
             'switch (strtol(argv[1], NULL, 10)) {']
    for index, (group, case) in enumerate(cases):
        lines += [f'case {index}: {{', *case_body(group, case), 'return 0;', '}']
    return '\n'.join(lines + ['default: return 2;', '}', '}', ''])


def expected_result(case):
    return [dict(stall=step['expected_stall'], **({'state': normalized_state(step['state'])}
                 if 'state' in step else {})) for step in case['steps']]


def difference(actual, expected):
    if not isinstance(actual, list) or len(actual) != len(expected):
        return f'expected {len(expected)} step results, got {str(actual)[:160]}'
    for index, (got, want) in enumerate(zip(actual, expected), 1):
        if not isinstance(got, dict):
            return f'step {index}: invalid snapshot {got!r}'
        if type(got.get('stall')) is not int or got.get('stall') != want['stall']:
            return f'step {index}: expected stall {want["stall"]}, got {got.get("stall")!r}'
        if 'state' not in want:
            continue
        state = got.get('state')
        if not isinstance(state, dict):
            return f'step {index}: missing scoreboard snapshot'
        for field, value in want['state'].items():
            if state.get(field) != value:
                return f'step {index} {field}: expected {value}, got {state.get(field)!r}'
    return None


def run_tests(source, cases_path=None, group='all', verbose=True):
    """Check literal results; each call receives its own explicit read-only input records."""
    if group not in (*GROUPS, 'all'):
        raise ValueError('unknown test group: ' + str(group))
    source = Path(source).resolve()
    cases_path = Path(cases_path) if cases_path else ROOT / 'tests/hazard_cases.json'
    bank = json.loads(cases_path.read_text())
    groups = GROUPS if group == 'all' else (group,)
    cases = []
    for name in groups:
        entries = bank[name]
        if not isinstance(entries, list) or not entries:
            raise ValueError(f'{name}: missing test cases')
        cases.extend((name, entry) for entry in entries)
    scores = {name: {'passed': 0, 'total': len(bank[name])} for name in groups}
    with tempfile.TemporaryDirectory(prefix='hazard-unit-') as directory:
        work = Path(directory)
        driver, executable = work / 'unit.c', work / 'unit'
        submission = work / 'hazards.c'
        submission.write_bytes(source.read_bytes())
        driver.write_text(harness(cases))
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c99', '-O2',
                   '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                   '-I' + str(ROOT / 'src'), str(driver), str(submission), '-o', str(executable)]
        try:
            build = subprocess.run(command, text=True, capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise BuildError(str(exc)) from exc
        if build.returncode:
            raise BuildError(build.stderr.strip() or build.stdout.strip())
        for index, (name, case) in enumerate(cases):
            expected = expected_result(case)
            detail = None
            try:
                result = subprocess.run([str(executable), str(index)], text=True,
                                        capture_output=True, timeout=5)
                if result.returncode:
                    detail = f'exit {result.returncode}: {result.stderr.strip()[:160]}'
                else:
                    detail = difference(json.loads(result.stdout), expected)
            except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
                detail = str(exc)
            if detail is None:
                scores[name]['passed'] += 1
            elif verbose:
                print(f'FAIL {name}/{case["name"]}: {detail}')
    if verbose:
        for name, score in scores.items():
            print(f'{name}: {score["passed"]}/{score["total"]} passed')
    return scores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/hazards.c')
    parser.add_argument('--group', choices=(*GROUPS, 'all'), default='all')
    parser.add_argument('--cases', type=Path, help='alternative instructor case file')
    args = parser.parse_args()
    try:
        scores = run_tests(args.source, args.cases, args.group)
    except (BuildError, ValueError, KeyError, TypeError, OSError) as exc:
        print('ERROR:', exc)
        return 2
    passed = sum(s['passed'] for s in scores.values())
    total = sum(s['total'] for s in scores.values())
    print(f'{passed}/{total} unit cases passed')
    return int(passed != total)


if __name__ == '__main__':
    raise SystemExit(main())
