#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.evidence_contract import (
    DEFAULT_CONTRACT_PATH,
    ProfileContract,
    ReleaseEvidenceContract,
    load_contract,
    release_gated_profile_ids,
)


DEFAULT_MIN_CTEST_CASES = 64
DEFAULT_MAX_CTEST_CASES = 1000
LOW_SOURCE_COVERAGE_PERCENT = 20.0


@dataclass
class SourceCoverage:
    source: str
    raw_features: int = 0
    wkt_cases: int = 0
    inventory_sampling_percent: float = 0.0


@dataclass
class ProfileCoverage:
    profile_id: str
    eligibility_file: str
    eligible_unique_ids: int = 0
    verification_selected_unique_ids: int = 0
    benchmark_selected_unique_ids: int = 0
    verification_coverage_percent: float = 0.0
    benchmark_coverage_percent: float = 0.0


@dataclass
class CoverageReport:
    total_raw_features: int
    total_wkt_cases: int
    geos_xml_files: int
    geos_xml_executed_sample: int
    sources: dict[str, SourceCoverage] = field(default_factory=dict)
    default_gate_recommendation: str = "keep-bounded"
    extended_gate_recommendation: str = "required"
    expand_default_candidates: list[str] = field(default_factory=list)
    expand_extended_candidates: list[str] = field(default_factory=list)
    missing_dimensions: list[str] = field(default_factory=list)
    operation_counts: dict[str, int] = field(default_factory=dict)
    wkt_tags: dict[str, int] = field(default_factory=dict)
    total_verification_cases: int = 0
    total_benchmark_cases: int = 0
    total_full_records: int = 0
    total_full_overlay_candidates: int = 0
    total_quarantine_records: int = 0
    fetch_failed_sources: int = 0
    unique_verification_coverage_percent: float = 0.0
    unique_benchmark_coverage_percent: float = 0.0
    coverage_admissible: bool = False
    profile_coverage: dict[str, ProfileCoverage] = field(default_factory=dict)
    required_profile_counts: dict[str, dict[str, int]] = field(default_factory=dict)
    missing_required_profiles: list[str] = field(default_factory=list)
    contract_errors: list[str] = field(default_factory=list)


@dataclass
class WktInventory:
    rows: int = 0
    operation_counts: dict[str, int] = field(default_factory=dict)
    tags: dict[str, int] = field(default_factory=dict)


