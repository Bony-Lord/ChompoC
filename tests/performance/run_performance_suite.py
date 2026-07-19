#!/usr/bin/env python3
import argparse
import subprocess
import sys
import time
from pathlib import Path

CASES = [
    ("arithmetic.chmp", "633518210", 12.0),
    ("functions.chmp", "393507460", 12.0),
    ("arrays.chmp", "937487500 25000", 12.0),
    ("scope_lookup.chmp", "2200000", 12.0),
    ("control_flow.chmp", "199860", 12.0),
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run heavy Chompo programs with correctness checks and TLE limits.")
    parser.add_argument("--executable", required=True)
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--limit-multiplier", type=float, default=1.0)
    args = parser.parse_args()

    failed = False
    total = 0.0

    for filename, expected, base_limit in CASES:
        path = args.cases / filename
        limit = base_limit * args.limit_multiplier
        started = time.perf_counter()

        try:
            completed = subprocess.run(
                [args.executable, str(path)],
                capture_output=True,
                text=True,
                timeout=limit,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"TLE  {filename}: exceeded {limit:.2f}s", file=sys.stderr)
            failed = True
            continue

        elapsed = time.perf_counter() - started
        total += elapsed
        output = completed.stdout.replace("\r\n", "\n").strip()

        if completed.returncode != 0:
            print(f"FAIL {filename}: exit={completed.returncode}, time={elapsed:.3f}s", file=sys.stderr)
            print(completed.stderr, file=sys.stderr)
            failed = True
        elif output != expected:
            print(f"FAIL {filename}: checksum mismatch, time={elapsed:.3f}s", file=sys.stderr)
            print(f"  expected: {expected!r}", file=sys.stderr)
            print(f"  actual:   {output!r}", file=sys.stderr)
            failed = True
        else:
            print(f"PASS {filename}: {elapsed:.3f}s / {limit:.2f}s")

    print(f"Total measured execution time: {total:.3f}s")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
