-- compile slang shaders to Slang IR (.slang-module)

local function _is_parent_relpath(p)
    return p and p:find("^%.%.[/\\]?") ~= nil
end

local function _resolve_relative_output(sourcefile, scriptdir, output_subdir)
    local source_abs = path.absolute(sourcefile)
    local anchors = {
        path.absolute(path.join(scriptdir, output_subdir)),
        path.absolute(scriptdir)
    }
    for _, anchor in ipairs(anchors) do
        local rel = path.relative(source_abs, anchor)
        if rel and rel ~= "" and not _is_parent_relpath(rel) then
            if rel == "." then
                return path.filename(sourcefile)
            end
            return rel
        end
    end
    return path.filename(sourcefile)
end

local function _ensure_output_dir(outputdir, relpath)
    if not os.isdir(outputdir) then
        os.mkdir(outputdir)
    end
    local rel_dir = path.directory(relpath)
    if rel_dir and rel_dir ~= "." then
        local final_outputdir = path.join(outputdir, rel_dir)
        if not os.isdir(final_outputdir) then
            os.mkdir(final_outputdir)
        end
        return final_outputdir
    end
    return outputdir
end

rule("slang")
    set_extensions(".slang", ".slangh")

    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        import("lib.detect.find_tool")

        local basename = path.basename(sourcefile)
        local filename = path.filename(sourcefile)
        local scriptdir = target:scriptdir()
        local extname = (path.extension(sourcefile) or ""):lower()
        local is_header = extname == ".slangh"

        local output_subdir = target:extraconf("rules", "slang", "outputdir") or "shaders"
        local outputdir = path.join(target:targetdir(), output_subdir)
        local relpath = _resolve_relative_output(sourcefile, scriptdir, output_subdir)
        local final_outputdir = _ensure_output_dir(outputdir, relpath)

        local outputname = is_header and filename or (basename .. ".slang-module")
        local outputfile = path.join(final_outputdir, outputname)

        if is_header then
            batchcmds:show_progress(opt.progress, "${color.build.object}copying.slangh %s", sourcefile)
            os.cp(sourcefile, outputfile)
        else
            local slangc = assert(find_tool("slangc"), "slangc not found!")
            local language_version = target:extraconf("rules", "slang", "language_version") or "default"
            local slangc_opt = {
                path(sourcefile),
                "-std", language_version,
                "-O2",
                "-o", path(outputfile),
            }

            batchcmds:show_progress(opt.progress, "${color.build.object}compiling.slang %s", sourcefile)
            batchcmds:vrunv(slangc.program, slangc_opt)
        end

        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(outputfile))
        batchcmds:set_depcache(target:dependfile(outputfile))
    end)

    after_clean(function (target)
        import("private.action.clean.remove_files")
        local output_subdir = target:extraconf("rules", "slang", "outputdir") or "shaders"
        local outputdir = path.join(target:targetdir(), output_subdir)
        remove_files(outputdir)
    end)