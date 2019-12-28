import os

from conans import ConanFile, CMake, tools


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".{}basic".format(os.sep))
            self.run(".{}fibonacci 10".format(os.sep))
