"""A single run owns execution, input freezes, and artifact identity receipts."""

from __future__ import annotations
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
from tools.release.source_inventory import file_record, source_state, tree_record


def write_new(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True)


def verify_file(record: dict) -> None:
    if file_record(Path(record["path"])) != record:
        raise ValueError(f"artifact identity changed: {record['path']}")


def verify_tree(record: dict) -> None:
    if tree_record(Path(record["path"])) != record:
        raise ValueError(f"header/generator tree identity changed: {record['path']}")


class Producer:
    def __init__(
        self,
        output: Path,
        source: Path,
        environment: dict,
        *,
        require_clean=True,
        scope="direct",
    ):
        if scope not in {"direct", "package-rgal"}:
            raise ValueError("unknown producer phase scope")
        self.scope = scope
        self.run_id = str(uuid.uuid4())
        self.directory = output.resolve() / self.run_id
        self.directory.mkdir(parents=True, exist_ok=False)
        self.source = source.resolve()
        self.state = source_state(self.source)
        if require_clean and (
            self.state["tracked_dirty"] or self.state["untracked_inputs"]
        ):
            raise ValueError("qualification requires a clean tracked source HEAD")
        self.environment = dict(os.environ, **environment)
        self.environment["CLIPPER2NEXT_CORRECTION_RUN_ID"] = self.run_id
        self.environment["CLIPPER2NEXT_CORRECTION_INPUT_SHA256"] = self.state[
            "input_sha256"
        ]
        self.steps = []
        self.failed = False
        self.consumers = {}
        self.started = time.time_ns()
        write_new(
            self.directory / "start.json",
            {
                "run_id": self.run_id,
                "source": self.state,
                "started_ns": self.started,
                "scope": self.scope,
            },
        )

    def step(
        self, name: str, command: list, *, outputs=(), cwd=None, environment=None
    ) -> Path:
        if self.failed:
            raise ValueError("producer failed; later dependent steps are forbidden")
        for output in outputs:
            if Path(output).exists():
                raise ValueError(f"pre-existing producer output: {output}")
            Path(output).parent.mkdir(parents=True, exist_ok=True)
        log = self.directory / f"{len(self.steps):02d}-{name}.log"
        started = time.time_ns()
        with log.open("x", encoding="utf-8") as stream:
            result = subprocess.run(
                list(map(str, command)),
                cwd=cwd or self.source,
                env=environment or self.environment,
                stdout=stream,
                stderr=subprocess.STDOUT,
                check=False,
            )
        ended = time.time_ns()
        step = {
            "name": name,
            "command": list(map(str, command)),
            "run_id": self.run_id,
            "started_ns": started,
            "ended_ns": ended,
            "exit_code": result.returncode,
            "log": file_record(log),
            "outputs": [file_record(Path(p)) for p in outputs if Path(p).is_file()],
        }
        self.steps.append(step)
        write_new(self.directory / f"{len(self.steps):02d}-{name}.json", step)
        if result.returncode or any(not Path(p).is_file() for p in outputs):
            self.failed = True
            raise ValueError(
                f"producer step {name} failed ({result.returncode}); see {log}"
            )
        return log

    def consumer(
        self,
        name: str,
        executable: Path,
        headers: Path,
        expected_modules: dict,
        *,
        arguments=(),
        environment=None,
    ) -> dict:
        before = file_record(executable)
        header_tree = tree_record(headers)
        expected = {
            key: file_record(Path(path)) for key, path in expected_modules.items()
        }
        log = self.step(name, [executable, *arguments], environment=environment)
        record = consumer_record(log, self.run_id, before, header_tree, expected)
        self.consumers[name] = record
        return record

    def seal(self, *, scope: str, junit: Path, extra: dict) -> Path:
        if scope != self.scope:
            raise ValueError("phase scope cannot change after producer start")
        if self.failed or self.state != source_state(self.source):
            raise ValueError(
                "producer failed or source/build inputs changed during run"
            )
        payload = {
            "schema_version": 2,
            "producer": "run_correction_verification",
            "run_id": self.run_id,
            "scope": scope,
            "source": self.state,
            "started_ns": self.started,
            "ended_ns": time.time_ns(),
            "steps": self.steps,
            "consumers": self.consumers,
            "ctest_junit": file_record(junit),
            "start": file_record(self.directory / "start.json"),
            **extra,
        }
        target = self.directory / "producer.json"
        write_new(target, payload)
        return target


