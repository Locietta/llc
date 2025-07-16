add_requires("vulkansdk", "stb")

target("platform")
    set_kind("static")
    add_files("util/platform/*.cpp")
    add_includedirs(".")
    if is_plat("windows") then
        add_syslinks("gdi32", "user32", "shell32")
    end

target("util")
    set_kind("static")
    add_files("util/*.cpp")
    add_packages("slang", "fmt")
    add_includedirs(".")
    add_deps("platform")

target("slang-demo")
    set_kind("binary")
    add_files("*.cpp")
    add_packages("fmt", "slang", "vulkansdk", "stb")
    add_includedirs(".")
    add_deps("util")
    after_build(function (target)
        os.cp(target:scriptdir() .. "/shaders/*.slang", target:targetdir() .. "/shaders/")
    end)
