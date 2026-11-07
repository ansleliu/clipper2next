"""Fixed public supplement inputs and the existing independent mesh authority."""
import hashlib
import importlib.util
import json
from pathlib import Path


def oracle():
    path = Path(__file__).resolve().parents[3] / "tests/triangulation/triangulation_contract_oracle.py"
    spec = importlib.util.spec_from_file_location("triangulation_contract_oracle", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_fixtures(payload):
    cases = payload["cases"]
    if len(cases) != 10 or len({c["name"] for c in cases}) != 10:
        raise ValueError("supplement requires ten unique fixed inputs")
    authority = oracle()
    for case in cases:
        paths = [[tuple(p) for p in r] for r in case["paths"]]
        if (sum(map(len, paths)) != case["point_count"] or len(paths) != case["ring_count"] or
                any(len(set(r)) != len(r) for r in paths) or
                any(authority.cross(r[i-1], r[i], r[(i+1) % len(r)]) == 0
                    for r in paths for i in range(len(r))) or
                sum(authority.area2(r) for r in paths) != int(case["expected_area2"])):
            raise ValueError("fixed cleaned input counts/area do not match")
        content = {k: v for k, v in case.items() if k not in {"name", "input_sha256"}}
        digest = hashlib.sha256(json.dumps(content, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        if digest != case["input_sha256"]:
            raise ValueError("fixture input digest changed")
    return cases


def supplement_rows(cases):
    return [c["name"] + "/" + mode + "/" + state
            for c in cases for mode in ("sweep", "cdt") for state in ("cold", "default")]


def fixture_text(cases):
    lines = [str(len(cases))]
    for case in cases:
        lines.append(f"{case['name']} {len(case['paths'])}")
        for ring in case["paths"]:
            lines.append(str(len(ring)))
            lines.extend(f"{x} {y}" for x, y in ring)
    return "\n".join(lines) + "\n"


def write_fixture_text(fixture, destination):
    cases = validate_fixtures(json.loads(Path(fixture).read_text(encoding="utf-8-sig")))
    Path(destination).write_text(fixture_text(cases), encoding="utf-8")
    return cases


def validate_public_outputs(old_log, candidate_log, fixture, combined_log):
    authority = oracle()
    cases = validate_fixtures(json.loads(Path(fixture).read_text(encoding="utf-8-sig")))
    merged = []
    for path, prefix in ((old_log, "TRI_LEGACY"), (candidate_log, "TRI_CONTRACT")):
        records = [authority.strict_json(line.removeprefix("TRI_PUBLIC "))
                   for line in Path(path).read_text(encoding="utf-8-sig").splitlines()
                   if line.startswith("TRI_PUBLIC ")]
        if len(records) != 20 or {r["case_index"] for r in records} != set(range(20)):
            raise ValueError("incomplete/duplicate public output capture")
        for record in records:
            index = record["case_index"]
            if record["input"] != cases[index // 2]["paths"] or record["use_delaunay"] != bool(index % 2):
                raise ValueError("executed public input/options differ from fixed fixture")
            merged.append(prefix + " " + json.dumps(record, separators=(",", ":")))
    Path(combined_log).write_text("\n".join(merged) + "\n", encoding="utf-8")
    verdict = authority.verify(combined_log, 20)
    for row in verdict["results"]:
        case = cases[row["case_index"] // 2]
        if row["area2"] != int(case["expected_area2"]) or row["triangles"] != case["expected_triangle_count"]:
            raise ValueError("actual mesh does not match independent area/Euler count")
        row["fixture"] = case["name"]
    return verdict