def consumer_record(
    log: Path, run_id: str, executable: dict, headers: dict, expected: dict
) -> dict:
    values = {}
    for line in log.read_text(encoding="utf-8", errors="strict").splitlines():
        if line.startswith("correction_") and "=" in line:
            key, value = line.split("=", 1)
            if key in values:
                raise ValueError("duplicate consumer identity field")
            values[key] = value
    if values.get("correction_run_id") != run_id or run_id == "unbound":
        raise ValueError("consumer belongs to an old or missing run ID")
    if (
        values.get("correction_contract") != "PASS"
        or int(values.get("correction_cases", 0)) <= 0
    ):
        raise ValueError("consumer failed, can_run=false or zero cases")
    verify_file(executable)
    verify_tree(headers)
    if Path(values.get("correction_executable", "")).resolve() != Path(
        executable["path"]
    ):
        raise ValueError("consumer executable identity mismatch")
    actual = {}
    for name, record in expected.items():
        verify_file(record)
        path = values.get("correction_module_" + name)
        if not path or not Path(path).is_absolute():
            raise ValueError(f"missing actual loaded module path: {name}")
        actual[name] = file_record(Path(path))
        if actual[name]["sha256"] != record["sha256"]:
            raise ValueError(f"actual loaded module hash mismatch: {name}")
    return {
        "run_id": run_id,
        "log": file_record(log),
        "executable": executable,
        "headers": headers,
        "modules": actual,
        "expected_modules": expected,
        "values": values,
    }


def validate_schedule(data: dict) -> None:
    run_id = data["run_id"]
    direct = [
        "profile-authority",
        "source-export",
        "configure",
        "product-build",
        "benchmark-registration",
        "direct-tests",
        "install",
        "direct-configure",
        "direct-build",
        "direct-header-dependencies",
        "direct-consumer",
    ]
    package = [
        "conan-create",
        "conan-create-header-dependencies",
        "rgal-discovery",
        "package-configure",
        "package-build",
        "package-header-dependencies",
        "package-consumer",
        "rgal-build",
        "rgal-registered",
        "rgal-contracts",
        "rgal-install",
        "rgal-consumer-configure",
        "rgal-consumer-build",
        "rgal-header-dependencies",
        "rgal-consumer",
    ]
    if data.get("scope") not in {"direct", "package-rgal"}:
        raise ValueError("invalid phase scope")
    expected = direct + (package if data["scope"] == "package-rgal" else [])
    if [step["name"] for step in data["steps"]] != expected:
        raise ValueError("required phase schedule is missing, duplicated or reordered")
    verify_file(data["start"])
    start = json.loads(Path(data["start"]["path"]).read_text(encoding="utf-8"))
    if (
        start.get("scope") != data["scope"]
        or start.get("run_id") != run_id
        or start.get("source") != data["source"]
    ):
        raise ValueError("phase intent/source cannot be rebound after start")
    previous = data["started_ns"]
    for step in data["steps"]:
        if (
            step["run_id"] != run_id
            or step["exit_code"] != 0
            or not previous
            <= step["started_ns"]
            <= step["ended_ns"]
            <= data["ended_ns"]
        ):
            raise ValueError("failed, old or unordered producer step")
        previous = step["ended_ns"]
        for record in [step["log"], *step["outputs"]]:
            verify_file(record)


def validate_configuration(cache_path: Path, corpus: dict) -> dict:
    from tools.release.run_correction_verification import cache_values
    from tools.release.prepare_correction_corpus import check_selection

    configured = cache_values(cache_path)
    if (
        configured.get("CLIPPER2NEXT_SELECTED_CORPUS_SCOPE") != "full-external"
        or Path(configured.get("CLIPPER2NEXT_SELECTED_CORPUS_ROOT", "")).resolve()
        != Path(corpus["derived_root"]).resolve()
        or configured.get("CLIPPER2NEXT_SELECTED_CORPUS_MANIFEST_SHA256")
        != corpus["sha256"]
    ):
        raise ValueError("formal build did not select the same full frozen corpus")
    path = Path(configured["CLIPPER2NEXT_SELECTED_CORPUS_SELECTION"])
    selection = check_selection(
        path, configured["CLIPPER2NEXT_SELECTED_CORPUS_SELECTION_SHA256"]
    )
    if selection.get("scope") != "full-external" or selection.get("selected") != corpus:
        raise ValueError(
            "profile-only or different receipt cannot grant full configuration qualification"
        )
    return file_record(path)


