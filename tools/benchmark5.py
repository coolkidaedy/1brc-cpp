#!/usr/bin/env python3
"""Warm-cache full-process wall timing, checked against a supplied golden output."""
import argparse
import csv
import pathlib
import statistics
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('input', type=pathlib.Path)
p.add_argument('golden', type=pathlib.Path)
p.add_argument('--binary', default='build/solution5')
p.add_argument('--threads', type=int, default=0)
p.add_argument('--runs', type=int, default=5)
p.add_argument('--csv', type=pathlib.Path, default=pathlib.Path('benchmark_solution5_local.csv'))
a = p.parse_args()
if a.runs < 5:
    p.error('use at least five measured runs')
if a.threads < 0 or a.threads > 1024:
    p.error('threads must be 0 (automatic) or 1..1024')
command = [a.binary, str(a.input)] + ([str(a.threads)] if a.threads else [])
expected = a.golden.read_bytes()
times = []
with tempfile.TemporaryFile() as output:
    for run in range(a.runs + 1):
        output.seek(0)
        output.truncate()
        epoch = int(time.time())
        start = time.perf_counter()
        subprocess.run(command, stdout=output, check=True)
        elapsed = time.perf_counter() - start
        output.seek(0)
        if output.read() != expected:
            raise SystemExit(f'output differs from golden on run {run}')
        if run:
            times.append((run, epoch, elapsed))
        # Allow the forked child's deferred teardown to finish between trials.
        time.sleep(1)
with a.csv.open('w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['solution', 'run', 'seq', 'start_epoch', 'seconds'])
    writer.writerows(('solution5', run, run, epoch, f'{elapsed:.6f}') for run, epoch, elapsed in times)
values = [row[2] for row in times]
print(f'{len(values)} runs: min={min(values):.6f}s median={statistics.median(values):.6f}s max={max(values):.6f}s')
print(f'Raw measurements: {a.csv}')
