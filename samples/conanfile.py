import os

from conans import ConanFile, CMake, tools
from conans.errors import ConanInvalidConfiguration


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    options = {
        "loglevel": [None, "trace", "debug", "info", "warn", "error"],
        "coroutines": [True, False]
    }
    default_options = {
        "loglevel": None,
        "coroutines": False
    }

    def requirements(self):
        if self.options.loglevel:
            self.requires("spdlog/1.4.2")

    def build(self):
        cmake = CMake(self)
        defs = dict()
        if self.options.loglevel:
            defs["PACKIO_LOGGING"] = self.options.loglevel
        if self.options.coroutines:
            defs["PACKIO_COROUTINES"] = "ON"
        cmake.configure(defs=defs)
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".{}basic".format(os.sep))
            self.run(".{}fibonacci 10".format(os.sep))
            if self.options.coroutines:
                self.run(".{}basic_coroutines".format(os.sep))
                self.run(".{}fibonacci_coroutines 10".format(os.sep))
