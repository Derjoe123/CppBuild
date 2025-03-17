#include <filesystem>
#include <functional>
#include <iostream>
#include <vector>

namespace cppb {
class Library {
  public:
    std::filesystem::path Path;
};
class Linker {
    std::function<std::string(
        const std::vector<std::filesystem::path>& Objects,
        const std::vector<std::filesystem::path>& LibraryNames,
        const std::vector<std::filesystem::path>& LibraryPaths,
        const std::filesystem::path& Output)>
        LinkCommandBuilder;
    Linker() = delete;

  public:
    Linker(std::function<
           std::string(const std::vector<std::filesystem::path>& Objects,
                       const std::vector<std::filesystem::path>& LibraryNames,
                       const std::vector<std::filesystem::path>& LibraryPaths,
                       const std::filesystem::path& Output)>&& CommandBuilder)
        : LinkCommandBuilder(std::move(CommandBuilder)) {}

    auto Link(const std::vector<std::filesystem::path>& Objects,
              const std::vector<std::filesystem::path>& LibraryNames,
              const std::vector<std::filesystem::path>& LibraryPaths,
              const std::filesystem::path& Output) -> int {
        std::string cmd =
            LinkCommandBuilder(Objects, LibraryNames, LibraryPaths, Output);
        std::cout << "Linker: " << cmd << "\n";
        return std::system(cmd.c_str());
    }
};
class Compiler {
    std::function<std::string(const std::filesystem::path& Source,
                              const std::filesystem::path& Output)>
        CompileCommandBuilder;
    Compiler() = delete;

  public:
    Compiler(std::function<std::string(const std::filesystem::path& Source,
                                       const std::filesystem::path& Output)>&&
                 CommandBuilder)
        : CompileCommandBuilder(std::move(CommandBuilder)) {}

    auto Compile(const std::filesystem::path& Source,
                 const std::filesystem::path& Output) const -> int {
        std::string cmd = CompileCommandBuilder(Source, Output);
        std::cout << "Compiler: " << cmd << "\n";
        return std::system(cmd.c_str());
    }
};
class SourceFile {
  public:
    std::filesystem::path Path;
    std::filesystem::path OutObjectPath;
    auto RebuildRequired() -> bool {

        if (!std::filesystem::exists(OutObjectPath))
            return true;
        if (!std::filesystem::exists(Path))
            return false;

        // if our own source code was modified later than the binary we need to
        // rebuild
        return std::filesystem::last_write_time(Path) >
               std::filesystem::last_write_time(OutObjectPath);
    }
    auto Build(const Compiler& Comp) -> bool {

        int Ret = Comp.Compile(Path, OutObjectPath);
        if (Ret != 0) {
            std::cout << "[-] Compiler returned non 0 exit code: " << Ret
                      << "\n";
        }
        return Ret == 0;
    }
};
class Target {
    Compiler Cmp;
    Linker Lnk;
    Target() = delete;

  public:
    std::vector<Library> Libraries;
    std::string BinaryName;

    Target(const std::string& binaryName, Compiler&& Comp, Linker&& Link)
        : Cmp(std::move(Comp)), Lnk(std::move(Link)), BinaryName(binaryName) {}
    Target(const std::string& binaryName, Compiler& Comp, Linker& Link)
        : Cmp(Comp), Lnk(Link), BinaryName(binaryName) {}

    Target(const std::string& binaryName, Compiler& Comp, Linker& Link,
           std::vector<Library>& Libs)
        : Cmp(Comp), Lnk(Link), Libraries(Libs), BinaryName(binaryName) {}

    std::vector<SourceFile> Sources;
    auto Build(const std::filesystem::path& BuildDir) -> bool {
        std::vector<std::filesystem::path> Paths{};
        for (auto src : Sources) {
            Paths.push_back(src.Path);
            if (src.RebuildRequired())
                if (!src.Build(Cmp))
                    return false;
        }
        std::vector<std::filesystem::path> LibraryNames;
        std::vector<std::filesystem::path> LibraryPaths;
        for (auto Lib : Libraries) {
            if (!Lib.Path.has_filename())
                continue;

            if (Lib.Path.has_parent_path() &&
                std::find(LibraryPaths.begin(), LibraryPaths.end(),
                          Lib.Path.parent_path()) == LibraryPaths.end())
                LibraryPaths.push_back(Lib.Path.parent_path());

            LibraryNames.push_back(Lib.Path.filename());
        }
        int ret =
            Lnk.Link(Paths, LibraryNames, LibraryPaths, BuildDir / BinaryName);
        if (ret != 0) {
            std::cout << "[-] Linker returned non 0 exit code: " << ret << "\n";
            return false;
        }
        return true;
    }
    auto ReBuild() -> void {}
};
class Project {
  public:
    std::vector<Target> BuildTargets;
    auto Build(const std::filesystem::path& BuildDir) -> bool {
        for (auto target : BuildTargets) {
            if (!target.Build(BuildDir)) {
                return false;
            }
        }
        return true;
    }
};

auto RebuildSelf() -> void {}

} // namespace cppb

auto main(int argc, char** argv) -> int {
    (void)argc;
    (void)argv;

    std::filesystem::path BuildDir = "./build/";

    cppb::Project Proj{};
    auto comp =
        cppb::Compiler([](const auto& src, const auto& out) -> std::string {
            return "c++.exe -c " + src.string() + " -o " + out.string();
        });

    auto lnk =
        cppb::Linker([](const auto& objs, const auto& libNames,
                        const auto& libPaths, const auto& out) -> std::string {
            std::string cmd = "c++.exe ";
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

    cppb::Target Target{"Test.exe", comp, lnk};

    cppb::SourceFile testcpp{};
    testcpp.Path = "./test.cpp";
    testcpp.OutObjectPath = BuildDir / "obj/test.o";
    Target.Sources.push_back(testcpp);

    Proj.BuildTargets.push_back(Target);

    bool buildSuccess = Proj.Build(BuildDir);
    if (!buildSuccess) {
        std::cout << "[-] Build unsuccessful!\n";
        return 1;
    }
    return 0;
}
