#!/usr/bin/env python3
import argparse
import json
import math
import sys
from pathlib import Path


MODE = "normalized-profile"
PROFILE = "geometry_corpus"
LEGACY_BENCHMARK = f"BM_external_legacy/{PROFILE}"
NEXT_BENCHMARKS = (
    (f"BM_external_next/{PROFILE}", "default"),
    (f"BM_external_next_prepared/{PROFILE}", "prepared"),
    (f"BM_external_next_batch/{PROFILE}", "batch"),
    (f"BM_external_next_prepared_batch/{PROFILE}", "prepared_batch"),
)


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def load_records(path: Path, time_field: str = "real_time") -> dict[str, dict[str, float | str]]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)

    benchmarks = payload.get("benchmarks")
    if not isinstance(benchmarks, list):
        raise ValueError(f"benchmark JSON has no benchmark list: {path}")

    records: dict[str, dict[str, float | str]] = {}
    means: dict[str, float] = {}
    cvs: dict[str, float] = {}

    for benchmark in benchmarks:
        name = benchmark.get("name")
        if not isinstance(name, str):
            continue
        measured_time = benchmark.get(time_field)
        if not isinstance(measured_time, int | float):
            continue

        time_value = float(measured_time)
        run_type = benchmark.get("run_type", "iteration")
        if run_type == "iteration":
            if name.endswith("_median") or name.endswith("_mean") or name.endswith("_cv"):
                continue
            records[name] = {"time": time_value, "source": name}
            continue

        if run_type != "aggregate":
            continue
        if name.endswith("_median"):
            base_name = name.removesuffix("_median")
            records[base_name] = {"time": time_value, "source": name}
        elif name.endswith("_mean"):
            means[name.removesuffix("_mean")] = time_value
        elif name.endswith("_cv"):
            cv_percent = time_value
            if benchmark.get("aggregate_unit") == "percentage" and abs(cv_percent) <= 1.0:
                cv_percent *= 100.0
            cvs[name.removesuffix("_cv")] = cv_percent

    for name, measured_time in means.items():
        records.setdefault(name, {"time": measured_time, "source": f"{name}_mean"})
    for name, cv_percent in cvs.items():
        records.setdefault(name, {})["cv"] = cv_percent
    return records


def is_positive_record(record: dict[str, float | str] | None) -> bool:
    return record is not None and "time" in record and float(record["time"]) > 0.0


def append_speedup_row(
    rows: list[dict],
    *,
    legacy_record: dict[str, float | str],
    next_name: str,
    next_record: dict[str, float | str],
    pairing: str,
) -> None:
    legacy_time = float(legacy_record["time"])
    next_time = float(next_record["time"])
    rows.append({
        "legacy": LEGACY_BENCHMARK,
        "next": next_name,
        "legacy_time": legacy_time,
        "next_time": next_time,
        "speedup": legacy_time / next_time,
        "legacy_cv_percent": float(legacy_record["cv"]) if "cv" in legacy_record else None,
        "next_cv_percent": float(next_record["cv"]) if "cv" in next_record else None,
        "pairing": pairing,
    })


def speedup_rows(records: dict[str, dict[str, float | str]]) -> tuple[list[dict], list[str]]:
    rows: list[dict] = []
    missing: list[str] = []
    legacy_record = records.get(LEGACY_BENCHMARK)
    if not is_positive_record(legacy_record):
        missing.append(LEGACY_BENCHMARK)
        return rows, missing

    for next_name, pairing in NEXT_BENCHMARKS:
        next_record = records.get(next_name)
        if not is_positive_record(next_record):
            missing.append(next_name)
            continue
        append_speedup_row(
            rows,
            legacy_record=legacy_record,
            next_name=next_name,
            next_record=next_record,
            pairing=pairing,
        )
    return rows, missing


def geomean_speedup(rows: list[dict]) -> float:
    return math.exp(sum(math.log(float(row["speedup"])) for row in rows) / len(rows))


def write_json_report(
    path: Path,
    *,
    status: str,
    min_pair_speedup: float,
    min_geomean_speedup: float,
    geomean: float | None,
    require_pair_floor: bool,
    time_field: str,
    rows: list[dict],
    missing: list[str],
    reason: str | None,
) -> None:
    ensure_parent(path)
    payload: dict = {
        "status": status,
        "mode": MODE,
        "profile": PROFILE,
        "min_pair_speedup": min_pair_speedup,
        "min_geomean_speedup": min_geomean_speedup,
        "geomean_speedup": geomean,
        "require_pair_floor": require_pair_floor,
        "time_field": time_field,
        "rows": rows,
        "missing": missing,
    }
    if reason is not None:
        payload["reason"] = reason
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def format_optional_percent(value: float | None) -> str:
    if value is None:
        return ""
    return f"{value:.2f}%"


