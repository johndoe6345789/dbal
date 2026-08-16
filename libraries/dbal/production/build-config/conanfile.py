from conan import ConanFile
from conan.tools.cmake import cmake_layout

class DBALDaemonConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("sqlite3/3.46.0")
        self.requires("fmt/12.0.0")
        self.requires("spdlog/1.16.0")
        self.requires("nlohmann_json/3.11.3")
        self.requires("drogon/1.9.7")
        self.requires("cpr/1.14.1")
        self.requires("inja/3.5.0")
        self.requires("mongo-cxx-driver/3.10.2")
        self.requires("boost/1.83.0", override=True)
        self.requires("gtest/1.14.0")
        self.requires("jwt-cpp/0.7.2")
        self.requires("argon2/20190702")
        # Required by src/cache/caching_adapter.cpp (the read-through cache).
        # Note src/adapters/redis/ -- the Redis *primary* adapter -- is excluded
        # from the build in CMakeLists, which is why this was not needed before
        # and why nothing noticed it was missing.
        self.requires("redis-plus-plus/1.3.15")
        # 1.2.x is no longer installable: ConanCenter has dropped every 1.2
        # recipe, and 1.2.31's source URL now returns 403. 1.3.12 is the only
        # maintained version. Note the 1.2 -> 1.3 API change affects
        # src/saml/xmldsig/{signer,verifier}.cpp, which use xmlSecAddIDs().
        self.requires("xmlsec/1.3.12")

    def configure(self):
        self.options["sqlite3"].shared = False

    def layout(self):
        cmake_layout(self)
