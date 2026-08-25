import json
import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CMakeLibraryTypeContractTests(unittest.TestCase):
    @staticmethod
    def _compiler_arguments() -> list[str]:
        if os.name == "nt":
            return []
        compiler = shutil.which("g++-13")
        if compiler is None:
            raise RuntimeError("Linux contract tests require g++-13")
        return [f"-DCMAKE_CXX_COMPILER={compiler}"]

    def test_conan_metadata_describes_the_single_shared_library_contract(self) -> None:
        conanfile = (ROOT / "conanfile.py").read_text(encoding="utf-8")
        self.assertIn('package_type = "shared-library"', conanfile)
        self.assertNotIn('toolchain.variables["BUILD_SHARED_LIBS"]', conanfile)
        self.assertIn('library.bindirs = ["lib"]', conanfile)
        self.assertIn(
            'rmdir(self, os.path.join(self.package_folder, "share"))',
            conanfile,
        )

    def test_linux_oracle_triplet_only_demotes_legacy_maybe_uninitialized(self) -> None:
        triplet_path = (
            ROOT / "cmake" / "oracle" / "vcpkg-triplets" / "x64-linux.cmake"
        )
        self.assertTrue(triplet_path.is_file())
        triplet = triplet_path.read_text(encoding="utf-8")
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        linux = next(
            preset
            for preset in presets["configurePresets"]
            if preset["name"] == "linux-gcc-oracle"
        )

        self.assertIn(
            'set(VCPKG_CXX_FLAGS "-Wno-error=maybe-uninitialized")',
            triplet,
        )
        self.assertIn('set(VCPKG_C_FLAGS "")', triplet)
        self.assertNotIn("-Wno-error ", triplet)
        self.assertEqual(
            "${sourceDir}/cmake/oracle/vcpkg-triplets",
            linux["cacheVariables"]["VCPKG_OVERLAY_TRIPLETS"],
        )

    @staticmethod
    def _write_consumer(source: Path, body: str) -> None:
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            textwrap.dedent(
                f"""\
                cmake_minimum_required(VERSION 3.24)
                project(clipper2next_cmake_contract LANGUAGES CXX)

                set(CLIPPER2NEXT_TESTS OFF CACHE BOOL "" FORCE)
                set(CLIPPER2NEXT_BENCHMARKS OFF CACHE BOOL "" FORCE)
                set(CLIPPER2NEXT_FETCH_DEPS OFF CACHE BOOL "" FORCE)
                add_subdirectory("{ROOT.as_posix()}" clipper2next)

                {body}
                """
            ),
            encoding="utf-8",
        )

    def test_build_shared_libs_cannot_turn_the_product_into_a_static_library(self) -> None:
        with tempfile.TemporaryDirectory(prefix="clipper2next-library-type-") as value:
            temporary = Path(value)
            source = temporary / "source"
            build = temporary / "build"
            self._write_consumer(
                source,
                """
                    set(BUILD_SHARED_LIBS OFF)
                    get_target_property(library_type clipper2next TYPE)
                    if(NOT library_type STREQUAL "SHARED_LIBRARY")
                      message(FATAL_ERROR
                        "clipper2next must remain SHARED when BUILD_SHARED_LIBS=OFF; "
                        "actual type: ${library_type}")
                    endif()
                """,
            )

            completed = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    *self._compiler_arguments(),
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )

        self.assertEqual(
            completed.returncode,
            0,
            msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
        )

    def test_non_ninja_generator_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="clipper2next-generator-") as value:
            temporary = Path(value)
            source = temporary / "source"
            build = temporary / "build"
            self._write_consumer(source, "")

            completed = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja Multi-Config",
                    *self._compiler_arguments(),
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "clipper2next requires the Ninja generator",
            completed.stdout + completed.stderr,
        )

    def test_runtime_artifacts_share_one_bin_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="clipper2next-runtime-layout-") as value:
            temporary = Path(value)
            source = temporary / "source"
            build = temporary / "build"
            self._write_consumer(
                source,
                """
                    get_target_property(runtime_output clipper2next RUNTIME_OUTPUT_DIRECTORY)
                    if(NOT runtime_output STREQUAL "${CMAKE_BINARY_DIR}/bin")
                      message(FATAL_ERROR
                        "clipper2next runtime artifacts must share ${CMAKE_BINARY_DIR}/bin; "
                        "actual path: ${runtime_output}")
                    endif()
                """,
            )

            completed = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    *self._compiler_arguments(),
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )

        self.assertEqual(
            completed.returncode,
            0,
            msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
        )


if __name__ == "__main__":
    unittest.main()
