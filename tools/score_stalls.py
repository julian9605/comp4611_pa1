#!/usr/bin/env python3
"""Score each returned stall count by its difference from the expected count."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location('stall_input_schema', ROOT / 'tools/check_unit.py')
_schema = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_schema)
GROUPS = _schema.GROUPS
BuildError = _schema.BuildError
CALL_TIMEOUT_SECONDS = 5


def stall_credit(actual, optimal):
    """Apply the same numerical credit rule to every hazard function."""
    if type(optimal) is not int or optimal < 0:
        raise ValueError('optimal stall must be a nonnegative integer')
    if type(actual) is not int or actual < optimal:
        return 0.0
    return max(0.0, (10 - (actual - optimal)) / 10)


def _calls(bank, groups):
    calls = []
    for group in groups:
        cases = bank[group]
        if not isinstance(cases, list) or not cases:
            raise ValueError(group + ': missing test cases')
        for case in cases:
            # Reuse input validation only; strict state/output comparisons are
            # deliberately not part of this scorer.
            _schema.case_body(group, case)
            for index, step in enumerate(case['steps']):
                calls.append({'group': group, 'name': case['name'], 'step_index': index,
                              'step': step, 'optimal': step['expected_stall'],
                              'before': None if group == 'control' else _schema.normalized_state(step['state'])})
        optimal_values = [c['optimal'] for c in calls if c['group'] == group]
        if not any(v == 0 for v in optimal_values) or not any(v > 0 for v in optimal_values):
            raise ValueError(group + ': scoring needs both optimal=0 and optimal>0 calls')
    return calls


def _harness(calls):
    lines = ['#include "hazards.h"', '#include <stdio.h>', '#include <stdlib.h>',
             'int main(int argc, char **argv) {', 'if (argc != 2) return 2;',
             'switch (strtol(argv[1], NULL, 10)) {']
    for index, call in enumerate(calls):
        step = call['step']
        setup = [] if call['group'] == 'control' else _schema.state_setup(call['before'])
        lines += [f'case {index}: {{', *setup,
                  f'int stall = {_schema.invocation(call["group"], step)};',
                  'printf("%d\\n", stall);', 'return 0;', '}']
    return '\n'.join(lines + ['default: return 2;', '}', '}', ''])


def _classify(call, actual):
    optimal = call['optimal']
    if type(actual) is not int or actual < 0:
        return 'invalid', 0.0
    if actual < optimal:
        return 'under', 0.0
    return ('exact' if actual == optimal else 'over'), stall_credit(actual, optimal)


def run_scores(source, cases_path=None, group='all', verbose=True):
    """Return per-group fractions, buckets, counts, and extra stall cycles.

    Every invocation gets its own process and canonical inputs. RAW/WAW/WB
    receive a pre-call state; control receives two cycle numbers. Each
    group's optimal=0 calls contribute 20%; optimal>0 calls contribute 80%.
    Calls are equally weighted within their bucket, irrespective of fixture
    case boundaries. Both buckets must be present; neither is reweighted.
    """
    if group not in (*GROUPS, 'all'):
        raise ValueError('unknown test group: ' + str(group))
    source = Path(source).resolve()
    cases_path = Path(cases_path) if cases_path else ROOT / 'tests/hazard_cases.json'
    bank = json.loads(cases_path.read_text())
    groups = GROUPS if group == 'all' else (group,)
    calls = _calls(bank, groups)
    scores = {name: {'fraction': 0.0, 'exact': 0, 'over': 0, 'under': 0,
                     'invalid': 0, 'calls': 0, 'cases': len(bank[name]),
                     'extra_cycles': 0, 'buckets': {key: {'calls': 0, 'mean': 0.0}
                                                  for key in ('zero', 'positive')}}
              for name in groups}
    credits = {name: {'zero': [], 'positive': []} for name in groups}
    with tempfile.TemporaryDirectory(prefix='hazard-stall-score-') as directory:
        work = Path(directory)
        driver, executable, submission = work / 'calls.c', work / 'calls', work / 'hazards.c'
        submission.write_bytes(source.read_bytes())
        driver.write_text(_harness(calls))
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c99', '-O2',
                   '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                   '-I' + str(ROOT / 'src'), str(driver), str(submission), '-o', str(executable)]
        try:
            build = subprocess.run(command, text=True, capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise BuildError(str(exc)) from exc
        if build.returncode:
            raise BuildError(build.stderr.strip() or build.stdout.strip())
        for index, call in enumerate(calls):
            category, credit, actual = 'invalid', 0.0, None
            try:
                result = subprocess.run([str(executable), str(index)], text=True,
                                        capture_output=True, timeout=CALL_TIMEOUT_SECONDS)
                if result.returncode == 0:
                    actual = json.loads(result.stdout)
                    category, credit = _classify(call, actual)
            except (OSError, ValueError, subprocess.TimeoutExpired):
                pass
            score = scores[call['group']]
            score['calls'] += 1
            score[category] += 1
            if type(actual) is int and actual > call['optimal']:
                score['extra_cycles'] += actual - call['optimal']
            bucket = 'zero' if call['optimal'] == 0 else 'positive'
            credits[call['group']][bucket].append(credit)
    for name, score in scores.items():
        for bucket, values in credits[name].items():
            score['buckets'][bucket] = {'calls': len(values), 'mean': sum(values) / len(values)}
        score['fraction'] = .2 * score['buckets']['zero']['mean'] + .8 * score['buckets']['positive']['mean']
        if verbose:
            print(f'{name}: {100 * score["fraction"]:.2f}% | exact {score["exact"]}, '
                  f'over {score["over"]}, under {score["under"]}, '
                  f'invalid {score["invalid"]}; extra cycles {score["extra_cycles"]}; '
                  f'{score["calls"]} calls in {score["cases"]} cases')
            print(f'  optimal=0: {score["buckets"]["zero"]["calls"]} calls, '
                  f'{100 * score["buckets"]["zero"]["mean"]:.2f}%; '
                  f'optimal>0: {score["buckets"]["positive"]["calls"]} calls, '
                  f'{100 * score["buckets"]["positive"]["mean"]:.2f}% (weights 20% / 80%)')
    return scores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/hazards.c')
    parser.add_argument('--cases', type=Path, help='alternative case bank')
    parser.add_argument('--group', choices=(*GROUPS, 'all'), default='all')
    args = parser.parse_args()
    try:
        run_scores(args.source, args.cases, args.group)
    except (BuildError, ValueError, KeyError, TypeError, OSError) as exc:
        print('ERROR:', exc)
        return 2
    # A partial score is a valid result; this command is not the strict checker.
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
