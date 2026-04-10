# LLC

Loia's Lean Compute (LLC) library.

This is just a personal project for learning and experimenting with (post-)modern C++ and GPU compute APIs.

## Build

### Prerequisites

This project uses [xmake](https://xmake.io/) as its build system. You also need to have a C++23 capable compiler installed (e.g. MSVC 2022 17.7+, Clang 19+, GCC 14+).

It's assumed [slang](https://github.com/shader-slang/slang) is installed and available in your PATH for `slangc` and its headers. It's recommended to install slang via [scoop](https://scoop.sh/) on Windows:

```powershell
scoop install slang
```

### Building

Git submodules is used for external dependencies that is not in xrepo (yet), so make sure to clone the repository with `--recurse-submodules` option or run `git submodule update --init --recursive` after cloning.

After installing all prerequisites, you can build the project by running the following commands in the project root:

```pwsh
xmake f -m <mode>  # configure the build mode, e.g. debug or release
xmake              # build the llc core library
xmake -g examples  # build examples
xmake -g tests     # build tests
```

### Running examples

You can run the built examples using `xmake run <target>` command. For example, to run the `A+B` example:

```pwsh
xmake run a+b
```

This will build the `a+b` example target if it hasn't been built yet, and then execute it.

## License
This project is licensed under the MIT License. See the [LICENSE](/LICENSE) file for details.