package("slang")
    set_homepage("https://shader-slang.org/")
    set_description("A shading language that makes it easier to build and maintain large shader codebases in a modular and extensible fashion.")
    set_license("https://github.com/shader-slang/slang/blob/master/LICENSE")

    on_load(function (package)
        import("lib.detect.find_path")
        import("lib.detect.find_library")
        import("lib.detect.find_tool")

        local slangc = assert(find_tool("slangc"), "slang not found!")
        local slang_dir = path.directory(path.directory(slangc.program))

        package:set("installdir", slang_dir)
    end)

    on_fetch(function (package)

        local result = {}

        local link_dir = package:installdir("lib")
        local bin_dir = package:installdir("bin")
        local include_dir = package:installdir("include")

        result.linkdirs = { link_dir }
        result.includedirs = { include_dir }
        result.bindir = bin_dir
        result.shared = true

        local libfiles = {}
        local libs = {}
        
        if package:is_plat("windows") then
            for _, file in ipairs(os.files(path.join(link_dir, "*.lib"))) do
                table.insert(libfiles, path.join(link_dir, file))
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
                table.insert(libfiles, path.join(link_dir, file))
                table.insert(libs, path.basename(file):sub(4)) -- remove "lib" prefix
            end

            result.rpathdirs = { link_dir }
        end

        result.links = libs
        result.libfiles = libfiles

        return result
    end)

    
