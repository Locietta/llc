
target("a+b")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("fmt", "slang-rhi")
    add_deps("util")

    after_build(function (target)
        -- copy shaders to build dir
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
    end)

    after_clean(function (target)
        -- remove shaders
        local shader_dir = path.join(target:targetdir(), "shaders")
        if os.isdir(shader_dir) then
            os.rmdir(shader_dir)
        end
    end)
