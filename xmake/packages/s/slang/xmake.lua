-- This package searches for an existing local installation of Slang. It does not build Slang from source.

local function _find_child_dir(root, names)
    local names_map = {}
    for _, name in ipairs(names) do
        names_map[name:lower()] = true
    end
    for _, dir in ipairs(os.dirs(path.join(root, "*"))) do
        local dirname = path.filename(dir):lower()
        if names_map[dirname] then
            return dir
        end
    end
end

local function _detect_layout(root)
    local bindir = _find_child_dir(root, {"bin"})
    local libdir = _find_child_dir(root, {"lib"})
    local includedir = _find_child_dir(root, {"include"})
    if not bindir or not libdir or not includedir then
        return nil
    end

    local has_root_header = os.isfile(path.join(includedir, "slang.h"))
    local has_nested_header = os.isfile(path.join(includedir, "slang", "slang.h"))
    if not has_root_header and not has_nested_header then
        return nil
    end

    -- Distinguish standalone Slang from Vulkan SDK bundle by include subtree.
    local has_sdk_ecosystem = os.isdir(path.join(includedir, "vulkan")) or
        os.isdir(path.join(includedir, "glslang")) or
        os.isdir(path.join(includedir, "spirv-tools"))
    local has_nested_slang_dir = os.isdir(path.join(includedir, "slang"))
    local kind = "unknown"
    if has_root_header and not has_nested_slang_dir and not has_sdk_ecosystem then
        kind = "standalone"
    elseif has_root_header and (has_nested_slang_dir or has_sdk_ecosystem) then
        kind = "vulkan-sdk"
    end

    local public_includedir = includedir
    if kind == "vulkan-sdk" then
        public_includedir = path.join(includedir, "slang")
    end

    return {
        bindir = bindir,
        libdir = libdir,
        includedir = public_includedir,
        kind = kind
    }
end

package("slang")
    set_homepage("https://shader-slang.org/")
    set_description("A shading language that makes it easier to build and maintain large shader codebases in a modular and extensible fashion.")
    set_license("https://github.com/shader-slang/slang/blob/master/LICENSE")

    on_load(function (package)
        import("lib.detect.find_tool")

        local slangc = assert(find_tool("slangc"), "slang not found!")
        local slang_dir = path.directory(path.directory(path.absolute(slangc.program)))
        local layout = _detect_layout(slang_dir)
        assert(layout, "unsupported slang layout: " .. slang_dir)

        package:set("installdir", slang_dir)
    end)

    on_fetch(function (package)
        local result = {}
        local slang_dir = package:installdir()
        local layout = _detect_layout(slang_dir)
        assert(layout, "unsupported slang layout: " .. slang_dir)

        local link_dir = layout.libdir
        local bin_dir = layout.bindir
        local include_dir = layout.includedir

        result.linkdirs = { link_dir }
        result.includedirs = { include_dir }
        result.bindir = bin_dir
        result.shared = true

        local libfiles = {}
        local libs = {}
        
        if package:is_plat("windows") then
            for _, file in ipairs(os.files(path.join(link_dir, "*.lib"))) do
                table.insert(libfiles, file)
                table.insert(libs, path.basename(file))
            end

            local dllfiles = {}

            for _, file in ipairs(os.files(path.join(bin_dir, "*.dll"))) do
                table.insert(libfiles, file)
                table.insert(dllfiles, file)
            end

            result.dllfiles = dllfiles
            result.rpathdirs = { bin_dir }
        else
            -- linux

            for _, file in ipairs(os.files(path.join(link_dir, "*.so"))) do
                table.insert(libfiles, file)
                table.insert(libs, path.basename(file):sub(4)) -- remove "lib" prefix
            end

            result.rpathdirs = { link_dir }
        end

        result.links = libs
        result.libfiles = libfiles

        return result
    end)

    
