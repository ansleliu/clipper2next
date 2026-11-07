import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class ConanSourceLifetimeTests(unittest.TestCase):
    class ConsumerReached(Exception):
        pass

    def run_to_consumer(self, *, mismatch=False, missing=False):
        from tools.release import run_correction_verification as owner

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "revision-source"
            source.mkdir()
            if not missing:
                (source / "CMakeLists.txt").write_bytes(
                    b"different source" if mismatch else (ROOT / "CMakeLists.txt").read_bytes()
                )
            package = base / "package"
            package.mkdir()
            module = package / "module"
            module.write_bytes(b"product")
            deleted = base / "deduplicated-build"

            def step(name, command, outputs):
                outputs[0].write_text(
                    json.dumps({"graph": {"nodes": {"1": {
                        "ref": "clipper2next/7.0.0@company/release#revision",
                        "package_folder": str(package),
                        "source_folder": str(source),
                        "build_folder": str(deleted),
                    }}}})
                )
                log = base / "create.log"
                log.write_text("create completed")
                return log

            def inspect_prefix(path):
                if path == deleted:
                    raise FileNotFoundError("identical Conan build was removed")
                self.assertEqual(path, base / "conan-test/Release")
                raise self.ConsumerReached

            run = SimpleNamespace(
                directory=base,
                state={"exported": {"source_files": {"CMakeLists.txt": "unused"}}},
                step=step,
                consumers={},
            )
            args = SimpleNamespace(configuration="Release", conan_user="company")
            with (
                patch.object(
                    owner, "source_state",
                    return_value={"tracked_dirty": False, "untracked_inputs": []},
                ),
                patch.object(
                    owner, "conan_profile",
                    side_effect=lambda path, *values: path.write_text("complete profile"),
                ),
                patch.object(
                    owner.build_install, "read_release_identity", return_value=object(),
                ),
                patch.object(
                    owner.build_install, "internal_create_command",
                    return_value=["conan", "create"],
                ),
                patch.object(
                    owner, "selected_package",
                    return_value={"package_root": str(package)},
                ),
                patch.object(owner, "runtime", return_value=module),
                patch.object(owner, "ninja_prefix", side_effect=inspect_prefix),
            ):
                owner.package_phase(run, args, SimpleNamespace(), base / "rgal")

    def test_same_package_revision_does_not_require_deleted_product_build(self):
        with self.assertRaises(self.ConsumerReached):
            self.run_to_consumer()

    def test_canonical_source_difference_still_blocks_consumer(self):
        with self.assertRaisesRegex(ValueError, "exported source differs"):
            self.run_to_consumer(mismatch=True)

    def test_missing_revision_source_is_not_replaced_with_another_build(self):
        with self.assertRaisesRegex(FileNotFoundError, "CMakeLists"):
            self.run_to_consumer(missing=True)


class ExportGateTests(unittest.TestCase):
    def test_export_inventory_ignores_private_workspace_but_rejects_exported_leak(self):
        from tools.release.source_inventory import (
            exported_inventory,
            validate_inventory,
        )
        from tools.checks.check_public_release_information import find_findings

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "conanfile.py").write_text(
                'class Recipe:\n    exports_sources = ("src/*",)\n'
            )
            (root / "src").mkdir()
            (root / "src" / "public.txt").write_text("public input")
            (root / "private.md").write_text("http://" + "192" + ".168.1.1")
            inventory = exported_inventory(root)
            self.assertEqual(
                [], find_findings(root, validate_inventory(root, inventory))
            )
            (root / "src" / "public.txt").write_text("http://" + "192" + ".168.1.1")
            with self.assertRaisesRegex(ValueError, "changed"):
                validate_inventory(root, inventory)
            self.assertTrue(
                find_findings(root, validate_inventory(root, exported_inventory(root)))
            )

    def test_center_stages_shared_contract(self):
        import build_install

        with tempfile.TemporaryDirectory() as directory:
            stage = build_install.stage_center_recipe(
                root=ROOT,
                destination=Path(directory),
                identity=build_install.read_release_identity(ROOT),
                homepage="https://github.com/ansleliu/clipper2next",
                archive_url="https://example.invalid/source.tar.gz",
                archive_sha256="a" * 64,
            )
            for name in ("correction_contract.h", "correction_contract.cpp"):
                self.assertTrue((stage / "all/test_package" / name).is_file(), name)


