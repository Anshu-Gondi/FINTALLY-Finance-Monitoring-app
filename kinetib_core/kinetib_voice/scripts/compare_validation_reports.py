#!/usr/bin/env python3
"""Compare fresh FinTally Voice 500- and 200-iteration validation reports."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Any

LANGUAGES = ("en", "fr", "hi")


def read_report(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(f"report not found: {path}")

    content = path.read_text(encoding="utf-8")
    seed_match = re.search(r"^- K-means seed:\s*(\d+)\s*$", content, re.MULTILINE)
    feature_match = re.search(r"^- Feature dimension:\s*(\d+)\s*$", content, re.MULTILINE)
    schema_match = re.search(r"^- Schema version:\s*(\d+)\s*$", content, re.MULTILINE)
    if not seed_match:
        raise ValueError(f"{path} has no K-means seed metadata; use the updated validation.rs")
    if not feature_match or not schema_match:
        raise ValueError(f"{path} is missing schema/feature-dimension metadata")

    rows: dict[str, dict[str, str]] = {}
    for raw_line in content.splitlines():
        line = raw_line.strip()
        if not (line.startswith("|") and line.endswith("|")):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if not cells or cells[0] not in LANGUAGES:
            continue
        if len(cells) < 13:
            raise ValueError(f"unexpected validation row in {path}: expected 13 columns, got {len(cells)}")
        rows[cells[0]] = {
            "backend": cells[1],
            "train_frames": cells[2],
            "validation_frames": cells[3],
            "clusters": cells[4],
            "occupied_validation": cells[5],
            "empty_validation": cells[6],
            "converged": cells[7].lower(),
            "iterations": cells[8],
            "training_inertia": cells[9],
            "validation_mse": cells[10],
            "validation_rmse_per_dimension": cells[11],
            "max_validation_squared_distance": cells[12],
        }

    missing = sorted(set(LANGUAGES) - rows.keys())
    if missing:
        raise ValueError(f"{path} is missing language rows: {', '.join(missing)}")
    return {
        "path": path,
        "seed": int(seed_match.group(1)),
        "feature_dim": int(feature_match.group(1)),
        "schema_version": int(schema_match.group(1)),
        "rows": rows,
    }


def as_float(value: str, field: str, language: str) -> float:
    try:
        result = float(value)
    except ValueError as exc:
        raise ValueError(f"invalid {field} for {language}: {value!r}") from exc
    if not (result == result and abs(result) != float("inf")):
        raise ValueError(f"non-finite {field} for {language}: {value!r}")
    return result


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: compare_validation_reports.py <fresh-500-report.md> <fresh-200-report.md>", file=sys.stderr)
        return 2

    baseline = read_report(Path(sys.argv[1]))
    candidate = read_report(Path(sys.argv[2]))

    print("FinTally Voice — controlled validation report comparison")
    print(f"500-iteration run: {baseline['path']}")
    print(f"200-iteration run: {candidate['path']}")
    print(f"K-means seed: {baseline['seed']} (500) / {candidate['seed']} (200)")
    print(f"Feature dimension: {baseline['feature_dim']} (500) / {candidate['feature_dim']} (200)")
    print("Lower validation MSE/RMSE means lower nearest-centroid error on held-out features.")

    invalid = False
    for field in ("seed", "feature_dim", "schema_version"):
        if baseline[field] != candidate[field]:
            print(f"ERROR: {field} differs between runs: {baseline[field]} vs {candidate[field]}")
            invalid = True
    if invalid:
        print("Comparison is not controlled; do not interpret the deltas.")
        return 1

    print()
    header = (
        f"{'Lang':<5} {'Iter 500':>9} {'Iter 200':>9} "
        f"{'MSE 500':>14} {'MSE 200':>14} {'MSE Δ %':>10} "
        f"{'RMSE Δ %':>10} {'Converged 500/200':>18}"
    )
    print(header)
    print("-" * len(header))

    mismatches: list[str] = []
    for language in LANGUAGES:
        old = baseline["rows"][language]
        new = candidate["rows"][language]

        for field in ("backend", "train_frames", "validation_frames", "clusters"):
            if old[field] != new[field]:
                mismatches.append(f"{language}: {field} differs ({old[field]} vs {new[field]})")

        old_iter = int(old["iterations"])
        new_iter = int(new["iterations"])
        if old_iter > 500 or new_iter > 200:
            mismatches.append(f"{language}: reported iterations exceed requested maximum ({old_iter}, {new_iter})")

        mse_500 = as_float(old["validation_mse"], "500-run MSE", language)
        mse_200 = as_float(new["validation_mse"], "200-run MSE", language)
        rmse_500 = as_float(old["validation_rmse_per_dimension"], "500-run RMSE", language)
        rmse_200 = as_float(new["validation_rmse_per_dimension"], "200-run RMSE", language)
        mse_delta = ((mse_200 - mse_500) / mse_500 * 100.0) if mse_500 else 0.0
        rmse_delta = ((rmse_200 - rmse_500) / rmse_500 * 100.0) if rmse_500 else 0.0

        print(
            f"{language:<5} {old_iter:>9} {new_iter:>9} "
            f"{mse_500:>14.6g} {mse_200:>14.6g} {mse_delta:>9.3f}% "
            f"{rmse_delta:>9.3f}% {(old['converged'] + '/' + new['converged']):>18}"
        )
        if old["converged"] != "true":
            print(f"  NOTE {language}: the 500-iteration run did not converge; inspect diagnostics.")
        if new["converged"] != "true":
            print(f"  NOTE {language}: the 200-iteration run hit its limit without convergence; this is expected if more refinement is needed.")

    if mismatches:
        print("\nERROR: run configuration/data mismatches detected:")
        for item in mismatches:
            print(f"  - {item}")
        print("Treat this comparison as uncontrolled until mismatches are resolved.")
        return 1

    print()
    print("Delta is (MSE_200 - MSE_500) / MSE_500; negative means the 200-run error is lower.")
    print("These results measure acoustic-feature quantization, not speech/intent accuracy.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