def validate_producer(path: Path, source: Path) -> dict:
    from tools.release.collect_release_runtime_evidence import collect_ctest_results

    data = json.loads(path.read_text(encoding="utf-8"))
    if (
        data.get("schema_version") != 2
        or data.get("producer") != "run_correction_verification"
    ):
        raise ValueError("missing current producer binding")
    required_fields = {
        "run_id",
        "scope",
        "source",
        "started_ns",
        "ended_ns",
        "steps",
        "consumers",
        "ctest_junit",
        "contract",
        "profile_report",
        "benchmark_list",
        "compiler",
        "exported_inputs",
        "build_configuration",
        "produced_records",
        "installed",
        "center_stage",
        "source_archive",
        "corpus",
        "direct_header_dependencies",
        "start",
        "corpus_selection",
        "configuration",
    }
    if not required_fields <= data.keys() or not data.get("steps"):
        raise ValueError("incomplete producer execution/input inventory")
    if (
        data["scope"] == "package-rgal"
        and not {
            "selection",
            "rgal_source",
            "rgal_installed",
            "rgal_runtime_closure",
            "rgal_junit",
            "conan_create",
            "package_header_dependencies",
            "conan_create_header_dependencies",
            "rgal_header_dependencies",
        }
        <= data.keys()
    ):
        raise ValueError("incomplete package/RGAL artifact authority")
    run_id = data["run_id"]
    if path.parent.name != run_id or data["source"] != source_state(source):
        raise ValueError("old run ID or source identity")
    if data["source"]["tracked_dirty"] or data["source"]["untracked_inputs"]:
        raise ValueError("dirty source is not release qualification")
    validate_schedule(data)
    verify_file(data["ctest_junit"])
    if data["ctest_junit"] not in [r for s in data["steps"] for r in s["outputs"]]:
        raise ValueError("JUnit was not generated by this producer")
    tests = collect_ctest_results(Path(data["ctest_junit"]["path"]))
    if any(status != "PASS" for status in tests.values()):
        raise ValueError("failed or skipped test cannot qualify")
    from tools.release.corpus_authority_manifest import validate_frozen_corpus_manifest
    from tools.release.evidence_contract import load_contract
    from tools.release.source_inventory import (
        matching_public_headers,
        public_archive_inventory,
        archive_record,
    )

    for name in (
        "contract",
        "profile_report",
        "benchmark_list",
        "compiler",
        "exported_inputs",
    ):
        verify_file(data[name])
    verify_file(data["build_configuration"])
    if (
        validate_configuration(
            Path(data["build_configuration"]["path"]), data["corpus"]
        )
        != data["corpus_selection"]
    ):
        raise ValueError("configured corpus selection changed")
    for record in data["produced_records"]:
        verify_file(record)
    for name in (
        "direct_header_dependencies",
        "conan_create_header_dependencies",
        "package_header_dependencies",
        "rgal_header_dependencies",
    ):
        if name in data:
            verify_file(data[name]["log"])
            if not data[name]["headers"]:
                raise ValueError("empty actual compiler header dependency inventory")
            for record in data[name]["headers"]:
                verify_file(record)
    if json.loads(
        Path(data["exported_inputs"]["path"]).read_text(encoding="utf-8")
    ) != public_archive_inventory(source):
        raise ValueError("export/archive input selection changed")
    verify_tree(data["installed"])
    verify_tree(data["center_stage"])
    if (
        data.get("center_stage_scope")
        != "local-assembly-only; publication not executed"
    ):
        raise ValueError(
            "local archive assembly cannot qualify a remote publication URL"
        )
    if (
        archive_record(Path(data["source_archive"]["artifact"]["path"]), source)
        != data["source_archive"]
    ):
        raise ValueError("actual source archive changed")
    matching_public_headers(Path(data["installed"]["path"]), source)
    corpus = data["corpus"]
    if len(data.get("legacy_inputs", [])) != 5:
        raise ValueError("missing five original legacy source inputs")
    for pair in data["legacy_inputs"]:
        verify_file(pair["source"])
        verify_file(pair["derived"])
        if pair["source"]["sha256"] != pair["derived"]["sha256"]:
            raise ValueError("legacy input bytes changed")
    validate_frozen_corpus_manifest(
        Path(corpus["manifest"]),
        Path(corpus["source_root"]),
        Path(corpus["derived_root"]),
        corpus["sha256"],
        load_contract(Path(data["contract"]["path"])),
    )
    profile = json.loads(
        Path(data["profile_report"]["path"]).read_text(encoding="utf-8")
    )
    if (
        profile.get("status") != "PASS"
        or profile.get("contract_sha256") != data["contract"]["sha256"]
    ):
        raise ValueError("profile qualification failed or used another contract")
    for name in ("profile_report", "benchmark_list"):
        record = data[name]
        if record not in [r for s in data["steps"] for r in [s["log"], *s["outputs"]]]:
            raise ValueError("unproduced old profile/benchmark evidence")
    if data["scope"] == "package-rgal":
        selection = data["selection"]
        verify_file(selection["graph"])
        verify_tree(selection["generators"])
        verify_tree(selection["headers"])
        verify_file(selection["profile"])
        verify_file(selection["runtime"])
        for record in selection["import_libraries"]:
            verify_file(record)
        verify_file(selection["discovery_profile"])
        for record in selection["source_inputs"]:
            verify_file(record)
        matching_public_headers(Path(selection["package_root"]), source)
        from tools.release.run_correction_verification import (
            selected_package,
            RGAL_CONTRACTS,
        )

        actual = selected_package(Path(selection["graph"]["path"]), "clipper2next")
        if any(actual[key] != selection[key] for key in actual):
            raise ValueError("selected graph revision/package/header identity mismatch")
        verify_file(data["conan_create"])
        created = selected_package(Path(data["conan_create"]["path"]), "clipper2next")
        if actual != created:
            raise ValueError(
                "selected dependency is not this run newly created package"
            )
        verify_file(data["rgal_junit"])
        if collect_ctest_results(Path(data["rgal_junit"]["path"])) != {
            name: "PASS" for name in sorted(RGAL_CONTRACTS)
        }:
            raise ValueError("missing/failed required RGAL contracts")
        for field in ("ref", "rrev", "package_id", "prev"):
            if not selection.get(field):
                raise ValueError("incomplete selected Conan package identity")
        if (
            selection["run_id"] != run_id
            or selection["source_sha256"] != data["source"]["input_sha256"]
        ):
            raise ValueError("old selected graph binding")
        if source_state(Path(data["rgal_source"]["root"])) != data["rgal_source"]:
            raise ValueError("RGAL source identity changed")
    elif data["scope"] != "direct":
        raise ValueError("invalid qualification scope")
    validate_consumers(data)
    data["tests"] = tests
    return data


