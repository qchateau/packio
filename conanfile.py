from conans import ConanFile


class PackioConan(ConanFile):
    name = "packio"
    version = "0.3.0"
    license = "GPLv3"
    author = "Quentin Chateau <quentin.chateau@gmail.com>"
    url = "https://github.com/qchateau/packio"
    description = "C++ implementation of msgpack-RPC"
    topics = ("rpc", "msgpack")
    exports_sources = "include/*"
    no_copy_source = True
    requires = ["msgpack/3.2.0@bincrafters/stable", "boost/1.71.0@conan/stable"]
    build_policy = "missing"

    def package(self):
        self.copy("*.h")

    def package_id(self):
        self.info.header_only()
