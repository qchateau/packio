from conans import ConanFile


class PackioConan(ConanFile):
    name = "packio"
    version = "1.1.0"
    license = "MPL-2.0"
    author = "Quentin Chateau <quentin.chateau@gmail.com>"
    url = "https://github.com/qchateau/packio"
    description = "C++ implementation of msgpack-RPC"
    topics = ("rpc", "msgpack", "cpp17")
    exports_sources = "include/*"
    no_copy_source = True
    requires = [
        "msgpack/3.2.1",
        "boost/[>=1.72]",
    ]

    def package(self):
        self.copy("*.h")
