#ifndef cppb_h
#define cppb_h
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <iostream>
#include <vector>

namespace cppb {
using path = std::filesystem::path;
struct FileCollection : public std::vector<path> {
    static auto FromDir(const path& DirPath,
                        const std::string& FileExtensionFilter)
        -> std::expected<FileCollection, std::string> {
        FileCollection Collection{};
        if (!DirPath.has_root_directory()) {
            return std::unexpected{"Invalid Path: no root_directory"};
        }
        for (const auto& entry : DirPath.root_directory()) {
            if (entry.has_extension() &&
                entry.extension().string() == FileExtensionFilter) {
                Collection.push_back(entry);
            }
        }
        return Collection;
    }
};

struct SourceFileCollection : public FileCollection {};
struct ModuleFileCollection : public FileCollection {};
struct ObjectFileCollection : public FileCollection {};
struct LibraryCollection {
    std::vector<path> ExtraSearchPaths;
    std::vector<std::string> LibraryNames;
};
struct BinaryFile {
    std::string Name;

    enum class Type : std::uint8_t {
        Unknown = 0,
        Executable,
        StaticLibrary,
        DynamicLibrary,
    };

    Type type;
};
class CompilerHelper {
  public:
    [[nodiscard]] static auto RebuildRequired(const path& BinaryPath,
                                              const path& SourcePath) -> bool {
        if (!std::filesystem::exists(BinaryPath)) {
            return true;
        }
        if (!std::filesystem::exists(SourcePath)) {
            return false;
        }

        // if the source code was modified later than the binary we need to
        // rebuild
        return std::filesystem::last_write_time(SourcePath) >
               std::filesystem::last_write_time(BinaryPath);
    }
    static auto CreateDirsIfNotExisting(const path& Dir) -> bool {
        if (!std::filesystem::exists(Dir)) {
            std::filesystem::create_directories(Dir);
            return true;
        }
        return false;
    }
    [[nodiscard]] static auto DependenciesChanged(const std::string& DepsCmd,
                                                  const path& ForFile, // NOLINT
                                                  const path& BuildFilePath)
        -> bool {
        auto deps =
            CompilerHelper::GetSourceDependencies(DepsCmd + ForFile.string());

        if (!deps.has_value()) {
            std::cout << "Could not parse dependencies with: " << DepsCmd;
            return false;
        }

        if (deps.value().empty()) {
            return false;
        }

        bool changed =
            std::ranges::any_of(deps.value(), [&](const auto& dep) -> bool {
                return CompilerHelper::RebuildRequired(BuildFilePath, dep);
            });

        if (changed) {
            return true;
        }
        return std::ranges::any_of(deps.value(), [&](const auto& dep) -> bool {
            return CompilerHelper::DependenciesChanged(DepsCmd, dep,
                                                       BuildFilePath);
        });
    }

  private:
    [[nodiscard]] static auto
    GetSourceDependencies(const std::string& PrintDepsCommand)
        -> std::expected<SourceFileCollection, bool> {
        SourceFileCollection Deps;
        std::array<char, 256> Buffer{}; // NOLINT
        std::string DepsStr{};

#ifdef _WIN32
        // NOLINTNEXTLINE
        FILE* pipe = _popen(PrintDepsCommand.c_str(), "r");
#else
        FILE* pipe = popen(PrintDepsCommand.c_str(), "r");
#endif
        if (pipe == nullptr) {
            return std::unexpected{false};
        }

        while (fgets(Buffer.data(), Buffer.size(), pipe) != nullptr) {
            DepsStr += Buffer.data();
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return SourceFileCollection{ParseDeps(DepsStr)};
    }
    [[nodiscard]] static auto ParseDeps(const std::string& DepsUnparsed)
        -> std::vector<path> {
        std::vector<path> Deps;
        size_t Colon = DepsUnparsed.find(':');
        if (Colon == DepsUnparsed.npos) {
            return Deps;
        }
        size_t LastSpace = DepsUnparsed.find(' ', Colon + 2);
        if (LastSpace == DepsUnparsed.npos) {
            return Deps;
        }
        while (true) {
            size_t Space = DepsUnparsed.find(' ', LastSpace + 1);
            // end of string
            if (Space == DepsUnparsed.npos) {
                Deps.emplace_back(DepsUnparsed.substr(
                    LastSpace + 1, DepsUnparsed.size() - LastSpace - 2));
                break;
            }
            // we have hit the end of line marker
            if (LastSpace + 1 < DepsUnparsed.size() &&
                DepsUnparsed[LastSpace + 1] == '\\') {
                LastSpace += 4;
                continue;
            }
            Deps.emplace_back(
                DepsUnparsed.substr(LastSpace + 1, Space - LastSpace - 1));
            LastSpace = Space;
        }

        return Deps;
    }
};

class BuildScript {
    path BinaryPath;
    path SourcePath;

