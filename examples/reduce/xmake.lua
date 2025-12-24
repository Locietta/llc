
target("reduce")
    set_kind("binary")
    add_files("*.cpp")
    add_files("shaders/**.slang")
    add_packages("fmt", "slang-rhi")
    add_deps("llc")