def read_manifest(path: Path) -> list[dict[str, str]]:
    if not path.exists() and path.name == "manifest.tsv":
        sources_manifest = path.parent / "manifests" / "sources.csv"
        if sources_manifest.exists():
            return read_sources_manifest(sources_manifest)
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def read_sources_manifest(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        rows = []
        for row in csv.DictReader(handle):
            rows.append(
                {
                    "source": row["source_id"],
                    "kind": row["kind"],
                    "url": row["url"],
                    "local_path": row["local_path"],
                    "sha256": row["sha256"],
                    "raw_features": row.get("raw_features", "0") or "0",
                    "wkt_cases": row.get("wkt_cases", "0") or "0",
                }
            )
        return rows


def source_coverage(rows: list[dict[str, str]]) -> dict[str, SourceCoverage]:
    grouped: dict[str, SourceCoverage] = {}
    for row in rows:
        source = row["source"]
        if source in {"geos_xml", "geos-xml"}:
            continue
        coverage = grouped.setdefault(source, SourceCoverage(source=source))
        coverage.raw_features += int(row["raw_features"])
        coverage.wkt_cases += int(row["wkt_cases"])
    for coverage in grouped.values():
        if coverage.raw_features > 0:
            coverage.inventory_sampling_percent = min(
                100.0,
                round((coverage.wkt_cases / coverage.raw_features) * 100.0, 2),
            )
    return grouped


def count_geos_xml_files(
    external_root: Path, manifest_rows: list[dict[str, str]]
) -> int:
    for row in manifest_rows:
        if row["source"] in {"geos_xml", "geos-xml"}:
            path = Path(row["local_path"])
            if not path.is_absolute():
                path = external_root / path
            if path.exists():
                return sum(1 for xml_path in path.rglob("*.xml") if xml_path.is_file())
            return int(row["raw_features"])
    geos_dir = external_root / "geos_xml"
    if geos_dir.exists():
        return sum(1 for xml_path in geos_dir.rglob("*.xml") if xml_path.is_file())
    return 0


def read_wkt_inventory(external_root: Path, wkt_dir_name: str = "wkt") -> WktInventory:
    inventory = WktInventory()
    for path in sorted((external_root / wkt_dir_name).rglob("*.tsv")):
        with path.open("r", encoding="utf-8") as handle:
            for line in handle:
                if not line.strip() or line.startswith("#"):
                    continue
                fields = line.rstrip("\n").split("\t")
                inventory.rows += 1
                if len(fields) > 1:
                    inventory.operation_counts[fields[1]] = (
                        inventory.operation_counts.get(fields[1], 0) + 1
                    )
                if fields:
                    name = fields[0]
                    for tag in (
                        "non_rectangle",
                        "non_rectangular",
                        "partial_overlap",
                        "translated_overlap",
                        "hole_heavy",
                    ):
                        if tag in name:
                            inventory.tags[tag] = inventory.tags.get(tag, 0) + 1
    merge_inventory(inventory, read_generated_wkt_inventory(external_root))
    return inventory


def parse_case_blocks(path: Path) -> list[dict[str, str]]:
    cases: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    for raw_line in path.read_text(encoding="ascii", errors="ignore").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line == "[case]":
            if current is not None:
                cases.append(current)
            current = {}
            continue
        if current is None or "=" not in line:
            continue
        key, value = line.split("=", 1)
        current[key.strip()] = value.strip()
    if current is not None:
        cases.append(current)
    return cases


def count_tag(inventory: WktInventory, tag: str) -> None:
    if tag:
        inventory.tags[tag] = inventory.tags.get(tag, 0) + 1


def add_case_to_inventory(inventory: WktInventory, case: dict[str, str]) -> None:
    inventory.rows += 1
    operation = case.get("operation", "")
    if operation:
        if operation == "symmetric_difference":
            operation = "xor"
        inventory.operation_counts[operation] = (
            inventory.operation_counts.get(operation, 0) + 1
        )
    for tag in case.get("tags", "").split(","):
        count_tag(inventory, tag.strip())
    for tag in (
        "non_rectangle",
        "non_rectangular",
        "partial_overlap",
        "translated_overlap",
        "hole_heavy",
    ):
        if tag in case.get("id", ""):
            count_tag(inventory, tag)


def read_generated_wkt_inventory(external_root: Path) -> WktInventory:
    inventory = WktInventory()
    normalized_dir = external_root / "normalized"
    if not normalized_dir.exists():
        return inventory
    for path in sorted(normalized_dir.rglob("*.wktcase")):
        for case in parse_case_blocks(path):
            add_case_to_inventory(inventory, case)
    for path in sorted(normalized_dir.rglob("*.wktshape")):
        for case in parse_case_blocks(path):
            add_case_to_inventory(inventory, case)
    return inventory


def merge_inventory(target: WktInventory, source: WktInventory) -> None:
    target.rows += source.rows
    for operation, count in source.operation_counts.items():
        target.operation_counts[operation] = (
            target.operation_counts.get(operation, 0) + count
        )
    for tag, count in source.tags.items():
        target.tags[tag] = target.tags.get(tag, 0) + count


def count_jsonl_records(path: Path) -> int:
    with path.open("r", encoding="utf-8") as handle:
        return sum(1 for line in handle if line.strip())


def count_jsonl_records_if_exists(path: Path) -> int:
    if not path.exists() or not path.is_file():
        return 0
    return count_jsonl_records(path)


def count_jsonl_dir(path: Path, exclude_names: set[str] | None = None) -> int:
    if not path.exists():
        return 0
    exclude_names = exclude_names or set()
    return sum(
        count_jsonl_records(file_path)
        for file_path in sorted(path.glob("*.jsonl"))
        if file_path.is_file() and file_path.name not in exclude_names
    )


def count_normalized_jsonl_cases(external_root: Path, case_set: str) -> int:
    case_dir = external_root / "normalized" / case_set
    if not case_dir.exists():
        return 0
    return sum(
        count_jsonl_records(path)
        for path in sorted(case_dir.glob("*.jsonl"))
        if path.is_file()
    )


def read_jsonl_objects(path: Path) -> list[dict[str, object]]:
    """Read non-empty JSONL rows and reject malformed or non-object records."""

    if not path.is_file():
        raise ValueError(f"missing JSONL file: {path}")

    records: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid JSON: {error}"
                ) from error
            if not isinstance(value, dict):
                raise ValueError(
                    f"{path}:{line_number}: JSONL record must be an object"
                )
            records.append(value)
    return records


