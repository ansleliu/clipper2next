#!/usr/bin/env python3
"""Produce fresh, source-bound direct or complete package/RGAL qualification."""

from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import re
import sys

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import build_install
from tools.release.correction_run import (
    Producer,
    write_new,
    consumer_record,
    validate_configuration,
)
from tools.release.source_inventory import (
    file_record,
    source_state,
    tree_record,
    public_archive_inventory,
    matching_public_headers,
    archive_record,
)

ROOT = Path(__file__).resolve().parents[2]
RGAL_CONTRACTS = (
    "LibRGABuildInstallContract",
    "LibRGAConanDependencyCoordinateContract",
    "LibRGARuntimeStagingContract",
    "LibRGAPolygonOperationsContract",
    "LibRGAPathContract",
    "LibRGAPathRasterOracleContract",
    "LibRGAContinuousExecutionQualificationContract",
)


def owned(path: Path, root: Path) -> Path:
    path, root = path.resolve(), root.resolve()
    if path == root or not path.is_relative_to(root):
        raise ValueError(f"output must be below declared workspace {root}: {path}")
    return path


def reserve_package_outputs(graph, generators, rgal_build) -> Path | None:
    values = (graph, generators, rgal_build)
    if not any(values):
        return None
    if not all(values):
        raise ValueError("package/RGAL requires all three reserved output arguments")
    rgal_build = rgal_build.resolve()
    roots = [
        p
        for p in rgal_build.parents
        if (p / "RGAL/requires.txt").is_file() and (p / ".git").exists()
    ]
    if len(roots) != 1:
        raise ValueError("RGAL build output is not inside the declared RGAL checkout")
    root = roots[0]
    owned(rgal_build, root / "build")
    for output in (graph, generators):
        resolved = output.resolve()
        if not (
            resolved.is_relative_to(ROOT / "build")
            or resolved.is_relative_to(root / "build")
        ):
            raise ValueError("reserved discovery output leaves declared build roots")
        if output.exists():
            raise ValueError(
                f"pre-existing discovery output cannot be rebound: {output}"
            )
    return root


def ninja_prefix(binary: Path) -> None:
    if os.name == "nt":
        rules = binary / "CMakeFiles/rules.ninja"
        if "msvc_deps_prefix = 注意: 包含文件:  \n" not in rules.read_text(
            encoding="utf-8"
        ):
            raise ValueError(
                f"actual Ninja includes prefix is not UTF8/two-space: {rules}; use tty and chcp 65001"
            )


def cache_values(path: Path) -> dict:
    values = {}
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.startswith(("#", "//")) and ":" in line and "=" in line:
                key, value = line.split("=", 1)
                values[key.split(":")[0]] = value
    return values


def runtime(root: Path, pattern: str) -> Path:
    root = root.resolve()
    paths = set()
    for directory in (root, root / "lib", root / "bin"):
        for candidate in directory.glob(pattern):
            if candidate.is_file():
                path = candidate.resolve()
                if not path.is_relative_to(root):
                    raise ValueError(f"product runtime resolves outside {root}: {path}")
                paths.add(path)
    if len(paths) != 1:
        raise ValueError(f"expected exactly one {pattern} product in {root}: {paths}")
    return paths.pop()


def runtime_environment(environment: dict, paths) -> dict:
    result = dict(environment)
    key = "PATH" if os.name == "nt" else "LD_LIBRARY_PATH"
    result[key] = os.pathsep.join(
        [*(str(Path(p).parent) for p in paths), result.get(key, "")]
    )
    return result


