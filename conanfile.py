from conan import ConanFile
from conan.tools.files import copy
import os


class Signals2Conan(ConanFile):
    name = "signals2"
    package_type = "header-library"
    version = "0.1.0"
    description = "Header-only C++ signals/slots-style library (namespace signals2)."
    license = "MIT"
    url = "https://github.com/PLACEHOLDER/signal"
    homepage = "https://github.com/PLACEHOLDER/signal"
    topics = ("signals", "slots", "header-only")

    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "include/signals/signals.h"
    no_copy_source = True

    def package_id(self):
        self.info.clear()

    def package(self):
        copy(
            self,
            "signals.h",
            os.path.join(self.source_folder, "include", "signals"),
            os.path.join(self.package_folder, "include", "signals"),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "signals2")
        self.cpp_info.set_property("cmake_target_name", "signals2::signals2")
        self.cpp_info.includedirs = ["include"]
