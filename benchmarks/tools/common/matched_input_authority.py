"""Producing-role authority for the fixed matched public benchmark protocol."""

import json
import os
from pathlib import Path
import re
import struct
import subprocess

from benchmarks.tools.common.evidence_identity import (
    sha256_file,
    validate_process_observation,
)
from benchmarks.tools.common.public_triangulation import oracle, supplement_rows
from tools.release.correction_run import validate_producer
from tools.release.run_correction_verification import compiled_header_inventory

BASELINE_HEAD = "7667354f20374f02386e812deee299a697e8941e"
BASELINE_MANIFEST_SHA = {
    "Windows": "c50a643c18ea4331dc912fd4f1409bb78ca0043cd03ac15fa5c1a0df6aa77e4e",
    "Linux": "4a724aa90397256686168aaa83a0d827aeeb6aab12298babc45de0bb7cb7e347",
}
LINUX_WITNESS_SHA = "419ebeb4b7fed6ac43fd102375eb5a8bc3fa65dca13de824a79134223a79a858"
HISTORICAL_LINUX_MANIFEST_SHA = "60978aa1ae09f3e5ad0011e22fd05f37b519ddd40ec124275b42543ac6678c6b"
HISTORICAL_LINUX_WITNESS_SHA = "984ddae0ceb364353d13e62ddd8b936915930fef612eb5a8495ba2529d2e0a2d"
RECOVERED_LOST_TEST_NAMES = frozenset({"clipper2next_tests", "clipper2next_oracle_tests"})
PRODUCT_PRODUCER_FREEZE_SHA = "3162d4b88323e7534bf6ba03ffbfaa778c4da3e89fade9b15b8c850065d9e42a"


def checked_file(record):
    path = Path(record["path"]).resolve()
    if sha256_file(path).removeprefix("sha256:") != record["sha256"].removeprefix(
        "sha256:"
    ):
        raise ValueError("producing proof file changed: " + str(path))
    return path


def git_bytes(root, head, relative):
    return subprocess.check_output(
        ["git", "-C", str(root), "show", f"{head}:{relative}"]
    )


def source_matches(root, record, head, relative):
    path = checked_file(record)
    # Public source/header text can have platform checkout line endings.
    if path.read_bytes().replace(b"\r\n", b"\n") != git_bytes(root, head, relative):
        raise ValueError(
            "public build source/header differs from producing Git authority"
        )


