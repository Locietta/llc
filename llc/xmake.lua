

target("llc")
    set_kind("static")
    add_files("*.cpp")
    add_includedirs("..", {public = true})
    add_packages("slang-rhi", "fmt")