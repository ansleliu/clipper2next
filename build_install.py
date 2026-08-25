from __future__ import annotations

import argparse
import ipaddress
import json
import os
import platform
import re
import shutil
import stat
import subprocess
import time
from pathlib import Path
from typing import Mapping, NamedTuple, Sequence
from urllib.parse import urlparse


PACKAGE_NAME = "clipper2next"
CONFIGURATIONS = ("Debug", "Release")
MINIMUM_CONAN_VERSION = (2, 20, 0)
PUBLIC_REPOSITORY = "https://github.com/ansleliu/clipper2next"

CMAKE_VERSION_PATTERN = re.compile(
    r"project\s*\(\s*clipper2next\s+VERSION\s+"
    r"(?P<version>[0-9]+\.[0-9]+\.[0-9]+)"
)
HEADER_VERSION_PATTERN = re.compile(
    r'CLIPPER2NEXT_VERSION\s*=\s*"(?P<version>[^"]+)"'
)
CONAN_VERSION_PATTERN = re.compile(
    r'^\s*version\s*=\s*"(?P<version>[^"]+)"\s*$', re.MULTILINE
)
CONAN_CLIENT_VERSION_PATTERN = re.compile(
    r"Conan version (?P<major>[0-9]+)\.(?P<minor>[0-9]+)\."
    r"(?P<patch>[0-9]+)"
)


class ReleaseIdentity(NamedTuple):
    package_name: str
    version: str
    public_repository: str


class CenterSource(NamedTuple):
    homepage: str
    archive_url: str
    archive_sha256: str


class InstalledArtifact(NamedTuple):
    prefix: Path
    include_directory: Path
    library_directory: Path


class BuildToolchain(NamedTuple):
    environment: dict[str, str]
    c_compiler: Path
    cxx_compiler: Path


def _matched_version(path: Path, pattern: re.Pattern[str]) -> str | None:
    match = pattern.search(path.read_text(encoding="utf-8"))
    return match.group("version") if match is not None else None


def validate_install_tree(
    prefix: Path,
    system_name: str,
    configuration: str,
) -> InstalledArtifact:
    if configuration not in CONFIGURATIONS:
        raise ValueError(f"unsupported configuration: {configuration}")
    if system_name not in {"Windows", "Linux"}:
        raise ValueError(f"unsupported platform: {system_name}")
    resolved = prefix.resolve()
    entries = {entry.name for entry in resolved.iterdir()}
    if entries != {"include", "lib", "share"}:
        raise ValueError(
            "clipper2next install prefix must contain only include/, lib/, "
            f"and share/; found {sorted(entries)}"
        )
    include_directory = resolved / "include"
    library_directory = resolved / "lib"
    if not (
        include_directory / "clipper2next" / "clipper.h"
    ).is_file():
        raise ValueError("clipper2next public headers are incomplete")
    if not (
        library_directory / "cmake" / "clipper2next"
    ).is_dir():
        raise ValueError("clipper2next CMake package is missing")
    if system_name == "Windows":
        required = ("clipper2next.dll", "clipper2next.lib")
        if any(not (library_directory / name).is_file() for name in required):
            raise ValueError("clipper2next Windows shared library pair is incomplete")
    elif not any(library_directory.glob("libclipper2next.so*")):
        raise ValueError("clipper2next Linux shared library is missing")
    return InstalledArtifact(
        prefix=resolved,
        include_directory=include_directory,
        library_directory=library_directory,
    )


def read_release_identity(root: Path) -> ReleaseIdentity:
    project_root = root.resolve()
    versions = {
        "CMakeLists.txt": _matched_version(
            project_root / "CMakeLists.txt", CMAKE_VERSION_PATTERN
        ),
        "version.h": _matched_version(
            project_root / "include" / "clipper2next" / "version.h",
            HEADER_VERSION_PATTERN,
        ),
        "vcpkg.json": json.loads(
            (project_root / "vcpkg.json").read_text(encoding="utf-8")
        ).get("version-string"),
        "conanfile.py": _matched_version(
            project_root / "conanfile.py", CONAN_VERSION_PATTERN
        ),
    }
    if any(value is None for value in versions.values()) or len(
        set(versions.values())
    ) != 1:
        raise ValueError(f"clipper2next release version mismatch: {versions}")
    return ReleaseIdentity(
        package_name=PACKAGE_NAME,
        version=next(iter(versions.values())),
        public_repository=PUBLIC_REPOSITORY,
    )