def validate_original_inventory(manifest):
    platform = "Windows" if os.name == "nt" else "Linux"
    authority = manifest["baseline_authority"]
    path = checked_file(authority["manifest"])
    if (
        authority["manifest"]["sha256"].removeprefix("sha256:")
        != BASELINE_MANIFEST_SHA[platform]
    ):
        raise ValueError("original platform baseline authority differs")
    payload = json.loads(path.read_text(encoding="utf-8"))
    entries = payload["artifacts"]
    expected = [
        {"path": str(Path(r["file"]).resolve()), "sha256": "sha256:" + r["sha256"]}
        for r in entries
    ]
    lost = {}
    if platform == "Linux":
        if (
            payload.get("schema_version") != 2
            or payload.get("kind") != "authorized-original766-recovered-authority"
            or payload.get("baseline_head") != BASELINE_HEAD
            or payload["historical_manifest"]["sha256"] != HISTORICAL_LINUX_MANIFEST_SHA
            or payload["historical_runtime_witness"]["sha256"] != HISTORICAL_LINUX_WITNESS_SHA
            or payload["runtime_witness"] != authority["runtime_witness"]
            or payload.get("original_eleven_archive_complete") is not False
        ):
            raise ValueError("Native recovery authority differs from the authorized contract")
        checked_file(payload["authorization"])
        declared = payload["lost_original_tests"]
        if len(declared) != 2 or {Path(r["path"]).name for r in declared} != RECOVERED_LOST_TEST_NAMES:
            raise ValueError("Native recovery loss declaration must name exactly two old tests")
        lost = {str(Path(r["path"]).resolve()): r["sha256"] for r in declared}
        for entry in entries:
            key = str(Path(entry["file"]).resolve())
            unavailable = key in lost
            if (
                entry["availability"] != ("lost-user-deleted" if unavailable else "present-byte-exact")
                or entry["consumed_in_performance"] != (not unavailable)
                or (unavailable and entry["sha256"] != lost[key])
            ):
                raise ValueError("Native original member availability/provenance differs")
        witness_path = checked_file(authority["runtime_witness"])
        if (
            authority["runtime_witness"]["sha256"].removeprefix("sha256:")
            != LINUX_WITNESS_SHA
        ):
            raise ValueError("recovered original SO witness differs")
        witness = json.loads(witness_path.read_text())
        runtime = witness["existing_runtime_before"]
        if (
            witness.get("kind") != "original766-exact-runtime-recovery-witness"
            or witness["historical_witness"]["sha256"] != HISTORICAL_LINUX_WITNESS_SHA
            or witness["authorization"] != payload["authorization"]
            or witness.get("original_runtime_rebuilt") is not False
            or runtime != witness["existing_runtime_after"]
            or runtime["sha256"]
            != "f3a2f003b17d4fa841d38c8203abd441c38ac310be57955e4be6405effde42c8"
        ):
            raise ValueError("original SO witness does not preserve the original")
        if {
            "path": str(Path(runtime["path"]).resolve()),
            "sha256": "sha256:" + runtime["sha256"],
        } not in expected:
            raise ValueError("recovered original runtime is not its canonical inventory member")

    def membership(records):
        values = [
            (str(Path(r["path"]).resolve()), r["sha256"].removeprefix("sha256:"))
            for r in records
        ]
        if len(values) != len(set(values)):
            raise ValueError("duplicate original baseline authority member")
        return set(values)

    if len(expected) != 11 or membership(expected) != membership(
        manifest["original_artifacts"]
    ):
        raise ValueError("exact canonical original eleven-artifact membership differs")
    consumed_product_bindings(expected, manifest)
    consumed = [manifest["old"]["module"], *manifest["old"]["products"].values()]
    if lost.keys() & {str(Path(r["path"]).resolve()) for r in consumed}:
        raise ValueError("lost archival test cannot be a consumed measurement producer")
    for record in manifest["original_artifacts"]:
        if str(Path(record["path"]).resolve()) in lost and not Path(record["path"]).exists():
            continue
        checked_file(record)
    missing = [r for r in payload.get("lost_original_tests", []) if not Path(r["path"]).exists()]
    return {
        "expected_members": 11,
        "present_members": sum(Path(r["path"]).is_file() for r in manifest["original_artifacts"]),
        "lost_original_tests": missing,
        "complete_original_archive": not missing,
        "baseline_runtime_identity_preserved": True,
        "authority": authority,
    }


