#!/usr/bin/env python3
import argparse
import hashlib
import json
import math
import os
import platform
import re
import random
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.benchmark_command_args import benchmark_min_time_arg
from benchmarks.tools.common.external_core_measurement import collect_pairwise_external_core
from benchmarks.tools.common.external_core_measurement import load_median_records
from benchmarks.tools.common.evidence_identity import (run_observed_command, sha256_file,
    validate_process_observation, git_repository_identity)
from benchmarks.tools.common.public_triangulation import supplement_rows, validate_fixtures, fixture_text
from benchmarks.tools.common.matched_input_authority import (
    checked_file, validate_original_inventory, public_build, capture_records,
    kernel_records, worker_records, producing_observation,
)
from benchmarks.tools.common.release_gate_policy import (
    CALIBRATED_EXTERNAL_MAX_CV_PERCENT,
    CALIBRATED_EXTERNAL_MIN_TIME_SECONDS,
    CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS,
    CALIBRATED_EXTERNAL_REPETITIONS,
    EXTERNAL_CORE_SPEEDUP_MODE,
)
from tools.release.correction_authority import (
    CORRECTION_CV_HOST_OVERRIDES,
    correction_max_cv_percent,
)


STATUS_RE = re.compile(r"^Overall status:\s+(?P<status>\w+)\s*$")
ROW_RE = re.compile(
    r"^\|\s*(?P<run>\d+)\s*\|.*\|\s*(?P<status>PASS|FAIL)\s*\|\s*(?P<failures>.*?)\s*\|$"
)
FAILURE_NAME_RE = re.compile(r"`([^`]+)`")
COMPARE_ROW_RE = re.compile(
    r"^(?P<name>[^:]+): .* regression=(?P<regression>-?\d+(?:\.\d+)?)%"
)
PGO_INSTRUMENT_FLAGS = ("PGINSTRUMENT", "GENPROFILE", "FASTGENPROFILE")
RELEASE_LINKER_FLAG_KEYS = (
    "CMAKE_EXE_LINKER_FLAGS_RELEASE",
    "CMAKE_SHARED_LINKER_FLAGS_RELEASE",
)
TARGET_SCOPED_PGO_MODE_KEY = "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE"
TARGET_SCOPED_PGO_DATABASE_KEY = "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE"
ORACLE_CORPUS_REQUIRED_PROFILES = (
    "overlay",
    "rectclip",
    "rectclip-lines",
    "open-path-overlay",
    "offset",
    "triangulation",
    "bounds",
    "minkowski",
    "polytree",
    "clip-tree",
    "batch",
)


def validate_matched_inputs(manifest):
    """Admit the frozen full matrix before launching any timed process."""
    protocol_path = Path(manifest["protocol"]["path"])
    if sha256_file(protocol_path) != manifest["protocol"]["sha256"]:
        raise ValueError("frozen protocol changed")
    protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
    committed = subprocess.check_output(["git", "-C", str(repo_root()), "show",
                                        "HEAD:benchmarks/public_triangulation/protocol.json"])
    if manifest["protocol"]["sha256"] != "sha256:" + hashlib.sha256(committed).hexdigest():
        raise ValueError("protocol differs from committed source authority")
    if (protocol["product_metric"] != "cpu_time" or protocol["core_metric"] != "real_time" or
            protocol["supplement_metric"] != "real_time" or protocol["statistic"] != "median" or
            protocol["repetitions"] != 7 or protocol["min_time_seconds"] != 0.5 or
            protocol["warmup_seconds"] != 1 or protocol["max_cv_percent"] != 5 or
            protocol.get("cv_host_overrides") != CORRECTION_CV_HOST_OVERRIDES or
            protocol["product_max_ratio"] != 1 or protocol["legacy_pair_min_speedup"] != 1.2 or
            protocol["legacy_geomean_min_speedup"] != 1.2 or protocol["outer_pairs"] != 7 or
            len(protocol["product_rows"]) != 8 or
            sum(len(v) for v in protocol["product_rows"].values()) != 77):
        raise ValueError("frozen mandatory matrix/statistic/policy changed")
    if manifest.get("baseline_head") != "7667354f20374f02386e812deee299a697e8941e":
        raise ValueError("original baseline source identity changed")
    platform_placement = protocol["placements"]["Windows" if os.name == "nt" else "Linux"]
    if manifest["placement"] != {key: platform_placement[key] for key in ("serial", "parallel", "observer")}:
        raise ValueError("platform placement differs from frozen protocol")
    for field in ("fixtures", "fixture_text", "mathematical_verdict", "combined_records", "capture_observation",
                  "kernel_entry", "core_preflight", "worker_placement"):
        entry = manifest[field]
        if sha256_file(Path(entry["path"])) != entry["sha256"]:
            raise ValueError(f"{field} proof/input changed")
    fixtures = json.loads(Path(manifest["fixtures"]["path"]).read_text(encoding="utf-8-sig"))
    cases = validate_fixtures(fixtures)
    if Path(manifest["fixture_text"]["path"]).read_text(encoding="utf-8") != fixture_text(cases):
        raise ValueError("timed fixture text differs from frozen coordinates")
    if manifest["fixtures"]["sha256"] != protocol["fixture_sha256"]:
        raise ValueError("platform fixture differs from common protocol")
    verdict = json.loads(Path(manifest["mathematical_verdict"]["path"]).read_text())
    if verdict.get("status") != "PASS" or verdict.get("cases") != 20 or len(verdict.get("results", [])) != 20:
        raise ValueError("full public output mathematical verdict is missing")
    if verdict.get("records_sha256") != manifest["combined_records"]["sha256"].removeprefix("sha256:"):
        raise ValueError("mathematical verdict does not bind captured complete outputs")
    repository = git_repository_identity(repo_root())
    if (not repository or repository["dirty"] or repository["head_commit"] != manifest["candidate_head"] or
            repository["canonical_source_identity"] != manifest["candidate_source_identity"]):
        raise ValueError("candidate source changed or does not match frozen build source")
    for side in ("old", "candidate"):
        if set(manifest[side]["products"]) != set(protocol["product_rows"]):
            raise ValueError("product executable inventory differs")
        for entry in [manifest[side]["module"], manifest[side]["public"], *manifest[side]["products"].values()]:
            if sha256_file(Path(entry["path"])) != entry["sha256"]:
                raise ValueError("frozen executable/runtime changed")
    original_archive = validate_original_inventory(manifest)
    if sha256_file(Path(manifest["candidate"]["core"]["path"])) != manifest["candidate"]["core"]["sha256"]:
        raise ValueError("frozen core executable changed")
    builds = {side: public_build(repo_root(), side, manifest[side]) for side in ("old", "candidate")}
    capture_records(manifest, cases, builds)
    kernel_records(manifest, cases)
    core = json.loads(Path(manifest["core_preflight"]["path"]).read_text())
    if core.get("status") != "PASS" or core.get("cases") != 240 or len(core.get("results",[])) != 240:
        raise ValueError("original240tri mathematical control is incomplete")
    core_step = manifest["core_producer"]
    expected_core_command = [manifest["candidate"]["core"]["path"], "--clipper2next_triangulation_records"]
    producing_observation(core_step, "external-triangulation-records", "candidate", expected_core_command,
                          manifest["candidate"]["core"], manifest["candidate"]["module"])
    core_log = checked_file(core_step["output"])
    identities = dict(line.split("=", 1) for line in core_log.read_text().splitlines() if line.startswith("correction_") and "=" in line)
    if (core["process_observation"] != core_step["observation"] or
            identities.get("correction_run_id") != core_step["run_id"] or
            core.get("records_sha256") != core_step["output"]["sha256"].removeprefix("sha256:")):
        raise ValueError("external mathematical records do not bind their actual producing run/output")
    if core["original_profile"]["sha256"] != "sha256:3fe7a8983e8c46dc4a189521322e3e4ebc859a84120049aa1339f5c0d5e3d82e":
        raise ValueError("original240request source identity changed")
    for field in ("profile", "original_profile"):
        if sha256_file(Path(core[field]["path"])) != core[field]["sha256"]:
            raise ValueError("external triangulation inputs changed")
    from tools.release.corpus_authority_manifest import validate_frozen_corpus_manifest
    from tools.release.evidence_contract import load_contract
    freeze = core["freeze"]
    validate_frozen_corpus_manifest(Path(freeze["manifest"]), Path(freeze["source_root"]),
                                   Path(freeze["derived_root"]), freeze["sha256"], load_contract())
    if Path(manifest["candidate"]["environment"]["CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT"]).resolve() != Path(freeze["derived_root"]).resolve():
        raise ValueError("timed core root differs from mathematical input authority")
    worker_records(manifest, builds)
    return protocol, cases, original_archive


