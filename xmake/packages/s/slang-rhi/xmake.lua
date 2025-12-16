package("slang-rhi")
    set_description("Slang Render Hardware Interface")
    set_license("https://github.com/shader-slang/slang-rhi/blob/main/LICENSE")
    add_deps("slang")
    add_deps("cmake")
    if is_plat("windows") then
        add_syslinks("d3d11", "d3d12", "dxgi", "dxguid", "Advapi32")
    elseif is_plat("linux") then
        add_syslinks("dl", "pthread")
    elseif is_plat("macosx") then
        add_frameworks("Foundation", "QuartzCore", "Metal")
    end

    set_sourcedir(path.join(os.projectdir(), "external", "slang-rhi"))
    -- add_urls("https://github.com/shader-slang/slang-rhi.git")
    -- add_versions("1.0", "<shasum256 or gitcommit>")

    add_configs("shared", { description = "Build shared library", default = false, type = "boolean", readonly = true })

    on_install("windows|x64", "macosx", "linux|x86_64", function (package)
        local configs = {}

        local slang_path = package:dep("slang"):installdir()
        -- convert to cmake style path
        slang_path = slang_path:gsub("\\", "/")

        table.insert(configs, "-DSLANG_RHI_BUILD_SHARED=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_RHI_SLANG_INCLUDE_DIR=" .. slang_path .. "/include")
        table.insert(configs, "-DSLANG_RHI_SLANG_BINARY_DIR=" .. slang_path)
        -- disable tests and examples
        table.insert(configs, "-DSLANG_RHI_BUILD_TESTS=OFF")
        table.insert(configs, "-DSLANG_RHI_BUILD_TESTS_WITH_GLFW=OFF")
        table.insert(configs, "-DSLANG_RHI_BUILD_EXAMPLES=OFF")

        table.insert(configs, "-DSLANG_RHI_FETCH_SLANG=OFF")

        import("package.tools.cmake").install(package, configs)

        package:add("links", "slang-rhi", "slang-rhi-resources")

        local build_dir = package:buildir()
        -- Copy slang-rhi-resources.lib to package lib directory
        if is_plat("windows") then
            local rc_lib = path.join(build_dir, "slang-rhi-resources.lib")
            if os.isfile(rc_lib) then
                os.cp(rc_lib, package:installdir("lib"))
            end
        else
            -- *nix
            local rc_lib = path.join(build_dir, "libslang-rhi-resources.a")
            if os.isfile(rc_lib) then
                os.cp(rc_lib, package:installdir("lib"))
            end
        end

        -- Copy NVAPI library to package lib directory
        if is_plat("windows") then
            local nvapi_lib = path.join(build_dir, "_deps/nvapi-src/amd64/nvapi64.lib")
            if os.isfile(nvapi_lib) then
                os.cp(nvapi_lib, package:installdir("lib"))
                -- add nvapi64.lib to links
                package:add("links", "nvapi64")
            end
        end
    end)

    on_test(function (package)
        local configs = {languages = "c++17"}
        
        if is_plat("windows") then
            configs.syslinks = {"d3d11", "d3d12", "dxgi", "dxguid", "Advapi32"}
        elseif is_plat("linux") then
            configs.syslinks = {"dl", "pthread"}
        elseif is_plat("macosx") then
            -- QUESTION: does this work for macos?
            configs.links = {"-framework Foundation", "-framework QuartzCore", "-framework Metal"}
        end

        assert(package:check_cxxsnippets({test = [[
            #include <slang-rhi.h>
            void test() {
                rhi::DeviceDesc device_desc = {};
                device_desc.slang.targetProfile = "spirv_1_6";
                device_desc.deviceType = rhi::DeviceType::Vulkan;
                auto device = rhi::getRHI()->createDevice(device_desc);
                auto session = device->getSlangSession(); 
            }
        ]]}, {configs = configs}))
    end)