def write_markdown_report(
    path: Path,
    *,
    status: str,
    min_pair_speedup: float,
    min_geomean_speedup: float,
    geomean: float | None,
    require_pair_floor: bool,
    time_field: str,
    rows: list[dict],
    missing: list[str],
    reason: str | None,
) -> None:
    ensure_parent(path)
    geomean_text = "n/a" if geomean is None else f"{geomean:.3f}x"
    pair_floor_text = "required" if require_pair_floor else "geomean-only"
    lines = [
        "# External Profile Speedup Gate",
        "",
        f"Status: **{status}**",
        f"Mode: **{MODE}**",
        f"Profile: **{PROFILE}**",
        f"Min pair speedup: **{min_pair_speedup:.2f}x**",
        f"Min geomean speedup: **{min_geomean_speedup:.2f}x**",
        f"Geomean speedup: **{geomean_text}**",
        f"Pair floor: **{pair_floor_text}**",
        f"Time field: **{time_field}**",
    ]
    if reason is not None:
        lines.extend(["", f"Reason: {reason}"])

    lines.extend([
        "",
        f"| Legacy | Next | Pairing | Legacy {time_field} | Next {time_field} | Speedup | Legacy CV | Next CV |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ])
    for row in rows:
        lines.append(
            f"| {row['legacy']} | {row['next']} | {row['pairing']} | "
            f"{row['legacy_time']:.3f} | {row['next_time']:.3f} | "
            f"{row['speedup']:.3f}x | "
            f"{format_optional_percent(row['legacy_cv_percent'])} | "
            f"{format_optional_percent(row['next_cv_percent'])} |"
        )

    if missing:
        lines.extend(["", "## Missing profile pairs", ""])
        lines.extend(f"- {name}" for name in missing)

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_reports(
    *,
    output_md: str | None,
    output_json: str | None,
    status: str,
    min_pair_speedup: float,
    min_geomean_speedup: float,
    geomean: float | None,
    require_pair_floor: bool,
    time_field: str,
    rows: list[dict],
    missing: list[str],
    reason: str | None,
) -> None:
    if output_md:
        write_markdown_report(
            Path(output_md),
            status=status,
            min_pair_speedup=min_pair_speedup,
            min_geomean_speedup=min_geomean_speedup,
            geomean=geomean,
            require_pair_floor=require_pair_floor,
            time_field=time_field,
            rows=rows,
            missing=missing,
            reason=reason,
        )
    if output_json:
        write_json_report(
            Path(output_json),
            status=status,
            min_pair_speedup=min_pair_speedup,
            min_geomean_speedup=min_geomean_speedup,
            geomean=geomean,
            require_pair_floor=require_pair_floor,
            time_field=time_field,
            rows=rows,
            missing=missing,
            reason=reason,
        )


def no_profile_reason() -> str:
    return (
        "no normalized external benchmark profile pairs found; run "
        "clipper2next_bench_external_corpus with CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT set "
        f"and include {LEGACY_BENCHMARK} plus BM_external_next*/{PROFILE}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--min-pair-speedup", type=float, default=1.2)
    parser.add_argument("--min-geomean-speedup", type=float, default=1.2)
    parser.add_argument("--time-field", choices=["real_time", "cpu_time"], default="real_time")
    parser.add_argument("--allow-slower-pairs", action="store_true")
    parser.add_argument("--output-md")
    parser.add_argument("--output-json")
    args = parser.parse_args()

    try:
        records = load_records(Path(args.candidate), time_field=args.time_field)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1

    rows, missing = speedup_rows(records)
    require_pair_floor = not args.allow_slower_pairs
    if not rows:
        reason = f"{no_profile_reason()} with {args.time_field}"
        print("status=FAIL")
        print(f"mode={MODE}")
        print(f"profile={PROFILE}")
        print(f"reason={reason}")
        write_reports(
            output_md=args.output_md,
            output_json=args.output_json,
            status="FAIL",
            min_pair_speedup=args.min_pair_speedup,
            min_geomean_speedup=args.min_geomean_speedup,
            geomean=None,
            require_pair_floor=require_pair_floor,
            time_field=args.time_field,
            rows=[],
            missing=missing,
            reason=reason,
        )
        return 1

    geomean = geomean_speedup(rows)
    failed_rows = [row for row in rows if float(row["speedup"]) < args.min_pair_speedup]

    status = "PASS"
    reason = None
    if missing:
        status = "FAIL"
        reason = f"missing normalized external benchmark profile pairs: {len(missing)}"
    elif require_pair_floor and failed_rows:
        status = "FAIL"
        reason = (
            f"{len(failed_rows)} normalized profile pairs are below min pair speedup "
            f"{args.min_pair_speedup:.3f}x"
        )
    elif geomean < args.min_geomean_speedup:
        status = "FAIL"
        reason = (
            f"geomean speedup {geomean:.3f}x is below required "
            f"{args.min_geomean_speedup:.3f}x"
        )

    print(f"status={status}")
    print(f"mode={MODE}")
    print(f"profile={PROFILE}")
    print(f"pairs={len(rows)}")
    print(f"geomean_speedup={geomean:.6f}")
    for row in rows:
        marker = " BELOW_PAIR_FLOOR" if row in failed_rows else ""
        print(
            f"{row['legacy']} => {row['next']}: "
            f"legacy={row['legacy_time']:.3f} next={row['next_time']:.3f} "
            f"speedup={row['speedup']:.3f}x pairing={row['pairing']}{marker}"
        )
    if missing:
        print(f"missing_pairs={len(missing)}")
    if reason:
        print(f"reason={reason}")

    write_reports(
        output_md=args.output_md,
        output_json=args.output_json,
        status=status,
        min_pair_speedup=args.min_pair_speedup,
        min_geomean_speedup=args.min_geomean_speedup,
        geomean=geomean,
        require_pair_floor=require_pair_floor,
        time_field=args.time_field,
        rows=rows,
        missing=missing,
        reason=reason,
    )

    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