def run_frozen_matched_suite(manifest_path, output_dir):
    # This is a fixed project matrix, not a configurable experiment framework.
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    protocol, cases, original_archive = validate_matched_inputs(manifest)
    host_name = platform.node()
    platform_name = platform.system()
    max_cv = correction_max_cv_percent(platform_name, host_name)
    if output_dir.exists():
        raise ValueError("matched evidence directory must be fresh")
    output_dir.mkdir(parents=True)
    receipt = {"status": "RUNNING", "evidence_level": "E3 attempted; acceptance pending",
               "manifest_sha256": sha256_file(Path(manifest_path)), "protocol_sha256": manifest["protocol"]["sha256"],
               "platform": platform_name, "host_name": host_name, "max_cv_percent": max_cv,
               "original_archive": original_archive,
               "runs": [], "pairs": [], "failures": []}
    rng = random.Random(protocol["outer_seed"])
    matrix = [("product", exe, row, "cpu_time") for exe, rows in protocol["product_rows"].items() for row in rows]
    matrix += [("supplement", "public", row, "real_time") for row in supplement_rows(cases)]
    def save():
        (output_dir / "matched-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    def run(side, family, exe_name, row, outer, metric):
        binding = manifest[side]
        executable = binding["public"] if family == "supplement" else binding["products"][exe_name]
        stem = f"{outer:02d}-{len(receipt['runs']):04d}-{side}"
        log, raw = output_dir / (stem + ".log"), output_dir / (stem + ".json")
        command = [executable["path"]]
        ready = go = None
        if family == "supplement":
            ready, go = output_dir / (stem + ".ready"), output_dir / (stem + ".go")
            command += ["--fixtures", manifest["fixture_text"]["path"], "--mode", "timing", "--row", row,
                        "--ready-file", str(ready), "--go-file", str(go)]
        else:
            command += [f"--benchmark_filter=^{re.escape(row)}$", "--benchmark_repetitions=7",
                        "--benchmark_min_time=0.5s", "--benchmark_min_warmup_time=1",
                        "--benchmark_report_aggregates_only=false", "--benchmark_enable_random_interleaving=true",
                        f"--benchmark_out={raw}", "--benchmark_out_format=json"]
        cpus = manifest["placement"]["parallel"] if exe_name in protocol["parallel_executables"] else manifest["placement"]["serial"]
        observation = run_observed_command(command, log, run_id=stem,
            executable=executable, module=binding["module"], benchmark_cpus=cpus,
            observer_cpu=manifest["placement"]["observer"], ready_file=ready, go_file=go,
            env=dict(os.environ, **binding.get("environment", {})))
        receipt["runs"].append({"outer_pair": outer, "family": family, "row": row, "side": side,
                                "log": log.name, "raw": raw.name, "process_observation": observation})
        save()
        if observation["exit_code"] != 0:
            raise ValueError(f"{stem}: process failed, retained {log}")
        if family == "supplement":
            raw.write_text(log.read_text(encoding="utf-8"), encoding="utf-8")
        payload = json.loads(raw.read_text(encoding="utf-8"))
        return load_median_records(
            payload, [row], time_field=metric,
            iteration_counts="adaptive" if family == "supplement" else "fixed",
        )[row]
    try:
        for outer in range(7):
            order = list(matrix)
            rng.shuffle(order)
            for family, exe, row, metric in order:
                sides = ["old", "candidate"]
                rng.shuffle(sides)
                measured = {side: run(side, family, exe, row, outer, metric) for side in sides}
                old, candidate = measured["old"], measured["candidate"]
                ratio = candidate["time"] / old["time"]
                valid = (old["time_unit"] == candidate["time_unit"] and ratio <= 1 and
                         old["cv"] <= max_cv and candidate["cv"] <= max_cv)
                pair = {"outer_pair": outer, "family": family, "row": row, "order": sides,
                        "metric": metric, "ratio": ratio, "old": old, "candidate": candidate,
                        "status": "PASS" if valid else "FAIL"}
                receipt["pairs"].append(pair)
                if not valid: receipt["failures"].append(f"{outer}/{family}/{row}: threshold or CV")
                save()
        # Preserve the existing pairwise-process core owner and all32/14 identities.
        from benchmarks.tools.common.release_gate_policy import EXTERNAL_CORE_BENCHMARK_NAMES, EXTERNAL_CORE_SPEEDUP_PAIRS
        for outer in range(7):
            binding = manifest["candidate"]
            def core_command(command, log):
                observation = run_observed_command(command, log, run_id=f"core-{outer}-{log.stem}",
                    executable=binding["core"], module=binding["module"],
                    benchmark_cpus=manifest["placement"]["serial"], observer_cpu=manifest["placement"]["observer"],
                    env=dict(os.environ, **binding.get("environment", {})))
                receipt["runs"].append({"family": "core", "outer_pair": outer, "process_observation": observation,
                                        "log": str(log.relative_to(output_dir))})
                save()
                return observation["exit_code"]
            raw = output_dir / f"core-{outer}.json"
            status, reason, _ = collect_pairwise_external_core(benchmark_exe=Path(binding["core"]["path"]),
                output_dir=output_dir, prefix=f"core-{outer}", repetitions=7, min_time=0.5,
                min_warmup_time=1, benchmark_json=raw, benchmark_log=output_dir / f"core-{outer}.log",
                benchmark_filter_arg=lambda value: f"--benchmark_filter={value}", run_command=core_command)
            if status: raise ValueError(reason)
            records = load_median_records(json.loads(raw.read_text()), EXTERNAL_CORE_BENCHMARK_NAMES, time_field="real_time")
            ratios = [records[a]["time"] / records[b]["time"] for a, b in EXTERNAL_CORE_SPEEDUP_PAIRS]
            geomean = math.exp(statistics.mean(math.log(r) for r in ratios))
            if min(ratios) < 1.2 or geomean < 1.2 or any(r["cv"] > max_cv for r in records.values()):
                receipt["failures"].append(f"core-{outer}: required legacy speedup/CV")
            receipt.setdefault("core", []).append({"outer_pair": outer, "geomean": geomean,
                                                   "ratios": ratios, "records": records})
            save()
        receipt["status"] = "FAIL" if receipt["failures"] else "PASS"
        receipt["evidence_level"] = "E3 complete matched protocol"
        # Median paired ratio is descriptive; all declared per-pair gates above remain binding.
        receipt["uncertainty"] = {}
        for family, _, row, _ in matrix:
            ratios = [p["ratio"] for p in receipt["pairs"] if p["family"] == family and p["row"] == row]
            samples = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(10000))
            receipt["uncertainty"][family + "/" + row] = {"median_paired_ratio": statistics.median(ratios),
                "descriptive_bootstrap_95_percent": [samples[250], samples[9749]], "waiver": False}
        validate_matched_inputs(manifest)
    except (OSError, ValueError, KeyError) as error:
        receipt["status"] = "FAIL"
        receipt["failures"].append(str(error))
    save()
    return int(receipt["status"] != "PASS")


