from __future__ import annotations

import importlib.util
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "build_install.py"


def load_build_install():
    if not MODULE_PATH.is_file():
        return None
    spec = importlib.util.spec_from_file_location(
        "clipper2next_build_install", MODULE_PATH
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuildInstallContractTests(unittest.TestCase):
    def test_release_identity_uses_consistent_public_metadata(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module, "build_install.py must own release orchestration")

        identity = module.read_release_identity(ROOT)

        self.assertEqual("clipper2next", identity.package_name)
        self.assertEqual("5.0.0", identity.version)
        self.assertEqual(
            "https://github.com/ansleliu/clipper2next",
            identity.public_repository,
        )

    def test_internal_reference_uses_build_type_channel(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "internal_reference"))
        identity = module.read_release_identity(ROOT)

        self.assertEqual(
            "clipper2next/5.0.0@company/debug",
            module.internal_reference(identity, "Debug", "company"),
        )
        self.assertEqual(
            "clipper2next/5.0.0@company/release",
            module.internal_reference(identity, "Release", "company"),
        )
        with self.assertRaises(ValueError):
            module.internal_reference(identity, "testing", "company")

    def test_center_reference_has_no_user_or_channel(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "center_reference"))
        identity = module.read_release_identity(ROOT)

        reference = module.center_reference(identity)

        self.assertEqual("clipper2next/5.0.0", reference)
        self.assertNotIn("@", reference)

    def test_center_source_rejects_private_or_unverified_archives(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "validate_center_source"))
        invalid = (
            ("http://github.com/example/clipper2next", "https://example.test/a.tar.gz", "a" * 64),
            ("https://192.0.2.10/clipper2next", "https://example.test/a.tar.gz", "a" * 64),
            ("https://github.com/example/clipper2next", "https://127.0.0.1/a.tar.gz", "a" * 64),
            ("https://github.com/example/clipper2next", "https://example.test/a.tar.gz", "not-a-sha256"),
        )
        for homepage, archive, sha256 in invalid:
            with self.subTest(homepage=homepage, archive=archive, sha256=sha256):
                with self.assertRaises(ValueError):
                    module.validate_center_source(homepage, archive, sha256)

    def test_center_stage_contains_a_complete_index_recipe_without_private_identity(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "stage_center_recipe"))
        identity = module.read_release_identity(ROOT)
        with tempfile.TemporaryDirectory() as directory:
            stage = module.stage_center_recipe(
                root=ROOT,
                destination=Path(directory),
                identity=identity,
                homepage="https://github.com/ansleliu/clipper2next",
                archive_url=(
                    "https://github.com/ansleliu/clipper2next/archive/refs/"
                    "tags/v5.0.0.tar.gz"
                ),
                archive_sha256="a" * 64,
            )

            expected = {
                "config.yml",
                "all/conandata.yml",
                "all/conanfile.py",
                "all/test_package/CMakeLists.txt",
                "all/test_package/conanfile.py",
                "all/test_package/main.cpp",
            }
            actual = {
                path.relative_to(stage).as_posix()
                for path in stage.rglob("*")
                if path.is_file()
            }
            self.assertEqual(expected, actual)
            combined = "\n".join(
                path.read_text(encoding="utf-8")
                for path in stage.rglob("*")
                if path.is_file()
            )
            self.assertIn('name = "clipper2next"', combined)
            self.assertIn('"5.0.0"', combined)
            self.assertIn("clipper2next::geotypes", combined)
            self.assertIn("clipper2next::clipper2next", combined)
            test_recipe = (
                stage / "all" / "test_package" / "conanfile.py"
            ).read_text(encoding="utf-8")
            center_recipe = (stage / "all" / "conanfile.py").read_text(
                encoding="utf-8"
            )
            self.assertIn(
                'CMakeToolchain(self, generator="Ninja")',
                test_recipe,
            )
            self.assertIn('cmake_layout(self, generator="Ninja")', test_recipe)
            self.assertIn('cmake_layout(self, generator="Ninja"', center_recipe)
            self.assertIn(
                'rmdir(self, os.path.join(self.package_folder, "share"))',
                center_recipe,
            )
            self.assertNotIn("company", combined)
            self.assertNotIn("testing", combined)

    def test_release_build_command_is_ninja_single_config_with_test_gates(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "cmake_configure_command"))

        command = module.cmake_configure_command(
            root=Path("C:/src/clipper2next"),
            build_directory=Path("C:/src/clipper2next/build/publish/Release"),
            install_prefix=Path("C:/src/clipper2next/install/Release"),
            configuration="Release",
            system_name="Windows",
            c_compiler=Path(r"C:\Program Files\VC\cl.exe"),
            cxx_compiler=Path(r"C:\Program Files\VC\cl.exe"),
        )

        self.assertEqual(["cmake", "-S"], command[:2])
        self.assertIn("Ninja", command)
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", command)
        self.assertIn("-DCLIPPER2NEXT_TESTS=ON", command)
        self.assertIn("-DCLIPPER2NEXT_BENCHMARKS=OFF", command)
        self.assertIn("-DCLIPPER2NEXT_WARNINGS_AS_ERRORS=ON", command)
        self.assertNotIn("--config", command)
        compiler_arguments = [
            value for value in command if value.startswith("-DCMAKE_C")
        ]
        self.assertTrue(compiler_arguments)
        self.assertTrue(all("\\" not in value for value in compiler_arguments))

    def test_linux_release_environment_owns_the_gcc13_compilers(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        toolchain = module.build_toolchain(
            "Linux",
            Path("/opt/gcc-13"),
            Path("/opt/g++-13"),
            dry_run=True,
        )

        self.assertIn("CC", toolchain.environment)
        self.assertIn("CXX", toolchain.environment)
        self.assertEqual(str(Path("/opt/gcc-13").resolve()), toolchain.environment["CC"])
        self.assertEqual(str(Path("/opt/g++-13").resolve()), toolchain.environment["CXX"])

    def test_internal_and_center_create_commands_keep_coordinates_separate(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "internal_create_command"))
        self.assertTrue(hasattr(module, "center_create_command"))
        identity = module.read_release_identity(ROOT)

        internal = module.internal_create_command(
            root=ROOT,
            identity=identity,
            configuration="Release",
            system_name="Windows",
            user="company",
        )
        center = module.center_create_command(
            recipe_root=Path("C:/stage/all"),
            identity=identity,
            configuration="Release",
            system_name="Windows",
        )

        self.assertIn("--user=company", internal)
        self.assertIn("--channel=release", internal)
        self.assertIn("--test-folder=" + str((ROOT / "packaging" / "test_package").resolve()), internal)
        self.assertNotIn("testing", " ".join(internal))
        self.assertIn("--version=5.0.0", center)
        self.assertFalse(any(value.startswith("--user") for value in center))
        self.assertFalse(any(value.startswith("--channel") for value in center))
        self.assertIn("--test-folder=" + str(Path("C:/stage/all/test_package").resolve()), center)

    def test_internal_upload_targets_only_the_internal_reference(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "internal_upload_command"))
        identity = module.read_release_identity(ROOT)

        command = module.internal_upload_command(
            identity=identity,
            configuration="Release",
            user="company",
            remote="private-repository",
        )

        self.assertEqual(
            [
                "conan",
                "upload",
                "clipper2next/5.0.0@company/release",
                "--remote=private-repository",
                "--check",
                "--force",
            ],
            command,
        )

    def test_install_validation_requires_shared_library_under_lib_and_no_bin(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "validate_install_tree"))
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            (prefix / "include" / "clipper2next").mkdir(parents=True)
            (prefix / "include" / "clipper2next" / "clipper.h").write_text(
                "#pragma once\n", encoding="utf-8"
            )
            (prefix / "lib" / "cmake" / "clipper2next").mkdir(parents=True)
            (prefix / "lib" / "clipper2next.dll").write_bytes(b"dll")
            (prefix / "lib" / "clipper2next.lib").write_bytes(b"lib")
            (prefix / "share").mkdir()

            artifact = module.validate_install_tree(
                prefix, "Windows", "Release"
            )

            self.assertEqual((prefix / "include").resolve(), artifact.include_directory)
            self.assertEqual((prefix / "lib").resolve(), artifact.library_directory)
            (prefix / "bin").mkdir()
            with self.assertRaises(ValueError):
                module.validate_install_tree(prefix, "Windows", "Release")

    def test_center_stage_may_live_only_in_the_owned_build_tree(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "checked_owned_path"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            owned = root / "build" / "publish"
            stage = owned / "conan-center-index" / "recipes" / "clipper2next"

            self.assertEqual(stage, module.checked_owned_path(stage, owned))
            for unsafe in (root, owned, root.parent):
                with self.subTest(unsafe=unsafe), self.assertRaises(ValueError):
                    module.checked_owned_path(unsafe, owned)

    def test_owned_cleanup_removes_readonly_fetch_content_files(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            owned = root / "build" / "publish"
            candidate = owned / "Release"
            readonly = candidate / "_deps" / "source" / ".git" / "objects" / "pack.idx"
            readonly.parent.mkdir(parents=True)
            readonly.write_bytes(b"generated")
            os.chmod(readonly, stat.S_IREAD)

            try:
                result = module.reset_owned_directory(candidate, owned)
            except PermissionError as error:
                self.fail(f"owned cleanup left a read-only generated file: {error}")

            self.assertEqual(candidate, result)
            self.assertTrue(candidate.is_dir())
            self.assertEqual([], list(candidate.iterdir()))

    def test_default_center_archive_is_the_public_version_tag(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "default_center_archive_url"))
        identity = module.read_release_identity(ROOT)

        self.assertEqual(
            "https://github.com/ansleliu/clipper2next/archive/refs/tags/"
            "v5.0.0.tar.gz",
            module.default_center_archive_url(identity),
        )

    def test_publish_requires_a_clean_source_commit(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "require_clean_repository"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(
                ["git", "-C", str(root), "config", "user.name", "Tester"],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(root),
                    "config",
                    "user.email",
                    "tester@example.invalid",
                ],
                check=True,
            )
            (root / "tracked.txt").write_text("clean\n", encoding="utf-8")
            subprocess.run(
                ["git", "-C", str(root), "add", "tracked.txt"], check=True
            )
            subprocess.run(
                ["git", "-C", str(root), "commit", "-qm", "baseline"],
                check=True,
            )

            commit = module.require_clean_repository(root)

            self.assertEqual(40, len(commit))
            (root / "tracked.txt").write_text("dirty\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                module.require_clean_repository(root)

    def test_cli_exposes_distinct_internal_and_center_release_actions(self) -> None:
        module = load_build_install()
        self.assertIsNotNone(module)
        self.assertTrue(hasattr(module, "parse_arguments"))

        internal = module.parse_arguments(
            ["publish-internal", "--config", "Release", "--dry-run"]
        )
        center = module.parse_arguments(
            [
                "prepare-center",
                "--config",
                "Release",
                "--center-archive-sha256",
                "a" * 64,
                "--dry-run",
            ]
        )

        self.assertEqual("publish-internal", internal.action)
        self.assertIsNone(internal.user)
        self.assertIsNone(internal.remote)
        self.assertEqual(["Release"], internal.configurations)
        self.assertEqual("prepare-center", center.action)
        self.assertEqual("a" * 64, center.center_archive_sha256)

    def test_dry_run_prints_complete_internal_and_center_workflows(self) -> None:
        common = [
            "--config",
            "Release",
            "--c-compiler",
            "C:/tool/cl.exe",
            "--cxx-compiler",
            "C:/tool/cl.exe",
            "--dry-run",
        ]
        internal = subprocess.run(
            [
                sys.executable,
                str(MODULE_PATH),
                "package-internal",
                "--user",
                "company",
                *common,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        center = subprocess.run(
            [
                sys.executable,
                str(MODULE_PATH),
                "prepare-center",
                "--center-archive-sha256",
                "a" * 64,
                *common,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(0, internal.returncode, internal.stdout + internal.stderr)
        self.assertIn("cmake", internal.stdout)
        self.assertIn("conan create", internal.stdout)
        self.assertIn("--channel=release", internal.stdout)
        self.assertNotIn("conan upload", internal.stdout)
        self.assertEqual(0, center.returncode, center.stdout + center.stderr)
        self.assertIn("Conan Center stage", center.stdout)
        self.assertIn("--version=5.0.0", center.stdout)
        self.assertNotIn("--user=", center.stdout)
        self.assertNotIn("--channel=", center.stdout)


if __name__ == "__main__":
    unittest.main()
