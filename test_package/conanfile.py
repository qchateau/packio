import os

from conans import ConanFile, CMake, tools


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = ["gtest/1.10.0"]
    options = {
        "boost": "ANY",
        "asio": "ANY",
        "coroutines": [True, False],
        "loglevel": [None, "trace", "debug", "info", "warn", "error"],
        "cppstd": ["17", "20"],
    }
    default_options = {
        "boost": None,
        "asio": None,
        "coroutines": False,
        "loglevel": None,
        "cppstd": "17",
    }

    def configure(self):
        self.options["gtest"].build_gmock = False

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
        if self.options.asio:
            defs["PACKIO_STANDALONE_ASIO"] = "1"
        if self.options.coroutines:
            defs["PACKIO_COROUTINES"] = "1"
        # dont use the compiler setting, it breaks pre-built binaries
        defs["CMAKE_CXX_STANDARD"] = self.options.cppstd

        cmake.configure(defs=defs)
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(os.path.abspath("tests"))
            if os.path.exists(os.path.abspath("basic")):
                self.run(os.path.abspath("basic"))
            if os.path.exists(os.path.abspath("fibonacci")):
                self.run(os.path.abspath("fibonacci") + " 5")