@dataclass(frozen=True)
class BenchmarkSuite:
    name: str
    stem: str
    target_name: str
    executable_stem: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def benchmark_tools_dir() -> Path:
    return repo_root() / "benchmarks" / "tools"


def benchmark_output_dirs(build_dir: Path) -> list[Path]:
    return [build_dir / "bin"]


def benchmark_output_dir(build_dir: Path) -> Path:
    return benchmark_output_dirs(build_dir)[0]


def resolve_path(path: str | Path) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate
    return repo_root() / candidate


def cmake_cache_values(build_dir: Path) -> dict[str, str]:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.exists():
        return {}

    values: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_type, value = line.split("=", 1)
        key = key_type.split(":", 1)[0]
        values[key] = value
    return values


def release_benchmark_build_errors(build_dir: Path) -> list[str]:
    values = cmake_cache_values(build_dir)
    errors: list[str] = []
    pgo_mode = values.get(TARGET_SCOPED_PGO_MODE_KEY, "OFF").upper()
    if pgo_mode == "INSTRUMENT":
        errors.append("CMakeCache.txt selects target-scoped PGO instrumentation mode")
    elif pgo_mode == "OPTIMIZE" and not values.get(TARGET_SCOPED_PGO_DATABASE_KEY, ""):
        errors.append("CMakeCache.txt selects PGO optimization without an explicit PGD")
    for key in RELEASE_LINKER_FLAG_KEYS:
        flags = values.get(key, "")
        upper_flags = flags.upper()
        if any(flag in upper_flags for flag in PGO_INSTRUMENT_FLAGS):
            errors.append(f"CMakeCache.txt contains PGO instrumentation flags in {key}: {flags}")
    return errors


