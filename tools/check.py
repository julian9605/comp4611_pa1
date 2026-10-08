#!/usr/bin/env python3
"""Check selected programs and preserve useful diagnostics for failed runs."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile


def stage_for(asm):
    number = asm.stem.split('_', 1)[0]
    if number in {'01', '02'}:
        return 'basic'
    if number in {'03', '04', '05', '06', '07', '08', '10', '11',
                  '12', '23', '25', '26'}:
        return 'multicycle'
    return 'integration'


def output_difference(actual, expected):
    if not isinstance(actual, dict):
        return 'output JSON must be an object'
    for key in ('R', 'F', 'memory'):
        values = actual.get(key)
        if not isinstance(values, list) or len(values) != len(expected[key]):
            return f'{key}: missing array or incorrect length'
        for j, (x, y) in enumerate(zip(values, expected[key])):
            if (not isinstance(x, (int, float)) or isinstance(x, bool)
                    or not math.isfinite(x)
                    or not math.isclose(x, y, rel_tol=1e-10, abs_tol=1e-10)):
                return f'{key}[{j}]: got {x!r}, expected {y}'
    for key in sorted(expected.keys() - {'R', 'F', 'memory'}):
        if key not in actual or actual[key] != expected[key]:
            return f'{key}: got {actual.get(key)!r}, expected {expected[key]}'
    return None


def read_trace(path):
    with path.open(newline='') as source:
        rows = csv.reader(source)
        if next(rows, None) != ['cycle', 'stage', 'seq', 'pc', 'detail']:
            raise ValueError(f'{path.name}: invalid trace header')
        cycles = defaultdict(Counter)
        for line, row in enumerate(rows, 2):
            if len(row) != 5:
                raise ValueError(f'{path.name}: malformed trace row {line}')
            cycle = int(row[0])
            cycles[cycle][tuple(row[1:])] += 1
    return cycles


def describe_events(events):
    if not events:
        return '(none)'
    return '; '.join(','.join(event) + (f' x{count}' if count > 1 else '')
                     for event, count in sorted(events.items()))


def trace_difference(actual_path, expected_path):
    actual, expected = read_trace(actual_path), read_trace(expected_path)
    # Compare numeric cycles; ignore row order but retain duplicate events.
    for cycle in sorted(actual.keys() | expected.keys()):
        if actual[cycle] != expected[cycle]:
            missing = describe_events(expected[cycle] - actual[cycle])
            extra = describe_events(actual[cycle] - expected[cycle])
            return (f'trace cycle {cycle}: expected-only [{missing}]; '
                    f'actual-only [{extra}]')
    return None


def captured_text(value):
    # TimeoutExpired can contain bytes even when subprocess uses text=True.
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else (value or '')


def parse_forwarding(value):
    if value not in ('all', 'none'):
        raise argparse.ArgumentTypeError('use all or none')
    return value


def forwarding_suffix(config):
    return '' if config == 'all' else '.nofwd'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sim', nargs='?', default='./sim')
    parser.add_argument('--case', help='test name prefix, e.g. 03; combined with --stage')
    parser.add_argument('--stage', choices=('basic', 'multicycle', 'integration', 'all'), default='all')
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--forwarding', type=parse_forwarding, help='all or none; default checks both')
    modes.add_argument('--all-forwarding-configs', action='store_true', help='check both forwarding settings')
    parser.add_argument('--results-dir', type=Path, help='failure artifacts directory (default: student/test-results)')
    args = parser.parse_args()
    sim = Path(args.sim).resolve()
    root = Path(__file__).resolve().parent.parent
    results_dir = args.results_dir if args.results_dir is not None else root / 'test-results'
    cases = [asm for asm in sorted((root / 'tests').glob('*.asm'))
             if asm.stem.startswith(args.case or '')
             and (args.stage == 'all' or stage_for(asm) == args.stage)]
    if not cases:
        parser.error('no matching cases')
    passed = 0
    configs = ['all', 'none'] if args.all_forwarding_configs or args.forwarding is None else [args.forwarding]
    for asm, config in ((asm, config) for asm in cases for config in configs):
        suffix = forwarding_suffix(config)
        label = asm.stem + suffix
        problems, stdout, stderr = [], '', ''
        with tempfile.TemporaryDirectory() as td:
            trace = Path(td) / 'actual.csv'
            command = [str(sim), '--forwarding', config, '--trace', str(trace)]
            try:
                run = subprocess.run(command + [str(asm)], text=True,
                                     encoding='utf-8', errors='replace',
                                     capture_output=True, timeout=8)
                stdout, stderr = run.stdout, run.stderr
                if run.returncode:
                    detail = ' '.join(stderr.split())[:220]
                    problems.append(f'exit {run.returncode}' + (f': {detail}' if detail else ''))
            except subprocess.TimeoutExpired as exc:
                stdout, stderr = captured_text(exc.stdout), captured_text(exc.stderr)
                problems.append('timeout after 8 seconds')
            except OSError as exc:
                problems.append(str(exc))

            try:
                expected = json.loads(asm.with_suffix(suffix + '.out').read_text())
                difference = output_difference(json.loads(stdout), expected)
                if difference:
                    problems.append(difference)
            except (ValueError, KeyError, TypeError, OverflowError, OSError) as exc:
                problems.append(f'output check: {exc}')

            # Do this even when output/statistics differ: the first divergent
            # cycle usually explains the failure better than final counts.
            try:
                difference = trace_difference(trace, asm.with_suffix(suffix + '.csv'))
                if difference:
                    problems.append(difference)
            except (ValueError, csv.Error, OSError) as exc:
                problems.append(f'trace check: {exc}')

            if problems:
                result = results_dir / label
                try:
                    result.mkdir(parents=True, exist_ok=True)
                    (result / 'actual.out').write_text(stdout)
                    if trace.exists():
                        shutil.copyfile(trace, result / 'actual.csv')
                    else:
                        (result / 'actual.csv').write_text('')
                        stderr += '\n[check.py] Simulator did not produce a trace.\n'
                    (result / 'stderr.txt').write_text(stderr + '\n[check.py] ' + '\n[check.py] '.join(problems) + '\n')
                    saved = result if args.results_dir is not None else Path('test-results') / label
                    problems.append(f'saved to {saved}/')
                except OSError as exc:
                    problems.append(f'could not save diagnostics: {exc}')
                print('FAIL', label, ' | '.join(problems))
            else:
                print('PASS', label)
                passed += 1
    total = len(cases) * len(configs)
    print(f'{passed}/{total} passed')
    return int(passed != total)


if __name__ == '__main__':
    raise SystemExit(main())