class InstalledRgalConsumerTests(unittest.TestCase):
    def exercise(self, invalid=False):
        from tools.release import run_correction_verification as owner

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            rgal = base / "rgal"
            (rgal / "RGAL").mkdir(parents=True)
            (rgal / "RGAL/version.txt").write_text("LibRGA:7.3.3")
            binary = base / "binary"
            install = binary / "correction-install"
            private = base / "private" / "vendor.so"
            librga = install / "lib" / "LibRGA"
            environments = {}
            steps = []

            def step(name, command, *, environment=None):
                steps.append(name)
                if name in {"rgal-consumer-configure", "rgal-consumer-build"}:
                    self.assertIsNotNone(environment)
                    key = "PATH" if sys.platform == "win32" else "LD_LIBRARY_PATH"
                    self.assertIn(str(private.parent), environment[key].split(os.pathsep))
                    environments[name] = environment

            def consumer(name, executable, headers, modules, *, arguments, environment):
                self.assertEqual(name, "rgal-consumer")
                self.assertEqual(arguments, [librga, base / "tbb", base / "clipper"])
                environments[name] = environment
                self.assertIs(environment, environments["rgal-consumer-build"])

            run = SimpleNamespace(environment={}, step=step, consumer=consumer)
            selection = {
                "runtime": {"path": str(base / "clipper")},
                "onetbb": {"runtime": {"path": str(base / "tbb")}},
                "package_root": str(base / "clipper-package"),
                "graph": {"path": str(base / "selected.json")},
            }
            closure = {"files": [{"path": str(private)}]}
            closure_result = {"side_effect": ValueError("invalid selected runtime")}
            if not invalid:
                closure_result = {"return_value": closure}
            with (
                patch.object(owner, "rgal_runtime_closure", **closure_result),
                patch.object(owner, "runtime", return_value=librga),
                patch.object(owner, "ninja_prefix"),
                patch.object(owner, "consumer_headers", return_value={"current": "headers"}),
                patch.object(owner, "tree_record", return_value={"current": "installation"}),
            ):
                result = owner._run_installed_rgal_consumer(
                    run, rgal, binary, selection, base / "generators",
                    Path("compiler"), "Release",
                )
            self.assertIs(
                environments["rgal-consumer-configure"],
                environments["rgal-consumer-build"],
            )
            self.assertEqual(result["rgal_runtime_closure"], closure)
            self.assertEqual(steps, [
                "rgal-install", "rgal-consumer-configure", "rgal-consumer-build",
            ])

    def test_declared_private_environment_reaches_configure_build_and_run(self):
        self.exercise()

    def test_invalid_private_runtime_blocks_consumer_configuration(self):
        with self.assertRaisesRegex(ValueError, "invalid selected runtime"):
            self.exercise(invalid=True)


