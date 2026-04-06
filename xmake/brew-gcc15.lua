local function _unique(items)
    local result = {}
    local seen = {}
    for _, item in ipairs(items) do
        if item and #item > 0 and not seen[item] then
            seen[item] = true
            table.insert(result, item)
        end
    end
    return result
end

local function _brew_roots()
    return _unique({
        os.getenv("HOMEBREW_PREFIX"),
        "/home/linuxbrew/.linuxbrew",
        "/linuxbrew/.linuxbrew",
    })
end

local function _find_brew_gcc15()
    for _, root in ipairs(_brew_roots()) do
        local gcc_bin = path.join(root, "bin")
        local binutils_bin = path.join(root, "opt", "binutils", "bin")
        local cc = path.join(gcc_bin, "gcc-15")
        local cxx = path.join(gcc_bin, "g++-15")
        if os.isfile(cc) and os.isfile(cxx) then
            return {
                gcc_bin = gcc_bin,
                binutils_bin = os.isdir(binutils_bin) and binutils_bin or nil,
                cc = cc,
                cxx = cxx,
                ar = os.isfile(path.join(binutils_bin, "ar")) and path.join(binutils_bin, "ar") or "ar",
                ranlib = os.isfile(path.join(binutils_bin, "ranlib")) and path.join(binutils_bin, "ranlib") or "ranlib",
                strip = os.isfile(path.join(binutils_bin, "strip")) and path.join(binutils_bin, "strip") or "strip",
                objcopy = os.isfile(path.join(binutils_bin, "objcopy")) and path.join(binutils_bin, "objcopy") or "objcopy",
            }
        end
    end
end

toolchain("brew-gcc15")
    set_kind("standalone")
    set_homepage("https://brew.sh/")
    set_description("Homebrew GCC 15 toolchain with matching binutils")
    set_runtimes("stdc++_static", "stdc++_shared")

    on_check(function ()
        return _find_brew_gcc15() ~= nil
    end)

    on_load(function (toolchain)
        local tc = assert(_find_brew_gcc15(), "Homebrew gcc-15 not found")

        toolchain:set("toolset", "cc", tc.cc)
        toolchain:set("toolset", "cxx", tc.cxx, tc.cc)
        toolchain:set("toolset", "ld", tc.cxx, tc.cc)
        toolchain:set("toolset", "sh", tc.cxx, tc.cc)
        toolchain:set("toolset", "ar", tc.ar)
        toolchain:set("toolset", "strip", tc.strip)
        toolchain:set("toolset", "objcopy", tc.objcopy)
        toolchain:set("toolset", "ranlib", tc.ranlib)
        toolchain:set("toolset", "mm", tc.cc)
        toolchain:set("toolset", "mxx", tc.cxx, tc.cc)
        toolchain:set("toolset", "as", tc.cc)

        -- GCC still shells out to `as`/`ld`, so package builds need matching
        -- Homebrew binutils in their runtime environment instead of /usr/bin.
        if tc.binutils_bin then
            toolchain:add("runenvs", "PATH", tc.binutils_bin)
            toolchain:add("runenvs", "COMPILER_PATH", tc.binutils_bin)
            local binutils_flag = "-B" .. tc.binutils_bin
            toolchain:add("cxflags", binutils_flag)
            toolchain:add("mxflags", binutils_flag)
            toolchain:add("asflags", binutils_flag)
            toolchain:add("ldflags", binutils_flag)
            toolchain:add("shflags", binutils_flag)
        end
        toolchain:add("runenvs", "PATH", tc.gcc_bin)

        local march
        if toolchain:is_arch("x86_64", "x64") then
            march = "-m64"
        elseif toolchain:is_arch("i386", "x86") then
            march = "-m32"
        end
        if march then
            toolchain:add("cxflags", march)
            toolchain:add("mxflags", march)
            toolchain:add("asflags", march)
            toolchain:add("ldflags", march)
            toolchain:add("shflags", march)
        end
    end)
