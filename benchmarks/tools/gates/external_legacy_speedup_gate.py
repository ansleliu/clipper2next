#!/usr/bin/env python3
import argparse
import json
import math
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.release_gate_policy import EXTERNAL_CORE_SPEEDUP_PAIRS


DEFAULT_UNPREPARED_MODE = "default-unprepared"
EXPLICIT_REUSE_MODE = "explicit-reuse"


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def load_records(path: Path, time_field: str = "real_time") -> dict[str, dict[str, float | str]]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)

    return load_records_from_payload(payload, time_field=time_field)


def load_records_from_payload(
    payload: dict, time_field: str = "real_time"
) -> dict[str, dict[str, float | str]]:

    benchmarks = payload.get("benchmarks")
    if not isinstance(benchmarks, list):
        raise ValueError("benchmark JSON has no benchmark list")

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


def default_speedup_pair_candidates(legacy_name: str) -> list[tuple[str, str]]:
    if not legacy_name.startswith("BM_external_"):
        return []
    prefix, separator, source = legacy_name.partition("/")
    if not separator or not prefix.endswith("_legacy"):
        return []
    next_prefix = prefix.removesuffix("_legacy") + "_next"
    return [
        (f"{next_prefix}_unprepared/{source}", "unprepared"),
        (f"{next_prefix}/{source}", "default"),
    ]


def explicit_reuse_pair_candidates(legacy_name: str) -> list[tuple[str, str]]:
    prefix, separator, source = legacy_name.partition("/")
    if not separator:
        return []
    if prefix == "BM_external_legacy":
        return [
            (f"BM_external_next_prepared/{source}", "prepared"),
            (f"BM_external_next_prepared_batch/{source}", "prepared_batch"),
        ]
    if prefix == "BM_external_open_clip_lines_legacy":
        return [(f"BM_external_open_clip_lines_next/{source}", "prepared")]
    if prefix == "BM_external_rectclip_polygon_legacy":
        return [
            (f"BM_external_rectclip_polygon_next/{source}", "prepared"),
            (f"BM_external_rectclip_polygon_next_immutable/{source}", "immutable"),
        ]
    return []


def speedup_pair_candidates(legacy_name: str, mode: str) -> list[tuple[str, str]]:
    if mode == EXPLICIT_REUSE_MODE:
        return explicit_reuse_pair_candidates(legacy_name)
    return default_speedup_pair_candidates(legacy_name)


def append_speedup_row(
    rows: list[dict],
    *,
    legacy_name: str,
    legacy_record: dict[str, float | str],
    next_name: str,
    next_record: dict[str, float | str],
    pairing: str,
) -> None:
    legacy_time = float(legacy_record["time"])
    next_time = float(next_record["time"])
    rows.append({
        "legacy": legacy_name,
        "next": next_name,
        "legacy_time": legacy_time,
        "next_time": next_time,
        "speedup": legacy_time / next_time,
        "legacy_cv_percent": float(legacy_record["cv"]) if "cv" in legacy_record else None,
        "next_cv_percent": float(next_record["cv"]) if "cv" in next_record else None,
        "pairing": pairing,
    })