def _required_record_id(record: dict[str, object], path: Path, index: int) -> str:
    value = record.get("id")
    if not isinstance(value, str) or not value:
        raise ValueError(f"{path}:{index}: record id must be a non-empty string")
    return value


def eligible_base_ids(root: Path, profile: ProfileContract) -> set[str]:
    """Return globally unique full-record IDs eligible for one profile."""

    path = root / "normalized" / "full" / profile.eligibility_file
    records = read_jsonl_objects(path)
    eligible: set[str] = set()
    seen: set[str] = set()

    for index, record in enumerate(records, 1):
        base_id = _required_record_id(record, path, index)
        if base_id in seen:
            raise ValueError(f"{path}:{index}: duplicate eligible full ID {base_id!r}")
        seen.add(base_id)

        if profile.eligibility_file == "shape-inputs.jsonl" and profile.geometry_types:
            geometry = record.get("geometry")
            if not isinstance(geometry, dict):
                raise ValueError(f"{path}:{index}: shape record is missing geometry")
            geometry_type = geometry.get("geometry_type")
            if not isinstance(geometry_type, str) or not geometry_type:
                raise ValueError(
                    f"{path}:{index}: shape record is missing geometry.geometry_type"
                )
            if geometry_type not in profile.geometry_types:
                continue

        eligible.add(base_id)

    return eligible


def selected_base_ids(root: Path, case_set: str, profile_id: str) -> set[str]:
    """Return unique selector.full_id values for an exact profile file."""

    path = root / "normalized" / case_set / f"{profile_id}.jsonl"
    records = read_jsonl_objects(path)
    selected: set[str] = set()
    record_ids: set[str] = set()

    for index, record in enumerate(records, 1):
        record_id = _required_record_id(record, path, index)
        if record_id in record_ids:
            raise ValueError(f"{path}:{index}: duplicate record id {record_id!r}")
        record_ids.add(record_id)

        selector = record.get("selector")
        if not isinstance(selector, dict):
            raise ValueError(f"{path}:{index}: selector must be an object")
        base_id = selector.get("full_id")
        if not isinstance(base_id, str) or not base_id:
            raise ValueError(
                f"{path}:{index}: selector.full_id must be a non-empty string"
            )
        selected.add(base_id)

    return selected


def unique_coverage_percent(selected: set[str], eligible: set[str]) -> float:
    """Calculate bounded coverage from literal unique base-ID sets."""

    if not eligible:
        if selected:
            raise ValueError("selected IDs exist without an eligible denominator")
        return 0.0
    if not selected.issubset(eligible):
        unknown = sorted(selected - eligible)
        raise ValueError(f"unknown selector full_id values: {unknown[:5]}")
    return round((len(selected) / len(eligible)) * 100.0, 2)


def required_profile_counts(
    external_root: Path,
    contract: ReleaseEvidenceContract,
) -> dict[str, dict[str, int]]:
    required_ids = release_gated_profile_ids(contract)
    return {
        profile.id: {
            case_set: count_jsonl_records_if_exists(
                external_root / "normalized" / case_set / f"{profile.id}.jsonl"
            )
            for case_set in profile.required_case_sets
        }
        for profile in contract.profiles.values()
        if profile.id in required_ids
    }


