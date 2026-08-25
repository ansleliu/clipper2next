#!/usr/bin/env python3

from tools.release.evidence_contract import load_contract


_CONTRACT = load_contract()
_PERFORMANCE = _CONTRACT.performance

RELEASE_EVIDENCE_CONTRACT_SHA256 = _CONTRACT.sha256
CALIBRATED_EXTERNAL_REPETITIONS = int(_PERFORMANCE["release_repetitions"])
CALIBRATED_EXTERNAL_MIN_TIME_SECONDS = float(_PERFORMANCE["min_time_seconds"])
CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS = float(
    _PERFORMANCE["min_warmup_time_seconds"]
)
CALIBRATED_EXTERNAL_MAX_CV_PERCENT = float(_PERFORMANCE["release_max_cv_percent"])
DIRECTIONAL_EXTERNAL_MAX_CV_PERCENT = float(
    _PERFORMANCE["directional_max_cv_percent"]
)
CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP = float(_PERFORMANCE["min_pair_speedup"])
CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP = float(
    _PERFORMANCE["min_geomean_speedup"]
)
EXTERNAL_CORE_SPEEDUP_MODE = "default-unprepared"
_SOURCE = "geometry_corpus"

EXTERNAL_CORE_BENCHMARK_GROUPS = (
    ("generic_pair", (f"BM_external_legacy/{_SOURCE}", f"BM_external_next/{_SOURCE}")),
    ("generic_prepared", (f"BM_external_next_prepared/{_SOURCE}",)),
    ("generic_batch", (f"BM_external_next_batch/{_SOURCE}",)),
    ("generic_prepared_batch", (f"BM_external_next_prepared_batch/{_SOURCE}",)),
    *(
        (
            f"overlay_{operation}_pair",
            (
                f"BM_external_overlay_{operation}_legacy/{_SOURCE}",
                f"BM_external_overlay_{operation}_next/{_SOURCE}",
            ),
        )
        for operation in ("intersection", "union", "difference", "xor")
    ),
    *(
        (
            f"{profile}_pair",
            (
                f"BM_external_{profile}_legacy/{_SOURCE}",
                f"BM_external_{profile}_next_unprepared/{_SOURCE}",
            ),
        )
        for profile in (
            "rectclip",
            "open_line_clip",
            "open_path_overlay",
            "offset",
            "triangulation",
            "minkowski",
            "polytree",
            "clip_tree",
            "batch_scalar",
        )
    ),
    ("batch_next_batch", (f"BM_external_batch_next_batch/{_SOURCE}",)),
)

EXTERNAL_CORE_BENCHMARK_NAMES = tuple(
    benchmark
    for _, benchmarks in EXTERNAL_CORE_BENCHMARK_GROUPS
    for benchmark in benchmarks
)
EXTERNAL_CORE_SPEEDUP_PAIRS = tuple(
    benchmarks
    for group_name, benchmarks in EXTERNAL_CORE_BENCHMARK_GROUPS
    if group_name.endswith("_pair")
)
EXTERNAL_CORE_BENCHMARK_FILTER = (
    "^(" + "|".join(EXTERNAL_CORE_BENCHMARK_NAMES) + ")$"
)