def rgal_runtime_closure(binary: Path, graph: Path) -> dict:
    """Read the runtime inventory owned by RGAL's selected package deployment."""
    manifest = binary / "RGAL/LibRGA-runtime-inputs.txt"
    nodes = json.loads(graph.read_text(encoding="utf-8"))["graph"]["nodes"].values()
    roots = [
        Path(node["package_folder"]).resolve()
        for node in nodes
        if node.get("context") == "host" and node.get("package_folder")
    ]
    stage = binary.resolve() / "RGAL/oneipp-runtime"
    if stage.is_symlink() or stage.resolve() != stage:
        raise ValueError("oneIPP helper-stage leaves its owned build directory")
    roots.append(stage)
    values = [
        Path(value)
        for value in manifest.read_text(encoding="utf-8").splitlines()
        if value
    ]
    files = []
    for path in values:
        if not path.is_absolute() or not any(
            path.resolve().is_relative_to(root) for root in roots
        ):
            raise ValueError(
                "RGAL runtime input leaves selected package/build authority"
            )
        record = file_record(path)
        if record not in files:
            files.append(record)
    if not files:
        raise ValueError("RGAL runtime closure is empty")
    inventory = binary / "RGAL/.LibRGA-runtime-files.txt"
    names = inventory.read_text(encoding="utf-8").splitlines()
    if (
        not names
        or any(Path(name).name != name for name in names)
        or set(names) != {p.name for p in values}
    ):
        raise ValueError(
            "RGAL runtime input list differs from complete deployment inventory"
        )
    ipp = Path(selected_package(graph, "oneipp")["package_root"])
    staged = sorted(p for p in stage.iterdir() if p.is_file())
    if not staged or {
        p.absolute() for p in values if p.parent.resolve() == stage
    } != set(staged):
        raise ValueError("RGAL runtime list omits oneIPP helper-stage output")
    origins = []
    for path in staged:
        # The pinned package helper copies selected lib files without renaming.
        # Enumerate its outputs; do not infer component or ISA filenames.
        original = file_record(ipp / "lib" / path.name)
        deployed = file_record(path)
        if original["sha256"] != deployed["sha256"]:
            raise ValueError("oneIPP helper-stage bytes differ from selected package")
        origins.append({"source": original, "staged": deployed})
    deployed = []
    for name in names:
        record = file_record(binary / "RGAL" / name)
        if not any(
            p.name == name and file_record(p)["sha256"] == record["sha256"]
            for p in values
        ):
            raise ValueError("RGAL deployed runtime differs from its selected input")
        deployed.append(record)
    return {
        "manifest": file_record(manifest),
        "inventory": file_record(inventory),
        "files": files,
        "oneipp_origins": origins,
        "deployed": deployed,
    }


def consumer_headers(
    run: Producer, name: str, binary: Path, headers: dict[str, Path]
) -> dict:
    log = run.step(name + "-header-dependencies", ["ninja", "-C", binary, "-t", "deps"])
    return {
        "log": file_record(log),
        "headers": compiled_header_inventory(
            log.read_text(encoding="utf-8"), binary, headers
        ),
    }


def compiled_header_inventory(
    text: str, binary: Path, headers: dict[str, Path]
) -> list[dict]:
    includes = []
    observed = set()
    for line in text.splitlines():
        value = line.strip().replace("\\", "/")
        namespace = next(
            (name for name in headers if "/include/" + name + "/" in value), None
        )
        if namespace is None:
            continue
        path = Path(value)
        actual = (path if path.is_absolute() else binary / path).resolve()
        if not actual.is_file() or not actual.is_relative_to(
            headers[namespace].resolve()
        ):
            raise ValueError("consumer actually compiled another header root: " + value)
        includes.append(file_record(actual))
        observed.add(namespace)
    if observed != set(headers):
        raise ValueError(
            "compiler dependency inventory omits required artifact headers"
        )
    return includes


def conan_profile(path: Path, configuration: str, toolchain) -> None:
    settings = ["arch=x86_64", f"os={platform.system()}", f"build_type={configuration}"]
    if os.name == "nt":
        settings += [
            "compiler=msvc",
            "compiler.version=194",
            "compiler.runtime=dynamic",
            f"compiler.runtime_type={configuration}",
        ]
    else:
        settings += [
            "compiler=gcc",
            "compiler.version=13",
            "compiler.libcxx=libstdc++11",
        ]
    settings.append("compiler.cppstd=23")
    compilers = json.dumps(
        {"c": toolchain.c_compiler.as_posix(), "cpp": toolchain.cxx_compiler.as_posix()}
    )
    with path.open("x", encoding="utf-8") as stream:
        stream.write(
            "[settings]\n"
            + "\n".join(settings)
            + "\n[conf]\ntools.cmake.cmaketoolchain:generator=Ninja\n"
            + "tools.build:compiler_executables="
            + compilers
            + "\n"
        )