def oracle_environment_errors() -> list[str]:
    errors: list[str] = []
    if os.environ.get("CLIPPER2NEXT_CALIBRATED_RUNNER") != "1":
        errors.append("CLIPPER2NEXT_CALIBRATED_RUNNER=1 is required")

    root_value = os.environ.get("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT", "").strip()
    if not root_value:
        errors.append("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is required")
        return errors
    root = Path(root_value)
    if not root.is_dir():
        errors.append(f"CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not a directory: {root}")
        return errors

    profile_dir = root / "normalized" / "benchmark"
    missing = [
        profile
        for profile in ORACLE_CORPUS_REQUIRED_PROFILES
        if not (profile_dir / f"{profile}.jsonl").is_file()
        or (profile_dir / f"{profile}.jsonl").stat().st_size == 0
    ]
    if missing:
        errors.append(
            "geometry corpus benchmark profiles are missing or empty: "
            + ", ".join(missing)
        )
    return errors


def product_suites() -> dict[str, BenchmarkSuite]:
    return {
        "clip": BenchmarkSuite(
            "Clip product baseline",
            "clip",
            "clipper2next_bench_product_clip",
            "clipper2next_bench_product_clip",
        ),
        "offset": BenchmarkSuite(
            "Offset product baseline",
            "offset",
            "clipper2next_bench_product_offset",
            "clipper2next_bench_product_offset",
        ),
        "rectclip": BenchmarkSuite(
            "RectClip product baseline",
            "rectclip",
            "clipper2next_bench_product_rectclip",
            "clipper2next_bench_product_rectclip",
        ),
        "batch": BenchmarkSuite(
            "Batch product baseline",
            "batch_parallel",
            "clipper2next_bench_batch_parallel",
            "clipper2next_bench_batch_parallel",
        ),
        "triangulation": BenchmarkSuite(
            "Triangulation product baseline",
            "triangulation",
            "clipper2next_bench_product_triangulation",
            "clipper2next_bench_product_triangulation",
        ),
    }


def selected_product_suites(name: str) -> dict[str, BenchmarkSuite]:
    suites = product_suites()
    if name == "all":
        return suites
    return {name: suites[name]}


def benchmark_json_path(output_dir: Path, prefix: str, benchmark: BenchmarkSuite) -> Path:
    return output_dir / f"{prefix}_{benchmark.stem}.json"


def find_benchmark_executable(build_dir: Path, executable_stem: str) -> Path | None:
    suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        benchmark_dir / f"{executable_stem}{suffix}"
        for benchmark_dir in benchmark_output_dirs(build_dir)
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def run_command(command: list[str], log_path: Path) -> int:
    with log_path.open("w", encoding="utf-8") as handle:
        handle.write(" ".join(command))
        handle.write("\n\n")
        completed = subprocess.run(
            command,
            cwd=repo_root(),
            stdout=handle,
            stderr=subprocess.STDOUT,
            check=False,
        )
    return completed.returncode


def run_product_benchmark(
    benchmark: BenchmarkSuite,
    build_dir: Path,
    output_json: Path,
    run_log: Path,
    repetitions: int,
    min_time: float,
) -> int:
    executable = find_benchmark_executable(build_dir, benchmark.executable_stem)
    if executable is None:
        run_log.write_text(
            f"missing benchmark executable for target {benchmark.target_name} under {build_dir}\n",
            encoding="utf-8",
        )
        return 1
    command = [
        str(executable),
        f"--benchmark_repetitions={repetitions}",
        "--benchmark_report_aggregates_only=true",
        "--benchmark_enable_random_interleaving=true",
        benchmark_min_time_arg(min_time),
        f"--benchmark_out={output_json}",
        "--benchmark_out_format=json",
    ]
    return run_command(command, run_log)


def run_external_core_benchmark(
    build_dir: Path,
    output_json: Path,
    run_log: Path,
    repetitions: int,
    min_time: float,
) -> int:
    executable = find_benchmark_executable(build_dir, "clipper2next_bench_external_corpus")
    if executable is None:
        run_log.write_text(
            f"missing benchmark executable for target clipper2next_bench_external_corpus under {build_dir}\n",
            encoding="utf-8",
        )
        return 1
    status, _, _ = collect_pairwise_external_core(
        benchmark_exe=executable,
        output_dir=output_json.parent,
        prefix=output_json.stem,
        repetitions=repetitions,
        min_time=min_time,
        min_warmup_time=CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS,
        benchmark_json=output_json,
        benchmark_log=run_log,
        benchmark_filter_arg=lambda value: f"--benchmark_filter={value}",
        run_command=run_command,
    )
    return status


def compare_product_benchmark(
    baseline: Path,
    candidate: Path,
    compare_log: Path,
    max_regression_percent: float,
) -> int:
    comparator = benchmark_tools_dir() / "compare" / "compare_benchmark_json.py"
    command = [
        sys.executable,
        str(comparator),
        "--baseline",
        str(baseline),
        "--candidate",
        str(candidate),
        "--max-regression-percent",
        str(max_regression_percent),
    ]
    return run_command(command, compare_log)


def classify_product_compare_log(
    compare_log: Path,
    max_regression_percent: float,
) -> tuple[str, bool, list[str]]:
    if not compare_log.exists():
        return ("FAIL", True, ["missing compare log"])

    failures: list[str] = []
    for line in compare_log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = COMPARE_ROW_RE.match(line)
        if not match:
            continue
        regression = float(match.group("regression"))
        if regression <= max_regression_percent:
            continue
        failures.append(match.group("name"))
    # Called after comparator rejection. Missing/error/CV failures are blocking
    # even when the log contains no rejected ratio row.
    return ("FAIL", True, failures)


