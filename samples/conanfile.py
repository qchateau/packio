import os

from conans import ConanFile, CMake, tools


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    options = {
        "loglevel": [None, "trace", "debug", "info", "warn", "error"],
    }
    default_options = {"loglevel": None}

    def requirements(self):
        if self.options.loglevel:
            self.requires("spdlog/1.4.2@bincrafters/stable")

    def build(self):
        cmake = CMake(self)
        defs = dict()
        if self.options.loglevel:
            defs["PACKIO_LOGGING"] = self.options.loglevel
        cmake.configure(defs=defs)
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".{}basic".format(os.sep))
            self.run(".{}fibonacci 10".format(os.sep))
