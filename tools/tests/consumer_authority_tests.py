"""Filesystem-backed role admission fixtures; these do not claim geometry qualification."""

from __future__ import annotations
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
import uuid

from tools.release.correction_run import consumer_record, validate_consumers
from tools.release.source_inventory import file_record, tree_record


def fixture(root: Path, *, role=None, module=None, wrong_headers=False):
    run_id = str(uuid.uuid4())
    run = root / run_id
    run.mkdir()
    source = root / "source"
    (source / "packaging/test_package").mkdir(parents=True)
    rgal_source = root / "rgal-source"
    (rgal_source / "packaging/test_package").mkdir(parents=True)
    extension = ".exe" if os.name == "nt" else ""
    clipper_name = "clipper2next.dll" if os.name == "nt" else "libclipper2next.so"
    rgal_name = "LibRGA.dll" if os.name == "nt" else "LibRGA.so"
    tbb_name = "tbb12.dll" if os.name == "nt" else "libtbb.so.12"

    def artifact(name, namespace, runtime_name):
        prefix = root / name
        (prefix / "include" / namespace).mkdir(parents=True)
        (prefix / "lib").mkdir()
        (prefix / "include" / namespace / "api.h").write_text(name + " header")
        dll = prefix / "lib" / runtime_name
        dll.write_text(name + " runtime")
        return prefix, dll

    installed, direct = artifact("installed", "clipper2next", clipper_name)
    package, clipper = artifact("package", "clipper2next", clipper_name)
    rgal_install, librga = artifact("rgal-install", "rgal", rgal_name)
    tbb_package, tbb = artifact("tbb-package", "oneapi", tbb_name)
    ipp_package, ipp = artifact(
        "ipp-package",
        "ipp",
        "ipp-runtime.dll" if os.name == "nt" else "libipp-runtime.so",
    )
    foreign, foreign_module = artifact("foreign", "clipper2next", clipper_name)
    generators = run / "generators"
    generators.mkdir()
    (generators / "config.cmake").write_text("selected")

    def node(name, prefix):
        return {
            "context": "host",
            "ref": name + "/1.0#revision",
            "rrev": "revision",
            "package_id": "package",
            "prev": "revision",
            "package_folder": str(prefix),
        }

    graph = {
        "graph": {
            "nodes": {
                "0": {"generators_folder": str(generators)},
                "1": node("clipper2next", package),
                "2": node("onetbb", tbb_package),
                "3": node("oneipp", ipp_package),
            }
        }
    }
    graph_path = run / "selected.json"
    graph_path.write_text(json.dumps(graph))
    create_path = run / "create.json"
    create_path.write_text(json.dumps(graph))
    from tools.release.run_correction_verification import (
        selected_package,
        rgal_runtime_closure,
    )

    selection = selected_package(graph_path, "clipper2next")
    selection.update(
        graph=file_record(graph_path),
        runtime=file_record(clipper),
        generators=tree_record(generators),
    )
    selection["onetbb"] = {
        **selected_package(graph_path, "onetbb"),
        "runtime": file_record(tbb),
    }
    steps = []

    def step(name, command, outputs=()):
        log = run / (name + ".log")
        log.write_text(name)
        value = {
            "name": name,
            "command": list(map(str, command)),
            "log": file_record(log),
            "outputs": list(outputs),
        }
        steps.append(value)
        return value

    step("configure", ["cmake", "-S", source, "-B", run / "product"])
    step("product-build", ["cmake", "--build", run / "product", "--clean-first"])
    step("install", ["cmake", "--install", run / "product", "--prefix", installed])
    create = step(
        "conan-create",
        [
            "conan",
            "create",
            source,
            "--test-folder=" + str(source / "packaging/test_package"),
            "--out-file=" + str(create_path),
            "-c",
            "tools.cmake.cmake_layout:test_folder=" + str(run / "conan-test"),
            "-c",
            "tools.cmake.cmake_layout:build_folder_vars=[]",
        ],
        [file_record(create_path)],
    )
    step(
        "rgal-discovery",
        ["cmake", "-S", rgal_source, "-B", run / "rgal-build"],
        [file_record(graph_path)],
    )
    step("rgal-build", ["cmake", "--build", run / "rgal-build", "--clean-first"])
    runtime_manifest = run / "rgal-build/RGAL/LibRGA-runtime-inputs.txt"
    runtime_manifest.parent.mkdir(parents=True)
    ipp_stage = runtime_manifest.parent / "oneipp-runtime"
    ipp_stage.mkdir()
    staged_ipp = ipp_stage / ipp.name
    staged_ipp.write_bytes(ipp.read_bytes())
    runtime_manifest.write_text(
        str(clipper) + "\n" + str(tbb) + "\n" + str(staged_ipp) + "\n"
    )
    (runtime_manifest.parent / ".LibRGA-runtime-files.txt").write_text(
        clipper.name + "\n" + tbb.name + "\n" + ipp.name + "\n"
    )
    for original in (clipper, tbb, staged_ipp):
        (runtime_manifest.parent / original.name).write_bytes(original.read_bytes())
    step(
        "rgal-install",
        ["cmake", "--install", run / "rgal-build", "--prefix", rgal_install],
    )
    consumers = {}
    dependencies = {}
    for label, binary, stem, prefix, modules in (
        (
            "direct",
            run / "direct-consumer",
            "clipper2next_install_smoke",
            installed,
            {"clipper": direct},
        ),
        (
            "package",
            run / "package-consumer",
            "clipper2next_package_test",
            package,
            {"clipper": clipper},
        ),
        (
            "rgal",
            run / "rgal-build/correction-consumer",
            "package_consumer",
            rgal_install,
            {"clipper": clipper, "librga": librga, "tbb": tbb},
        ),
        (
            "conan-create",
            run / "conan-test/Release",
            "clipper2next_package_test",
            package,
            {"clipper": clipper},
        ),
    ):
        binary.mkdir(parents=True)
        exe = binary / (stem + extension)
        exe.write_text(label + " executable")
        headers = tree_record(prefix / "include")
        actual_modules = dict(modules)
        if label == role:
            if module:
                actual_modules[module] = foreign_module
            if wrong_headers:
                headers = tree_record(foreign / "include")
        if label == "conan-create":
            execution = create
        else:
            configure_name = (
                "rgal-consumer-configure" if label == "rgal" else label + "-configure"
            )
            build_name = "rgal-consumer-build" if label == "rgal" else label + "-build"
            src = (
                rgal_source / "packaging/test_package"
                if label == "rgal"
                else source
                / (
                    "tests/install_smoke_project"
                    if label == "direct"
                    else "packaging/test_package"
                )
            )
            prefixes = (
                [prefix]
                if label == "direct"
                else [generators]
                if label == "package"
                else [prefix, generators]
            )
            step(
                configure_name,
                [
                    "cmake",
                    "-S",
                    src,
                    "-B",
                    binary,
                    "-DCMAKE_PREFIX_PATH=" + ";".join(map(str, prefixes)),
                ],
            )
            step(build_name, ["cmake", "--build", binary])
        header_step = step(
            label + "-header-dependencies", ["ninja", "-C", binary, "-t", "deps"]
        )
        roots = {
            "clipper2next": (installed if label == "direct" else package) / "include"
        }
        if label == "rgal":
            roots["rgal"] = rgal_install / "include"
        paths = [p for path in roots.values() for p in path.rglob("*.h")]
        Path(header_step["log"]["path"]).write_text(
            "".join("    " + str(p) + "\n" for p in paths)
        )
        header_step["log"] = file_record(Path(header_step["log"]["path"]))
        dependencies[label.replace("-", "_") + "_header_dependencies"] = {
            "log": header_step["log"],
            "headers": [file_record(p) for p in paths],
        }
        if label != "conan-create":
            execution = step(
                label + "-consumer",
                [exe, *([librga, tbb, clipper] if label == "rgal" else [])],
            )
        log = Path(execution["log"]["path"])
        log.write_text(
            f"correction_run_id={run_id}\ncorrection_executable={exe}\ncorrection_contract=PASS\ncorrection_cases=1\n"
            + "".join(
                f"correction_module_{key}={path}\n"
                for key, path in actual_modules.items()
            )
        )
        execution["log"] = file_record(log)
        consumers[label + "-consumer"] = consumer_record(
            log,
            run_id,
            file_record(exe),
            headers,
            {k: file_record(v) for k, v in actual_modules.items()},
        )
    return {
        "run_id": run_id,
        "scope": "package-rgal",
        "source": {"root": str(source)},
        "rgal_source": {"root": str(rgal_source)},
        "configuration": "Release",
        "steps": steps,
        "consumers": consumers,
        "installed": tree_record(installed),
        "rgal_installed": tree_record(rgal_install),
        "rgal_runtime_closure": rgal_runtime_closure(run / "rgal-build", graph_path),
        "selection": selection,
        "conan_create": file_record(create_path),
        **dependencies,
    }


