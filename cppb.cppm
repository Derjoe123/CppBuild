module;
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <vector>
export module cppb;

export namespace cppb {
namespace internal {
auto RebuildRequired(const std::filesystem::path& BinaryPath,
                     const std::filesystem::path& SourcePath) -> bool {
    if (!std::filesystem::exists(BinaryPath))
        return true;
    if (!std::filesystem::exists(SourcePath))
        return false;

    // if our own source code was modified later than the binary we need to
    // rebuild
    return std::filesystem::last_write_time(SourcePath) >
           std::filesystem::last_write_time(BinaryPath);
}

} // namespace internal
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
        std::cout << "Linking: " << cmd << "\n";
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
        std::cout << "Compiling: " << cmd << "\n";
        return std::system(cmd.c_str());
    }
};
class SourceFile {

  public:
    SourceFile() = delete;
    SourceFile(const std::filesystem::path& Path) : Path(Path) {}
    auto RebuildRequired(const std::filesystem::path& BuildDir) -> bool {
        std::filesystem::path OutObjectPath = GetOutObjectPath(BuildDir);
        return internal::RebuildRequired(OutObjectPath, Path);
    }
    auto Build(const Compiler& Comp, const std::filesystem::path& BuildDir)
        -> bool {
        std::filesystem::path OutObjectPath = GetOutObjectPath(BuildDir);
        int Ret = Comp.Compile(Path, OutObjectPath);
        if (Ret != 0) {
            std::cout << "[-] Compiler returned non 0 exit code: " << Ret
                      << "\n";
        }
        return Ret == 0;
    }

  private:
    auto GetOutObjectPath(const std::filesystem::path& BuildDir)
        -> std::filesystem::path {
        std::string ObjectName =
            Path.filename().replace_extension(".o").string();
        return BuildDir / ObjectName;
    }

  public:
    std::filesystem::path Path;
};
class Target {
  public:
    Target() = delete;
    Target(const std::string& binaryName, Compiler&& Comp, Linker&& Link)
        : Cmp(std::move(Comp)), Lnk(std::move(Link)), BinaryName(binaryName) {}
    Target(const std::string& binaryName, Compiler& Comp, Linker& Link)
        : Cmp(Comp), Lnk(Link), BinaryName(binaryName) {}

    Target(const std::string& binaryName, Compiler& Comp, Linker& Link,
           std::vector<Library>& Libs)
        : Cmp(Comp), Lnk(Link), Libraries(Libs), BinaryName(binaryName) {}

    auto Build(const std::filesystem::path& BuildDir, bool RebuildAll = false)
        -> bool {
        if (!Compile(RebuildAll, BuildDir))
            return false;
        if (!Link(BuildDir))
            return false;

        return true;
    }

  private:
    auto Link(const std::filesystem::path& BuildDir) -> bool {
        std::vector<std::filesystem::path> Paths{GetPaths()};
        auto [LibraryNames, LibraryPaths] = ParseLibraries();

        int ret{
            Lnk.Link(Paths, LibraryNames, LibraryPaths, BuildDir / BinaryName)};
        if (ret != 0) {
            std::cout << "[-] Linker returned non 0 exit code: " << ret << "\n";
            return false;
        }
        return true;
    }
    auto Compile(bool RebuildAll, const std::filesystem::path& BuildDir)
        -> bool {
        for (auto src : Sources) {
            if (RebuildAll || src.RebuildRequired(BuildDir))
                if (!src.Build(Cmp, BuildDir))
                    return false;
        }
        return true;
    }
    auto GetPaths() -> std::vector<std::filesystem::path> {
        std::vector<std::filesystem::path> Paths{};
        for (auto src : Sources) {
            Paths.push_back(src.Path);
        }
        return Paths;
    }
    auto ParseLibraries() const
        -> std::pair<std::vector<std::filesystem::path>,
                     std::vector<std::filesystem::path>> {

        std::vector<std::filesystem::path> LibraryNames;
        std::vector<std::filesystem::path> LibraryPaths;
        for (auto Lib : Libraries) {
            if (!Lib.Path.has_filename())
                continue;

            if (Lib.Path.has_parent_path() &&
                std::find(begin(LibraryPaths), end(LibraryPaths),
                          Lib.Path.parent_path()) == end(LibraryPaths))
                LibraryPaths.push_back(Lib.Path.parent_path());

            LibraryNames.push_back(Lib.Path.filename());
        }
        return {LibraryNames, LibraryPaths};
    }

  private:
    Compiler Cmp;
    Linker Lnk;

  public:
    std::vector<Library> Libraries;
    std::vector<SourceFile> Sources;
    std::string BinaryName;
};
class Project {
  public:
    std::string Name;
    std::string Version;
    std::vector<Target> BuildTargets;
    auto Build(const std::filesystem::path& BuildDir) -> bool {
        if (!Name.empty() && !Version.empty())
            std::cout << "Building Project: " << Name << " (" << Version
                      << ").\n";
        else if (!Name.empty())
            std::cout << "Building Project: " << Name << ".\n";
        for (auto target : BuildTargets) {
            if (!target.Build(BuildDir)) {
                return false;
            }
        }
        return true;
    }
};
class BuildScript {

  public:
    BuildScript() = delete;
    BuildScript(const std::filesystem::path& BinaryPath,
                const std::filesystem::path& SourcePath)
        : BinaryPath(BinaryPath), SourcePath(SourcePath) {}

    auto Execute() -> int { return std::system(BinaryPath.string().c_str()); }
    // false = no rebuild, true = rebuilt
    auto Rebuild(const Compiler& compiler) -> bool {
        if (!RebuildRequired())
            return false;
        std::cout << "[+] Change in build script detected, rebuilding.\n";
        if (MoveOldBinary()) {
            std::cout << "[+] Moving old script binary.\n";
        }
        int ret = compiler.Compile(SourcePath, BinaryPath);
        if (ret != 0) {
            std::cout << "[-] Compiler returned non 0 exit code: " << ret
                      << "\n";
            return false;
        }
        return true;
    }

  private:
    auto RebuildRequired() -> bool {
        return internal::RebuildRequired(BinaryPath, SourcePath);
    }
    auto MoveOldBinary() -> bool {
        if (!std::filesystem::exists(BinaryPath))
            return false;
        std::filesystem::path OldBinaryPath = BinaryPath;
        OldBinaryPath.replace_extension(".old.exe");
        if (std::filesystem::exists(OldBinaryPath)) {
            std::filesystem::remove(OldBinaryPath);
        }
        std::filesystem::rename(BinaryPath, OldBinaryPath);
        return true;
    }

  public:
    std::filesystem::path BinaryPath;
    std::filesystem::path SourcePath;
};

} // namespace cppb
