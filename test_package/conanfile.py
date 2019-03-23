import os

from conans import ConanFile, CMake, tools


class RpcpackConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = ["gtest/1.8.1@bincrafters/stable", "spdlog/1.4.2@bincrafters/stable"]

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".%sbasic" % os.sep)