class ConsumerAuthorityTests(unittest.TestCase):
    def test_runtime_inventory_rejects_missing_dependency_and_foreign_stage_bytes(self):
        from tools.release.run_correction_verification import rgal_runtime_closure

        for mutation in ("missing-input", "foreign-stage"):
            with (
                self.subTest(mutation=mutation),
                tempfile.TemporaryDirectory() as directory,
            ):
                data = fixture(Path(directory).resolve())
                record = data["rgal_runtime_closure"]
                manifest = Path(record["manifest"]["path"])
                if mutation == "missing-input":
                    lines = manifest.read_text().splitlines()
                    manifest.write_text("\n".join(lines[1:]) + "\n")
                else:
                    Path(record["oneipp_origins"][0]["staged"]["path"]).write_text(
                        "foreign package bytes"
                    )
                with self.assertRaisesRegex(
                    ValueError, "complete deployment inventory|selected package"
                ):
                    rgal_runtime_closure(
                        manifest.parent.parent, Path(data["selection"]["graph"]["path"])
                    )

    def test_runtime_closure_remains_bound_to_selected_inputs(self):
        from tools.release.run_correction_verification import rgal_runtime_closure

        with tempfile.TemporaryDirectory() as directory:
            data = fixture(Path(directory).resolve())
            wrong = copy.deepcopy(data)
            wrong["rgal_runtime_closure"]["files"].pop()
            with self.assertRaisesRegex(ValueError, "runtime closure"):
                validate_consumers(wrong)
            manifest = Path(data["rgal_runtime_closure"]["manifest"]["path"])
            binary = manifest.parent.parent
            foreign = Path(data["installed"]["path"]) / "lib"
            foreign_dll = next(foreign.iterdir())
            with manifest.open("a") as stream:
                stream.write(str(foreign_dll) + "\n")
            with self.assertRaisesRegex(ValueError, "selected package/build authority"):
                rgal_runtime_closure(binary, Path(data["selection"]["graph"]["path"]))

    def test_invocation_and_compiler_records_are_joined_to_the_role(self):
        with tempfile.TemporaryDirectory() as directory:
            data = fixture(Path(directory).resolve())
            wrong = copy.deepcopy(data)
            direct = next(s for s in wrong["steps"] if s["name"] == "direct-consumer")
            direct["command"][0] = wrong["consumers"]["package-consumer"]["executable"][
                "path"
            ]
            with self.assertRaisesRegex(ValueError, "invocation"):
                validate_consumers(wrong)
            wrong = copy.deepcopy(data)
            wrong["rgal_header_dependencies"]["headers"] = [
                record
                for record in wrong["rgal_header_dependencies"]["headers"]
                if "/rgal/" not in record["path"].replace("\\", "/")
            ]
            with self.assertRaisesRegex(ValueError, "compiler headers"):
                validate_consumers(wrong)
            wrong = copy.deepcopy(data)
            wrong["selection"]["onetbb"]["runtime"] = wrong["selection"]["runtime"]
            with self.assertRaisesRegex(ValueError, "oneTBB"):
                validate_consumers(wrong)

    def test_same_run_records_cannot_be_relabelled_or_orphaned(self):
        with tempfile.TemporaryDirectory() as directory:
            data = fixture(Path(directory).resolve())
            validate_consumers(data)
            for replacement in ("package-consumer", "conan-create-consumer"):
                with self.subTest(replacement=replacement):
                    wrong = copy.deepcopy(data)
                    wrong["consumers"]["direct-consumer"] = wrong["consumers"][
                        replacement
                    ]
                    with self.assertRaises(ValueError):
                        validate_consumers(wrong)
            wrong = copy.deepcopy(data)
            consumer = wrong["consumers"]["direct-consumer"]
            orphan = Path(directory) / "orphan.log"
            orphan.write_bytes(Path(consumer["log"]["path"]).read_bytes())
            consumer["log"] = file_record(orphan)
            with self.assertRaises(ValueError):
                validate_consumers(wrong)

    def test_role_modules_and_headers_come_from_artifact_authority(self):
        for role, module, headers in [
            ("direct", "clipper", False),
            ("conan-create", "clipper", False),
            ("rgal", "librga", False),
            ("rgal", "tbb", False),
            ("direct", None, True),
            ("package", None, True),
            ("rgal", None, True),
        ]:
            with (
                self.subTest(role=role, module=module, headers=headers),
                tempfile.TemporaryDirectory() as directory,
            ):
                data = fixture(
                    Path(directory).resolve(),
                    role=role,
                    module=module,
                    wrong_headers=headers,
                )
                with self.assertRaises(ValueError):
                    validate_consumers(data)


if __name__ == "__main__":
    unittest.main()
