set_xmakever("3.0.0")
set_project("llc")

add_rules("mode.release", "mode.debug", "mode.releasedbg")

set_languages("cxx23")

if is_os("windows") then
    -- set_toolchains("clang-cl") -- slang-rhi does not support clang-cl yet
    add_defines("_CRT_SECURE_NO_WARNINGS")
    add_defines("WIN32_LEAN_AND_MEAN", "UNICODE", "_UNICODE", "NOMINMAX", "_WINDOWS")
end

add_repositories("loia https://github.com/Locietta/xmake-repo")

add_moduledirs("xmake/modules")

includes("xmake/rules/*.lua")

add_rules("output.separate-per-target")
add_rules("slang", {
    outputdir = "shaders",
    language_version = "2026",
})

add_requires("fmt", { system = false})
add_requires("glm", { system = false })
add_requires("mdspan", { system = false })
add_requires("slang", "slang-rhi")

includes("*/xmake.lua")
