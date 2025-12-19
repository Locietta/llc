-- this makes xmake place each target's build output in its own separate directory

rule("output.separate-per-target")
    on_load(function (target)
        import("core.project.config")

        local per_target_path = path.join(
            config.builddir(),
            config.plat(),
            config.arch(),
            config.mode(),
            target:name()
        )

        target:set("targetdir", per_target_path)
    end)

    on_clean(function (target)
        import("private.action.clean.remove_files")

        remove_files(target:targetdir())
    end)