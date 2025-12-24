
target("a+b")
    set_kind("binary")
    add_files("main.cpp")
    add_files("shaders/*.slang")
    add_packages("fmt", "slang-rhi")
    add_deps("llc")