def external_core_speedup_gate_command(
    candidate_json: Path,
    output_md: Path,
    output_json: Path,
) -> list[str]:
    gate = benchmark_tools_dir() / "gates" / "external_legacy_speedup_gate.py"
    return [
        sys.executable,
        str(gate),
        "--candidate",
        str(candidate_json),
        "--mode",
        EXTERNAL_CORE_SPEEDUP_MODE,
        "--require-core-pairs",
        "--output-md",
        str(output_md),
        "--output-json",
        str(output_json),
    ]


def external_core_variance_gate_command(
    candidate_json: Path,
    output_md: Path,
    output_json: Path,
    *,
    max_cv_percent: float,
) -> list[str]:
    gate = benchmark_tools_dir() / "gates" / "external_benchmark_variance_gate.py"
    return [
        sys.executable,
        str(gate),
        "--candidate",
        str(candidate_json),
        "--max-cv-percent",
        str(max_cv_percent),
        "--require-calibrated-runner",
        "--require-core-benchmarks",
        "--output-md",
        str(output_md),
        "--output-json",
        str(output_json),
    ]


def classify_external_core_speedup_gate(
    report_json: Path,
    *,
    exit_code: int,
) -> tuple[str, bool, str]:
    if not report_json.exists():
        return ("FAIL", True, f"mode={EXTERNAL_CORE_SPEEDUP_MODE}; missing speedup JSON")

    try:
        payload = json.loads(report_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return ("FAIL", True, f"mode={EXTERNAL_CORE_SPEEDUP_MODE}; invalid speedup JSON: {error}")

    status = str(payload.get("status", "FAIL"))
    if status not in ("PASS", "FAIL"):
        status = "FAIL"

    mode = str(payload.get("mode") or EXTERNAL_CORE_SPEEDUP_MODE)
    min_pair = payload.get("min_pair_speedup")
    min_geomean = payload.get("min_geomean_speedup")
    geomean = payload.get("geomean_speedup")
    note_parts = [f"mode={mode}"]
    if isinstance(min_pair, int | float):
        note_parts.append(f"min_pair={float(min_pair):.2f}x")
    if isinstance(min_geomean, int | float):
        note_parts.append(f"min_geomean={float(min_geomean):.2f}x")
    if isinstance(geomean, int | float):
        note_parts.append(f"geomean={float(geomean):.3f}x")
    reason = payload.get("reason")
    if isinstance(reason, str) and reason:
        note_parts.append(reason)
    if exit_code != 0 and status == "PASS":
        status = "FAIL"
        note_parts.append(f"gate exit code {exit_code}")

    release_blocking = status != "PASS"
    return (status, release_blocking, "; ".join(note_parts))


def classify_external_core_variance_gate(
    report_json: Path,
    *,
    exit_code: int,
) -> tuple[str, bool, str]:
    if not report_json.exists():
        return ("FAIL", True, "missing variance JSON")

    try:
        payload = json.loads(report_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return ("FAIL", True, f"invalid variance JSON: {error}")

    status = str(payload.get("status", "FAIL"))
    if status not in ("PASS", "NOISY", "FAIL"):
        status = "FAIL"

    max_cv = payload.get("max_cv_percent")
    rows = payload.get("rows")
    noisy_rows = 0
    if isinstance(rows, list):
        noisy_rows = sum(1 for row in rows if isinstance(row, dict) and row.get("status") == "NOISY")

    note_parts: list[str] = []
    if isinstance(max_cv, int | float):
        note_parts.append(f"max_cv={float(max_cv):.2f}%")
    note_parts.append(f"noisy_rows={noisy_rows}")
    reason = payload.get("reason")
    if isinstance(reason, str) and reason:
        note_parts.append(reason)
    if exit_code not in (0, 2) and status != "FAIL":
        status = "FAIL"
        note_parts.append(f"gate exit code {exit_code}")
    if exit_code == 0 and status != "PASS":
        status = "FAIL"
        note_parts.append("gate passed with non-PASS status")

    release_blocking = status != "PASS"
    return (status, release_blocking, "; ".join(note_parts))


def run_product_suite(args: argparse.Namespace, output_dir: Path, build_dir: Path) -> int:
    suites: list[dict[str, object]] = []
    for suite_key, benchmark in selected_product_suites(args.suite).items():
        if args.capture_baseline:
            baseline = benchmark_json_path(output_dir, args.baseline_prefix, benchmark)
            run_log = output_dir / f"{args.prefix}_{benchmark.stem}_baseline_run.log"
            run_code = run_product_benchmark(
                benchmark,
                build_dir,
                baseline,
                run_log,
                args.repetitions,
                args.min_time,
            )
            status = "BASELINE_CAPTURED" if run_code == 0 else "FAIL"
            suites.append(
                {
                    "name": benchmark.name,
                    "status": status,
                    "release_blocking": run_code != 0,
                    "baseline": baseline.name,
                    "candidate": "",
                    "run_log": run_log.name,
                    "compare_log": "",
                    "exit_code": run_code,
                    "note": f"suite={suite_key}",
                }
            )
            continue

        baseline = benchmark_json_path(output_dir, args.baseline_prefix, benchmark)
        candidate = benchmark_json_path(output_dir, args.prefix, benchmark)
        run_log = output_dir / f"{args.prefix}_{benchmark.stem}_run.log"
        compare_log = output_dir / f"{args.prefix}_{benchmark.stem}_compare.log"
        if not baseline.exists():
            compare_log.write_text(f"missing baseline JSON: {baseline}\n", encoding="utf-8")
            suites.append(
                {
                    "name": benchmark.name,
                    "status": "FAIL",
                    "release_blocking": True,
                    "baseline": baseline.name,
                    "candidate": candidate.name,
                    "run_log": run_log.name,
                    "compare_log": compare_log.name,
                    "exit_code": 1,
                    "note": f"suite={suite_key}; capture with --capture-baseline first",
                }
            )
            continue

        run_code = run_product_benchmark(
            benchmark,
            build_dir,
            candidate,
            run_log,
            args.repetitions,
            args.min_time,
        )
        compare_code = None
        if run_code == 0 and candidate.exists():
            compare_code = compare_product_benchmark(
                baseline,
                candidate,
                compare_log,
                args.max_regression_percent,
            )
        else:
            compare_log.write_text("candidate benchmark did not complete\n", encoding="utf-8")

        if run_code != 0 or compare_code is None:
            status = "FAIL"
            release_blocking = True
            failures: list[str] = []
        elif compare_code == 0:
            status = "PASS"
            release_blocking = False
            failures = []
        else:
            status, release_blocking, failures = classify_product_compare_log(
                compare_log,
                args.max_regression_percent,
            )

        note = f"suite={suite_key}; threshold={args.max_regression_percent:.2f}%"
        if failures:
            note = f"{note}; noisy_or_failing_rows={', '.join(failures)}"
        suites.append(
            {
                "name": benchmark.name,
                "status": status,
                "release_blocking": release_blocking,
                "baseline": baseline.name,
                "candidate": candidate.name,
                "run_log": run_log.name,
                "compare_log": compare_log.name,
                "exit_code": run_code if compare_code is None else compare_code,
                "note": note,
            }
        )

    release_blocking = any(bool(suite["release_blocking"]) for suite in suites)
    overall_status = "FAIL" if release_blocking else (
        "NOISY" if any(suite["status"] == "NOISY" for suite in suites) else "PASS"
    )
    report = output_dir / f"{args.prefix}_report.md"
    lines = [
        "# Release Performance Suite",
        "",
        "Mode: `product`",
        f"Build dir: `{build_dir}`",
        f"Baseline prefix: `{args.baseline_prefix}`",
        f"Overall status: {overall_status}",
        f"Release blocking: {'YES' if release_blocking else 'NO'}",
        "",
        "| Suite | Status | Release Blocking | Baseline | Candidate | Run Log | Compare Log | Exit Code | Note |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for suite in suites:
        lines.append(
            f"| {suite['name']} | {suite['status']} | "
            f"{'YES' if suite['release_blocking'] else 'NO'} | "
            f"`{suite['baseline']}` | `{suite['candidate']}` | "
            f"`{suite['run_log']}` | `{suite['compare_log']}` | "
            f"{suite['exit_code']} | {suite['note']} |"
        )
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"report={report}")
    print(f"status={overall_status}")
    print(f"release_blocking={'YES' if release_blocking else 'NO'}")
    return 1 if release_blocking else 0


def parse_overall_status(report: Path) -> str:
    if not report.exists():
        return "FAIL"
    for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
        match = STATUS_RE.match(line.strip())
        if match:
            return match.group("status")
    return "FAIL"


def parse_repeated_rows(report: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    if not report.exists():
        return rows
    for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
        match = ROW_RE.match(line.strip())
        if not match:
            continue
        rows.append(
            {
                "run": int(match.group("run")),
                "status": match.group("status"),
                "failures": FAILURE_NAME_RE.findall(match.group("failures")),
            }
        )
    return rows


def repeated_threshold_status(report: Path, required_passes: int) -> str:
    rows = parse_repeated_rows(report)
    if not rows:
        return "FAIL"
    pass_count = sum(1 for row in rows if row["status"] == "PASS")
    return "PASS" if pass_count >= required_passes else parse_overall_status(report)


def clip_family(name: str) -> tuple[str, str] | None:
    if name.startswith("BM_next_"):
        return ("next", name.removeprefix("BM_next_"))
    if name.startswith("BM_legacy_"):
        return ("legacy", name.removeprefix("BM_legacy_"))
    return None


def calibrated_clip_status(report: Path) -> tuple[str, list[str]]:
    rows = parse_repeated_rows(report)
    if not rows:
        return ("FAIL", ["missing clip repeated rows"])

    blocking: list[str] = []
    noisy_rows = 0
    for row in rows:
        failures = row["failures"]
        if not failures:
            continue
        next_families: set[str] = set()
        legacy_families: set[str] = set()
        for failure in failures:
            family = clip_family(failure)
            if family is None:
                continue
            side, name = family
            if side == "next":
                next_families.add(name)
            else:
                legacy_families.add(name)
        next_only = sorted(next_families - legacy_families)
        if next_only:
            for family in next_only:
                blocking.append(f"run {row['run']}: next-only {family}")
        else:
            noisy_rows += 1

    if blocking:
        return ("FAIL", blocking)
    if noisy_rows:
        return ("NOISY", [])
    return ("PASS", [])


def run_oracle_baseline_capture(args: argparse.Namespace, output_dir: Path, build_dir: Path) -> int:
    benchmark_dir = benchmark_output_dir(build_dir)
    runner = benchmark_tools_dir() / "runners" / "run_benchmark_gate.py"
    suites = ("clip", "offset", "rectclip", "batch")
    rows: list[dict[str, object]] = []

    for suite_key in suites:
        run_log = output_dir / f"{args.prefix}_{suite_key}_baseline_capture.log"
        command = [
            sys.executable,
            str(runner),
            "--suite",
            suite_key,
            "--output-dir",
            str(output_dir),
            "--prefix",
            args.baseline_prefix,
            "--benchmark-dir",
            str(benchmark_dir),
            "--repetitions",
            str(args.repetitions),
            "--min-time",
            str(args.min_time),
            "--skip-build",
        ]
        run_code = run_command(command, run_log)
        stem = "batch_parallel" if suite_key == "batch" else suite_key
        rows.append(
            {
                "suite": suite_key,
                "status": "BASELINE_CAPTURED" if run_code == 0 else "FAIL",
                "baseline": f"{args.baseline_prefix}_{stem}.json",
                "log": run_log.name,
                "exit_code": run_code,
            }
        )

    failed = any(row["exit_code"] != 0 for row in rows)
    overall_status = "FAIL" if failed else "PASS"
    report = output_dir / f"{args.prefix}_oracle_baseline_capture_report.md"
    lines = [
        "# Oracle Baseline Capture",
        "",
        f"Benchmark dir: `{benchmark_dir}`",
        f"Baseline prefix: `{args.baseline_prefix}`",
        f"Overall status: {overall_status}",
        "",
        "| Suite | Status | Baseline | Log | Exit Code |",
        "| --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| {row['suite']} | {row['status']} | `{row['baseline']}` | "
            f"`{row['log']}` | {row['exit_code']} |"
        )
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"report={report}")
    print(f"status={overall_status}")
    return 1 if failed else 0


def run_oracle_suite(args: argparse.Namespace, output_dir: Path, build_dir: Path) -> int:
    benchmark_dir = benchmark_output_dir(build_dir)

    tools_dir = benchmark_tools_dir()
    batch_runner = tools_dir / "runners" / "run_batch_pair_ratio_gate.py"
    offset_runner = tools_dir / "runners" / "run_offset_pair_ratio_gate.py"
    repeated_runner = tools_dir / "runners" / "run_repeated_benchmark_gate.py"

    suites: list[dict[str, object]] = []

    batch_prefix = f"{args.prefix}_batch_pair"
    batch_log = output_dir / f"{batch_prefix}_gate.log"
    batch_command = [
        sys.executable,
        str(batch_runner),
        "--runs",
        "5",
        "--baseline-prefix",
        args.baseline_prefix,
        "--output-dir",
        str(output_dir),
        "--prefix",
        batch_prefix,
        "--benchmark-dir",
        str(benchmark_dir),
        "--skip-build",
    ]
    batch_code = run_command(batch_command, batch_log)
    batch_report = output_dir / f"{batch_prefix}_pair_ratio_report.md"
    batch_status = parse_overall_status(batch_report)
    suites.append(
        {
            "name": "Batch paired ratio",
            "status": batch_status,
            "release_blocking": batch_status == "FAIL",
            "report": batch_report.name,
            "log": batch_log.name,
            "exit_code": batch_code,
            "note": "requires 3/5 passing runs",
        }
    )

    offset_prefix = f"{args.prefix}_offset_pair"
    offset_log = output_dir / f"{offset_prefix}_gate.log"
    offset_command = [
        sys.executable,
        str(offset_runner),
        "--runs",
        "5",
        "--baseline-prefix",
        args.baseline_prefix,
        "--output-dir",
        str(output_dir),
        "--prefix",
        offset_prefix,
        "--benchmark-dir",
        str(benchmark_dir),
        "--skip-build",
    ]
    offset_code = run_command(offset_command, offset_log)
    offset_report = output_dir / f"{offset_prefix}_offset_pair_ratio_report.md"
    offset_status = parse_overall_status(offset_report)
    suites.append(
        {
            "name": "Offset paired ratio",
            "status": offset_status,
            "release_blocking": offset_status == "FAIL",
            "report": offset_report.name,
            "log": offset_log.name,
            "exit_code": offset_code,
            "note": "requires 3/5 passing runs",
        }
    )

    rectclip_prefix = f"{args.prefix}_rectclip_repeated"
    rectclip_log = output_dir / f"{rectclip_prefix}_gate.log"
    rectclip_command = [
        sys.executable,
        str(repeated_runner),
        "--suite",
        "rectclip",
        "--runs",
        "3",
        "--baseline-prefix",
        args.baseline_prefix,
        "--output-dir",
        str(output_dir),
        "--prefix",
        rectclip_prefix,
        "--benchmark-dir",
        str(benchmark_dir),
        "--max-regression-percent",
        "5.0",
        "--skip-build",
    ]
    rectclip_code = run_command(rectclip_command, rectclip_log)
    rectclip_report = output_dir / f"{rectclip_prefix}_repeated_report.md"
    rectclip_status = repeated_threshold_status(rectclip_report, required_passes=2)
    suites.append(
        {
            "name": "RectClip repeated",
            "status": rectclip_status,
            "release_blocking": rectclip_status == "FAIL",
            "report": rectclip_report.name,
            "log": rectclip_log.name,
            "exit_code": rectclip_code,
            "note": "requires 2/3 passing runs",
        }
    )

    clip_prefix = f"{args.prefix}_clip_calibrated"
    clip_log = output_dir / f"{clip_prefix}_gate.log"
    clip_command = [
        sys.executable,
        str(repeated_runner),
        "--suite",
        "clip",
        "--runs",
        "3",
        "--baseline-prefix",
        args.baseline_prefix,
        "--output-dir",
        str(output_dir),
        "--prefix",
        clip_prefix,
        "--benchmark-dir",
        str(benchmark_dir),
        "--max-regression-percent",
        "5.0",
        "--skip-build",
    ]
    clip_code = run_command(clip_command, clip_log)
    clip_report = output_dir / f"{clip_prefix}_repeated_report.md"
    clip_status, blocking_rows = calibrated_clip_status(clip_report)
    suites.append(
        {
            "name": "Clip calibrated repeated",
            "status": clip_status,
            "release_blocking": bool(blocking_rows),
            "report": clip_report.name,
            "log": clip_log.name,
            "exit_code": clip_code,
            "note": "; ".join(blocking_rows) if blocking_rows else "legacy drift makes matching failures NOISY",
        }
    )

    external_prefix = f"{args.prefix}_external_core"
    external_candidate = output_dir / f"{external_prefix}_benchmark.json"
    external_run_log = output_dir / f"{external_prefix}_benchmark.log"
    external_variance_report = output_dir / f"{external_prefix}_variance_gate.md"
    external_variance_json = output_dir / f"{external_prefix}_variance_gate.json"
    external_variance_log = output_dir / f"{external_prefix}_variance_gate.log"
    external_speedup_report = output_dir / f"{external_prefix}_speedup_gate.md"
    external_speedup_json = output_dir / f"{external_prefix}_speedup_gate.json"
    external_speedup_log = output_dir / f"{external_prefix}_speedup_gate.log"
    external_run_code = run_external_core_benchmark(
        build_dir,
        external_candidate,
        external_run_log,
        args.repetitions,
        args.min_time,
    )
    if external_run_code == 0 and external_candidate.exists():
        external_variance_code = run_command(
            external_core_variance_gate_command(
                external_candidate,
                external_variance_report,
                external_variance_json,
                max_cv_percent=args.max_external_cv_percent,
            ),
            external_variance_log,
        )
        external_speedup_code = run_command(
            external_core_speedup_gate_command(
                external_candidate,
                external_speedup_report,
                external_speedup_json,
            ),
            external_speedup_log,
        )
    else:
        external_variance_code = 1
        external_variance_log.write_text(
            "external corpus benchmark did not complete\n",
            encoding="utf-8",
        )
        external_speedup_code = 1
        external_speedup_log.write_text(
            "external corpus benchmark did not complete\n",
            encoding="utf-8",
        )
    external_variance_status, external_variance_blocking, external_variance_note = (
        classify_external_core_variance_gate(
            external_variance_json,
            exit_code=external_variance_code,
        )
    )
    suites.append(
        {
            "name": "External core variance",
            "status": external_variance_status,
            "release_blocking": external_variance_blocking,
            "report": external_variance_report.name,
            "log": external_variance_log.name,
            "exit_code": external_variance_code if external_run_code == 0 else external_run_code,
            "note": f"{external_variance_note}; benchmark_log={external_run_log.name}",
        }
    )
    external_status, external_blocking, external_note = classify_external_core_speedup_gate(
        external_speedup_json,
        exit_code=external_speedup_code,
    )
    suites.append(
        {
            "name": "External core speedup",
            "status": external_status,
            "release_blocking": external_blocking,
            "report": external_speedup_report.name,
            "log": external_speedup_log.name,
            "exit_code": external_speedup_code if external_run_code == 0 else external_run_code,
            "note": f"{external_note}; benchmark_log={external_run_log.name}",
        }
    )

    release_blocking = any(bool(suite["release_blocking"]) for suite in suites)
    overall_status = "FAIL" if release_blocking else (
        "NOISY" if any(suite["status"] == "NOISY" for suite in suites) else "PASS"
    )

    report = output_dir / f"{args.prefix}_report.md"
    lines = [
        "# Release Performance Suite",
        "",
        "Mode: `oracle`",
        f"Benchmark dir: `{benchmark_dir}`",
        f"Baseline prefix: `{args.baseline_prefix}`",
        f"Overall status: {overall_status}",
        f"Release blocking: {'YES' if release_blocking else 'NO'}",
        "",
        "| Suite | Status | Release Blocking | Report | Log | Exit Code | Note |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for suite in suites:
        lines.append(
            f"| {suite['name']} | {suite['status']} | "
            f"{'YES' if suite['release_blocking'] else 'NO'} | "
            f"`{suite['report']}` | `{suite['log']}` | {suite['exit_code']} | {suite['note']} |"
        )
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"report={report}")
    print(f"status={overall_status}")
    print(f"release_blocking={'YES' if release_blocking else 'NO'}")
    return 1 if release_blocking else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix")
    parser.add_argument("--matched-manifest", type=Path)
    parser.add_argument("--baseline-prefix")
    parser.add_argument("--mode", choices=("product", "oracle"), default="product")
    parser.add_argument("--suite", choices=("all", *product_suites().keys()), default="all")
    parser.add_argument("--capture-baseline", action="store_true")
    parser.add_argument(
        "--repetitions",
        type=int,
        default=CALIBRATED_EXTERNAL_REPETITIONS,
    )
    parser.add_argument(
        "--min-time",
        type=float,
        default=CALIBRATED_EXTERNAL_MIN_TIME_SECONDS,
    )
    parser.add_argument("--max-regression-percent", type=float, default=0.0)
    parser.add_argument(
        "--max-external-cv-percent",
        type=float,
        default=CALIBRATED_EXTERNAL_MAX_CV_PERCENT,
    )
    args = parser.parse_args()

    if args.matched_manifest:
        return run_frozen_matched_suite(args.matched_manifest, resolve_path(args.output_dir))
    if not args.build_dir or not args.prefix:
        parser.error("ordinary suite requires --build-dir and --prefix")

    if args.mode == "product":
        args.baseline_prefix = args.baseline_prefix or "product_baseline"
    elif args.baseline_prefix is None:
        print(
            "--baseline-prefix is required in oracle mode; "
            "use --capture-baseline with an explicit prefix first if you need to create one",
            file=sys.stderr,
        )
        return 2

    output_dir = resolve_path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    build_dir = resolve_path(args.build_dir)
    build_errors = release_benchmark_build_errors(build_dir)
    if build_errors:
        print(
            "Refusing to run release performance benchmarks against a PGO instrumentation build.",
            file=sys.stderr,
        )
        for error in build_errors:
            print(f"- {error}", file=sys.stderr)
        print(
            "Use a clean Release build or the /LTCG:PGOPTIMIZE /USEPROFILE output for PGO release evidence.",
            file=sys.stderr,
        )
        return 2

    if args.mode == "oracle" and not args.capture_baseline:
        environment_errors = oracle_environment_errors()
        if environment_errors:
            print(
                "Refusing to run calibrated oracle benchmarks without a complete environment.",
                file=sys.stderr,
            )
            for error in environment_errors:
                print(f"- {error}", file=sys.stderr)
            return 2

    if args.mode == "oracle":
        if args.capture_baseline:
            return run_oracle_baseline_capture(args, output_dir, build_dir)
        return run_oracle_suite(args, output_dir, build_dir)
    return run_product_suite(args, output_dir, build_dir)


if __name__ == "__main__":
    raise SystemExit(main())
