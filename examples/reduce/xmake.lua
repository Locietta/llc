add_requires("cxxopts")

target("reduce")
    set_kind("binary")
    add_files("*.cpp")
    add_files("shaders/**.slang")
    add_packages("fmt", "slang-rhi", "cxxopts")
    add_deps("llc")