def consumed_product_bindings(expected, manifest):
    """Join matrix keys to the executable actually consumed for each side."""
    suffix = ".exe" if os.name == "nt" else ""
    frozen = {Path(record["path"]).name: record for record in expected}

    def identity(record):
        return (
            str(Path(record["path"]).resolve()),
            record["sha256"].removeprefix("sha256:"),
        )

    module_name = "clipper2next.dll" if os.name == "nt" else "libclipper2next.so.6.1.1"
    if identity(manifest["old"]["module"]) != identity(frozen[module_name]):
        raise ValueError("consumed old runtime differs from frozen original authority")
    if "product_producer_freeze" in manifest:
        # The authorized report-only rebuild has its own producing authority.
        # Original archival membership and runtime identity remain independent.
        proof = manifest["product_producer_freeze"]
        path = checked_file(proof)
        if proof["sha256"].removeprefix("sha256:") != PRODUCT_PRODUCER_FREEZE_SHA:
            raise ValueError("product producer re-freeze authority differs")
        payload = json.loads(path.read_text())
        if (payload["kind"] != "verified-report-preallocation-product-producer-freeze"
                or payload["baseline"] != BASELINE_HEAD):
            raise ValueError("product producer re-freeze contract differs")
        platform = "windows" if os.name == "nt" else "native"
        producing = [p for p in payload["platforms"] if p["platform"] == platform]
        if len(producing) != 1:
            raise ValueError("product producer re-freeze platform is not unique")
        producing = producing[0]
        for record in producing["framework"].values():
            checked_file(record)
        for side, role in (("old", "original"), ("candidate", "trial")):
            products = producing["products"][role]
            if len(products) != 8 or set(products) != set(manifest[side]["products"]):
                raise ValueError("re-frozen product inventory differs")
            if side == "candidate" and any(identity(p["runtime"]) != identity(manifest[side]["module"])
                                           for p in products.values()):
                # Consumers and candidate runtimes have different lifetimes.
                # A source correction may reuse these exact consumers only with
                # a fresh R6 producer bound to the current clean source tree.
                proof = manifest[side].get("runtime_producer")
                if proof is None:
                    raise ValueError("candidate runtime replacement requires its fresh R6 producer")
                source = Path(__file__).resolve().parents[3]
                producer = validate_producer(checked_file(proof), source)
                if (producer["scope"] != "package-rgal"
                        or producer["source"]["commit"] != manifest["candidate_head"]
                        or identity(producer["selection"]["runtime"]) != identity(manifest[side]["module"])):
                    raise ValueError("candidate runtime producer/source identity differs")
            for name, product in products.items():
                if (identity(product["executable"]) != identity(manifest[side]["products"][name])
                        or (side == "old" and identity(product["runtime"]) != identity(manifest[side]["module"]))):
                    raise ValueError("consumed re-frozen product/runtime differs")
                for record in [product["executable"], product["runtime"], *product["payload_objects"]]:
                    checked_file(record)
        return
    for name, record in manifest["old"]["products"].items():
        if name + suffix not in frozen or identity(record) != identity(
            frozen[name + suffix]
        ):
            raise ValueError(
                "consumed old product key differs from its exact frozen executable"
            )
    # The independent actual core-producing executable anchors the current bin.
    current_bin = Path(manifest["candidate"]["core"]["path"]).resolve().parent
    for name, record in manifest["candidate"]["products"].items():
        if Path(record["path"]).resolve() != current_bin / (name + suffix):
            raise ValueError(
                "candidate product key differs from its producing binary inventory"
            )


