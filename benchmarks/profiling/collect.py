"""Collect alternating, affinity-pinned profile processes and static disassembly."""
import argparse
import csv
import json
import os
from pathlib import Path
import platform
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('before', type=Path)
p.add_argument('after', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--before-revision', required=True)
p.add_argument('--after-revision', required=True)
p.add_argument('--rounds', type=int, default=7)
p.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
a = p.parse_args()
if a.rounds < 1:
    p.error('positive round count required')
a.output.mkdir(parents=True, exist_ok=False)
os.sched_setaffinity(0, {a.cpu})
meta = dict(before=a.before_revision, after=a.after_revision, rounds=a.rounds,
            cpu=a.cpu, platform=platform.platform(), frequency='uncontrolled',
            background_activity='uncontrolled', commands={})
for command in (['lscpu'], ['cc', '--version'], ['objdump', '--version']):
    meta['commands'][' '.join(command)] = subprocess.check_output(command, text=True)
(a.output / 'environment.json').write_text(json.dumps(meta, indent=2)+'\n')
with (a.output / 'raw.csv').open('w') as out:
    writer = None
    for round_number in range(a.rounds):
        for label in (('before', 'after') if round_number % 2 == 0 else ('after', 'before')):
            dump = a.output / label
            dump.mkdir(exist_ok=True)
            binary = a.before if label == 'before' else a.after
            result = subprocess.check_output([str(binary.resolve()), str(dump.resolve()), str(round_number)], text=True)
            for row in csv.DictReader(result.splitlines()):
                if writer is None:
                    writer = csv.DictWriter(out, ['revision', 'round', *row], lineterminator='\n')
                    writer.writeheader()
                writer.writerow(dict(revision=label, round=round_number+1, **row))
            out.flush()
with (a.output / 'instructions.csv').open('w') as out:
    writer = csv.writer(out, lineterminator='\n')
    writer.writerow(['revision', 'workload', 'bytes', 'instructions', 'stack_loads', 'stack_stores', 'branches'])
    for label in ('before', 'after'):
        for binary in sorted((a.output / label).glob('*.bin')):
            disasm = subprocess.check_output(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64',
                                             '--no-show-raw-insn', str(binary)], text=True)
            # Drop the absolute file header, retaining instruction addresses for navigation.
            disasm = '\n'.join(disasm.splitlines()[3:])+'\n'
            binary.with_suffix('.asm').write_text(disasm)
            instructions = [line.split(':', 1)[1].strip() for line in disasm.splitlines()
                            if re.match(r'^\s*[0-9a-f]+:', line)]
            loads = stores = branches = 0
            for insn in instructions:
                operands = insn.split(None, 1)[1] if ' ' in insn or '\t' in insn else ''
                if '(%rsp)' in operands:
                    stores += operands.endswith('(%rsp)')
                    loads += not operands.endswith('(%rsp)')
                branches += insn.startswith('j')
            writer.writerow([label, binary.stem, binary.stat().st_size, len(instructions), loads, stores, branches])
            binary.unlink()
