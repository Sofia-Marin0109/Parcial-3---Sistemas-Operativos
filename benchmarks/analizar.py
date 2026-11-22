#!/usr/bin/env python3
"""Read the per-round CSV produced by run.sh and print a Markdown summary."""

from argparse import ArgumentParser
from collections import defaultdict
import csv
from statistics import median


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", help="CSV producido por benchmarks/run.sh")
    arguments = parser.parse_args()
    groups = defaultdict(list)

    with open(arguments.csv_file, encoding="utf-8", newline="") as source:
        for row in csv.DictReader(source):
            groups[row["target"], row["condition"]].append(row)

    conditions = ("baseline", "policy", "audit")
    targets = ("protected", "control")
    expected_rounds = {str(number) for number in range(1, 7)}
    if set(groups) != {(target, condition) for target in targets for condition in conditions}:
        parser.error("el CSV debe incluir los dos archivos y las tres condiciones")

    print("| Archivo | Condición | p50 mmap (ns) | p95 mmap (ns) | Cambio p50 | "
          "Primeros toques (ms) | Menores / mayores | Fallos/s |")
    print("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |")
    for target in targets:
        baseline = median(float(row["mmap_p50_ns"]) for row in groups[target, "baseline"])
        for condition in conditions:
            rows = groups[target, condition]
            if len(rows) != len(expected_rounds) or {row["round"] for row in rows} != expected_rounds:
                parser.error(f"faltan rondas o hay duplicados en {target}/{condition}")

            p50 = median(float(row["mmap_p50_ns"]) for row in rows)
            p95 = median(int(row["mmap_p95_ns"]) for row in rows)
            touch = median(int(row["touch_total_ns"]) / 1_000_000 for row in rows)
            minor = median(int(row["minor_faults"]) for row in rows)
            major = median(int(row["major_faults"]) for row in rows)
            rate = median(
                1_000_000_000 * (int(row["minor_faults"]) + int(row["major_faults"]))
                / int(row["touch_total_ns"])
                for row in rows
            )
            change = 100 * (p50 / baseline - 1)
            print(f"| {target} | {condition} | {p50:.1f} | {p95:.1f} | "
                  f"{change:+.1f} % | {touch:.2f} | {minor:g} / {major:g} | {rate:.0f} |")


if __name__ == "__main__":
    main()