def internal_reference(
    identity: ReleaseIdentity,
    configuration: str,
    user: str,
) -> str:
    if configuration not in CONFIGURATIONS:
        raise ValueError(f"unsupported configuration: {configuration}")
    if not user:
        raise ValueError("internal Conan user must not be empty")
    return (
        f"{identity.package_name}/{identity.version}@"
        f"{user}/{configuration.lower()}"
    )


def center_reference(identity: ReleaseIdentity) -> str:
    return f"{identity.package_name}/{identity.version}"


def default_center_archive_url(identity: ReleaseIdentity) -> str:
    return (
        f"{identity.public_repository}/archive/refs/tags/"
        f"v{identity.version}.tar.gz"
    )


def _validate_public_https_url(value: str, field: str) -> str:
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.hostname or not parsed.path:
        raise ValueError(f"{field} must be an absolute public HTTPS URL")
    if parsed.username is not None or parsed.password is not None:
        raise ValueError(f"{field} must not contain credentials")
    hostname = parsed.hostname.casefold()
    if hostname == "localhost" or hostname.endswith(".local"):
        raise ValueError(f"{field} must not reference a local host")
    try:
        address = ipaddress.ip_address(hostname)
    except ValueError:
        return value
    if not address.is_global:
        raise ValueError(f"{field} must not reference a private IP address")
    return value


def validate_center_source(
    homepage: str,
    archive_url: str,
    archive_sha256: str,
) -> CenterSource:
    validated_homepage = _validate_public_https_url(homepage, "homepage")
    validated_archive = _validate_public_https_url(
        archive_url, "archive URL"
    )
    if re.fullmatch(r"[0-9a-fA-F]{64}", archive_sha256) is None:
        raise ValueError("archive SHA-256 must contain exactly 64 hex digits")
    return CenterSource(
        homepage=validated_homepage,
        archive_url=validated_archive,
        archive_sha256=archive_sha256.lower(),
    )


def _render_template(path: Path, replacements: dict[str, str]) -> str:
    rendered = path.read_text(encoding="utf-8")
    for token, value in replacements.items():
        rendered = rendered.replace(f"@{token}@", value)
    unresolved = sorted(set(re.findall(r"@[A-Z][A-Z0-9_]*@", rendered)))
    if unresolved:
        raise ValueError(
            f"unresolved Conan Center template tokens in {path}: {unresolved}"
        )
    return rendered


def stage_center_recipe(
    *,
    root: Path,
    destination: Path,
    identity: ReleaseIdentity,
    homepage: str,
    archive_url: str,
    archive_sha256: str,
    owned_root: Path | None = None,
) -> Path:
    project_root = root.resolve()
    stage = destination.resolve()
    source = validate_center_source(homepage, archive_url, archive_sha256)
    if homepage != identity.public_repository:
        raise ValueError(
            "Conan Center homepage must match the public release identity"
        )
    if stage == project_root:
        raise ValueError("Conan Center stage must not overwrite the source tree")
    if owned_root is not None:
        checked_owned_path(stage, owned_root)
    if stage.exists() and any(stage.iterdir()):
        raise ValueError(f"Conan Center stage is not empty: {stage}")

    template_root = project_root / "packaging" / "conan-center"
    recipe_root = stage / "all"
    test_package = recipe_root / "test_package"
    test_package.mkdir(parents=True, exist_ok=True)
    replacements = {
        "VERSION": identity.version,
        "HOMEPAGE": source.homepage,
        "ARCHIVE_URL": source.archive_url,
        "ARCHIVE_SHA256": source.archive_sha256,
    }
    rendered_files = {
        stage / "config.yml": template_root / "config.yml.in",
        recipe_root / "conandata.yml": template_root / "conandata.yml.in",
        recipe_root / "conanfile.py": template_root / "conanfile.py.in",
    }
    for target, template in rendered_files.items():
        target.write_text(
            _render_template(template, replacements),
            encoding="utf-8",
            newline="\n",
        )
    common_test_package = project_root / "packaging" / "test_package"
    for name in ("CMakeLists.txt", "conanfile.py", "main.cpp"):
        shutil.copy2(common_test_package / name, test_package / name)
    return stage