def selected_package(graph: Path, name: str) -> dict:
    nodes = json.loads(graph.read_text(encoding="utf-8"))["graph"]["nodes"].values()
    selected = [
        node
        for node in nodes
        if node.get("context") == "host"
        and str(node.get("ref", "")).startswith(name + "/")
        and node.get("package_folder")
    ]
    if len(selected) != 1:
        raise ValueError(f"graph must resolve exactly one {name} host package")
    node = selected[0]
    result = {field: node.get(field) for field in ("ref", "rrev", "package_id", "prev")}
    if not all(result.values()):
        raise ValueError(f"graph omits full revision identity: {result}")
    result["package_root"] = str(Path(node["package_folder"]).resolve())
    result["headers"] = tree_record(Path(result["package_root"]) / "include")
    return result


def _run_installed_rgal_consumer(
    run: Producer,
    rgal: Path,
    binary: Path,
    selection: dict,
    generators: Path,
    compiler: Path,
    configuration: str,
) -> dict:
    install = binary / "correction-install"
    run.step(
        "rgal-install",
        ["cmake", "--install", binary, "--component", "LibRGA", "--prefix", install],
    )
    librga = runtime(
        install,
        ("LibRGAd" if configuration == "Debug" else "LibRGA")
        + (".dll" if os.name == "nt" else ".so*"),
    )
    clipper = Path(selection["runtime"]["path"])
    tbb = Path(selection["onetbb"]["runtime"]["path"])
    closure = rgal_runtime_closure(binary, Path(selection["graph"]["path"]))
    # Link and run the installed consumer with one verified private runtime closure.
    environment = runtime_environment(
        run.environment, [librga, *(Path(r["path"]) for r in closure["files"])]
    )
    consumer = binary / "correction-consumer"
    version = (rgal / "RGAL/version.txt").read_text().strip().split(":")[1]
    run.step(
        "rgal-consumer-configure",
        [
            "cmake",
            "-S",
            rgal / "packaging/test_package",
            "-B",
            consumer,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=" + configuration,
            "-DCMAKE_CXX_COMPILER=" + str(compiler),
            "-DCMAKE_PREFIX_PATH=" + str(install) + ";" + str(generators),
            "-DLIBRGA_REQUIRED_VERSION=" + version,
            "-DLIBRGA_EXPECTED_PREFIX=" + str(install),
        ],
        environment=environment,
    )
    ninja_prefix(consumer)
    run.step(
        "rgal-consumer-build", ["cmake", "--build", consumer], environment=environment
    )
    headers = consumer_headers(
        run, "rgal", consumer,
        {
            "clipper2next": Path(selection["package_root"]) / "include",
            "rgal": install / "include",
        },
    )
    executable = consumer / (
        "package_consumer.exe" if os.name == "nt" else "package_consumer"
    )
    run.consumer(
        "rgal-consumer", executable, install / "include",
        {"clipper": clipper, "librga": librga, "tbb": tbb},
        arguments=[librga, tbb, clipper],
        environment=environment,
    )
    return {
        "rgal_installed": tree_record(install),
        "rgal_runtime_closure": closure,
        "rgal_header_dependencies": headers,
    }