def missing_required_profiles(
    profile_counts: dict[str, dict[str, int]],
    contract: ReleaseEvidenceContract,
) -> list[str]:
    missing: list[str] = []
    required_ids = release_gated_profile_ids(contract)
    for profile in contract.profiles.values():
        if profile.id not in required_ids:
            continue
        counts = profile_counts[profile.id]
        for case_set in profile.required_case_sets:
            if counts.get(case_set, 0) <= 0:
                missing.append(f"{case_set}:{profile.id}")
    return sorted(missing)


def _safe_unique_coverage_percent(
    selected: set[str],
    eligible: set[str],
    context: str,
    errors: list[str],
) -> float:
    try:
        return unique_coverage_percent(selected, eligible)
    except ValueError as error:
        errors.append(f"{context}: {error}")
        if not eligible:
            return 0.0
        return round((len(selected & eligible) / len(eligible)) * 100.0, 2)


def manifest_mentions(rows: list[dict[str, str]], text: str) -> bool:
    needle = text.lower()
    for row in rows:
        if any(needle in value.lower() for value in row.values()):
            return True
    return False


def build_missing_dimensions(
    sources: dict[str, SourceCoverage],
    rows: list[dict[str, str]],
    inventory: WktInventory,
) -> list[str]:
    missing: list[str] = []
    if "geofabrik_osm" in sources and sources["geofabrik_osm"].raw_features < 1000:
        missing.append("larger_osm_geography")
    operations = set(inventory.operation_counts)
    if not {"union", "difference", "xor"}.issubset(operations):
        missing.append("operation_mix")
    if (
        inventory.tags.get("non_rectangle", 0) == 0
        and inventory.tags.get("non_rectangular", 0) == 0
    ):
        missing.append("non_rectangle_clip_shapes")
    if not (manifest_mentions(rows, "10m") and manifest_mentions(rows, "50m")):
        missing.append("natural_earth_10m_50m_scales")
    if (
        "tiger" in sources
        and sources["tiger"].wkt_cases < sources["tiger"].raw_features
    ):
        missing.append("all_tiger_counties_or_stratified_sample")
    if inventory.tags.get("hole_heavy", 0) == 0:
        missing.append("shapefile_multipolygon_hole_diversity")
    return missing


def choose_default_candidates(
    sources: dict[str, SourceCoverage], total_wkt_cases: int
) -> list[str]:
    if total_wkt_cases >= DEFAULT_MAX_CTEST_CASES:
        return []
    candidates: list[str] = []
    for source, coverage in sorted(sources.items()):
        if source == "tiger":
            continue
        if coverage.raw_features == 0:
            continue
        if (
            coverage.inventory_sampling_percent < LOW_SOURCE_COVERAGE_PERCENT
            and coverage.wkt_cases < DEFAULT_MIN_CTEST_CASES
        ):
            candidates.append(source)
    return candidates


def choose_extended_candidates(sources: dict[str, SourceCoverage]) -> list[str]:
    candidates: list[str] = []
    for source, coverage in sorted(sources.items()):
        if coverage.raw_features == 0:
            continue
        if coverage.inventory_sampling_percent < 100.0:
            candidates.append(source)
    return candidates