  public:
    BuildScript(path&& BinaryPath, path&& SourcePath)
        : BinaryPath(std::move(BinaryPath)), SourcePath(std::move(SourcePath)) {
    }
    template <typename CompilerType>
    auto Rebuild(const CompilerType& Compiler) -> bool {
        std::vector<path> Src{SourcePath};
        if (!Compiler.DependenciesChanged(SourcePath, BinaryPath) &&
            !CompilerHelper::RebuildRequired(BinaryPath, SourcePath)) {
            return false;
        }

        auto ObjectFile = Compiler.Compile(SourceFileCollection{Src},
                                           BinaryPath.parent_path());
        if (!ObjectFile.has_value()) {
            std::cout << "Script: " << SourcePath.string()
                      << " Rebuild compilation error: " << ObjectFile.error()
                      << "\n";
            return false;
        }

        auto [MovedBinary, OldBinaryPath] = MoveOldBinary(BinaryPath);
        if (MovedBinary) {
            std::cout << "Moved old Script binary: " << BinaryPath.string()
                      << " -> " << OldBinaryPath.string() << "\n";
        }

        auto Executable = Compiler.Link(
            ObjectFile.value(), {}, BinaryPath.parent_path(),
            BinaryPath.filename().string(), BinaryFile::Type::Executable);
        if (!Executable.has_value()) {
            std::cout << "Script: " << SourcePath.string()
                      << " Rebuild linking error: " << Executable.error()
                      << "\n";
            return false;
        }
        for (auto obj : ObjectFile.value()) {
            if (std::filesystem::exists(obj)) {
                std::cout << "Removing script build artifact: " << obj.string()
                          << "\n";
                std::filesystem::remove(obj);
            }
        }
        return true;
    }
    auto Execute(const std::string& ExtraArgs = "") -> int {
        std::string Command = BinaryPath.string() + " " + ExtraArgs;
        std::cout << "Executing Build Script: " << Command << "\n";
        return std::system(Command.c_str()); // NOLINT
    }

  private:
    static auto MoveOldBinary(const path& BinaryPath) -> std::pair<bool, path> {
        if (!std::filesystem::exists(BinaryPath)) {
            return {false, path{}};
        }
        auto OldExtension = BinaryPath.extension();
        path OldBinaryPath =
            BinaryPath.parent_path() / BinaryPath.filename().replace_extension(
                                           ".old" + OldExtension.string());
        std::filesystem::rename(BinaryPath, OldBinaryPath);
        return {true, OldBinaryPath};
    }
};
namespace meta {
template <typename T>
concept CanCompile = requires(T comp) {
    {
        comp.Compile(SourceFileCollection{}, path{"./build"})
    } -> std::same_as<std::expected<ObjectFileCollection, int>>;
};
template <typename T>
concept CanLink = requires(T comp) {
    {
        comp.Link(ObjectFileCollection{}, LibraryCollection{}, path{"./build"},
                  std::string{"somebinaryname"},
                  BinaryFile::Type::DynamicLibrary)
    } -> std::same_as<std::expected<BinaryFile, int>>;
};
template <typename T>
concept CanPrecompileModules = requires(T comp) {
    {
        comp.PrecompileModules(ModuleFileCollection{}, path{"./examplebuild"})
    } -> std::same_as<std::expected<SourceFileCollection, int>>;
};
template <typename T>
concept CanCheckDependencies = requires(T comp) {
    {
        comp.DependenciesChanged(path{}, path{"./examplebuild"})
    } -> std::same_as<bool>;
};
} // namespace meta

template <typename CompilerImpl> class Compiler {
    CompilerImpl Impl;

  public:
    Compiler() : Impl() {}
    explicit Compiler(const CompilerImpl& impl) : Impl(impl) {}

    [[nodiscard]] auto Compile(const SourceFileCollection& SourceFiles,
                               const path& BuildDir) const
        -> std::expected<ObjectFileCollection, int>
        requires meta::CanCompile<CompilerImpl>
    {
        return Impl.Compile(SourceFiles, BuildDir);
    }
    [[nodiscard]] auto Link(const ObjectFileCollection& ObjectFiles,
                            const LibraryCollection& Libraries,
                            const path& BuildDir, const std::string& BinaryName,
                            BinaryFile::Type Type) const
        -> std::expected<BinaryFile, int>
        requires meta::CanLink<CompilerImpl>
    {
        return Impl.Link(ObjectFiles, Libraries, BuildDir, BinaryName, Type);
    }
    [[nodiscard]] auto
    PrecompileModules(const ModuleFileCollection& ModuleFiles,
                      const path& BuildDir) const
        -> std::expected<SourceFileCollection, int>
        requires meta::CanPrecompileModules<CompilerImpl>
    {
        return Impl.PrecompileModules(ModuleFiles, BuildDir);
    }
    [[nodiscard]] auto DependenciesChanged(const path& SourcePath,
                                           const path& BuildPath) const -> bool
        requires meta::CanCheckDependencies<CompilerImpl>
    {
        return Impl.DependenciesChanged(SourcePath, BuildPath);
    }
};
namespace CompilerImpl {

class Clang {
  public:
    path Path{"clang++ "};
    std::string Flags{"-std=c++23 -Wall -Wextra -Wpedantic -Werror -O2 "};

