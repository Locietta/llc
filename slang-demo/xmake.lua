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
        local shader_dir = path.join(target:scriptdir(), "shaders")
        local target_shader_dir = path.join(target:targetdir(), "shaders")
        if not os.isdir(target_shader_dir) then
            os.mkdir(target_shader_dir)
        end

        for _, file in ipairs(os.files(path.join(shader_dir, "*.slang"))) do
            local filename = path.filename(file)
            local target_file = path.join(target_shader_dir, filename)
            if not os.exists(target_file) then
                os.ln(file, target_file)
            end
        end

        if is_plat("windows") then
            local slang_dll = target:pkg("slang"):get("dllfiles")
            if slang_dll then
                for _, dll in ipairs(slang_dll) do
                    local filename = path.filename(dll)
                    local target_dll = path.join(target:targetdir(), filename)
                    if not os.exists(target_dll) then
                        os.ln(dll, target_dll)
                    end
                end
            end
        end
    end)

    after_clean(function (target)
        -- remove shaders
        local shader_dir = path.join(target:targetdir(), "shaders")
        if os.isdir(shader_dir) then
            os.rmdir(shader_dir)
        end
        -- remove slang dlls
        if is_plat("windows") then
            local slang_dll = target:pkg("slang"):get("dllfiles")
            if slang_dll then
                for _, dll in ipairs(slang_dll) do
                    local filename = path.filename(dll)
                    local target_dll = path.join(target:targetdir(), filename)
                    if os.exists(target_dll) then
                        os.rm(target_dll)
                    end
                end
            end
        end
    end)

    after_install(function (target)
        -- slang dlls
        local slang_dir = target:pkg("slang"):get("installdir")

        local installed_bindir = target:installdir("bin")

        if slang_dir then
            os.cp(path.join(slang_dir, "bin", "*.dll"), installed_bindir)
        end

        -- shaders
        local shader_dir = path.join(target:scriptdir(), "shaders")
        if os.isdir(shader_dir) then
            -- create shaders directory in installed bindir
            local installed_shader_dir = path.join(installed_bindir, "shaders/")
            if not os.isdir(installed_shader_dir) then
                os.mkdir(installed_shader_dir)
            end
            os.cp(path.join(shader_dir, "*.slang"), installed_shader_dir)
        end
    end)