def audit_profile_selections(
    external_root: Path,
    contract: ReleaseEvidenceContract,
) -> tuple[
    dict[str, ProfileCoverage],
    float,
    float,
    list[str],
]:
    profile_coverage: dict[str, ProfileCoverage] = {}
    contract_errors: list[str] = []
    all_eligible: set[str] = set()
    all_valid_verification: set[str] = set()
    all_valid_benchmark: set[str] = set()
    eligibility_cache: dict[
        tuple[str, tuple[str, ...]],
        tuple[frozenset[str], str | None],
    ] = {}
    required_ids = release_gated_profile_ids(contract)

    for profile in contract.profiles.values():
        if profile.id not in required_ids:
            continue
        eligibility_key = (profile.eligibility_file, profile.geometry_types)
        if eligibility_key not in eligibility_cache:
            try:
                loaded_eligible = eligible_base_ids(external_root, profile)
                eligibility_cache[eligibility_key] = (
                    frozenset(loaded_eligible),
                    None,
                )
            except ValueError as error:
                eligibility_cache[eligibility_key] = (frozenset(), str(error))

        cached_eligible, eligibility_error = eligibility_cache[eligibility_key]
        eligible = set(cached_eligible)
        if eligibility_error is not None:
            contract_errors.append(f"profile {profile.id}: {eligibility_error}")
        else:
            if not eligible:
                contract_errors.append(
                    f"profile {profile.id}: eligible denominator is empty"
                )

        selected_by_case_set: dict[str, set[str]] = {}
        coverage_by_case_set: dict[str, float] = {}
        for case_set in profile.required_case_sets:
            try:
                selected = selected_base_ids(
                    external_root,
                    case_set,
                    profile.id,
                )
            except ValueError as error:
                contract_errors.append(f"profile {profile.id} {case_set}: {error}")
                selected = set()
            selected_by_case_set[case_set] = selected
            coverage_by_case_set[case_set] = _safe_unique_coverage_percent(
                selected,
                eligible,
                f"profile {profile.id} {case_set}",
                contract_errors,
            )

        verification = selected_by_case_set.get("verification", set())
        benchmark = selected_by_case_set.get("benchmark", set())
        profile_coverage[profile.id] = ProfileCoverage(
            profile_id=profile.id,
            eligibility_file=profile.eligibility_file,
            eligible_unique_ids=len(eligible),
            verification_selected_unique_ids=len(verification),
            benchmark_selected_unique_ids=len(benchmark),
            verification_coverage_percent=coverage_by_case_set.get("verification", 0.0),
            benchmark_coverage_percent=coverage_by_case_set.get("benchmark", 0.0),
        )

        all_eligible.update(eligible)
        all_valid_verification.update(verification & eligible)
        all_valid_benchmark.update(benchmark & eligible)

    unique_verification_coverage = _safe_unique_coverage_percent(
        all_valid_verification,
        all_eligible,
        "overall verification",
        contract_errors,
    )
    unique_benchmark_coverage = _safe_unique_coverage_percent(
        all_valid_benchmark,
        all_eligible,
        "overall benchmark",
        contract_errors,
    )
    return (
        profile_coverage,
        unique_verification_coverage,
        unique_benchmark_coverage,
        contract_errors,
    )


