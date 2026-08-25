import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, rmdir


class Clipper2NextConan(ConanFile):
    name = "clipper2next"
    version = "4.0.0"
    package_type = "shared-library"
    license = "BSL-1.0"
    description = "Standalone C++23 integer polygon geometry library"
    url = "https://github.com/ansleliu/clipper2next"
    homepage = "https://github.com/ansleliu/clipper2next"
    topics = ("geometry", "polygon-clipping", "c++23")

    settings = "os", "compiler", "build_type", "arch"
    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "src/*",
        "LICENSE_1_0.txt",
        "NOTICE.md",
    )

    def layout(self):
        cmake_layout(self, generator="Ninja")

    def validate(self):
        check_min_cppstd(self, "23")

    def generate(self):
        toolchain = CMakeToolchain(self, generator="Ninja")
        toolchain.variables["CLIPPER2NEXT_TESTS"] = False
        toolchain.variables["CLIPPER2NEXT_BENCHMARKS"] = False
        toolchain.variables["CLIPPER2NEXT_DEMOS"] = False
        toolchain.variables["CLIPPER2NEXT_QML_DEMO"] = False
        toolchain.variables["CLIPPER2NEXT_FETCH_DEPS"] = False
        toolchain.variables["CLIPPER2NEXT_CXX_STANDARD"] = "23"
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        license_directory = os.path.join(self.package_folder, "licenses")
        copy(self, "LICENSE_1_0.txt", self.source_folder, license_directory)
        copy(self, "NOTICE.md", self.source_folder, license_directory)
        rmdir(self, os.path.join(self.package_folder, "share"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "clipper2next")

        geotypes = self.cpp_info.components["geotypes"]
        geotypes.set_property("cmake_target_name", "clipper2next::geotypes")

        library = self.cpp_info.components["clipper2next"]
        library.libs = ["clipper2next"]
        library.bindirs = ["lib"]
        library.requires = ["geotypes"]
        library.set_property("cmake_target_name", "clipper2next::clipper2next")