def speedup_rows(
    records: dict[str, dict[str, float | str]],
    *,
    mode: str,
) -> tuple[list[dict], list[str], list[str]]:
    rows: list[dict] = []
    missing: list[str] = []
    skipped: list[str] = []
    legacy_names = sorted(
        name
        for name, record in records.items()
        if name.startswith("BM_external_")
        and name.split("/", 1)[0].endswith("_legacy")
        and "time" in record
        and float(record["time"]) > 0.0
    )

    for legacy_name in legacy_names:
        legacy_record = records[legacy_name]
        candidates = speedup_pair_candidates(legacy_name, mode)
        if mode == EXPLICIT_REUSE_MODE and not candidates:
            skipped.append(legacy_name)
            continue

        if mode == EXPLICIT_REUSE_MODE:
            matched = False
            for candidate_name, pairing in candidates:
                candidate_record = records.get(candidate_name)
                if candidate_record is None or "time" not in candidate_record:
                    continue
                if float(candidate_record["time"]) <= 0.0:
                    continue
                append_speedup_row(
                    rows,
                    legacy_name=legacy_name,
                    legacy_record=legacy_record,
                    next_name=candidate_name,
                    next_record=candidate_record,
                    pairing=pairing,
                )
                matched = True
            if not matched:
                missing.append(legacy_name)
            continue

        selected_name = ""
        selected_record: dict[str, float | str] | None = None
        selected_pairing = ""
        for candidate_name, pairing in candidates:
            candidate_record = records.get(candidate_name)
            if candidate_record is None or "time" not in candidate_record:
                continue
            if float(candidate_record["time"]) <= 0.0:
                continue
            selected_name = candidate_name
            selected_record = candidate_record
            selected_pairing = pairing
            break

        if selected_record is None:
            missing.append(legacy_name)
            continue
        append_speedup_row(
            rows,
            legacy_name=legacy_name,
            legacy_record=legacy_record,
            next_name=selected_name,
            next_record=selected_record,
            pairing=selected_pairing,
        )

    return rows, missing, skipped


def core_speedup_rows(
    records: dict[str, dict[str, float | str]],
) -> tuple[list[dict], list[str], list[str]]:
    rows: list[dict] = []
    missing: list[str] = []
    for legacy_name, next_name in EXTERNAL_CORE_SPEEDUP_PAIRS:
        legacy_record = records.get(legacy_name)
        next_record = records.get(next_name)
        if not is_positive_record(legacy_record) or not is_positive_record(next_record):
            missing.append(f"{legacy_name} => {next_name}")
            continue
        append_speedup_row(
            rows,
            legacy_name=legacy_name,
            legacy_record=legacy_record,
            next_name=next_name,
            next_record=next_record,
            pairing="unprepared" if "_next_unprepared/" in next_name else "default",
        )
    return rows, missing, []


def is_positive_record(record: dict[str, float | str] | None) -> bool:
    return record is not None and "time" in record and float(record["time"]) > 0.0


def geomean_speedup(rows: list[dict]) -> float:
    return math.exp(sum(math.log(float(row["speedup"])) for row in rows) / len(rows))