    [[nodiscard]] auto Compile(const SourceFileCollection& SourceFiles,
                               const path& BuildDir) const
        -> std::expected<ObjectFileCollection, int> {
        ObjectFileCollection Objects{};
        for (const auto& src : SourceFiles) {
            auto BuildFilePath = GetBuildFilePath(src, BuildDir, ".o");
            bool DepsChanged = DependenciesChanged(src, BuildFilePath);
            if (CompilerHelper::CreateDirsIfNotExisting(
                    BuildFilePath.parent_path())) {
                std::cout << "Created directory: "
                          << BuildFilePath.parent_path().string() << "\n";
            }
            if (!DepsChanged &&
                !CompilerHelper::RebuildRequired(BuildFilePath, src)) {
                Objects.push_back(BuildFilePath);
                continue;
            }
            std::string Command = Path.string() + Flags + "-c " + src.string() +
                                  " -o " + BuildFilePath.string();
            std::cout << "Compiling: " << Command << "\n";

            if (int ret = std::system(Command.c_str()) != 0) { // NOLINT
                std::cout << "[-] Compiler returned: " << ret << "\n";
                return std::unexpected{ret};
            }
            Objects.push_back(BuildFilePath);
        }
        return Objects;
    }
    [[nodiscard]] auto Link(const ObjectFileCollection& ObjectFiles,
                            const LibraryCollection& Libraries,
                            const path& BuildDir, const std::string& BinaryName,
                            BinaryFile::Type Type) const
        -> std::expected<BinaryFile, int> {
        BinaryFile Binary = {.Name = BinaryName, .type = Type};
        if (CompilerHelper::CreateDirsIfNotExisting(BuildDir)) {
            std::cout << "Created directory: " << BuildDir.string() << "\n";
        }
        path BinaryPath = BuildDir / path(BinaryName);
        bool NeedsRebuild =
            std::ranges::any_of(ObjectFiles, [&](const auto& obj) -> bool {
                return CompilerHelper::RebuildRequired(BinaryPath, obj);
            });
        if (!NeedsRebuild) {
            return Binary;
        }
        std::string Command = Path.string() + Flags;
        for (const auto& libPath : Libraries.ExtraSearchPaths) {
            Command += " -L" + libPath.string();
        }
        for (const auto& library : Libraries.LibraryNames) {
            Command += " -l" + library;
        }
        for (const auto& obj : ObjectFiles) {
            Command += " " + obj.string();
        }
        Command += " -o " + (BinaryPath).string();
        std::cout << "Linking: " << Command << "\n";
        if (int ret = std::system(Command.c_str()) != 0) { // NOLINT
            std::cout << "[-] Linker returned: " << ret << "\n";
            return std::unexpected{ret};
        }
        return Binary;
    }
    [[nodiscard]] auto
    PrecompileModules(const ModuleFileCollection& ModuleFiles,
                      const path& BuildDir) const
        -> std::expected<SourceFileCollection, int> {
        SourceFileCollection SrcFiles{};
        for (const auto& module : ModuleFiles) {
            auto BuildFilePath = GetBuildFilePath(module, BuildDir, ".pcm");
            if (CompilerHelper::CreateDirsIfNotExisting(
                    BuildFilePath.parent_path())) {
                std::cout << "Created directory: "
                          << BuildFilePath.parent_path().string() << "\n";
            }
            if (!CompilerHelper::RebuildRequired(BuildFilePath, module)) {
                continue;
            }
            std::string Command = Path.string() + Flags +
                                  "-fmodules --precompile " + module.string() +
                                  " -o " + BuildFilePath.string();
            std::cout << "Precompiling modules: " << Command << "\n";
            if (int ret = std::system(Command.c_str()) != 0) { // NOLINT
                std::cout << "[-] Compiler returned: " << ret << "\n";
                return std::unexpected{ret};
            }
            SrcFiles.push_back(BuildFilePath);
        }
        return SrcFiles;
    }
    [[nodiscard]] auto DependenciesChanged(const path& SourcePath,
                                           const path& BuildPath) const
        -> bool {
        std::string DepsCmd = Path.string() + "-MM ";
        return CompilerHelper::DependenciesChanged(DepsCmd, SourcePath,
                                                   BuildPath);
    }

  private:
    static auto GetBuildFilePath(const path& FilePath, const path& BuildDir,
                                 const std::string& Extension) -> path {
        return BuildDir / FilePath.filename().replace_extension(Extension);
    }
};
} // namespace CompilerImpl
} // namespace cppb
#endif // cppb_h
