"""Run each cell in a fresh process; keep unaggregated measurements."""
import argparse
import csv
import json
import os
from pathlib import Path
import platform
import subprocess

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--rounds', type=int, default=3)
p.add_argument('--cpu', type=int)
p.add_argument('--scaling-only', action='store_true')
p.add_argument('--revision', required=True)
p.add_argument('--mir-revision', default='unavailable')
a = p.parse_args()
if a.rounds < 2:
    p.error('at least two alternating rounds required')
a.output.mkdir(parents=True, exist_ok=False)
if a.cpu is not None:
    os.sched_setaffinity(0, {a.cpu})
environment = {'revision': a.revision, 'mir_revision': a.mir_revision,
               'platform': platform.platform(), 'affinity': sorted(os.sched_getaffinity(0)),
               'rounds': a.rounds, 'frequency': 'uncontrolled',
               'commands': {}}
for command in [['lscpu'], ['gcc', '--version'], ['clang', '--version'], ['llvm-config', '--version']]:
    try:
        environment['commands'][' '.join(command)] = subprocess.check_output(command, text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        environment['commands'][' '.join(command)] = str(error)
(a.output / 'environment.json').write_text(json.dumps(environment, indent=2) + '\n')
engines = [('c', 'compare'), ('interpret', 'compare'), ('fixed', 'compare'), ('adaptive', 'compare')]
for engine in ('llvm', 'mir'):
    if (a.build / f'compare-{engine}').exists():
        engines.append((engine, f'compare-{engine}'))
with (a.output / 'raw.csv').open('w') as output:
    writer = None
    for round_number in range(a.rounds):
        order = engines if round_number % 2 == 0 else engines[::-1]
        for kind in (range(0) if a.scaling_only else range(6)):
            for calls in (1, 10, 100, 1000, 10000, 100000):
                for engine, binary in order:
                    result = subprocess.check_output([str((a.build / binary).resolve()), engine,
                                                      str(kind), str(calls), '1'], text=True)
                    row = next(csv.DictReader(result.splitlines()))
                    if writer is None:
                        writer = csv.DictWriter(output, ['round', *row], lineterminator='\n')
                        writer.writeheader()
                    writer.writerow({'round': round_number + 1, **row})
                    output.flush()
        for threads in ((1, 2, 4) if a.scaling_only else (2, 4)):
            for engine, binary in order:
                result = subprocess.check_output([str((a.build / binary).resolve()), engine,
                                                  '0', '100000', str(threads)], text=True)
                row = next(csv.DictReader(result.splitlines()))
                if writer is None:
                    writer = csv.DictWriter(output, ['round', *row], lineterminator='\n')
                    writer.writeheader()
                writer.writerow({'round': round_number + 1, **row})
                output.flush()