def public_build(root, side, binding):
    build = binding["public_build"]
    if "public_headers" in binding:
        raise ValueError("obsolete unjoined public header proof")
    if (
        build["role"] != "matched-public-build"
        or build["side"] != side
        or build["executable"] != binding["public"]
        or build["module"] != binding["module"]
    ):
        raise ValueError(
            "public build output/side does not match the consuming artifact"
        )
    binary = Path(build["binary_dir"]).resolve()
    if Path(binding["public"]["path"]).resolve().parent != binary:
        raise ValueError(
            "public output executable is outside producing binary directory"
        )
    cache_path = checked_file(build["cache"])
    if cache_path != binary / "CMakeCache.txt":
        raise ValueError("public build cache is not its producing cache")
    cache = {}
    for line in cache_path.read_text().splitlines():
        if not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    source = checked_file(build["source"])
    if (
        Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != source.parent
        or Path(cache["PUBLIC_HEADERS"]).resolve()
        != Path(build["header_root"]).resolve()
        or Path(cache["PUBLIC_LIBRARY"]).resolve() != checked_file(build["library"])
        or Path(cache["CMAKE_CXX_COMPILER"]).resolve()
        != checked_file(build["compiler"])
    ):
        raise ValueError(
            "public source/compiler/headers/link library differs from producing cache"
        )
    source_matches(
        root,
        build["source"],
        build["source_head"],
        "benchmarks/public_triangulation/main.cpp",
    )
    source_matches(
        root, build["source"], "HEAD", "benchmarks/public_triangulation/main.cpp"
    )
    header_head = BASELINE_HEAD if side == "old" else build["source_head"]
    if build["header_head"] != header_head:
        raise ValueError(
            "public headers are not bound to the correct producing side/head"
        )
    dependency = checked_file(build["dependencies"])
    headers = compiled_header_inventory(
        dependency.read_text(encoding="utf-8"),
        binary,
        {"clipper2next": Path(build["header_root"]) / "clipper2next"},
    )
    if headers != build["compiled_headers"]:
        raise ValueError(
            "public build compiler headers differ from producing dependency inventory"
        )
    for header in headers:
        relative = (
            Path(header["path"])
            .resolve()
            .relative_to(Path(build["header_root"]).resolve())
        )
        source_matches(root, header, header_head, "include/" + relative.as_posix())
        if side == "candidate":
            source_matches(root, header, "HEAD", "include/" + relative.as_posix())
    commands = checked_file(build["commands"]).read_text().replace("\\", "/")
    compile_line, link_line = commands.splitlines()
    if (
        source.as_posix() not in compile_line
        or Path(build["header_root"]).resolve().as_posix() not in compile_line
        or checked_file(build["library"]).as_posix() not in link_line
        or Path(binding["public"]["path"]).name not in link_line
    ):
        raise ValueError(
            "public native compiler/link commands do not produce the declared build"
        )
    log = checked_file(build["native_log"]).read_text()
    object_path = checked_file(build["object"])
    if object_path.parent.parent.parent != binary:
        raise ValueError("public compiled object is outside producing build")
    outputs = [
        line.split("\t")[3]
        for line in log.splitlines()
        if not line.startswith("#") and len(line.split("\t")) == 5
    ]
    if (
        object_path.relative_to(binary).as_posix() not in outputs
        or Path(binding["public"]["path"]).name not in outputs
    ):
        raise ValueError("native executed build log lacks the object/executable output")
    checked_file(binding["public"])
    return build


def public_command(
    executable, fixture, mode, *, row=None, seconds=None, ready=None, go=None
):
    command = [executable["path"], "--fixtures", fixture["path"], "--mode", mode]
    if row is not None:
        command += ["--row", row, "--seconds", seconds]
    if ready is not None:
        command += ["--ready-file", ready["path"], "--go-file", go["path"]]
    return command


def producing_observation(step, role, side, command, executable, module):
    if (
        step["role"] != role
        or step["side"] != side
        or not isinstance(step.get("run_id"), str)
        or not step["run_id"]
        or step["command"] != command
    ):
        raise ValueError("proof producing role/command/run identity differs")
    validate_process_observation(
        step["observation"], command, step["run_id"], executable, module
    )
    return step["observation"]