def audit_coverage(
    external_root: Path,
    geos_xml_executed_sample: int = 32,
    manifest_name: str = "manifest.tsv",
    wkt_dir_name: str = "wkt",
    contract: ReleaseEvidenceContract | None = None,
) -> CoverageReport:
    if contract is None:
        contract = load_contract()

    rows = read_manifest(external_root / manifest_name)
    sources = source_coverage(rows)
    total_raw_features = sum(source.raw_features for source in sources.values())
    inventory = read_wkt_inventory(external_root, wkt_dir_name=wkt_dir_name)
    total_wkt_cases = inventory.rows
    if total_wkt_cases == 0:
        total_wkt_cases = sum(source.wkt_cases for source in sources.values())
    total_verification_cases = count_normalized_jsonl_cases(
        external_root, "verification"
    )
    total_benchmark_cases = count_normalized_jsonl_cases(external_root, "benchmark")
    total_full_records = count_normalized_jsonl_cases(external_root, "full")
    total_full_overlay_candidates = count_jsonl_records_if_exists(
        external_root / "normalized" / "full" / "overlay-candidates.jsonl"
    )
    total_quarantine_records = count_jsonl_dir(
        external_root / "quarantine",
        exclude_names={"fetch-failed.jsonl"},
    )
    fetch_failed_sources = count_jsonl_records_if_exists(
        external_root / "quarantine" / "fetch-failed.jsonl"
    )
    required_counts = required_profile_counts(external_root, contract)
    profile_coverage, verification_coverage, benchmark_coverage, contract_errors = (
        audit_profile_selections(external_root, contract)
    )
    missing_profiles = missing_required_profiles(required_counts, contract)
    geos_xml_files = count_geos_xml_files(external_root, rows)
    default_candidates = choose_default_candidates(sources, total_wkt_cases)
    extended_candidates = choose_extended_candidates(sources)
    return CoverageReport(
        total_raw_features=total_raw_features,
        total_wkt_cases=total_wkt_cases,
        geos_xml_files=geos_xml_files,
        geos_xml_executed_sample=geos_xml_executed_sample,
        sources=sources,
        default_gate_recommendation="keep-bounded",
        extended_gate_recommendation="required",
        expand_default_candidates=default_candidates,
        expand_extended_candidates=extended_candidates,
        missing_dimensions=build_missing_dimensions(sources, rows, inventory),
        operation_counts=inventory.operation_counts,
        wkt_tags=inventory.tags,
        total_verification_cases=total_verification_cases,
        total_benchmark_cases=total_benchmark_cases,
        total_full_records=total_full_records,
        total_full_overlay_candidates=total_full_overlay_candidates,
        total_quarantine_records=total_quarantine_records,
        fetch_failed_sources=fetch_failed_sources,
        unique_verification_coverage_percent=verification_coverage,
        unique_benchmark_coverage_percent=benchmark_coverage,
        coverage_admissible=not contract_errors and not missing_profiles,
        profile_coverage=profile_coverage,
        required_profile_counts=required_counts,
        missing_required_profiles=missing_profiles,
        contract_errors=contract_errors,
    )


def report_as_dict(report: CoverageReport) -> dict[str, object]:
    data = asdict(report)
    data["sources"] = {
        name: asdict(source) for name, source in sorted(report.sources.items())
    }
    data["profile_coverage"] = {
        name: asdict(coverage)
        for name, coverage in sorted(report.profile_coverage.items())
    }
    return data


