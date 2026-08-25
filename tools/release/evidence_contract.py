#!/usr/bin/env python3
"""Load and validate the authoritative release-evidence contract."""

from __future__ import annotations

import hashlib
import json
import math
import re
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any


DEFAULT_CONTRACT_PATH = Path(__file__).with_name("release-evidence-contract.json")

_ROOT_FIELDS = {
    "schema_version",
    "equivalence",
    "profiles",
    "algorithms",
    "performance",
    "provenance",
}
_EQUIVALENCE_FIELDS = {
    "coordinate_tolerance",
    "normalize_closed_ring_start",
    "normalize_independent_path_order",
    "normalize_winding",
    "normalize_open_path_direction",
}
_PROFILE_FIELDS = {
    "id",
    "eligibility_file",
    "geometry_types",
    "required_case_sets",
    "scenario_quotas",
    "forbidden_scenarios",
}
_ALGORITHM_FIELDS = {
    "id",
    "release_gated",
    "required_tests",
    "required_benchmarks",
    "required_profiles",
}
_PERFORMANCE_FIELDS = {
    "release_repetitions",
    "min_time_seconds",
    "min_warmup_time_seconds",
    "release_max_cv_percent",
    "directional_max_cv_percent",
    "min_pair_speedup",
    "min_geomean_speedup",
    "time_field",
}
_PROVENANCE_FIELDS = {
    "git_commit",
    "git_tree_state",
    "contract_sha256",
    "profile_sha256",
    "benchmark_executable_sha256",
    "compiler",
    "build_flags",
    "runner_id",
}
_GEOMETRY_TYPES = {"Polygon", "MultiPolygon", "LineString", "MultiLineString"}
_CASE_SETS = {"verification", "benchmark"}
_PROFILE_ID_RE = re.compile(r"^[a-z][a-z0-9-]*$")
_ALGORITHM_ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")


@dataclass(frozen=True)
class ScenarioQuota:
    min_count: int = 0
    max_count: int | None = None


@dataclass(frozen=True)
class ProfileContract:
    id: str
    eligibility_file: str
    geometry_types: tuple[str, ...]
    required_case_sets: tuple[str, ...]
    scenario_quotas: Mapping[str, ScenarioQuota]
    forbidden_scenarios: tuple[str, ...]


@dataclass(frozen=True)
class AlgorithmContract:
    id: str
    release_gated: bool
    required_tests: tuple[str, ...]
    required_benchmarks: tuple[str, ...]
    required_profiles: tuple[str, ...]


@dataclass(frozen=True)
class ReleaseEvidenceContract:
    schema_version: int
    equivalence: Mapping[str, bool | int]
    profiles: Mapping[str, ProfileContract]
    algorithms: Mapping[str, AlgorithmContract]
    performance: Mapping[str, int | float | str]
    provenance: tuple[str, ...]
    sha256: str


def release_gated_profile_ids(
    contract: ReleaseEvidenceContract,
) -> frozenset[str]:
    """Return profiles that participate in at least one release-gated family."""

    return frozenset(
        profile_id
        for algorithm in contract.algorithms.values()
        if algorithm.release_gated
        for profile_id in algorithm.required_profiles
    )


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"contract contains non-finite JSON number {value}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"contract contains duplicate JSON key {key!r}")
        result[key] = value
    return result


def _as_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return value


def _exact_fields(value: Mapping[str, Any], expected: set[str], context: str) -> None:
    missing = sorted(expected - set(value))
    unknown = sorted(set(value) - expected)
    if missing:
        raise ValueError(f"{context} is missing fields: {missing}")
    if unknown:
        raise ValueError(f"{context} has unknown fields: {unknown}")


def _nonempty_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{context} must be a non-empty string")
    if value != value.strip():
        raise ValueError(f"{context} must not contain leading or trailing whitespace")
    return value