def _command_value(command: list[str], option: str) -> str:
    values = []
    for index, value in enumerate(command):
        if value == option:
            if index + 1 >= len(command):
                raise ValueError("missing producer command argument: " + option)
            values.append(command[index + 1])
        elif value.startswith(option + "="):
            values.append(value[len(option) + 1 :])
    if len(values) != 1:
        raise ValueError("missing or ambiguous producer command argument: " + option)
    return values[0]


def _build_directory(steps: dict, configure: str, build: str, source: Path) -> Path:
    command = steps[configure]["command"]
    if Path(_command_value(command, "-S")).resolve() != source.resolve():
        raise ValueError(
            "consumer source differs from its producing role: " + configure
        )
    binary = Path(_command_value(command, "-B")).resolve()
    if Path(_command_value(steps[build]["command"], "--build")).resolve() != binary:
        raise ValueError(
            "consumer build differs from configured binary directory: " + build
        )
    return binary


def _installation(steps: dict, install: str, binary: Path, recorded: dict) -> Path:
    command = steps[install]["command"]
    if Path(_command_value(command, "--install")).resolve() != binary:
        raise ValueError(
            "installed artifact was not produced by its required build: " + install
        )
    prefix = Path(_command_value(command, "--prefix")).resolve()
    if tree_record(prefix) != recorded:
        raise ValueError(
            "installed artifact authority differs from producing step: " + install
        )
    return prefix


