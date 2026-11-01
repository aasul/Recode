"""Summarize checked benchmark CSV without external Python dependencies."""
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median
import sys

samples = defaultdict(list)
for argument in sys.argv[1:]:
    path = Path(argument)
    for row in csv.reader(path.open()):
        if not row:
            continue
        if row[0] == 'integer':
            fields = dict(zip(row[2::2], row[3::2]))
            key = (row[1], fields['distribution'], fields['mode'], fields['calls'])
            value = float(fields['ns'])
        elif row[0] == 'run':
            fields = dict(zip(row[3::2], row[4::2]))
            key = (row[1], '-', row[2], fields['calls'])
            value = float(fields['ns_per_call'])
        else:
            continue
        samples[key].append(value)
writer = csv.writer(sys.stdout)
writer.writerow(['workload', 'distribution', 'mode', 'calls', 'samples', 'median_ns', 'min_ns', 'max_ns'])
for key, values in sorted(samples.items()):
    writer.writerow([*key, len(values), f'{median(values):.3f}', f'{min(values):.3f}', f'{max(values):.3f}'])
