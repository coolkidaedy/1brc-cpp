#!/usr/bin/env python3
"""Differential tests with an independent integer reference, no external packages."""
import pathlib
import random
import subprocess
import sys
import tempfile


def tenths(v):
    return ('-' if v < 0 else '') + f'{abs(v) // 10}.{abs(v) % 10}'


def reference(rows):
    stations = {}
    for name, value in rows:
        stations.setdefault(name, []).append(value)
    return '{' + ', '.join(
        f'{name}={tenths(min(values))}/'
        f'{tenths((2 * sum(values) + len(values)) // (2 * len(values)))}/'
        f'{tenths(max(values))}'
        for name, values in sorted(stations.items())
    ) + '}\n'


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    rng = random.Random(12345)
    names = ['x' * n for n in range(1, 101)]
    names += ['x' * 15 + 'a', 'x' * 15 + 'ab', 'x' * 15 + 'ac', '東京', 'Zürich', 'é' * 50]
    cases = [[], [('a', 0)], [('a', -1), ('a', 0)], [('a', 0), ('a', 1)]]
    cases += [[(name, v) for name in names for v in (-999, -100, -1, 0, 1, 100, 999)]]
    cases += [[(f'value-{v}', v) for v in range(-999, 1000)]]
    cases += [[(f'station-{i:05}', (i % 1999) - 999) for i in range(10000)]]
    cases += [[(rng.choice(names), rng.randint(-999, 999)) for _ in range(100000)]]
    # Final 16-byte loads straddle exact and partial mapping pages.
    for size in (4095, 4096, 4097, 16383, 16384, 16385):
        count, remainder = divmod(size, 6)
        rows = [('a', 0)] * (count - 1) + [('a' * (1 + remainder), 0)]
        cases.append(rows)
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / 'measurements.txt'
        checks = 0
        for rows in cases:
            path.write_text(''.join(f'{name};{tenths(v)}\n' for name, v in rows), encoding='utf-8')
            expected = reference(rows)
            for threads in (1, 2, 8):
                result = subprocess.run([str(binary), str(path), str(threads)],
                                        check=True, capture_output=True, text=True, timeout=30)
                assert result.stdout == expected, f'mismatch: case {checks}, threads={threads}'
                checks += 1
        for args in ([str(path) + '.missing'], [str(path), '0'], [str(path), 'abc']):
            result = subprocess.run([str(binary), *args], capture_output=True, timeout=10)
            assert result.returncode != 0 and not result.stdout
        path.write_text('a;1.0')
        result = subprocess.run([str(binary), str(path)], capture_output=True, timeout=10)
        assert result.returncode != 0 and not result.stdout
    print(f'{checks} differential checks and 4 error-path checks passed')


if __name__ == '__main__':
    main()
