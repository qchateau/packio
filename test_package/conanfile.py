import os

from conans import ConanFile, CMake, tools

TEST_PACKAGE_DIR = os.path.dirname(os.path.realpath(__file__))


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = ["gtest/1.10.0", "openssl/1.1.1i"]
    options = {
        "boost": "ANY",
        "asio": "ANY",
        "loglevel": [None, "trace", "debug", "info", "warn", "error"],
        "cppstd": ["17", "20"],
        "unity_batch": "ANY",
        "build_samples": [None, True, False],
    }
    default_options = {
        "boost": None,
        "asio": None,
        "loglevel": None,
        "cppstd": "17",
        "unity_batch": None,
        "build_samples": None,
    }

    def configure(self):
        self.options["gtest"].build_gmock = False
        self.options["packio"].standalone_asio = bool(self.options.asio)

    def requirements(self):
        if self.options.build_samples is None:
            self.options.build_samples = self._can_build_samples()
        if self.options.boost:
            self.requires("boost/{}".format(self.options.boost))
        if self.options.asio:
            self.requires("asio/{}".format(self.options.asio))
        if self.options.loglevel:
            self.requires("spdlog/1.4.2")

    def build(self):
        cmake = CMake(self)
        defs = dict()
        if self.options.loglevel:
            defs["PACKIO_LOGGING"] = self.options.loglevel
        # dont use the compiler setting, it breaks pre-built binaries
        defs["CMAKE_CXX_STANDARD"] = self.options.cppstd

        if self.options.unity_batch:
            defs["CMAKE_UNITY_BUILD"] = "1"
            defs["CMAKE_UNITY_BUILD_BATCH_SIZE"] = str(self.options.unity_batch)
        if self.options.build_samples:
            defs["BUILD_SAMPLES"] = "1"
        cmake.configure(defs=defs)
        cmake.build()

    def test(self):
        if tools.cross_building(self.settings):
            return

        os.chdir("bin")
        for path, args in [
            (os.path.abspath("tests"), ""),
            (os.path.abspath("basic"), ""),
            (os.path.abspath("ssl"), ""),
            (os.path.abspath("fibonacci"), "5"),
        ]:
            if not os.path.exists(path):
                continue
            self.run(f"{path} {args}", cwd=TEST_PACKAGE_DIR)

    def _can_build_samples(self):
        if self.options.cppstd == "17":
            return False
        if self.options.boost and self._numeric_version(
            self.options.boost
        ) < self._numeric_version("1.74.0"):
            return False
        if self.options.asio and self._numeric_version(
            self.options.asio
        ) < self._numeric_version("1.17.0"):
            return False
        return True

    @staticmethod
    def _numeric_version(ver_str):
        major, minor, patch = ver_str.split(".")
        return major * 1e6 + minor * 1e3 + patch
