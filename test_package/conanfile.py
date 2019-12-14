import os

from conans import ConanFile, CMake, tools


class PackioConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = ["gtest/1.8.1@bincrafters/stable", "spdlog/1.4.2@bincrafters/stable"]
    options = {"boost": "ANY", "msgpack": "ANY"}
    default_options = {"boost": None, "msgpack": None}

    def requirements(self):
        if self.options.boost:
            self.requires("boost/{}@conan/stable".format(self.options.boost))
        if self.options.msgpack:
            self.requires("msgpack/{}@bincrafters/stable".format(self.options.msgpack))

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            os.chdir("bin")
            self.run(".{}basic".format(os.sep))
            self.run(".{}mt".format(os.sep))
