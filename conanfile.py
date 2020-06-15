from conans import ConanFile


class PackioConan(ConanFile):
    name = "packio"
    version = "1.3.1"
    license = "MPL-2.0"
    author = "Quentin Chateau <quentin.chateau@gmail.com>"
    url = "https://github.com/qchateau/packio"
    description = "C++ implementation of msgpack-RPC"
    topics = ("rpc", "msgpack", "cpp17", "cpp20", "coroutine")
    exports_sources = "include/*"
    no_copy_source = True
    options = {"standalone_asio": [True, False]}
    default_options = {"standalone_asio": False}

    def requirements(self):
        self.requires("msgpack/3.2.1")

        if self.options.standalone_asio:
            self.requires("asio/[>=1.13.0]")
        else:
            self.requires("boost/[>=1.70.0]")

    def package(self):
        self.copy("*.h")

    def package_info(self):
        if self.options.standalone_asio:
            self.cpp_info.defines.append("PACKIO_STANDALONE_ASIO")
