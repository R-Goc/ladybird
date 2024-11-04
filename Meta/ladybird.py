#!/usr/bin/env python3
"""Ladybird wrapper script"""
# pylint: disable=missing-function-docstring,global-statement,consider-using-f-string,line-too-long

import argparse
import sys
import os
import subprocess
import shutil
from pathlib import PurePath

# Check minimum version.
if sys.version_info < (3, 9):
    print("This build script only works with python 3.9+.")
    print("You have python {}.".format(sys.version))
    sys.exit(1)

arg0 = sys.argv[0]


def print_help():
    name = os.path.basename(sys.argv[0])
    print(f"""
Usage: {name} COMMAND [ARGS...]
  Supported COMMANDs:
    build:      Compiles the target binaries, [ARGS...] are passed through to ninja
    install:    Installs the target binary
    run:        {name} run EXECUTABLE [ARGS...]
                    Runs the EXECUTABLE on the build host, e.g.
                    'shell' or 'js', [ARGS...] are passed through to the executable
    gdb:        Same as run, but also starts a gdb remote session.
                {name} gdb EXECUTABLE [-ex 'any gdb command']...
                    Passes through '-ex' commands to gdb
    vcpkg:      Ensure that dependencies are available
    test:       {name} test [TEST_NAME_PATTERN]
                    Runs the unit tests on the build host, or if TEST_NAME_PATTERN
                    is specified tests matching it.
    delete:     Removes the build environment
    rebuild:    Deletes and re-creates the build environment, and compiles the project
    addr2line:  {name} addr2line BINARY_FILE ADDRESS
                    Resolves the ADDRESS in BINARY_FILE to a file:line. It will
                    attempt to find the BINARY_FILE in the appropriate build directory
    """)


def usage():
    print_help()
    sys.exit(1)


if len(sys.argv) == 1:
    usage()

cmd = sys.argv[1]
if cmd is None:
    usage()

if cmd == "help":
    print_help()
    sys.exit(0)

if os.name != "nt":
    # pylint: disable=no-member
    if os.geteuid() == 0:
        print(
            "Do not run ladybird.py as root, your Build directory will become root-owned"
        )
        sys.exit(1)

LADYBIRD_SOURCE_DIR = PurePath(
    subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True
    ).removesuffix("\n")
)
NUMBER_OF_PROCESSING_UNITS = os.cpu_count()
DIRECTORY = os.path.dirname(os.path.realpath(__file__))
BUILD_PRESET = os.environ.get("BUILD_PRESET", "default")
BUILD_DIR = PurePath()

cmake_args = []

pkgconfig = shutil.which("pkg-config")
if pkgconfig is None:
    print("Could not find pkg-config")
    sys.exit(1)
pkgconfig = PurePath(pkgconfig)
cmake_args.append(f"-DPKG_CONFIG_EXECUTABLE={pkgconfig.as_posix()}")
os.environ["PKGCONFIG"] = str(pkgconfig)


# caller must ensure target_path. Accepts a PurePath, will call str on it.
def add_to_path(path_to_dir):
    current_path = os.environ.get("PATH", "")
    new_path = str(path_to_dir) + os.pathsep + current_path
    os.environ["PATH"] = new_path


# TODO: Give it actual logic. Respect paths and versioning.
def get_compiler_args():
    args = []
    if os.name == "nt":
        cc_options = ["clang-cl"]
    else:
        cc_options = ["clang", "gcc"]
    for cc in cc_options.copy():
        if shutil.which(cc) is None:
            cc_options.remove(cc)
    if not cc_options:
        # TODO: Actually take compilers from env and match them to supported ones.
        print("No supported compiler found.")
        sys.exit(1)
    best_version = ""
    for cc in cc_options:
        subprocess.check_output([cc, "--version"])
    cc = cc_options[0]
    # TODO: make this make sense and respect absolute paths
    if cc_options[0] == "clang-cl":
        cxx = "clang-cl"
    if cc_options[0] == "clang":
        cxx = "clang++"
    # os.environ["CC"] = cc
    # os.environ["CXX"] = cxx
    args.append(f"-DCMAKE_C_COMPILER={cc}")
    args.append(f"-DCMAKE_CXX_COMPILER={cxx}")
    if os.name == "nt":
        args.append("-DCMAKE_LINKER=lld-link")
    return args


def cmd_with_target():
    global cmake_args
    global BUILD_DIR
    cmake_args += get_compiler_args()
    os.environ["LADYBIRD_SRC_DIR"] = str(LADYBIRD_SOURCE_DIR)

    if BUILD_PRESET == "default":
        BUILD_DIR = LADYBIRD_SOURCE_DIR / "Build" / "Ladybird"
    elif BUILD_PRESET == "debug":
        BUILD_DIR = LADYBIRD_SOURCE_DIR / "Build" / "Ladybird-debug"
    elif BUILD_PRESET == "sanitizer":
        BUILD_DIR = LADYBIRD_SOURCE_DIR / "Build" / "Ladybird-sanitizers"
    else:
        print("Unknown build preset")
        sys.exit(1)

    cmake_args.append(
        (
            f"-DCMAKE_INSTALL_PREFIX={LADYBIRD_SOURCE_DIR.as_posix()}/Build/ladybird-install-{BUILD_PRESET}"
        )
    )

    cmake_path = LADYBIRD_SOURCE_DIR / "Toolchain" / "Local" / "cmake" / "bin"
    add_to_path(cmake_path)

    vcpkg_path = LADYBIRD_SOURCE_DIR / "Toolchain" / "Local" / "vcpkg" / "bin"
    add_to_path(vcpkg_path)

    vcpkg_root = LADYBIRD_SOURCE_DIR / "Toolchain" / "Tarballs" / "vcpkg"
    os.environ["VCPKG_ROOT"] = str(vcpkg_root)