def package_phase(run: Producer, args, toolchain, rgal: Path) -> dict:
    rgal_state = source_state(rgal)
    if rgal_state["tracked_dirty"] or rgal_state["untracked_inputs"]:
        raise ValueError("RGAL qualification requires clean tracked source")
    profile = run.directory / "profile"
    conan_profile(profile, args.configuration, toolchain)
    profile_hash = file_record(profile)
    create = run.directory / "clipper-create.json"
    test_root = run.directory / "conan-test"
    if test_root.exists():
        raise ValueError("Conan create requires a fresh declared test binary directory")
    command = build_install.internal_create_command(
        root=ROOT,
        identity=build_install.read_release_identity(ROOT),
        configuration=args.configuration,
        system_name=platform.system(),
        user=args.conan_user,
        profile=profile,
        output_json=create,
    )
    create_log = run.step("conan-create", command, outputs=[create])
    created = selected_package(create, "clipper2next")
    created_node = next(
        n
        for n in json.loads(create.read_text())["graph"]["nodes"].values()
        if str(n.get("ref", "")).startswith("clipper2next/") and n.get("package_folder")
    )
    source_inputs = []
    for relative in run.state["exported"]["source_files"]:
        cached = Path(created_node["source_folder"]) / relative
        if cached.read_bytes().replace(b"\r\n", b"\n") != (
            ROOT / relative
        ).read_bytes().replace(b"\r\n", b"\n"):
            raise ValueError("Conan exported source differs: " + relative)
        source_inputs.append(file_record(cached))
    created_runtime = runtime(
        Path(created["package_root"]),
        "clipper2next.dll" if os.name == "nt" else "libclipper2next.so*",
    )
    created_exe = (
        test_root
        / args.configuration
        / (
            "clipper2next_package_test.exe"
            if os.name == "nt"
            else "clipper2next_package_test"
        )
    )
    ninja_prefix(created_exe.parent)
    run.consumers["conan-create-consumer"] = consumer_record(
        create_log,
        run.run_id,
        file_record(created_exe),
        created["headers"],
        {"clipper": file_record(created_runtime)},
    )
    created_headers = consumer_headers(
        run,
        "conan-create",
        created_exe.parent,
        {"clipper2next": Path(created["package_root"]) / "include"},
    )
    rgal_binary = args.rgal_build_dir.resolve() / run.run_id
    if rgal_binary.exists():
        raise ValueError("RGAL binary directory must be fresh")
    graph = args.selected_conan_graph.resolve()
    graph.parent.mkdir(parents=True, exist_ok=True)
    generators = args.conan_generators_dir.resolve()
    discovery_environment = dict(
        run.environment,
        CLIPPER2NEXT_CORRECTION_GRAPH=str(graph),
        CLIPPER2NEXT_CORRECTION_GENERATORS=str(generators),
    )
    run.step(
        "rgal-discovery",
        [
            "cmake",
            "-S",
            rgal,
            "-B",
            rgal_binary,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=" + args.configuration,
            "-DBUILD_TESTING=ON",
            "-DRGAL_ENABLE_QUALIFICATION_COUNTERS=ON",
            "-DCONAN_BUILD_POLICY=never",
            "-DCMAKE_CXX_COMPILER=" + str(toolchain.cxx_compiler),
        ],
        outputs=[graph, Path(str(graph) + ".producer.json")],
        environment=discovery_environment,
    )
    ninja_prefix(rgal_binary)
    binding = json.loads(
        Path(str(graph) + ".producer.json").read_text(encoding="utf-8")
    )
    if binding != {"run_id": run.run_id, "source_sha256": run.state["input_sha256"]}:
        raise ValueError("discovery did not bind this source/run")
    selected = selected_package(graph, "clipper2next")
    if selected != created:
        raise ValueError(
            "RGAL discovery selected a different Conan revision/header tree"
        )
    node0 = json.loads(graph.read_text(encoding="utf-8"))["graph"]["nodes"]["0"]
    actual_generators = Path(node0["generators_folder"]).resolve()
    if not actual_generators.is_relative_to(generators):
        raise ValueError("resolved generators leave reserved output directory")
    package_root = Path(selected["package_root"])
    matching_public_headers(package_root, ROOT)
    clipper = runtime(
        package_root, "clipper2next.dll" if os.name == "nt" else "libclipper2next.so*"
    )
    selected.update(
        graph=file_record(graph),
        generators=tree_record(generators),
        runtime=file_record(clipper),
        source_inputs=source_inputs,
        discovery_profile=file_record(
            rgal_binary / "conan-dependencies/build/conan_host_profile"
        ),
        import_libraries=[file_record(p) for p in (package_root / "lib").glob("*.lib")],
        run_id=run.run_id,
        source_sha256=run.state["input_sha256"],
        profile=profile_hash,
    )
    tbb_package = selected_package(graph, "onetbb")
    debug = args.configuration == "Debug"
    tbb = runtime(
        Path(tbb_package["package_root"]),
        ("tbb12_debug.dll" if debug else "tbb12.dll")
        if os.name == "nt"
        else ("libtbb_debug.so*" if debug else "libtbb.so*"),
    )
    selected["onetbb"] = {**tbb_package, "runtime": file_record(tbb)}
    consumer = run.directory / "package-consumer"
    run.step(
        "package-configure",
        [
            "cmake",
            "-S",
            ROOT / "packaging/test_package",
            "-B",
            consumer,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=" + args.configuration,
            "-DCMAKE_CXX_COMPILER=" + str(toolchain.cxx_compiler),
            "-DCMAKE_PREFIX_PATH=" + str(actual_generators),
        ],
    )
    ninja_prefix(consumer)
    run.step(
        "package-build", ["cmake", "--build", consumer, "--parallel", str(args.jobs)]
    )
    package_headers = consumer_headers(
        run, "package", consumer, {"clipper2next": package_root / "include"}
    )
    exe = consumer / (
        "clipper2next_package_test.exe"
        if os.name == "nt"
        else "clipper2next_package_test"
    )
    run.consumer(
        "package-consumer",
        exe,
        package_root / "include",
        {"clipper": clipper},
        environment=runtime_environment(run.environment, [clipper]),
    )
    write_new(run.directory / "selection.json", selected)
    # Discovery only configures. No RGAL executable is allowed to run before this checked build.
    run.step(
        "rgal-build",
        [
            "cmake",
            "--build",
            rgal_binary,
            "--clean-first",
            "--parallel",
            str(args.jobs),
        ],
    )
    registered = run.step("rgal-registered", ["ctest", "--test-dir", rgal_binary, "-N"])
    registered_names = set(
        re.findall(r"Test\s+#\d+:\s+(\S+)", registered.read_text(encoding="utf-8"))
    )
    if not set(RGAL_CONTRACTS) <= registered_names:
        raise ValueError("required seven RGAL contracts are not all registered")
    junit = run.directory / "rgal-junit.xml"
    run.step(
        "rgal-contracts",
        [
            "ctest",
            "--test-dir",
            rgal_binary,
            "--output-on-failure",
            "--parallel",
            "1",
            "--output-junit",
            junit,
            "-R",
            "^(" + "|".join(RGAL_CONTRACTS) + ")$",
        ],
        outputs=[junit],
    )
    from tools.release.collect_release_runtime_evidence import collect_ctest_results

    if collect_ctest_results(junit) != {
        name: "PASS" for name in sorted(RGAL_CONTRACTS)
    }:
        raise ValueError("seven RGAL contracts did not each execute successfully")
    installed_rgal = _run_installed_rgal_consumer(
        run, rgal, rgal_binary, selected, actual_generators,
        toolchain.cxx_compiler, args.configuration,
    )
    if rgal_state != source_state(rgal):
        raise ValueError("RGAL source changed during qualification")
    return {
        "selection": selected,
        "rgal_source": rgal_state,
        **installed_rgal,
        "rgal_junit": file_record(junit),
        "package_header_dependencies": package_headers,
        "conan_create_header_dependencies": created_headers,
        "conan_create": file_record(create),
        "profile": profile_hash,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build-dir", "install-prefix", "output-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(
        "--selected-conan-graph",
        type=Path,
        help="Reserved new discovery output; must not already exist",
    )
    parser.add_argument(
        "--conan-generators-dir",
        type=Path,
        help="Reserved new discovery output; must not already exist",
    )
    parser.add_argument(
        "--rgal-build-dir",
        type=Path,
        help="Declared RGAL checkout build root; each run uses a new UUID child",
    )
    parser.add_argument(
        "--configuration", choices=("Debug", "Release"), default="Release"
    )
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument(
        "--corpus-source",
        type=Path,
        default=os.environ.get("CLIPPER2NEXT_GEOMETRY_CORPUS_SOURCE_ROOT"),
        help="Read-only complete original normalized corpus parent; no subset can qualify",
    )
    parser.add_argument(
        "--conan-user", default=os.environ.get("CLIPPER2NEXT_CONAN_USER")
    )
    args = parser.parse_args(argv)
    try:
        for path in (args.build_dir, args.install_prefix, args.output_dir):
            owned(path, ROOT / "build")
        rgal = reserve_package_outputs(
            args.selected_conan_graph, args.conan_generators_dir, args.rgal_build_dir
        )
        if rgal is not None and not args.conan_user:
            raise ValueError(
                "package production requires --conan-user or CLIPPER2NEXT_CONAN_USER"
            )
        toolchain = build_install.build_toolchain(platform.system())
        run = Producer(
            args.output_dir,
            ROOT,
            toolchain.environment,
            scope="package-rgal" if rgal else "direct",
        )
        if not args.corpus_source:
            raise ValueError(
                "complete original corpus must be selected with --corpus-source or CLIPPER2NEXT_GEOMETRY_CORPUS_SOURCE_ROOT"
            )
        from tools.release.prepare_correction_corpus import prepare
        from tools.release.evidence_contract import DEFAULT_CONTRACT_PATH

        corpus = prepare(
            args.corpus_source,
            run.directory / "geometry",
            legacy_source=args.corpus_source,
        )
        legacy_inputs = corpus["legacy_inputs"]
        full_receipt = run.directory / "geometry-freeze.json"
        full_receipt_digest = file_record(full_receipt)["sha256"]
        run.environment["CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT"] = corpus["derived_root"]
        run.environment["CLIPPER2NEXT_GEOMETRY_CORPUS_SOURCE_ROOT"] = str(
            args.corpus_source.resolve()
        )
        profile_report = run.directory / "profile-report.json"
        run.step(
            "profile-authority",
            [
                sys.executable,
                ROOT / "tools/checks/check_geometry_corpus_profiles.py",
                corpus["derived_root"],
                "--output-json",
                profile_report,
            ],
            outputs=[profile_report],
        )
        binary = args.build_dir.resolve() / run.run_id
        install = args.install_prefix.resolve() / run.run_id
        cache = cache_values(args.build_dir / "CMakeCache.txt")
        if not cache:
            cache = cache_values(ROOT / "build/correction-release/CMakeCache.txt")
        if (
            cache.get("CMAKE_HOME_DIRECTORY")
            and Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != ROOT
        ):
            raise ValueError("dependency seed cache belongs to another source")
        command = [
            "cmake",
            "-S",
            ROOT,
            "-B",
            binary,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=" + args.configuration,
            "-DCMAKE_CXX_COMPILER=" + str(toolchain.cxx_compiler),
            "-DCLIPPER2NEXT_TESTS=ON",
            "-DCLIPPER2NEXT_CORRECTION_FULL_RECEIPT=" + str(full_receipt),
            "-DCLIPPER2NEXT_CORRECTION_FULL_RECEIPT_SHA256=" + full_receipt_digest,
            "-DCLIPPER2NEXT_BENCHMARKS=ON",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
        ]
        for key in (
            "CLIPPER2NEXT_FETCH_DEPS",
            "FETCHCONTENT_SOURCE_DIR_GOOGLETEST",
            "FETCHCONTENT_SOURCE_DIR_GOOGLEBENCHMARK",
            "Clipper2_DIR",
            "CLIPPER2NEXT_BUILD_ORACLE",
        ):
            if key in cache:
                command.append("-D" + key + "=" + cache[key])
        inputs = public_archive_inventory(ROOT)
        write_new(run.directory / "exported-inputs.json", inputs)
        archive = run.directory / "source.tar"
        run.step(
            "source-export",
            ["git", "archive", "--format=tar", "--output=" + str(archive), "HEAD"],
            outputs=[archive],
        )
        source_archive = archive_record(archive, ROOT)
        center = build_install.stage_center_recipe(
            root=ROOT,
            destination=run.directory / "center-stage",
            identity=build_install.read_release_identity(ROOT),
            homepage=build_install.read_release_identity(ROOT).public_repository,
            archive_url="https://example.invalid/correction-source.tar",
            archive_sha256=source_archive["artifact"]["sha256"],
        )
        run.step("configure", command)
        ninja_prefix(binary)
        corpus_selection = validate_configuration(binary / "CMakeCache.txt", corpus)
        run.step(
            "product-build",
            ["cmake", "--build", binary, "--clean-first", "--parallel", str(args.jobs)],
        )
        benchmark = (
            binary
            / "bin"
            / (
                "clipper2next_bench_external_corpus.exe"
                if os.name == "nt"
                else "clipper2next_bench_external_corpus"
            )
        )
        run.environment["CLIPPER2NEXT_EXPECTED_PRODUCT"] = str(
            binary / "bin/clipper2next.dll"
            if os.name == "nt"
            else runtime(binary, "libclipper2next.so*")
        )
        benchmark_list = run.step(
            "benchmark-registration", [benchmark, "--benchmark_list_tests"]
        )
        junit = run.directory / "direct-junit.xml"
        run.step(
            "direct-tests",
            [
                "ctest",
                "--test-dir",
                binary,
                "--output-on-failure",
                "--parallel",
                "1",
                "--output-junit",
                junit,
            ],
            outputs=[junit],
        )
        run.step("install", ["cmake", "--install", binary, "--prefix", install])
        matching_public_headers(install, ROOT)
        consumer = run.directory / "direct-consumer"
        run.step(
            "direct-configure",
            [
                "cmake",
                "-S",
                ROOT / "tests/install_smoke_project",
                "-B",
                consumer,
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=" + args.configuration,
                "-DCMAKE_CXX_COMPILER=" + str(toolchain.cxx_compiler),
                "-DCMAKE_PREFIX_PATH=" + str(install),
            ],
        )
        ninja_prefix(consumer)
        run.step("direct-build", ["cmake", "--build", consumer])
        direct_headers = consumer_headers(
            run, "direct", consumer, {"clipper2next": install / "include"}
        )
        clipper = runtime(
            install, "clipper2next.dll" if os.name == "nt" else "libclipper2next.so*"
        )
        exe = consumer / (
            "clipper2next_install_smoke.exe"
            if os.name == "nt"
            else "clipper2next_install_smoke"
        )
        run.consumer(
            "direct-consumer",
            exe,
            install / "include",
            {"clipper": clipper},
            environment=runtime_environment(run.environment, [clipper]),
        )
        extra = {
            "configuration": args.configuration,
            "compiler": file_record(toolchain.cxx_compiler),
            "corpus": corpus,
            "contract": file_record(DEFAULT_CONTRACT_PATH),
            "profile_report": file_record(profile_report),
            "legacy_inputs": legacy_inputs,
            "benchmark_list": file_record(benchmark_list),
            "source_archive": source_archive,
            "center_stage": tree_record(center),
            "center_stage_scope": "local-assembly-only; publication not executed",
            "direct_header_dependencies": direct_headers,
            "build_configuration": file_record(binary / "CMakeCache.txt"),
            "corpus_selection": corpus_selection,
            "produced_records": [
                file_record(p)
                for p in binary.rglob("*")
                if p.is_file()
                and p.suffix in {".json", ".log", ".xml"}
                and not any(
                    part in {"_deps", "CMakeFiles"}
                    for part in p.relative_to(binary).parts
                )
            ],
            "exported_inputs": file_record(run.directory / "exported-inputs.json"),
            "installed": tree_record(install),
        }
        if rgal:
            extra.update(package_phase(run, args, toolchain, rgal))
        manifest = run.seal(
            scope="package-rgal" if rgal else "direct", junit=junit, extra=extra
        )
        from tools.release.collect_release_runtime_evidence import (
            build_runtime_evidence,
        )

        evidence = build_runtime_evidence(
            producer_manifest=manifest, repository_root=ROOT
        )
        write_new(run.directory / "runtime-evidence.json", evidence)
        print(f"scope={evidence['scope']} run_id={run.run_id} producer={manifest}")
        return 0
    except (ValueError, OSError, KeyError) as error:
        print("qualification failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
