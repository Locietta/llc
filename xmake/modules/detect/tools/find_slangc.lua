-- imports
import("lib.detect.find_program")
import("lib.detect.find_programver")

local function _is_windows()
    return os.host() == "windows"
end

local function _is_scoop_shim(program)
    if not _is_windows() then
        return false
    end
    local lower = program:lower()
    return lower:find("[/\\]scoop[/\\]shims[/\\]") ~= nil
end

local function _is_abs_path(p)
    return p and (p:find("^%a:[/\\]") ~= nil or p:find("^[/\\]") ~= nil)
end

local function _resolve_scoop_shim(program)
    if not _is_scoop_shim(program) then
        return program
    end

    local shimfile = path.join(path.directory(program), "slangc.shim")
    if not os.isfile(shimfile) then
        return program
    end

    local content = io.readfile(shimfile)
    if not content then
        return program
    end

    local real = content:match('path%s*=%s*"(.-)"') or content:match("path%s*=%s*'(.-)'")
    if not real then
        return program
    end

    if not _is_abs_path(real) then
        real = path.join(path.directory(shimfile), real)
    end

    if os.isfile(real) then
        return real
    end
    return program
end

local function _candidate_paths()
    local paths = {}
    local scoop = os.getenv("SCOOP")
    local userprofile = os.getenv("USERPROFILE") or os.getenv("HOME")
    if scoop and scoop ~= "" then
        table.insert(paths, path.join(scoop, "apps", "slang", "current", "bin"))
    end
    if userprofile and userprofile ~= "" then
        table.insert(paths, path.join(userprofile, "scoop", "apps", "slang", "current", "bin"))
    end
    table.insert(paths, "D:/Scoop/apps/slang/current/bin")
    table.insert(paths, "$(env PATH)")
    table.insert(paths, "/opt/shader-slang-bin/bin")
    table.insert(paths, "$(env VULKAN_SDK)/Bin")
    table.insert(paths, "$(env VK_SDK_PATH)/Bin")
    return paths
end

local function _find_child_dir_case_insensitive(root, child)
    local child_lower = child:lower()
    for _, dir in ipairs(os.dirs(path.join(root, "*"))) do
        if path.filename(dir):lower() == child_lower then
            return dir
        end
    end
end

local function _detect_slang_layout_kind(program)
    if not program or not os.isfile(program) then
        return nil
    end

    local root = path.directory(path.directory(path.absolute(program)))
    local include_dir = _find_child_dir_case_insensitive(root, "include")
    local lib_dir = _find_child_dir_case_insensitive(root, "lib")
    if not include_dir or not lib_dir then
        return nil
    end

    local has_root_header = os.isfile(path.join(include_dir, "slang.h"))
    local has_nested_slang_dir = os.isdir(path.join(include_dir, "slang"))
    local has_vulkan_dir = os.isdir(path.join(include_dir, "vulkan"))
    local has_sdk_ecosystem = has_vulkan_dir or
        os.isdir(path.join(include_dir, "glslang")) or
        os.isdir(path.join(include_dir, "spirv-tools"))

    if has_root_header and not has_nested_slang_dir and not has_sdk_ecosystem then
        return "standalone"
    end
    if has_root_header and (has_nested_slang_dir or has_sdk_ecosystem) then
        return "vulkan-sdk"
    end
    return "unknown"
end

-- find slangc
--
-- @param opt   the argument options, e.g. {version = true, program = "c:\xxx\slangc.exe"}
--
-- @return      program, version
--
-- @code
--
-- local slangc = find_slangc()
-- local slangc, version = find_slangc({version = true})
-- local slangc, version = find_slangc({version = true, program = "c:\xxx\slangc.exe"})
--
-- @endcode
--
function main(opt)
    opt = opt or {}
    opt.paths = opt.paths or _candidate_paths()
    opt.check = opt.check or "-v"
    local program = nil
    if opt.program then
        program = _resolve_scoop_shim(opt.program)
    else
        -- Prefer standalone slang when multiple slangc executables exist.
        for _, p in ipairs(opt.paths) do
            local candidate = find_program("slangc", table.join(opt, {paths = {p}}))
            candidate = candidate and _resolve_scoop_shim(candidate) or nil
            if candidate and _detect_slang_layout_kind(candidate) == "standalone" then
                program = candidate
                break
            end
            if candidate and not program then
                program = candidate
            end
        end
        if not program then
            program = find_program("slangc", opt)
            program = program and _resolve_scoop_shim(program) or nil
        end
    end
    local version = nil
    if program and opt and opt.version then
        version = find_programver(program, opt)
    end
    return program, version
end