def write_json_report(
    path: Path,
    *,
    mode: str,
    status: str,
    min_pair_speedup: float,
    min_geomean_speedup: float,
    geomean: float | None,
    require_pair_floor: bool,
    time_field: str,
    rows: list[dict],
    missing: list[str],
    skipped: list[str],
    reason: str | None,
) -> None:
    ensure_parent(path)
    payload: dict = {
        "status": status,
        "mode": mode,
        "min_pair_speedup": min_pair_speedup,
        "min_geomean_speedup": min_geomean_speedup,
        "geomean_speedup": geomean,
        "require_pair_floor": require_pair_floor,
        "time_field": time_field,
        "rows": rows,
        "missing": missing,
        "skipped": skipped,
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
    mode: str,
    status: str,
    min_pair_speedup: float,
    min_geomean_speedup: float,
    geomean: float | None,
    require_pair_floor: bool,
    time_field: str,
    rows: list[dict],
    missing: list[str],
    skipped: list[str],
    reason: str | None,
) -> None:
    ensure_parent(path)
    geomean_text = "n/a" if geomean is None else f"{geomean:.3f}x"
    pair_floor_text = "required" if require_pair_floor else "geomean-only"
    lines = [
        "# External Legacy Speedup Gate",
        "",
        f"Status: **{status}**",
        f"Mode: **{mode}**",
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
        lines.extend(["", "## Missing pairs", ""])
        lines.extend(f"- {name}" for name in missing)
    if skipped:
        lines.extend(["", "## Skipped legacy rows", ""])
        lines.extend(f"- {name}" for name in skipped)

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_reports(
    *,
    output_md: str | None,
    output_json: str | None,
    mode: str,
    status: str,
    min_pair_speedup: float,
    min_geomean_speedup: float,
    geomean: float | None,
    require_pair_floor: bool,
    time_field: str,
    rows: list[dict],
    missing: list[str],
    skipped: list[str],
    reason: str | None,
) -> None:
    if output_md:
        write_markdown_report(
            Path(output_md),
            mode=mode,
            status=status,
            min_pair_speedup=min_pair_speedup,
            min_geomean_speedup=min_geomean_speedup,
            geomean=geomean,
            require_pair_floor=require_pair_floor,
            time_field=time_field,
            rows=rows,
            missing=missing,
            skipped=skipped,
            reason=reason,
        )
    if output_json:
        write_json_report(
            Path(output_json),
            mode=mode,
            status=status,
            min_pair_speedup=min_pair_speedup,
            min_geomean_speedup=min_geomean_speedup,
            geomean=geomean,
            require_pair_floor=require_pair_floor,
            time_field=time_field,
            rows=rows,
            missing=missing,
            skipped=skipped,
            reason=reason,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True)
    parser.add_argument(
        "--mode",
        choices=[EXPLICIT_REUSE_MODE, DEFAULT_UNPREPARED_MODE],
        default=DEFAULT_UNPREPARED_MODE,
    )
    parser.add_argument("--min-pair-speedup", type=float, default=1.2)
    parser.add_argument("--min-geomean-speedup", type=float, default=1.2)
    parser.add_argument("--time-field", choices=["real_time", "cpu_time"], default="real_time")
    parser.add_argument("--allow-slower-pairs", action="store_true")
    parser.add_argument("--require-core-pairs", action="store_true")
    parser.add_argument("--output-md")
    parser.add_argument("--output-json")
    args = parser.parse_args()

    try:
        records = load_records(Path(args.candidate), time_field=args.time_field)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1

    if args.require_core_pairs:
        if args.mode != DEFAULT_UNPREPARED_MODE:
            parser.error("--require-core-pairs requires --mode default-unprepared")
        rows, missing, skipped = core_speedup_rows(records)
    else:
        rows, missing, skipped = speedup_rows(records, mode=args.mode)
    require_pair_floor = not args.allow_slower_pairs
    if not rows:
        reason = (
            "no explicit reuse benchmark pairs found"
            if args.mode == EXPLICIT_REUSE_MODE
            else "no external legacy benchmark pairs found"
        )
        reason = f"{reason} with {args.time_field}"
        print("status=FAIL")
        print(f"mode={args.mode}")
        print(f"reason={reason}")
        write_reports(
            output_md=args.output_md,
            output_json=args.output_json,
            mode=args.mode,
            status="FAIL",
            min_pair_speedup=args.min_pair_speedup,
            min_geomean_speedup=args.min_geomean_speedup,
            geomean=None,
            require_pair_floor=require_pair_floor,
            time_field=args.time_field,
            rows=[],
            missing=missing,
            skipped=skipped,
            reason=reason,
        )
        return 1

    geomean = geomean_speedup(rows)
    failed_rows = [
        row for row in rows if float(row["speedup"]) < args.min_pair_speedup
    ]

    status = "PASS"
    reason = None
    if missing:
        status = "FAIL"
        reason = f"missing next benchmark pairs for {len(missing)} legacy rows"
    elif require_pair_floor and failed_rows:
        status = "FAIL"
        reason = (
            f"{len(failed_rows)} legacy pairs are below min pair speedup "
            f"{args.min_pair_speedup:.3f}x"
        )
    elif geomean < args.min_geomean_speedup:
        status = "FAIL"
        reason = (
            f"geomean speedup {geomean:.3f}x is below required "
            f"{args.min_geomean_speedup:.3f}x"
        )

    print(f"status={status}")
    print(f"mode={args.mode}")
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
    if skipped:
        print(f"skipped_legacy_rows={len(skipped)}")
    if reason:
        print(f"reason={reason}")

    write_reports(
        output_md=args.output_md,
        output_json=args.output_json,
        mode=args.mode,
        status=status,
        min_pair_speedup=args.min_pair_speedup,
        min_geomean_speedup=args.min_geomean_speedup,
        geomean=geomean,
        require_pair_floor=require_pair_floor,
        time_field=args.time_field,
        rows=rows,
        missing=missing,
        skipped=skipped,
        reason=reason,
    )

    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