class ProducerTests(unittest.TestCase):
    def test_runtime_accepts_declared_root_lib_and_bin_layouts(self):
        from tools.release.run_correction_verification import runtime

        for layout in (".", "lib", "bin"):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                target = root / layout / "libclipper2next.so.7.0.0"
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"product")
                self.assertEqual(target, runtime(root, "libclipper2next.so*"))

    def test_runtime_rejects_missing_distinct_and_undeclared_candidates(self):
        from tools.release.run_correction_verification import runtime

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            foreign = root / "other-build"
            foreign.mkdir()
            (foreign / "libclipper2next.so").write_bytes(b"foreign")
            with self.assertRaisesRegex(ValueError, "exactly one"):
                runtime(root, "libclipper2next.so*")
            (root / "lib").mkdir()
            (root / "bin").mkdir()
            (root / "lib/libclipper2next.so").write_bytes(b"first")
            (root / "bin/libclipper2next.so").write_bytes(b"second")
            with self.assertRaisesRegex(ValueError, "exactly one"):
                runtime(root, "libclipper2next.so*")

    def test_runtime_deduplicates_aliases_and_rejects_foreign_symlink_target(self):
        from tools.release.run_correction_verification import runtime

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve() / "product"
            root.mkdir()
            (root / "lib").mkdir()
            target = root / "libclipper2next.so.7.0.0"
            target.write_bytes(b"product")
            alias = root / "lib/libclipper2next.so"
            alias.symlink_to(target)
            self.assertEqual(target, runtime(root, "libclipper2next.so*"))
            alias.unlink()
            target.unlink()
            foreign = root.parent / "foreign.so"
            foreign.write_bytes(b"foreign")
            alias.symlink_to(foreign)
            with self.assertRaisesRegex(ValueError, "outside"):
                runtime(root, "libclipper2next.so*")

    def test_untracked_demo_build_input_is_not_private_documentation(self):
        from tools.tests.audit_algorithm_coverage_matrix_tests import (
            initialize_clean_repository,
        )
        from tools.release.source_inventory import source_state

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            initialize_clean_repository(source)
            (source / "demo").mkdir()
            (source / "docs").mkdir()
            (source / "demo/model.cpp").write_text("int model;")
            (source / "docs/private.md").write_text("private notes")
            self.assertEqual(
                ["demo/model.cpp"], source_state(source)["untracked_inputs"]
            )

    def test_ninja_header_paths_are_relative_to_the_consumer_binary_directory(self):
        from tools.release.run_correction_verification import compiled_header_inventory
        import os

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            binary = root / "build"
            binary.mkdir()
            headers = root / "install/include"
            (headers / "clipper2next").mkdir(parents=True)
            header = headers / "clipper2next/api.h"
            header.write_text("matching installed header")
            relative = os.path.relpath(header, binary)
            records = compiled_header_inventory(
                "    " + relative + "\n", binary, {"clipper2next": headers}
            )
            self.assertEqual(str(header), records[0]["path"])
            wrong = root / "source/include/clipper2next/api.h"
            wrong.parent.mkdir(parents=True)
            wrong.write_text(header.read_text())
            with self.assertRaisesRegex(ValueError, "another header root"):
                compiled_header_inventory(
                    "    " + os.path.relpath(wrong, binary) + "\n",
                    binary,
                    {"clipper2next": headers},
                )

    def test_configuration_does_not_promote_profile_receipts_to_full_scope(self):
        from tools.release.prepare_correction_corpus import (
            prepare,
            configure_selection,
            LEGACY_FILES,
        )
        from tools.release.source_inventory import file_record

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            raw = root / "original-text-fixture"
            data = raw / "sources/clipper2-rust/Tests/data"
            data.mkdir(parents=True)
            for name in LEGACY_FILES:
                (data / name).write_text("copy-boundary fixture " + name)
            prepare(
                ROOT / "tests/oracle/corpus/geometry",
                root / "profiles",
                release_eligibility=False,
                legacy_source=raw,
            )
            receipt = root / "profiles-freeze.json"
            with self.assertRaisesRegex(ValueError, "omits required"):
                configure_selection(
                    root / "full-config", raw, receipt, file_record(receipt)["sha256"]
                )
            with self.assertRaisesRegex(ValueError, "receipt differs"):
                configure_selection(root / "wrong-hash-config", raw, receipt, "0" * 64)

    def test_sealed_phase_envelope_rejects_missing_or_reordered_work(self):
        from tools.release.correction_run import Producer, validate_schedule
        from tools.tests.audit_algorithm_coverage_matrix_tests import (
            initialize_clean_repository,
        )
        import json

        phases = [
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
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "source"
            initialize_clean_repository(source)
            variants = [
                phases,
                [p for p in phases if p != "product-build"],
                phases[:2] + [phases[3], phases[2]] + phases[4:],
                phases + package,
                phases + package[:-1],
                phases,
            ]
            for index, sequence in enumerate(variants):
                scope = "package-rgal" if index >= 3 else "direct"
                run = Producer(root / f"outputs-{index}", source, {}, scope=scope)
                junit = run.directory / "junit.xml"
                for name in sequence:
                    if name == "direct-tests":
                        run.step(
                            name,
                            [
                                sys.executable,
                                "-c",
                                "from pathlib import Path; import sys; Path(sys.argv[1]).write_text('<testsuite><testcase name=\"phase-fixture\"/></testsuite>')",
                                str(junit),
                            ],
                            outputs=[junit],
                        )
                    else:
                        run.step(name, [sys.executable, "-c", "pass"])
                if index == 5:
                    with self.assertRaisesRegex(ValueError, "scope"):
                        run.seal(scope="direct", junit=junit, extra={})
                manifest = run.seal(scope=scope, junit=junit, extra={})
                envelope = json.loads(manifest.read_text())
                if index in (0, 3):
                    validate_schedule(envelope)
                else:
                    with self.assertRaisesRegex(ValueError, "phase"):
                        validate_schedule(envelope)

    def test_old_run_changed_module_and_changed_headers_are_rejected(self):
        from tools.release.correction_run import consumer_record
        from tools.release.source_inventory import file_record, tree_record

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            headers = root / "include"
            headers.mkdir()
            (headers / "api.h").write_text("new header")
            library = root / "same-name.dll"
            library.write_bytes(Path(sys.executable).read_bytes())
            exe = file_record(Path(sys.executable))
            expected = {"clipper": file_record(library)}
            tree = tree_record(headers)
            log = root / "consumer.log"
            log.write_text(
                f"correction_run_id=current\ncorrection_executable={exe['path']}\ncorrection_module_clipper={library}\ncorrection_contract=PASS\ncorrection_cases=1\n"
            )
            self.assertEqual(
                "current",
                consumer_record(log, "current", exe, tree, expected)["run_id"],
            )
            with self.assertRaisesRegex(ValueError, "run ID"):
                consumer_record(log, "new-run", exe, tree, expected)
            library.write_bytes(b"different same-name library")
            with self.assertRaisesRegex(ValueError, "identity changed"):
                consumer_record(log, "current", exe, tree, expected)
            library.write_bytes(Path(sys.executable).read_bytes())
            (headers / "api.h").write_text("old header")
            with self.assertRaisesRegex(ValueError, "tree identity"):
                consumer_record(log, "current", exe, tree, expected)

    def test_partial_package_arguments_and_preexisting_graph_rejected(self):
        from tools.release.run_correction_verification import reserve_package_outputs
        from tools.release import run_correction_verification as runner
        from unittest import mock

        with self.assertRaisesRegex(ValueError, "all three"):
            reserve_package_outputs(Path("old.json"), None, None)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            rgal = root / "rgal"
            (rgal / "RGAL").mkdir(parents=True)
            (rgal / ".git").mkdir()
            (rgal / "RGAL/requires.txt").write_text("dependency")
            clipper = root / "clipper"
            (clipper / "build").mkdir(parents=True)
            graph = clipper / "build/graph.json"
            generators = clipper / "build/generators"
            graph.write_text('{"run_id":"apparently-matching"}')
            with mock.patch.object(runner, "ROOT", clipper):
                with self.assertRaisesRegex(ValueError, "pre-existing"):
                    reserve_package_outputs(graph, generators, rgal / "build/new")
                graph.unlink()
                generators.mkdir()
                with self.assertRaisesRegex(ValueError, "pre-existing"):
                    reserve_package_outputs(graph, generators, rgal / "build/new")

    def test_manual_schema_one_evidence_cannot_enter_release_audit(self):
        from tools.tests.audit_algorithm_coverage_matrix_tests import (
            create_evidence_fixture,
        )

        with tempfile.TemporaryDirectory() as directory:
            fixture = create_evidence_fixture(Path(directory))
            output = Path(directory) / "admission.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/audits/audit_algorithm_coverage_matrix.py"),
                    "--repository-root",
                    str(fixture["repository"]),
                    "--contract",
                    str(fixture["contract"]),
                    "--runtime-evidence",
                    str(fixture["runtime_evidence"]),
                    "--profile-report",
                    str(fixture["profile_report"]),
                    "--output-json",
                    str(output),
                    "--fail-on-blocked",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(2, result.returncode)
            self.assertIn("schema2 producer", output.read_text())

    def test_vendored_manifest_cannot_claim_full_external_eligibility(self):
        from tools.release.prepare_correction_corpus import prepare
        from tools.release.corpus_authority_manifest import (
            validate_frozen_corpus_manifest,
        )
        from tools.release.evidence_contract import load_contract, DEFAULT_CONTRACT_PATH

        source = ROOT / "tests/oracle/corpus/geometry"
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "derived"
            receipt = prepare(source, target, release_eligibility=False)
            with self.assertRaisesRegex(ValueError, "omits required"):
                validate_frozen_corpus_manifest(
                    Path(receipt["manifest"]),
                    source,
                    target,
                    receipt["sha256"],
                    load_contract(DEFAULT_CONTRACT_PATH),
                )

    def test_failed_step_cannot_consume_old_success_and_fresh_outputs_reject_old_junit(
        self,
    ):
        from tools.release.correction_run import Producer

        with tempfile.TemporaryDirectory() as directory:
            run = Producer(Path(directory), ROOT, {}, require_clean=False)
            junit = run.directory / "old.xml"
            junit.write_text('<testsuite><testcase name="old"/></testsuite>')
            with self.assertRaisesRegex(ValueError, "pre-existing"):
                run.step("old", [sys.executable, "-c", "pass"], outputs=[junit])
            with self.assertRaisesRegex(ValueError, "failed"):
                run.step("bad", [sys.executable, "-c", "raise SystemExit(9)"])
            with self.assertRaisesRegex(ValueError, "failed"):
                run.step("later", [sys.executable, "-c", 'print("PASS")'])

    def test_collector_rejects_unbound_old_artifacts(self):
        from tools.release.collect_release_runtime_evidence import (
            build_runtime_evidence,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "old.json").write_text("{}")
            with self.assertRaises(ValueError):
                build_runtime_evidence(
                    producer_manifest=root / "old.json", repository_root=ROOT
                )


