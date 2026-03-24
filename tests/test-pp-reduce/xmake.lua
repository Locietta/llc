add_requires("cxxopts")

target("test-pp-reduce")
    set_kind("binary")
    add_files("*.cpp")
    add_packages("fmt", "slang-rhi", "cxxopts")
    add_deps("llc")
