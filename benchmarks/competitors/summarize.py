"""Median, minimum and maximum for every numeric metric, with no discarded samples."""
import csv
from collections import defaultdict
from statistics import median
import sys

samples = defaultdict(list)
for path in sys.argv[1:]:
    with open(path) as source:
        for row in csv.DictReader(source):
            samples[tuple(row[k] for k in ('engine', 'workload', 'calls', 'threads'))].append(row)
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(['engine', 'workload', 'calls', 'threads', 'samples', 'metric', 'median', 'min', 'max'])
for key, rows in sorted(samples.items()):
    for metric in rows[0]:
        if metric in ('round', 'engine', 'workload', 'calls', 'threads', 'checksum'):
            continue
        values = [int(row[metric]) for row in rows]
        writer.writerow([*key, len(rows), metric, median(values), min(values), max(values)])
