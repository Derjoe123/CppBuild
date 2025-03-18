#include <filesystem>
#include <iostream>
import cppb;
auto main(int argc, char** argv) -> int {
    (void)argc;
    const static std::string CompilerName = "clang++";
    const static std::string CompilerFlags =
        " -std=c++20 -Wall -Wextra -Wpedantic ";

    cppb::BuildScript ThisScript{argv[0], __FILE__};

    auto comp =
        cppb::Compiler([&](const auto& src, const auto& out) -> std::string {
            return CompilerName + CompilerFlags + "-c " + src.string() +
                   " -o " + out.string() + ".o";
        });

    if (ThisScript.Rebuild(cppb::Compiler(
            [&](const auto& src, const auto& out) -> std::string {
                return CompilerName + CompilerFlags + src.string() + " -o " +
                       out.string() + ".exe ";
            }))) {
        return ThisScript.Execute();
    }

    std::filesystem::path BuildDir = "./build/";

    cppb::Project Proj{"Proj"};

    auto lnk =
        cppb::Linker([&](const auto& objs, const auto& libNames,
                         const auto& libPaths, const auto& out) -> std::string {
            std::string cmd = CompilerName + CompilerFlags;
            for (auto obj : objs) {
                cmd += " " + obj.string();
            }
            for (auto libPath : libPaths) {
                cmd += " -L" + libPath.string();
            }
            for (auto lib : libNames) {
                cmd += " -l" + lib.string();
            }
            cmd += " -o " + out.string();
            return cmd;
        });

    cppb::Target Target{"Test", comp, lnk};

    Target.Sources = {{"test.cpp"}};

    Proj.BuildTargets.push_back(Target);

    bool buildSuccess = Proj.Build(BuildDir);
    if (!buildSuccess) {
        std::cout << "[-] Build unsuccessful!\n";
        return 1;
    }
    return 0;
}

/* class Clang {
  public:
    std::string Path{"clang++ "};
    std::string Flags{" -std=c++23 -Wall -Wextra -Wpedantic "};

  public:
    auto PrecompileModule(const cppb::SourceFile& Module,
                          const std::filesystem::path& BuildDir)
        -> std::string {
        return Path + Flags + " -fmodules --precompile " + Module.Path +
               " -o " +
               (BuildDir / Module.Path.filename().replace_extension(".pcm"))
                   .string();
    }
    auto Compile(const cppb::SourceFile& CompileAble) -> objectfile
    {

    }
}; */
/*
myexecutable:
    precompile non precompiled modules
    compile non compiled precompiled modules
    compile normal source file
    link everything

auto precompiledmodules = Compiler.precompileModules(modules);
auto moduleobjects = Compiler.compile(precompiledmodules);
auto sourceobjects = Compiler.compile(sources);

auto myexecutable = Linker.link(moduleobjects,sourceobjects,libraries);
*/