def format_markdown(report: CoverageReport) -> str:
    lines = [
        "# External Corpus Coverage Audit",
        "",
        f"Total generated WKT cases: **{report.total_wkt_cases}**",
        f"Total source raw features: **{report.total_raw_features}**",
        f"GEOS XML files local: **{report.geos_xml_files}**",
        f"GEOS XML bounded execution sample: **{report.geos_xml_executed_sample}**",
        f"Unique verification base-ID coverage: **{report.unique_verification_coverage_percent:.2f}%**",
        f"Unique benchmark base-ID coverage: **{report.unique_benchmark_coverage_percent:.2f}%**",
        "Release coverage admission: "
        f"**{'ADMISSIBLE' if report.coverage_admissible else 'BLOCKED'}**",
        f"Contract errors: **{len(report.contract_errors)}**",
        f"Missing required profile files: **{len(report.missing_required_profiles)}**",
    ]
    if report.total_verification_cases or report.total_benchmark_cases:
        lines.extend(
            [
                f"Informational full normalized rows: **{report.total_full_records}**",
                "Informational full overlay-candidate rows: "
                f"**{report.total_full_overlay_candidates}**",
                "Informational normalized verification rows: "
                f"**{report.total_verification_cases}**",
                "Informational normalized benchmark rows: "
                f"**{report.total_benchmark_cases}**",
                f"Quarantine records: **{report.total_quarantine_records}**",
                f"Fetch-failed sources: **{report.fetch_failed_sources}**",
            ]
        )
    lines.extend(
        [
            "",
            "| Source | Raw features | WKT cases | Informational sampling (capped) |",
            "| --- | ---: | ---: | ---: |",
        ]
    )
    for source, coverage in sorted(report.sources.items()):
        lines.append(
            f"| {source} | {coverage.raw_features} | {coverage.wkt_cases} | "
            f"{coverage.inventory_sampling_percent:.2f}% |"
        )

    lines.extend(
        [
            "",
            "## Recommendation",
            "",
            f"- Default CTest external corpus: **{report.default_gate_recommendation.replace('-', ' ')}**.",
            "- Do not enlarge the default gate until runtime is budgeted; current local full oracle is already dominated by external corpus execution.",
            f"- Extended/nightly external corpus: **{report.extended_gate_recommendation}**.",
            "- Add an extended preset that runs a larger stratified WKT corpus and a larger GEOS XML execution sample.",
            "",
            "## Default Gate Expansion Candidates",
            "",
        ]
    )
    if report.expand_default_candidates:
        for source in report.expand_default_candidates:
            lines.append(f"- {source}")
    else:
        lines.append("- None. Keep the default gate bounded.")

    lines.extend(["", "## Extended Gate Candidates", ""])
    if report.expand_extended_candidates:
        for source in report.expand_extended_candidates:
            lines.append(f"- {source}")
    else:
        lines.append(
            "- None from this manifest; use calibrated/nightly runtime budget to raise sample caps."
        )

    lines.extend(["", "## Informational Inventory Gaps", ""])
    if report.missing_dimensions:
        for dimension in report.missing_dimensions:
            lines.append(f"- {dimension}")
    else:
        lines.append("- None detected from the current manifest and WKT inventory.")

    lines.extend(["", "## Contract Profile Coverage", ""])
    lines.append(
        "| Profile | Eligible unique IDs | Verification unique IDs | "
        "Verification coverage | Benchmark unique IDs | Benchmark coverage | Status |"
    )
    lines.append("| --- | ---: | ---: | ---: | ---: | ---: | --- |")
    for profile, coverage in sorted(report.profile_coverage.items()):
        counts = report.required_profile_counts.get(profile, {})
        present = all(
            counts.get(case_set, 0) > 0 for case_set in ("verification", "benchmark")
        )
        status = "PRESENT" if present else "GAP"
        lines.append(
            f"| {profile} | {coverage.eligible_unique_ids} | "
            f"{coverage.verification_selected_unique_ids} | "
            f"{coverage.verification_coverage_percent:.2f}% | "
            f"{coverage.benchmark_selected_unique_ids} | "
            f"{coverage.benchmark_coverage_percent:.2f}% | {status} |"
        )
    if report.missing_required_profiles:
        lines.extend(["", "Missing required profile entries:"])
        lines.extend(f"- {profile}" for profile in report.missing_required_profiles)
    if report.contract_errors:
        lines.extend(["", "## Contract Errors", ""])
        lines.extend(f"- {error}" for error in report.contract_errors)
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Audit local external corpus coverage and expansion need."
    )
    parser.add_argument("external_root", type=Path)
    parser.add_argument("--geos-xml-executed-sample", type=int, default=32)
    parser.add_argument("--manifest-name", default="manifest.tsv")
    parser.add_argument("--wkt-dir-name", default="wkt")
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT_PATH)
    parser.add_argument("--output-md", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--fail-on-contract-gaps", action="store_true")
    args = parser.parse_args()

    try:
        contract = load_contract(args.contract)
    except ValueError as error:
        print(f"contract_error={error}")
        return 2

    report = audit_coverage(
        args.external_root,
        geos_xml_executed_sample=args.geos_xml_executed_sample,
        manifest_name=args.manifest_name,
        wkt_dir_name=args.wkt_dir_name,
        contract=contract,
    )
    if args.output_json is not None:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(
            json.dumps(report_as_dict(report), indent=2),
            encoding="utf-8",
        )
    markdown = format_markdown(report)
    if args.output_md is not None:
        args.output_md.parent.mkdir(parents=True, exist_ok=True)
        args.output_md.write_text(markdown, encoding="utf-8")
    else:
        print(markdown, end="")
    if args.fail_on_contract_gaps and not report.coverage_admissible:
        print(
            "contract_gaps="
            f"{len(report.missing_required_profiles) + len(report.contract_errors)}"
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
