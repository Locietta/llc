-- compile slang shaders to Slang IR (.slang-module)

rule("slang")
    set_extensions(".slang")

    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        import("lib.detect.find_tool")

        local slangc = assert(find_tool("slangc"), "slangc not found!")
        local basename = path.basename(sourcefile)

        local output_subdir = target:extraconf("rules", "slang", "outputdir") or "slang-modules"
        local outputdir = path.join(target:targetdir(), output_subdir)
        if not os.isdir(outputdir) then 
            os.mkdir(outputdir)
        end

        local language_version = target:extraconf("rules", "slang", "language_version") or "default"
        local outputfile = path.join(outputdir, basename .. ".slang-module")

        slangc_opt = {
            path(sourcefile),
            "-std", language_version,
            "-O2",
            "-o", path(outputfile),
        }

        batchcmds:show_progress(opt.progress, "${color.build.object}compiling.slang %s", sourcefile)
        batchcmds:vrunv(slangc.program, slangc_opt)

        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(outputfile))
        batchcmds:set_depcache(target:dependfile(outputfile))
    end)

    after_clean(function (target)
        import("private.action.clean.remove_files")
        local output_subdir = target:extraconf("rules", "slang", "outputdir") or "slang-modules"
        local outputdir = path.join(target:targetdir(), output_subdir)
        remove_files(outputdir)
    end)