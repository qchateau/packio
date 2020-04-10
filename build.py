import os
from cpt.packager import ConanMultiPackager

if __name__ == "__main__":
    builder = ConanMultiPackager(build_policy="missing", archs=["x86_64"])
    if os.getenv("BUILDER_CLANG_COROUTINE", None):
        builder.add(settings={"compiler.libcxx": "libc++", "build_type":"Release"}, options={"coroutines": True})
        builder.add(settings={"compiler.libcxx": "libc++", "build_type":"Debug"}, options={"coroutines": True})
    else:
        builder.add_common_builds()
    builder.run()
