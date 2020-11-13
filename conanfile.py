from conans import ConanFile


class PackioConan(ConanFile):
    name = "packio"
    version = "2.0.1"
    license = "MPL-2.0"
    author = "Quentin Chateau <quentin.chateau@gmail.com>"
    url = "https://github.com/qchateau/packio"
    description = "Asynchrnous msgpack-RPC and JSON-RPC server and client"
    topics = (
        "rpc",
        "async",
        "msgpack",
        "json",
        "msgpack-rpc",
        "json-rpc",
        "cpp17",
        "cpp20",
        "coroutine",
    )
    exports_sources = "include/*"
    no_copy_source = True
    options = {
        "standalone_asio": [True, False],
        "msgpack": [True, False],
        "nlohmann_json": [True, False],
    }
    default_options = {
        "standalone_asio": False,
        "msgpack": True,
        "nlohmann_json": True,
    }

    def requirements(self):
        if self.options.msgpack:
            self.requires("msgpack/3.2.1")
        if self.options.nlohmann_json:
            self.requires("nlohmann_json/3.9.1")

        if self.options.standalone_asio:
            self.requires("asio/[>=1.13.0]")
        else:
            self.requires("boost/[>=1.70.0]")

    def package(self):
        self.copy("*.h")

    def package_info(self):
        if self.options.standalone_asio:
            self.cpp_info.defines.append("PACKIO_STANDALONE_ASIO")
