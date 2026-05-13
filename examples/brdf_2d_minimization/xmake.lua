add_requires("daw_json_link")
add_requires("stb")

target("brdf_2d_minimization")
    set_kind("binary")
    add_files("*.cpp")
    add_files("shaders/**.slang")
    add_packages("fmt", "slang-rhi", "daw_json_link", "stb")
    add_deps("llc")