def _consumer_prefixes(step: dict, expected: list[Path]) -> None:
    actual = _command_value(step["command"], "-DCMAKE_PREFIX_PATH").split(";")
    if [Path(p).resolve() for p in actual] != [p.resolve() for p in expected]:
        raise ValueError("consumer package search differs from its artifact authority")


def _header_dependencies(
    data: dict, steps: dict, role: str, binary: Path, roots: dict[str, Path]
) -> None:
    from tools.release.run_correction_verification import compiled_header_inventory

    step = steps[role + "-header-dependencies"]
    if Path(_command_value(step["command"], "-C")).resolve() != binary or step[
        "command"
    ][-2:] != ["-t", "deps"]:
        raise ValueError(
            "header dependencies were not observed in the producing binary directory"
        )
    record = data[role.replace("-", "_") + "_header_dependencies"]
    if record["log"] != step["log"]:
        raise ValueError("header dependency log is not the producing step log")
    verify_file(step["log"])
    actual = compiled_header_inventory(
        Path(step["log"]["path"]).read_text(encoding="utf-8"), binary, roots
    )
    if record["headers"] != actual:
        raise ValueError("compiler headers differ from role artifact authority")


def validate_consumers(data: dict) -> None:
    """Join each observed consumer to its execution, build and artifact authority."""
    from tools.release.run_correction_verification import (
        selected_package,
        runtime,
        rgal_runtime_closure,
    )

    steps = {step["name"]: step for step in data["steps"]}
    source = Path(data["source"]["root"])
    suffix = ".exe" if os.name == "nt" else ""
    clipper_pattern = "clipper2next.dll" if os.name == "nt" else "libclipper2next.so*"
    product = _build_directory(steps, "configure", "product-build", source)
    installed = _installation(steps, "install", product, data["installed"])
    direct = _build_directory(
        steps,
        "direct-configure",
        "direct-build",
        source / "tests/install_smoke_project",
    )
    _consumer_prefixes(steps["direct-configure"], [installed])
    _header_dependencies(
        data, steps, "direct", direct, {"clipper2next": installed / "include"}
    )
    # Values below are derived from producing steps/graphs, never from consumer claims.
    authorities = {
        "direct-consumer": (
            steps["direct-consumer"],
            direct / ("clipper2next_install_smoke" + suffix),
            tree_record(installed / "include"),
            {"clipper": file_record(runtime(installed, clipper_pattern))},
            [],
        )
    }
    if data["scope"] == "package-rgal":
        selection = data["selection"]
        graph_record = selection["graph"]
        create_record = data["conan_create"]
        for record, name in (
            (graph_record, "rgal-discovery"),
            (create_record, "conan-create"),
        ):
            verify_file(record)
            if record not in steps[name]["outputs"]:
                raise ValueError(
                    "dependency graph was not emitted by its producing step: " + name
                )
        graph = Path(graph_record["path"])
        created = selected_package(Path(create_record["path"]), "clipper2next")
        selected = selected_package(graph, "clipper2next")
        if created != selected:
            raise ValueError("created and selected artifact authority differ")
        package = Path(selected["package_root"])
        clipper = file_record(runtime(package, clipper_pattern))
        if selection["runtime"] != clipper:
            raise ValueError("selected runtime differs from resolved package authority")
        generators = Path(
            json.loads(graph.read_text(encoding="utf-8"))["graph"]["nodes"]["0"][
                "generators_folder"
            ]
        ).resolve()
        if not generators.is_relative_to(
            Path(selection["generators"]["path"]).resolve()
        ):
            raise ValueError("resolved generators leave their producing authority")
        package_binary = _build_directory(
            steps,
            "package-configure",
            "package-build",
            source / "packaging/test_package",
        )
        _consumer_prefixes(steps["package-configure"], [generators])
        _header_dependencies(
            data,
            steps,
            "package",
            package_binary,
            {"clipper2next": package / "include"},
        )
        authorities["package-consumer"] = (
            steps["package-consumer"],
            package_binary / ("clipper2next_package_test" + suffix),
            selected["headers"],
            {"clipper": clipper},
            [],
        )
        create = steps["conan-create"]
        command = create["command"]
        if (
            Path(_command_value(command, "--out-file")).resolve()
            != Path(create_record["path"]).resolve()
            or Path(_command_value(command, "--test-folder")).resolve()
            != source / "packaging/test_package"
        ):
            raise ValueError(
                "Conan create graph/test source differs from producing command"
            )
        test_root = Path(
            _command_value(command, "tools.cmake.cmake_layout:test_folder")
        ).resolve()
        if (
            test_root != Path(create["log"]["path"]).parent / "conan-test"
            or _command_value(command, "tools.cmake.cmake_layout:build_folder_vars")
            != "[]"
        ):
            raise ValueError(
                "Conan test executable lacks a fresh declared build context"
            )
        conan_exe = (
            test_root / data["configuration"] / ("clipper2next_package_test" + suffix)
        )
        _header_dependencies(
            data,
            steps,
            "conan-create",
            conan_exe.parent,
            {"clipper2next": package / "include"},
        )
        authorities["conan-create-consumer"] = (
            create,
            conan_exe,
            created["headers"],
            {"clipper": clipper},
            None,
        )
        rgal_source = Path(data["rgal_source"]["root"])
        rgal_binary = _build_directory(
            steps, "rgal-discovery", "rgal-build", rgal_source
        )
        rgal_install = _installation(
            steps, "rgal-install", rgal_binary, data["rgal_installed"]
        )
        rgal_consumer = _build_directory(
            steps,
            "rgal-consumer-configure",
            "rgal-consumer-build",
            rgal_source / "packaging/test_package",
        )
        _consumer_prefixes(steps["rgal-consumer-configure"], [rgal_install, generators])
        _header_dependencies(
            data,
            steps,
            "rgal",
            rgal_consumer,
            {"clipper2next": package / "include", "rgal": rgal_install / "include"},
        )
        tbb_package = selected_package(graph, "onetbb")
        debug = data["configuration"] == "Debug"
        tbb_pattern = (
            ("tbb12_debug.dll" if debug else "tbb12.dll")
            if os.name == "nt"
            else ("libtbb_debug.so*" if debug else "libtbb.so*")
        )
        tbb = file_record(runtime(Path(tbb_package["package_root"]), tbb_pattern))
        if selection["onetbb"] != {**tbb_package, "runtime": tbb}:
            raise ValueError(
                "oneTBB module/header authority differs from selected graph"
            )
        closure = rgal_runtime_closure(rgal_binary, graph)
        if data["rgal_runtime_closure"] != closure or any(
            record not in closure["files"] for record in (clipper, tbb)
        ):
            raise ValueError(
                "RGAL runtime closure differs from producing artifact authority"
            )
        librga = file_record(
            runtime(
                rgal_install,
                ("LibRGAd" if debug else "LibRGA")
                + (".dll" if os.name == "nt" else ".so*"),
            )
        )
        authorities["rgal-consumer"] = (
            steps["rgal-consumer"],
            rgal_consumer / ("package_consumer" + suffix),
            tree_record(rgal_install / "include"),
            {"clipper": clipper, "librga": librga, "tbb": tbb},
            [librga["path"], tbb["path"], clipper["path"]],
        )
    if set(data["consumers"]) != set(authorities):
        raise ValueError("missing or unknown required consumer role")
    for role, (step, exe, headers, modules, arguments) in authorities.items():
        consumer = data["consumers"][role]
        if consumer["log"] != step["log"]:
            raise ValueError("consumer log is not its producing role step: " + role)
        if arguments is not None:
            command = step["command"]
            if (
                len(command) != 1 + len(arguments)
                or not Path(command[0]).is_absolute()
                or Path(command[0]).resolve() != exe.resolve()
                or [Path(p).resolve() for p in command[1:]]
                != [Path(p).resolve() for p in arguments]
            ):
                raise ValueError(
                    "consumer invocation differs from its producing build/artifacts: "
                    + role
                )
        verify_file(step["log"])
        expected = consumer_record(
            Path(step["log"]["path"]),
            data["run_id"],
            file_record(exe),
            headers,
            modules,
        )
        if consumer != expected:
            raise ValueError(
                "consumer record differs from role artifact authority: " + role
            )
