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
        "unity_batch": "ANY",
        "build_samples": [True, False],
    }
    default_options = {
        "boost": None,
        "asio": None,
        "loglevel": None,
        "unity_batch": None,
        "build_samples": False,
    }

    def configure(self):
        self.options["gtest"].build_gmock = False
        self.options["packio"].standalone_asio = bool(self.options.asio)

    def requirements(self):
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
            (os.path.abspath("ssl_stream"), ""),
            (os.path.abspath("fibonacci"), "5"),
        ]:
            if not os.path.exists(path):
                continue
            self.run(f"{path} {args}", cwd=TEST_PACKAGE_DIR)
