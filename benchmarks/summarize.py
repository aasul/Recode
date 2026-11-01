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
        if row[0] in ('integer', 'data'):
            fields = dict(zip(row[2::2], row[3::2]))
            key = (row[1], fields['distribution'], fields['mode'], fields['calls'])
            value = float(fields['ns'])
        elif row[0] == 'run':
            fields = dict(zip(row[3::2], row[4::2]))
            key = (row[1], '-', row[2], fields['calls'])
            value = float(fields['ns_per_call'])
        else:
            continue
        samples[key].append((value, float(fields['total_us']), float(fields['compile_ns'])))
writer = csv.writer(sys.stdout, lineterminator="\n")
metrics = ['ns', 'total_us', 'compile_ns']
writer.writerow(['workload', 'distribution', 'mode', 'calls', 'samples',
                 *[f'{stat}_{metric}' for metric in metrics for stat in ('median', 'min', 'max')]])
for key, values in sorted(samples.items()):
    row = [*key, len(values)]
    for metric in range(len(metrics)):
        observations = [value[metric] for value in values]
        row.extend(f'{stat(observations):.3f}' for stat in (median, min, max))
    writer.writerow(row)