def checked_owned_path(candidate: Path, owned_root: Path) -> Path:
    resolved_candidate = candidate.resolve()
    resolved_root = owned_root.resolve()
    if resolved_candidate == resolved_root or not resolved_candidate.is_relative_to(
        resolved_root
    ):
        raise ValueError(
            f"refusing to operate outside the owned root {resolved_root}: "
            f"{resolved_candidate}"
        )
    return resolved_candidate


def _remove_readonly_generated_file(
    function,
    path: str,
    error_info,
) -> None:
    error = error_info[1]
    if not isinstance(error, PermissionError):
        raise error
    os.chmod(path, stat.S_IWRITE)
    function(path)


def reset_owned_directory(path: Path, owned_root: Path) -> Path:
    checked = checked_owned_path(path, owned_root)
    if checked.exists():
        shutil.rmtree(checked, onerror=_remove_readonly_generated_file)
    checked.mkdir(parents=True)
    return checked


def require_clean_repository(root: Path) -> str:
    repository = root.resolve()
    status = subprocess.run(
        [
            "git",
            "-C",
            str(repository),
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if status:
        raise RuntimeError(
            "Conan publication requires a clean Git repository:\n" + status
        )
    return subprocess.run(
        ["git", "-C", str(repository), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def cmake_configure_command(
    *,
    root: Path,
    build_directory: Path,
    install_prefix: Path,
    configuration: str,
    system_name: str,
    c_compiler: Path,
    cxx_compiler: Path,
) -> list[str]:
    if configuration not in CONFIGURATIONS:
        raise ValueError(f"unsupported configuration: {configuration}")
    if system_name not in {"Windows", "Linux"}:
        raise ValueError(f"unsupported platform: {system_name}")

    def compiler_path(path: Path) -> str:
        value = str(path)
        return value.replace("\\", "/") if system_name == "Windows" else value

    return [
        "cmake",
        "-S",
        str(root.resolve()),
        "-B",
        str(build_directory.resolve()),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={configuration}",
        f"-DCMAKE_INSTALL_PREFIX={install_prefix.resolve()}",
        f"-DCMAKE_C_COMPILER={compiler_path(c_compiler)}",
        f"-DCMAKE_CXX_COMPILER={compiler_path(cxx_compiler)}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCLIPPER2NEXT_TESTS=ON",
        "-DCLIPPER2NEXT_BENCHMARKS=OFF",
        "-DCLIPPER2NEXT_DEMOS=OFF",
        "-DCLIPPER2NEXT_QML_DEMO=OFF",
        "-DCLIPPER2NEXT_FETCH_DEPS=ON",
        "-DCLIPPER2NEXT_BUILD_ORACLE=OFF",
        "-DCLIPPER2NEXT_WARNINGS_AS_ERRORS=ON",
        "-DCLIPPER2NEXT_CXX_STANDARD=23",
    ]


def conan_settings(configuration: str, system_name: str) -> list[str]:
    if configuration not in CONFIGURATIONS:
        raise ValueError(f"unsupported configuration: {configuration}")
    if system_name not in {"Windows", "Linux"}:
        raise ValueError(f"unsupported platform: {system_name}")
    settings = [
        "-s",
        f"os={system_name}",
        "-s",
        "arch=x86_64",
        "-s",
        f"build_type={configuration}",
        "-s",
        "compiler.cppstd=23",
    ]
    if system_name == "Windows":
        settings.extend(
            [
                "-s",
                "compiler.runtime=dynamic",
                "-s",
                f"compiler.runtime_type={configuration}",
            ]
        )
    return settings


def internal_create_command(
    *,
    root: Path,
    identity: ReleaseIdentity,
    configuration: str,
    system_name: str,
    user: str,
) -> list[str]:
    internal_reference(identity, configuration, user)
    return [
        "conan",
        "create",
        str(root.resolve()),
        f"--user={user}",
        f"--channel={configuration.lower()}",
        f"--test-folder={(root / 'packaging' / 'test_package').resolve()}",
        "--build=missing",
        *conan_settings(configuration, system_name),
    ]


def center_create_command(
    *,
    recipe_root: Path,
    identity: ReleaseIdentity,
    configuration: str,
    system_name: str,
) -> list[str]:
    return [
        "conan",
        "create",
        str(recipe_root.resolve()),
        f"--version={identity.version}",
        f"--test-folder={(recipe_root / 'test_package').resolve()}",
        "--build=missing",
        *conan_settings(configuration, system_name),
    ]


def internal_upload_command(
    *,
    identity: ReleaseIdentity,
    configuration: str,
    user: str,
    remote: str,
) -> list[str]:
    if not remote:
        raise ValueError("internal Conan remote must not be empty")
    return [
        "conan",
        "upload",
        internal_reference(identity, configuration, user),
        f"--remote={remote}",
        "--check",
        "--force",
    ]


def parse_arguments(
    argv: Sequence[str] | None = None,
) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build, test, install, package, and prepare clipper2next for "
            "the internal JFrog Conan remote or Conan Center."
        )
    )
    parser.add_argument(
        "action",
        choices=(
            "build",
            "package-internal",
            "publish-internal",
            "prepare-center",
        ),
    )
    parser.add_argument(
        "--config",
        action="append",
        choices=CONFIGURATIONS,
        dest="configurations",
    )
    parser.add_argument(
        "--build-root", type=Path, default=Path("build/publish")
    )
    parser.add_argument("--install-root", type=Path, default=Path("install"))
    parser.add_argument("--user")
    parser.add_argument("--remote")
    parser.add_argument("--center-homepage")
    parser.add_argument("--center-archive-url")
    parser.add_argument("--center-archive-sha256")
    parser.add_argument("--c-compiler", type=Path)
    parser.add_argument("--cxx-compiler", type=Path)
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def command_text(command: Sequence[str | Path]) -> str:
    return subprocess.list2cmdline([str(argument) for argument in command])


def run_command(
    command: Sequence[str | Path],
    *,
    cwd: Path,
    environment: Mapping[str, str],
    dry_run: bool = False,
) -> None:
    print(f"Working directory: {cwd}", flush=True)
    print(f"Running: {command_text(command)}", flush=True)
    if dry_run:
        return
    started = time.perf_counter()
    subprocess.run(
        [str(argument) for argument in command],
        cwd=cwd,
        env=dict(environment),
        check=True,
    )
    print(f"Elapsed: {time.perf_counter() - started:.2f} seconds", flush=True)


def require_program(name: str, environment: Mapping[str, str]) -> None:
    if shutil.which(name, path=environment.get("PATH")) is None:
        raise RuntimeError(f"required program is not on PATH: {name}")


def parse_conan_client_version(output: str) -> tuple[int, int, int]:
    match = CONAN_CLIENT_VERSION_PATTERN.fullmatch(output.strip())
    if match is None:
        raise RuntimeError(f"cannot parse Conan version: {output.strip()!r}")
    return tuple(
        int(match.group(name)) for name in ("major", "minor", "patch")
    )


def require_conan_version(environment: Mapping[str, str]) -> None:
    completed = subprocess.run(
        ["conan", "--version"],
        check=True,
        capture_output=True,
        text=True,
        env=dict(environment),
    )
    actual = parse_conan_client_version(completed.stdout)
    if actual < MINIMUM_CONAN_VERSION:
        required = ".".join(str(value) for value in MINIMUM_CONAN_VERSION)
        found = ".".join(str(value) for value in actual)
        raise RuntimeError(
            f"clipper2next publication requires Conan {required} or newer; "
            f"found {found}"
        )


def visual_studio_environment(
    environment: Mapping[str, str],
) -> dict[str, str]:
    configured = dict(environment)
    program_files_x86 = configured.get("ProgramFiles(x86)")
    if not program_files_x86:
        program_files_x86 = str(
            Path(configured.get("SystemDrive", "C:")) / "Program Files (x86)"
        )
    vswhere = (
        Path(program_files_x86)
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.is_file():
        raise RuntimeError(f"Visual Studio locator was not found: {vswhere}")
    installation = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        check=True,
        capture_output=True,
        text=True,
        env=configured,
    ).stdout.strip()
    if not installation:
        raise RuntimeError("Visual Studio 2022 C++ tools were not found")
    developer_command = (
        Path(installation) / "Common7" / "Tools" / "VsDevCmd.bat"
    )
    if not developer_command.is_file():
        raise RuntimeError(
            f"Visual Studio environment script was not found: {developer_command}"
        )
    output = subprocess.run(
        (
            f'cmd.exe /d /c ""{developer_command}" '
            '-arch=amd64 -host_arch=amd64 >nul && set"'
        ),
        check=True,
        capture_output=True,
        text=True,
        env=configured,
    ).stdout
    for line in output.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            configured[name] = value
    return configured


def build_toolchain(
    system_name: str,
    c_compiler: Path | None = None,
    cxx_compiler: Path | None = None,
    *,
    dry_run: bool = False,
) -> BuildToolchain:
    environment = os.environ.copy()
    if system_name == "Windows":
        if not dry_run or c_compiler is None or cxx_compiler is None:
            environment = visual_studio_environment(environment)
        compiler = shutil.which("cl.exe", path=environment.get("PATH"))
        if not dry_run and compiler is None:
            raise RuntimeError("MSVC compiler was not configured by VsDevCmd")
        fallback = Path(compiler) if compiler is not None else Path("cl.exe")
        resolved_c = c_compiler or fallback
        resolved_cxx = cxx_compiler or fallback
    elif system_name == "Linux":
        resolved_c = c_compiler or Path("/usr/bin/gcc-13")
        resolved_cxx = cxx_compiler or Path("/usr/bin/g++-13")
        if not dry_run and (
            not resolved_c.is_file() or not resolved_cxx.is_file()
        ):
            raise RuntimeError(
                "Linux qualification requires /usr/bin/gcc-13 and /usr/bin/g++-13"
            )
    else:
        raise RuntimeError("clipper2next supports Windows and Linux only")
    if not dry_run:
        for program in ("cmake", "ctest", "ninja", "conan"):
            require_program(program, environment)
        require_conan_version(environment)
    resolved_c_path = Path(resolved_c).resolve()
    resolved_cxx_path = Path(resolved_cxx).resolve()
    if system_name == "Linux":
        environment["CC"] = str(resolved_c_path)
        environment["CXX"] = str(resolved_cxx_path)
    return BuildToolchain(
        environment=environment,
        c_compiler=resolved_c_path,
        cxx_compiler=resolved_cxx_path,
    )


def build_and_install(
    *,
    root: Path,
    build_root: Path,
    install_root: Path,
    configuration: str,
    system_name: str,
    toolchain: BuildToolchain,
    clean: bool,
    dry_run: bool,
) -> InstalledArtifact | None:
    build_directory = build_root / configuration
    install_prefix = install_root / configuration
    if clean and not dry_run:
        reset_owned_directory(build_directory, build_root)
        reset_owned_directory(install_prefix, install_root)
    elif not dry_run:
        build_directory.mkdir(parents=True, exist_ok=True)
        install_prefix.mkdir(parents=True, exist_ok=True)
    run_command(
        cmake_configure_command(
            root=root,
            build_directory=build_directory,
            install_prefix=install_prefix,
            configuration=configuration,
            system_name=system_name,
            c_compiler=toolchain.c_compiler,
            cxx_compiler=toolchain.cxx_compiler,
        ),
        cwd=root,
        environment=toolchain.environment,
        dry_run=dry_run,
    )
    run_command(
        ["cmake", "--build", str(build_directory.resolve()), "--parallel"],
        cwd=root,
        environment=toolchain.environment,
        dry_run=dry_run,
    )
    run_command(
        [
            "ctest",
            "--test-dir",
            str(build_directory.resolve()),
            "--output-on-failure",
            "--no-tests=error",
        ],
        cwd=root,
        environment=toolchain.environment,
        dry_run=dry_run,
    )
    run_command(
        ["cmake", "--install", str(build_directory.resolve())],
        cwd=root,
        environment=toolchain.environment,
        dry_run=dry_run,
    )
    if dry_run:
        return None
    return validate_install_tree(install_prefix, system_name, configuration)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    root = Path(__file__).resolve().parent
    system_name = platform.system()
    configurations = arguments.configurations or list(CONFIGURATIONS)
    build_root = (root / arguments.build_root).resolve()
    install_root = (root / arguments.install_root).resolve()
    identity = read_release_identity(root)
    if arguments.action in {"package-internal", "publish-internal"} and not arguments.user:
        raise ValueError(
            f"{arguments.action} requires an explicit --user"
        )
    if arguments.action == "publish-internal" and not arguments.remote:
        raise ValueError("publish-internal requires an explicit --remote")
    toolchain = build_toolchain(
        system_name,
        arguments.c_compiler,
        arguments.cxx_compiler,
        dry_run=arguments.dry_run,
    )
    if not arguments.dry_run:
        build_root.mkdir(parents=True, exist_ok=True)
        install_root.mkdir(parents=True, exist_ok=True)

    if arguments.action == "publish-internal" and not arguments.dry_run:
        commit = require_clean_repository(root)
        print(f"Publishing clean source commit {commit}", flush=True)

    center_stage = (
        build_root
        / "conan-center-index"
        / "recipes"
        / identity.package_name
    )
    if arguments.action == "prepare-center":
        if not arguments.center_archive_sha256:
            raise ValueError(
                "prepare-center requires --center-archive-sha256"
            )
        homepage = arguments.center_homepage or identity.public_repository
        archive_url = (
            arguments.center_archive_url
            or default_center_archive_url(identity)
        )
        validate_center_source(
            homepage,
            archive_url,
            arguments.center_archive_sha256,
        )
        if arguments.dry_run:
            print(f"Conan Center stage: {center_stage}", flush=True)
        else:
            reset_owned_directory(center_stage, build_root)
            stage_center_recipe(
                root=root,
                destination=center_stage,
                identity=identity,
                homepage=homepage,
                archive_url=archive_url,
                archive_sha256=arguments.center_archive_sha256,
                owned_root=build_root,
            )
            print(f"Conan Center stage: {center_stage}", flush=True)

    for configuration in configurations:
        build_and_install(
            root=root,
            build_root=build_root,
            install_root=install_root,
            configuration=configuration,
            system_name=system_name,
            toolchain=toolchain,
            clean=(
                arguments.clean
                or arguments.action in {"publish-internal", "prepare-center"}
            ),
            dry_run=arguments.dry_run,
        )
        if arguments.action in {"package-internal", "publish-internal"}:
            run_command(
                internal_create_command(
                    root=root,
                    identity=identity,
                    configuration=configuration,
                    system_name=system_name,
                    user=arguments.user,
                ),
                cwd=root,
                environment=toolchain.environment,
                dry_run=arguments.dry_run,
            )
        if arguments.action == "publish-internal":
            run_command(
                internal_upload_command(
                    identity=identity,
                    configuration=configuration,
                    user=arguments.user,
                    remote=arguments.remote,
                ),
                cwd=root,
                environment=toolchain.environment,
                dry_run=arguments.dry_run,
            )
        if arguments.action == "prepare-center":
            run_command(
                center_create_command(
                    recipe_root=center_stage / "all",
                    identity=identity,
                    configuration=configuration,
                    system_name=system_name,
                ),
                cwd=root,
                environment=toolchain.environment,
                dry_run=arguments.dry_run,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