def _string_tuple(
    value: Any,
    context: str,
    *,
    allow_empty: bool = False,
) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise ValueError(f"{context} must be an array")
    result = tuple(
        _nonempty_string(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    )
    if not allow_empty and not result:
        raise ValueError(f"{context} must not be empty")
    if len(set(result)) != len(result):
        raise ValueError(f"{context} contains duplicate values")
    return result


def _integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{context} must be an integer")
    return value


def _number(value: Any, context: str) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{context} must be a number")
    if not math.isfinite(float(value)):
        raise ValueError(f"{context} must be finite")
    return value


def _boolean(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"{context} must be a boolean")
    return value


def _load_equivalence(value: Any) -> Mapping[str, bool | int]:
    raw = _as_object(value, "equivalence")
    _exact_fields(raw, _EQUIVALENCE_FIELDS, "equivalence")

    coordinate_tolerance = _number(
        raw["coordinate_tolerance"], "equivalence.coordinate_tolerance"
    )
    if coordinate_tolerance != 0:
        raise ValueError("equivalence.coordinate_tolerance must be zero")

    closed_ring_start = _boolean(
        raw["normalize_closed_ring_start"],
        "equivalence.normalize_closed_ring_start",
    )
    if not closed_ring_start:
        raise ValueError("equivalence.normalize_closed_ring_start must be true")

    path_order = _boolean(
        raw["normalize_independent_path_order"],
        "equivalence.normalize_independent_path_order",
    )
    if not path_order:
        raise ValueError("equivalence.normalize_independent_path_order must be true")

    winding = _boolean(raw["normalize_winding"], "equivalence.normalize_winding")
    if winding:
        raise ValueError("equivalence.normalize_winding must be false")

    open_direction = _boolean(
        raw["normalize_open_path_direction"],
        "equivalence.normalize_open_path_direction",
    )
    if open_direction:
        raise ValueError("equivalence.normalize_open_path_direction must be false")

    return MappingProxyType(
        {
            "coordinate_tolerance": coordinate_tolerance,
            "normalize_closed_ring_start": closed_ring_start,
            "normalize_independent_path_order": path_order,
            "normalize_winding": winding,
            "normalize_open_path_direction": open_direction,
        }
    )


def _load_quota(value: Any, context: str) -> ScenarioQuota:
    raw = _as_object(value, context)
    unknown = sorted(set(raw) - {"min_count", "max_count"})
    if unknown:
        raise ValueError(f"{context} has unknown fields: {unknown}")

    min_count = _integer(raw.get("min_count", 0), f"{context}.min_count")
    max_value = raw.get("max_count")
    max_count = (
        None if max_value is None else _integer(max_value, f"{context}.max_count")
    )
    if min_count < 0:
        raise ValueError(f"{context}.min_count must be non-negative")
    if max_count is not None and max_count < 0:
        raise ValueError(f"{context}.max_count must be non-negative")
    if max_count is not None and max_count < min_count:
        raise ValueError(f"{context}.max_count must not be below min_count")
    return ScenarioQuota(min_count=min_count, max_count=max_count)


def _load_profile(value: Any, index: int) -> ProfileContract:
    context = f"profiles[{index}]"
    raw = _as_object(value, context)
    _exact_fields(raw, _PROFILE_FIELDS, context)

    profile_id = _nonempty_string(raw["id"], f"{context}.id")
    if not _PROFILE_ID_RE.fullmatch(profile_id):
        raise ValueError(f"{context}.id is not a canonical profile ID")

    eligibility_file = _nonempty_string(
        raw["eligibility_file"], f"{context}.eligibility_file"
    )
    eligibility_path = Path(eligibility_file)
    if (
        eligibility_path.name != eligibility_file
        or eligibility_path.suffix != ".jsonl"
        or eligibility_path.is_absolute()
    ):
        raise ValueError(
            f"{context}.eligibility_file must be a JSONL basename without directories"
        )

    geometry_types = _string_tuple(
        raw["geometry_types"], f"{context}.geometry_types", allow_empty=True
    )
    unknown_geometry_types = sorted(set(geometry_types) - _GEOMETRY_TYPES)
    if unknown_geometry_types:
        raise ValueError(
            f"{context}.geometry_types contains unsupported values: "
            f"{unknown_geometry_types}"
        )

    required_case_sets = _string_tuple(
        raw["required_case_sets"], f"{context}.required_case_sets"
    )
    if set(required_case_sets) != _CASE_SETS:
        raise ValueError(
            f"{context}.required_case_sets must contain verification and benchmark"
        )

    raw_quotas = _as_object(raw["scenario_quotas"], f"{context}.scenario_quotas")
    quotas: dict[str, ScenarioQuota] = {}
    for scenario, quota in raw_quotas.items():
        scenario_id = _nonempty_string(scenario, f"{context}.scenario_quotas key")
        quotas[scenario_id] = _load_quota(
            quota, f"{context}.scenario_quotas[{scenario_id!r}]"
        )

    forbidden_scenarios = _string_tuple(
        raw["forbidden_scenarios"],
        f"{context}.forbidden_scenarios",
        allow_empty=True,
    )
    for scenario in forbidden_scenarios:
        quota = quotas.get(scenario)
        if quota is None or quota.max_count != 0:
            raise ValueError(
                f"{context}.forbidden_scenarios entry {scenario!r} "
                "must have a scenario quota with max_count 0"
            )

    return ProfileContract(
        id=profile_id,
        eligibility_file=eligibility_file,
        geometry_types=geometry_types,
        required_case_sets=required_case_sets,
        scenario_quotas=MappingProxyType(quotas),
        forbidden_scenarios=forbidden_scenarios,
    )


def _load_algorithm(value: Any, index: int) -> AlgorithmContract:
    context = f"algorithms[{index}]"
    raw = _as_object(value, context)
    _exact_fields(raw, _ALGORITHM_FIELDS, context)

    algorithm_id = _nonempty_string(raw["id"], f"{context}.id")
    if not _ALGORITHM_ID_RE.fullmatch(algorithm_id):
        raise ValueError(f"{context}.id is not a canonical algorithm ID")

    release_gated = _boolean(raw["release_gated"], f"{context}.release_gated")
    required_tests = _string_tuple(raw["required_tests"], f"{context}.required_tests")
    required_benchmarks = _string_tuple(
        raw["required_benchmarks"], f"{context}.required_benchmarks"
    )
    required_profiles = _string_tuple(
        raw["required_profiles"], f"{context}.required_profiles"
    )

    return AlgorithmContract(
        id=algorithm_id,
        release_gated=release_gated,
        required_tests=required_tests,
        required_benchmarks=required_benchmarks,
        required_profiles=required_profiles,
    )


def _load_performance(value: Any) -> Mapping[str, int | float | str]:
    raw = _as_object(value, "performance")
    _exact_fields(raw, _PERFORMANCE_FIELDS, "performance")

    repetitions = _integer(
        raw["release_repetitions"], "performance.release_repetitions"
    )
    if repetitions < 7:
        raise ValueError("performance.release_repetitions must be at least 7")

    min_time = _number(raw["min_time_seconds"], "performance.min_time_seconds")
    if min_time < 0.5:
        raise ValueError("performance.min_time_seconds must be at least 0.5")

    min_warmup_time = _number(
        raw["min_warmup_time_seconds"],
        "performance.min_warmup_time_seconds",
    )
    if min_warmup_time < 1.0:
        raise ValueError(
            "performance.min_warmup_time_seconds must be at least 1.0"
        )

    release_cv = _number(
        raw["release_max_cv_percent"], "performance.release_max_cv_percent"
    )
    if release_cv <= 0 or release_cv > 5.0:
        raise ValueError(
            "performance.release_max_cv_percent must be greater than 0 and at most 5.0"
        )

    directional_cv = _number(
        raw["directional_max_cv_percent"],
        "performance.directional_max_cv_percent",
    )
    if directional_cv < release_cv or directional_cv > 15.0:
        raise ValueError(
            "performance.directional_max_cv_percent must be at least the release "
            "CV and at most 15.0"
        )

    pair_speedup = _number(raw["min_pair_speedup"], "performance.min_pair_speedup")
    if pair_speedup < 1.2:
        raise ValueError("performance.min_pair_speedup must be at least 1.2")

    geomean_speedup = _number(
        raw["min_geomean_speedup"], "performance.min_geomean_speedup"
    )
    if geomean_speedup < 1.2:
        raise ValueError("performance.min_geomean_speedup must be at least 1.2")

    time_field = _nonempty_string(raw["time_field"], "performance.time_field")
    if time_field != "real_time":
        raise ValueError("performance.time_field must be real_time")

    return MappingProxyType(
        {
            "release_repetitions": repetitions,
            "min_time_seconds": min_time,
            "min_warmup_time_seconds": min_warmup_time,
            "release_max_cv_percent": release_cv,
            "directional_max_cv_percent": directional_cv,
            "min_pair_speedup": pair_speedup,
            "min_geomean_speedup": geomean_speedup,
            "time_field": time_field,
        }
    )


def load_contract(
    path: Path = DEFAULT_CONTRACT_PATH,
) -> ReleaseEvidenceContract:
    """Read a release contract and reject incomplete or weakened policy."""

    contract_path = Path(path)
    try:
        raw_bytes = contract_path.read_bytes()
    except OSError as error:
        raise ValueError(
            f"cannot read release evidence contract {contract_path}: {error}"
        ) from error

    try:
        payload = json.loads(
            raw_bytes.decode("utf-8"),
            object_pairs_hook=_unique_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(
            f"invalid release evidence contract JSON in {contract_path}: {error}"
        ) from error

    root = _as_object(payload, "contract")
    _exact_fields(root, _ROOT_FIELDS, "contract")
    schema_version = _integer(root["schema_version"], "schema_version")
    if schema_version != 1:
        raise ValueError(
            f"unsupported release evidence contract schema_version {schema_version}"
        )

    equivalence = _load_equivalence(root["equivalence"])

    raw_profiles = root["profiles"]
    if not isinstance(raw_profiles, list) or not raw_profiles:
        raise ValueError("profiles must be a non-empty array")
    profiles: dict[str, ProfileContract] = {}
    for index, raw_profile in enumerate(raw_profiles):
        profile = _load_profile(raw_profile, index)
        if profile.id in profiles:
            raise ValueError(f"duplicate profile ID {profile.id!r}")
        profiles[profile.id] = profile

    raw_algorithms = root["algorithms"]
    if not isinstance(raw_algorithms, list) or not raw_algorithms:
        raise ValueError("algorithms must be a non-empty array")
    algorithms: dict[str, AlgorithmContract] = {}
    for index, raw_algorithm in enumerate(raw_algorithms):
        algorithm = _load_algorithm(raw_algorithm, index)
        if algorithm.id in algorithms:
            raise ValueError(f"duplicate algorithm ID {algorithm.id!r}")
        unknown_profiles = sorted(set(algorithm.required_profiles) - set(profiles))
        if unknown_profiles:
            raise ValueError(
                f"algorithm {algorithm.id!r} references unknown profile IDs: "
                f"{unknown_profiles}"
            )
        algorithms[algorithm.id] = algorithm

    performance = _load_performance(root["performance"])
    provenance = _string_tuple(root["provenance"], "provenance")
    missing_provenance = sorted(_PROVENANCE_FIELDS - set(provenance))
    if missing_provenance:
        raise ValueError(f"provenance is missing required fields: {missing_provenance}")

    return ReleaseEvidenceContract(
        schema_version=schema_version,
        equivalence=equivalence,
        profiles=MappingProxyType(profiles),
        algorithms=MappingProxyType(algorithms),
        performance=performance,
        provenance=provenance,
        sha256=hashlib.sha256(raw_bytes).hexdigest(),
    )
