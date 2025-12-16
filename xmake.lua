set_xmakever("3.0.0")
set_project("llc")

add_rules("mode.release", "mode.debug", "mode.releasedbg")

set_languages("cxx23")

if is_os("windows") then
    -- set_toolchains("clang-cl")
    add_defines("_CRT_SECURE_NO_WARNINGS")
    add_defines("WIN32_LEAN_AND_MEAN", "UNICODE", "_UNICODE", "NOMINMAX", "_WINDOWS")
end

-- add_requires("kompute 0.9.*", "fmt")

-- includes("3rd-party")

add_repositories("local-repo xmake")
add_moduledirs("xmake/modules")

includes("xmake/*.lua")

add_requires("fmt")
add_requires("slang", "slang-rhi")

includes("*/xmake.lua")