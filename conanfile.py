from conans import ConanFile


class PackioConan(ConanFile):
    name = "packio"
    version = "2.4.0"
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
        "boost_json": [True, False, "default"],
    }
    default_options = {
        "standalone_asio": False,
        "msgpack": True,
        "nlohmann_json": True,
        "boost_json": "default",  # defaults to True if using boost, False if using asio
    }

    def requirements(self):
        boost_require = None

        if self.options.boost_json == "default":
            self.options.boost_json = not self.options.standalone_asio

        if self.options.msgpack:
            self.requires("msgpack-cxx/4.1.3")
        if self.options.nlohmann_json:
            self.requires("nlohmann_json/3.9.1")
        if self.options.boost_json:
            boost_require = "boost/[>=1.75.0]"

        if self.options.standalone_asio:
            self.requires("asio/[>=1.13.0]")
        elif not boost_require:
            boost_require = "boost/[>=1.70.0]"

        if boost_require:
            self.requires(boost_require)

    def package(self):
        self.copy("*.h")

    def package_info(self):
        self.cpp_info.defines.append(
            f"PACKIO_STANDALONE_ASIO={1 if self.options.standalone_asio else 0}"
        )
        self.cpp_info.defines.append(
            f"PACKIO_HAS_MSGPACK={1 if self.options.msgpack else 0}"
        )
        self.cpp_info.defines.append(
            f"PACKIO_HAS_NLOHMANN_JSON={1 if self.options.nlohmann_json else 0}"
        )
        self.cpp_info.defines.append(
            f"PACKIO_HAS_BOOST_JSON={1 if self.options.boost_json else 0}"
        )
