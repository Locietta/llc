package("mdspan")
    set_homepage("https://github.com/kokkos/mdspan")
    set_description("Reference mdspan implementation from Kokkos")
    set_license("Apache-2.0 WITH LLVM-exception")

    set_sourcedir(path.join(os.projectdir(), "external", "mdspan"))

    on_fetch(function (package)
        return {
            includedirs = {path.join(package:sourcedir(), "include", "mdspan")}
        }
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <mdspan.hpp>
            #include <vector>
            #include <cstddef>
            void test() {
                std::vector<float> v(16);
                Kokkos::mdspan<float, Kokkos::dextents<std::size_t, 2>> view(v.data(), 4, 4);
                (void)view;
            }
        ]]}, {configs = {languages = "c++23"}}))
    end)