class ArchiveInventoryContractTests(unittest.TestCase):
    def test_deleted_source_is_absent_and_old_actual_archive_is_rejected(self):
        from tools.release.source_inventory import public_archive_inventory, archive_record

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "source"
            root.mkdir()
            (root / "src").mkdir()
            (root / "conanfile.py").write_text('class Recipe:\n    exports_sources = ("src/*",)\n')
            (root / "src/current.cpp").write_text("int current;\n")
            obsolete = root / "src/obsolete.h"
            obsolete.write_text("int obsolete;\n")

            def git(*args):
                subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)

            git("init", "-q")
            git("config", "user.name", "fixture")
            git("config", "user.email", "fixture@example.invalid")
            git("add", ".")
            git("commit", "-qm", "initial fixture")
            old_archive = Path(directory) / "old.tar"
            git("archive", "--format=tar", "--output=" + str(old_archive), "HEAD")
            obsolete.unlink()
            current = public_archive_inventory(root)
            self.assertEqual({"conanfile.py", "src/current.cpp"}, set(current["archive_files"]))
            with self.assertRaisesRegex(ValueError, "unexpected"):
                archive_record(old_archive, root)
            git("add", "-u")
            git("commit", "-qm", "remove obsolete fixture")
            new_archive = Path(directory) / "current.tar"
            git("archive", "--format=tar", "--output=" + str(new_archive), "HEAD")
            self.assertEqual(set(current["archive_files"]), set(archive_record(new_archive, root)["files"]))


if __name__ == "__main__":
    unittest.main()
