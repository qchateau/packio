import os

from conans import ConanFile, CMake, tools


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = ["gtest/1.10.0"]
    options = {
        "boost": "ANY",
        "loglevel": [None, "trace", "debug", "info", "warn", "error"],
        "rtti": [True, False]
    }
    default_options = {"boost": None, "loglevel": None, "rtti": True}

    def requirements(self):
        if self.options.boost:
            self.requires("boost/{}".format(self.options.boost))
        if self.options.loglevel:
            self.requires("spdlog/1.4.2")

    def build(self):
        cmake = CMake(self)
        defs = dict()
        if self.options.loglevel:
            defs["PACKIO_LOGGING"] = self.options.loglevel
        if not self.options.rtti:
            defs["PACKIO_NO_RTTI"] = "1"
        cmake.configure(defs=defs)
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".{}basic".format(os.sep))
            self.run(".{}mt".format(os.sep))
