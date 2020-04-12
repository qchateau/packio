from conans import ConanFile


class PackioConan(ConanFile):
    name = "packio"
    version = "1.2.0"
    license = "MPL-2.0"
    author = "Quentin Chateau <quentin.chateau@gmail.com>"
    url = "https://github.com/qchateau/packio"
    description = "C++ implementation of msgpack-RPC"
    topics = ("rpc", "msgpack", "cpp17")
    exports_sources = "include/*"
    no_copy_source = True
    requires = [
        "msgpack/3.2.1",
        "zlib/1.2.8"
    ]
    options = {
        "standalone_asio": [True, False]
    }
    default_options = {
        "standalone_asio": False
    }

    def requirements(self):
        if self.options.standalone_asio:
            self.requires("asio/[>=1.13.0]")
        else:
            self.requires("boost/[>=1.70.0]")

    def package(self):
        self.copy("*.h")
