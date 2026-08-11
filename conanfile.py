from conan import ConanFile
from conan.tools.build import can_run, check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class Signals2Conan(ConanFile):
    name = "signals2"
    package_type = "header-library"
    version = "0.1.0"
    description = "Header-only C++20 signals/slots-style library (namespace signals2)."
    license = "MIT"
    url = "https://github.com/McMurphy-Luo/signal"
    homepage = "https://github.com/McMurphy-Luo/signal"
    topics = ("signals", "slots", "header-only")

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "build_tests": [True, False],
        "build_benchmarks": [True, False],
    }
    default_options = {
        "build_tests": False,
        "build_benchmarks": False,
        "boost/*:header_only": True,
    }
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "test/*"
    no_copy_source = True

    def build_requirements(self):
        if self.options.build_tests or self.options.build_benchmarks:
            self.test_requires("boost/1.88.0")
        if self.options.build_benchmarks:
            self.test_requires("benchmark/1.9.4")

    def validate(self):
        check_min_cppstd(self, "20")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["SIGNALS2_BUILD_TESTS"] = bool(
            self.options.build_tests
        )
        toolchain.cache_variables["SIGNALS2_BUILD_BENCHMARKS"] = bool(
            self.options.build_benchmarks
        )
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if self.options.build_tests and can_run(self):
            cmake.ctest(cli_args=["--output-on-failure"])

    def package_id(self):
        self.info.clear()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "signals2")
        self.cpp_info.set_property("cmake_target_name", "signals2::signals2")
        self.cpp_info.includedirs = ["include"]
