set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Clipper2 2.0.1 is a test-only oracle and unconditionally enables -Werror.
# GCC diagnoses its PathsD triangulation error accumulator as maybe
# uninitialized. Preserve every warning and demote only that diagnostic from
# an error so the unmodified legacy oracle can be built and then checked by
# strict differential tests.
set(VCPKG_C_FLAGS "")
set(VCPKG_CXX_FLAGS "-Wno-error=maybe-uninitialized")