# TODO: Implement exception handling, with logging of errors
# function to build vcpkg, corresopnds to Toolchain/buildVcpkg.sh
def build_vcpkg():
    # no need to check for running as root, already done.
    vcpkg_git_repo = "https://github.com/microsoft/vcpkg.git"
    vcpkg_git_rev = "f7423ee180c4b7f40d43402c2feb3859161ef625"  # 2024.06.15
    vcpkg_git_tag = "2024.06.15"  # this needs keeping equal to line above, but makes first clone quicker
    toolchain_dir = LADYBIRD_SOURCE_DIR / "Toolchain"
    vcpkg_prefix_dir = toolchain_dir / "Local" / "vcpkg"

    os.makedirs(toolchain_dir / "Tarballs", exist_ok=True)

    os.chdir(toolchain_dir / "Tarballs")
    if not os.path.exists(toolchain_dir / "Tarballs" / "vcpkg"):
        subprocess.run(
            ["git", "clone", str(vcpkg_git_repo), "--branch", vcpkg_git_tag], check=True
        )
    else:
        bootstrapped_vcpkg_version = subprocess.check_output(
            ["git", "-C", "vcpkg", "rev-parse", "HEAD"], text=True
        ).removesuffix("\n")
        if bootstrapped_vcpkg_version == vcpkg_git_rev:
            os.chdir(LADYBIRD_SOURCE_DIR)
            return

    print("Building vcpkg")

    os.chdir(toolchain_dir / "Tarballs" / "vcpkg")
    subprocess.run(["git", "fetch", "origin"], check=True)
    subprocess.run(["git", "checkout", vcpkg_git_rev], check=True)

    subprocess.run(["bootstrap-vcpkg.bat", "-disableMetrics"], check=True)

    os.makedirs(vcpkg_prefix_dir / "bin", exist_ok=True)
    if os.name == "nt":
        shutil.copy("vcpkg.exe", vcpkg_prefix_dir / "bin")
    else:
        shutil.copy("vcpkg", vcpkg_prefix_dir / "bin")
    os.chdir(LADYBIRD_SOURCE_DIR)


def ensure_toolchain():
    build_vcpkg()


def create_build_dir(args):
    command = ["cmake", "--preset", BUILD_PRESET]
    command += cmake_args
    command += ["-S", LADYBIRD_SOURCE_DIR.as_posix(), "-B", BUILD_DIR.as_posix()]
    command += args.args
    print(command)
    subprocess.run(command, check=True)


def ensure_target(args):
    if not os.path.exists(BUILD_DIR / "build.ninja"):
        create_build_dir(args)
    return


def build(args):
    cmd_with_target()
    ensure_toolchain()
    ensure_target(args)
    command = ["cmake", "--build", BUILD_DIR]
    print(command)
    return command


def install(args):
    command = ["cmake", "--install", str(BUILD_DIR), args.args]
    return command


def run(args):
    command = [args.executable] + args.args
    return command


def gdb(args):
    debugger = shutil.which("gdb")
    if debugger is None:
        debugger = shutil.which("lldb")
        if debugger is None:
            print("Could not find gdb nor lldb.")
            sys.exit(1)
    command = ["{debugger}", "--args", args.executable] + args.args
    if args.ex_commands:
        for ex_command in args.ex_commands:
            command += ["-ex", ex_command]
    return command


def vcpkg(args):
    command = []
    return command


def test(args):
    command = ["echo"]
    if args.test_name_pattern:
        command += ""
    else:
        command += ""

    return command


def delete(args):
    command = [""]
    return command


def rebuild(args):
    command = [""]
    return command


def addr2line(args):
    command = [""]
    return command


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command")

    # Build
    parser_build = subparsers.add_parser("build")
    parser_build.add_argument("args", nargs=argparse.REMAINDER)
    parser_build.set_defaults(func=build)

    # Install
    parser_install = subparsers.add_parser("install")
    parser_install.set_defaults(func=install)

    # Run
    parser_run = subparsers.add_parser("run")
    parser_run.add_argument(
        "executable",
    )
    parser_run.add_argument("args", nargs=argparse.REMAINDER)
    parser_run.set_defaults(func=run)

    # Gdb
    parser_gdb = subparsers.add_parser("gdb")
    parser_gdb.add_argument("executable")
    parser_gdb.add_argument("-ex", dest="ex_commands", action="append")
    parser_gdb.set_defaults(func=gdb)

    # Vcpkg
    parser_vcpkg = subparsers.add_parser("vcpkg")
    parser_vcpkg.set_defaults(func=vcpkg)

    # Test
    parser_test = subparsers.add_parser("test")
    parser_test.add_argument("test_name_pattern", nargs="?")
    parser_test.set_defaults(func=test)

    # Delete
    parser_delete = subparsers.add_parser("delete")
    parser_delete.set_defaults(func=delete)

    # Rebuild
    parser_rebuild = subparsers.add_parser("rebuild")
    parser_rebuild.set_defaults(func=rebuild)

    # Addr2line
    parser_addr2line = subparsers.add_parser("addr2line")
    parser_addr2line.add_argument("binary_file")
    parser_addr2line.add_argument("address")
    parser_addr2line.set_defaults(func=addr2line)

    # Parse and execute
    args = parser.parse_args()
    if hasattr(args, "func"):
        args.func(args)
    else:
        usage()
    command = args.func(args)
    subprocess.run(command, check=False)


if __name__ == "__main__":
    main()