def capture_records(manifest, cases, builds):
    authority = oracle()
    combined = {}
    for line in checked_file(manifest["combined_records"]).read_text().splitlines():
        prefix, value = line.split(" ", 1)
        row = authority.strict_json(value)
        key = (prefix, row["case_index"])
        if key in combined:
            raise ValueError("duplicate actual mathematical record")
        combined[key] = row
    captures = json.loads(checked_file(manifest["capture_observation"]).read_text())
    if set(captures) != {"old", "candidate"}:
        raise ValueError("capture producing sides incomplete")
    for side in ("old", "candidate"):
        step = captures[side]
        expected = public_command(
            manifest[side]["public"],
            manifest["fixture_text"],
            "records",
            ready=step["ready"],
            go=step["go"],
        )
        observed = producing_observation(
            step,
            "public-records",
            side,
            expected,
            manifest[side]["public"],
            manifest[side]["module"],
        )
        if (
            observed["exit_code"] != 0
            or checked_file(step["go"]).read_text() != step["run_id"]
        ):
            raise ValueError(
                "public record producer did not complete its run handshake"
            )
        checked_file(step["ready"])
        if (
            step["source"] != builds[side]["source"]
            or step["fixture"] != manifest["fixture_text"]
        ):
            raise ValueError(
                "capture source/fixture differs from producing public build"
            )
        output = checked_file(step["output"])
        rows = [
            authority.strict_json(line[len("TRI_PUBLIC ") :])
            for line in output.read_text().splitlines()
            if line.startswith("TRI_PUBLIC ")
        ]
        if len(rows) != 20 or {r["case_index"] for r in rows} != set(range(20)):
            raise ValueError("actual capture output incomplete/duplicate")
        prefix = "TRI_LEGACY" if side == "old" else "TRI_CONTRACT"
        for row in rows:
            i = row["case_index"]
            if (
                row["input"] != cases[i // 2]["paths"]
                or row["use_delaunay"] != bool(i % 2)
                or combined.get((prefix, i)) != row
            ):
                raise ValueError(
                    "actual capture does not project to frozen mathematical input/output"
                )
        reuse = manifest["math_basis"][side]
        basis = checked_file(reuse["output"])
        if (
            output.read_bytes() != basis.read_bytes()
            or reuse["module"] != manifest[side]["module"]
        ):
            raise ValueError(
                "unchanged-byte mathematical reuse lacks exact output/module association"
            )
    if len(combined) != 40:
        raise ValueError("combined mathematical output is incomplete")


def kernel_rva(attribution, *, side, sweep=False, cocircular_quad=False):
    # Each comparison side enters its owning graph service. Current certified
    # domains need not execute the general sweep used by the frozen original.
    if side not in {"old", "candidate"}:
        raise ValueError("unknown kernel comparison side")
    direct = sweep or cocircular_quad
    if (sweep and cocircular_quad) or (direct and side != "candidate"):
        raise ValueError("invalid direct kernel attribution")
    function = "run_triangulation_sweep" if side == "old" else "execute_triangulation_graph"
    if sweep:
        function = "execute_sweep_triangulation_into"
    if cocircular_quad:
        function = "try_project_cocircular_quad"
    elf_prefix = f"_ZN12clipper2next8internal{len(function)}{function}E".encode()
    coff_prefix = f"?{function}@internal@clipper2next@@".encode()
    if cocircular_quad:
        # MSVC emits the integer wrapper as a tail jump. Attribute the body
        # it jumps to rather than matching a relocation-masked jump opcode.
        coff_prefix = b"??$project_cocircular_quad@_J@?A0x"
    module = checked_file(attribution["module"]).read_bytes()
    if attribution["format"] == "elf-symbol":
        if module[:6] != b"\x7fELF\x02\x01":
            raise ValueError("kernel attribution requires ELF64 little-endian")
        offset = struct.unpack_from("<Q", module, 40)[0]
        width, count = struct.unpack_from("<HH", module, 58)
        sections = [
            struct.unpack_from("<IIQQQQIIQQ", module, offset + i * width)
            for i in range(count)
        ]
        matches = []
        for section in sections:
            if section[1] != 2:
                continue
            strings = sections[section[6]]
            names = module[strings[4] : strings[4] + strings[5]]
            for i in range(section[4], section[4] + section[5], section[9]):
                name, info, _, defined_section, value, _ = struct.unpack_from(
                    "<IBBHQQ", module, i
                )
                symbol = names[name : names.find(b"\0", name)]
                if (
                    symbol.startswith(elf_prefix)
                    and (not direct or ((b"Point2IlE" in symbol or b"Point2IxE" in symbol)
                                       and not symbol.endswith(b"d")))
                    and b"." not in symbol
                    and info & 15 == 2
                    and defined_section > 0
                    and value > 0
                ):
                    matches.append(value)
        if len(matches) != 1:
            raise ValueError("actual ELF kernel symbol attribution is not unique")
        return matches[0]
    if attribution["format"] != "coff-body":
        raise ValueError("unknown kernel attribution format")
    data = checked_file(attribution["object"]).read_bytes()
    _, _, _, symbols_offset, symbols, opt, _ = struct.unpack_from("<HHIIIHH", data)
    strings = data[symbols_offset + symbols * 18 :]
    targets = []
    for i in range(symbols):
        name, value, section, kind, storage, _ = struct.unpack_from(
            "<8sIhHBB", data, symbols_offset + i * 18
        )
        if name[:4] == b"\0" * 4:
            index = int.from_bytes(name[4:], "little")
            name = strings[index : strings.find(b"\0", index)]
        if (
            name.startswith(coff_prefix)
            and (not cocircular_quad or b"@internal@clipper2next@@" in name)
            and (not sweep or (b"Point2@_J@" in name and b"Point2@N@" not in name
                               and not name.endswith(b"N@Z")))
            and section > 0
            and storage == (3 if cocircular_quad else 2)
            and kind & 0x20
        ):
            targets.append((value, section))
    if len(targets) != 1:
        raise ValueError("actual COFF kernel function attribution is not unique")
    value, section = targets[0]
    header = 20 + opt + (section - 1) * 40
    size, raw, reloc = struct.unpack_from("<III", data, header + 16)
    body = data[raw + value : raw + size]
    mask = bytearray(b"\1" * len(body))
    for i in range(struct.unpack_from("<H", data, header + 32)[0]):
        address, _, kind = struct.unpack_from("<IIH", data, reloc + i * 10)
        for j in range(
            max(0, address - value),
            min(address - value + (8 if kind == 1 else 4), len(body)),
        ):
            mask[j] = 0
    runs = []
    start = 0
    while start < len(mask):
        if not mask[start]:
            start += 1
            continue
        end = start
        while end < len(mask) and mask[end]:
            end += 1
        runs.append((end - start, start, end))
        start = end
    _, start, end = max(runs)
    matches = []
    cursor = 0
    while (found := module.find(body[start:end], cursor)) >= 0:
        position = found - start
        if all(not m or module[position + i] == body[i] for i, m in enumerate(mask)):
            matches.append(position)
        cursor = found + 1
    if len(matches) != 1:
        raise ValueError("actual COFF/DLL whole kernel body attribution is not unique")
    pe = struct.unpack_from("<I", module, 0x3C)[0]
    for i in range(struct.unpack_from("<H", module, pe + 6)[0]):
        section = pe + 24 + struct.unpack_from("<H", module, pe + 20)[0] + i * 40
        _, virtual, size, raw = struct.unpack_from("<IIII", module, section + 8)
        if raw <= matches[0] < raw + size:
            return virtual + matches[0] - raw
    raise ValueError("kernel body outside loaded PE sections")


def kernel_records(manifest, cases):
    rows = json.loads(checked_file(manifest["kernel_entry"]).read_text())
    cold = {r for r in supplement_rows(cases) if r.endswith("/cold")}
    if len(rows) != 40 or {(r["side"], r["row"]) for r in rows} != {
        (s, r) for s in ("old", "candidate") for r in cold
    }:
        raise ValueError("complete actual kernel-entry matrix missing/duplicate")
    if any(manifest[s]["kernel_attribution"]["module"] != manifest[s]["module"]
           for s in ("old", "candidate")):
        raise ValueError("kernel attribution runtime differs from its consuming side")
    rvas = {
        s: kernel_rva(manifest[s]["kernel_attribution"], side=s) for s in ("old", "candidate")
    }
    sweep_attribution = manifest["candidate"].get("sweep_kernel_attribution")
    sweep_rva = None
    if sweep_attribution is not None:
        if sweep_attribution["module"] != manifest["candidate"]["module"]:
            raise ValueError("Sweep kernel attribution runtime differs from its consuming side")
        sweep_rva = kernel_rva(sweep_attribution, side="candidate", sweep=True)
    quad_attribution = manifest["candidate"].get("cocircular_quad_kernel_attribution")
    quad_row = "exact_cocircular_tie/cdt/cold"
    quad_rva = None
    if quad_attribution is not None:
        if quad_attribution["module"] != manifest["candidate"]["module"] or quad_row not in cold:
            raise ValueError("cocircular kernel attribution differs from its consuming side or fixture")
        quad_rva = kernel_rva(quad_attribution, side="candidate", cocircular_quad=True)
    for row in rows:
        side = row["side"]
        expected_rva = rvas[side]
        if side == "candidate":
            if quad_rva is not None and row["row"] == quad_row:
                expected_rva = quad_rva
            elif sweep_rva is not None and row["row"].endswith("/sweep/cold"):
                expected_rva = sweep_rva
        step = row["producer"]
        expected = public_command(
            manifest[side]["public"],
            manifest["fixture_text"],
            "timing",
            row=row["row"],
            seconds="0.000001",
        )
        observed = producing_observation(
            step,
            "kernel-entry",
            side,
            expected,
            manifest[side]["public"],
            manifest[side]["module"],
        )
        text = checked_file(step["output"]).read_text()
        if os.name == "nt":
            pid = re.search(r"id:\s*([0-9a-f]+)", text)
            rip = re.search(r"rip=([0-9a-f`]+)", text)
            loaded = re.search(
                r"^ModLoad:\s+([0-9a-f`]+)\s+[0-9a-f`]+\s+([^\r\n]*clipper2next\.dll)\s*$",
                text,
                re.M | re.I,
            )
            if not pid or not rip or not loaded:
                raise ValueError("actual CDB kernel trace is incomplete")
            actual_pid, pc, base = (
                int(pid[1], 16),
                int(rip[1].replace("`", ""), 16),
                int(loaded[1].replace("`", ""), 16),
            )
            module = Path(loaded[2]).resolve()
            hits = text.count("\nKERNEL_ENTRY\n")
        else:
            # GDB producer emits this single raw JSON line while the inferior is stopped.
            trace = [
                json.loads(line[len("KERNEL_ENTRY ") :])
                for line in text.splitlines()
                if line.startswith("KERNEL_ENTRY ")
            ]
            if len(trace) != 1:
                raise ValueError("actual GDB kernel trace missing/duplicate")
            actual_pid, pc, base = (
                trace[0]["pid"],
                int(trace[0]["rip"], 16),
                int(trace[0]["module_base"], 16),
            )
            module = Path(trace[0]["module"]).resolve()
            hits = 1
        if (
            actual_pid != observed["pid"]
            or module != Path(manifest[side]["module"]["path"]).resolve()
            or hits != 1
            or row["hit_count"] != 1
            or pc != int(row["rip"].replace("`", ""), 16)
            or base != int(row["module_base"], 16)
            or pc - base != expected_rva
            or int(row["rva"], 16) != expected_rva
        ):
            raise ValueError(
                "kernel cold row/PID/log/function/base/RIP attribution differs"
            )


def worker_records(manifest, builds):
    rows = json.loads(checked_file(manifest["worker_placement"]).read_text())
    if len(rows) != 2 or {r["side"] for r in rows} != {"old", "candidate"}:
        raise ValueError("worker producing sides incomplete")
    for row in rows:
        side = row["side"]
        step = row["producer"]
        expected = public_command(
            manifest[side]["public"],
            manifest["fixture_text"],
            "placement",
            ready=step["ready"],
            go=step["go"],
        )
        observed = producing_observation(
            step,
            "worker-placement",
            side,
            expected,
            manifest[side]["public"],
            manifest[side]["module"],
        )
        output = checked_file(step["output"])
        actual = [
            int(line.split()[1])
            for line in output.read_text().splitlines()
            if line.startswith("WORKER_CPU ")
        ]
        if (
            observed["exit_code"] != 0
            or checked_file(step["go"]).read_text() != step["run_id"]
            or step["source"] != builds[side]["source"]
            or actual != row["actual_worker_cpus"]
            or len(actual) != 16
            or not set(actual) <= set(manifest["placement"]["parallel"])
        ):
            raise ValueError("actual producing worker output/placement differs")
