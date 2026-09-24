// The source CSV has unquoted numeric fields and a solution identifier.
import { readFileSync } from 'node:fs';
const path = process.argv[2] ?? 'benchmark_results_all_solutions.csv';
const lines = readFileSync(path, 'utf8').trim().split(/\r?\n/);
if (lines.shift() !== 'solution,run,seq,start_epoch,seconds') throw Error('Unexpected CSV schema');
const groups = new Map();
const sequences = new Set();
for (const line of lines) {
  const [solution, run, seq, epoch, seconds, extra] = line.split(',');
  if (extra !== undefined || !/^[a-zA-Z0-9_]+$/.test(solution) ||
      ![run, seq, epoch, seconds].every(v => v !== '' && Number.isFinite(Number(v))) || Number(seconds) <= 0)
    throw Error(`Invalid row: ${line}`);
  if (sequences.has(seq)) throw Error(`Duplicate sequence: ${seq}`);
  sequences.add(seq);
  if (!groups.has(solution)) groups.set(solution, []);
  groups.get(solution).push(Number(seconds));
}
const median = values => (values[Math.floor((values.length - 1) / 2)] + values[Math.floor(values.length / 2)]) / 2;
for (const values of groups.values()) values.sort((a, b) => a - b);
const fastest = median(groups.get('solution5'));
console.log('| Program | Runs | Min (s) | Median (s) | Max (s) | Median / solution5 |');
console.log('|---|---:|---:|---:|---:|---:|');
for (const [name, values] of [...groups].sort((a, b) => median(b[1]) - median(a[1]))) {
  console.log(`| ${name} | ${values.length} | ${values[0].toFixed(2)} | ${median(values).toFixed(3)} | ${values.at(-1).toFixed(2)} | ${(median(values) / fastest).toFixed(2)}x |`);
}
