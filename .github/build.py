import os
import sys
import platform
import subprocess
import cpt.packager

GCC = "gcc"
CLANG = "clang"
APPLE_CLANG = "apple-clang"
MSVC = "Visual Studio"

DEFAULT_VERSION = {GCC: "10", CLANG: "10", APPLE_CLANG: "11.0", MSVC: "16"}

LINUX = platform.system() == "Linux"
MACOS = platform.system() == "Darwin"
WIN = platform.system() == "Windows"


def get_packio_reference():
    this_dir = os.path.dirname(os.path.realpath(__file__))
    root = os.path.split(this_dir)[0]
    sys.path.insert(0, root)

    from conanfile import PackioConan

    return f"{PackioConan.name}/{PackioConan.version}"


def clear_local_cache():
    subprocess.check_call(["conan", "remove", "-f", get_packio_reference()])


# fmt: off


class Packager(cpt.packager.ConanMultiPackager):
    def __init__(self):
        super().__init__(build_policy="missing", archs=["x86_64"])

    def add(self, compiler, compiler_version, cppstd, build_type="Release",  settings=None, options=None):
        settings = settings or {}
        settings["compiler"] = compiler
        settings["compiler.version"] = compiler_version
        settings["build_type"] = build_type

        # Use header only libraries to avoid re-building dependencies all the time
        # NOTE: link issues on windows with boost header only
        options = options or {}
        if "asio" not in options:
            options.update(self._get_boost_options())
        options["msgpack:cpp_api"] = True
        options["msgpack:c_api"] = False
        options["cppstd"] = cppstd

        unity_batch = os.environ.get("UNITY_BATCH", None)
        if unity_batch:
            options["unity_batch"] = unity_batch

        env_vars = {}
        if compiler == GCC:
            env_vars.update({"CXX": f"g++-{compiler_version}", "CC": f"gcc-{compiler_version}"})
        elif compiler == CLANG:
            if compiler_version == "7.0":
                compiler_version = "7"
            env_vars.update({"CXX": f"clang++-{compiler_version}", "CC": f"clang-{compiler_version}"})

        super().add(settings, options, env_vars)

    @staticmethod
    def _get_boost_options():
        options = {}
        if not WIN:
            options["boost:header_only"] = True
        return options


def test_linux():
    # easier to use on my ubuntu dev machine
    os.environ["CONAN_PIP_COMMAND"] = "pip3"

    builder = Packager()

    # Test coroutines
    builder.add(compiler=CLANG, compiler_version="10", cppstd="20", settings={"compiler.libcxx": "libc++"}, options={"boost": "1.74.0", "coroutines": True})
    builder.add(compiler=CLANG, compiler_version="10", cppstd="20", settings={"compiler.libcxx": "libc++"}, options={"asio": "1.17.0", "packio:standalone_asio": True, "coroutines": True})

    # Test debug build
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", build_type="Debug")
    builder.add(compiler=CLANG, compiler_version="10", cppstd="20", build_type="Debug")

    # Test supported GCC versions
    builder.add(compiler=GCC, compiler_version="7", cppstd="17")
    builder.add(compiler=GCC, compiler_version="8", cppstd="17")
    builder.add(compiler=GCC, compiler_version="9", cppstd="17")
    builder.add(compiler=GCC, compiler_version="10", cppstd="20")

    # Test supported clang versions
    builder.add(compiler=CLANG, compiler_version="6.0", cppstd="17")
    builder.add(compiler=CLANG, compiler_version="7.0", cppstd="17")
    builder.add(compiler=CLANG, compiler_version="8", cppstd="17")
    builder.add(compiler=CLANG, compiler_version="9", cppstd="17")
    builder.add(compiler=CLANG, compiler_version="10", cppstd="20")

    # Test supported boost versions
    # NOTE: boost 1.72.0 and before are not compatible with C++20
    builder.add(compiler=GCC, compiler_version="10", cppstd="17", options={"boost": "1.70.0"})
    builder.add(compiler=GCC, compiler_version="10", cppstd="17", options={"boost": "1.71.0"})
    builder.add(compiler=GCC, compiler_version="10", cppstd="17", options={"boost": "1.72.0"})
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"boost": "1.73.0"})

    # Test supported asio versions
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"asio": "1.13.0", "packio:standalone_asio": True})
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"asio": "1.14.1", "packio:standalone_asio": True})
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"asio": "1.16.1", "packio:standalone_asio": True})
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"asio": "1.17.0", "packio:standalone_asio": True})

    # Test logs
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"loglevel": "trace"}, build_type="Debug")
    builder.add(compiler=GCC, compiler_version="10", cppstd="20", options={"loglevel": "trace"})

    builder.run()


def test_mac():
    builder = Packager()
    builder.add(compiler=APPLE_CLANG, compiler_version="11.0", cppstd="20", build_type="Debug")
    builder.add(compiler=APPLE_CLANG, compiler_version="11.0", cppstd="20")
    builder.add(compiler=APPLE_CLANG, compiler_version="11.0", cppstd="17", build_type="Debug")
    builder.add(compiler=APPLE_CLANG, compiler_version="11.0", cppstd="17")
    builder.run()


def test_windows():
    builder = Packager()
    builder.add(compiler=MSVC, compiler_version="16", cppstd="20", build_type="Debug")
    builder.add(compiler=MSVC, compiler_version="16", cppstd="20")
    builder.add(compiler=MSVC, compiler_version="16", cppstd="17", build_type="Debug")
    builder.add(compiler=MSVC, compiler_version="16", cppstd="17")
    builder.run()


# fmt: on


def main():
    clear_local_cache()

    if LINUX:
        test_linux()
    elif MACOS:
        test_mac()
    elif WIN:
        test_windows()
    else:
        print(f"Unknown platform: {platform.system()}")
        sys.exit(1)


if __name__ == "__main__":
    main()
