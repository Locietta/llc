add_requires("daw_json_link")

target("mlp")
    set_kind("binary")
    add_files("*.cpp")
    add_files("shaders/**.slang")
    add_packages("fmt", "slang-rhi", "daw_json_link")
    add_deps("llc")
