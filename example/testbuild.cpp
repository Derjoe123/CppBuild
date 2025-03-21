#include "../cppb.hpp"
#include <string>

auto main(int argc, char** argv) -> int {
    (void)argc;
    cppb::BuildScript ThisScript{argv[0], __FILE__};
    cppb::CompilerImpl::Clang clang{};
    cppb::Compiler<cppb::CompilerImpl::Clang> Compiler{clang};

    if (ThisScript.Rebuild(Compiler)) {
        return ThisScript.Execute();
    }

    std::vector<std::filesystem::path> test = {"test.cpp"};
    cppb::SourceFileCollection TestFiles = {test};
    std::filesystem::path BuildDir = "build";
    std::filesystem::path ObjectBuildDir = "build/objects";
    auto objectFiles = Compiler.Compile(TestFiles, ObjectBuildDir);
    if (!objectFiles.has_value()) {
        return objectFiles.error();
    }

    auto Executable =
        Compiler.Link(objectFiles.value(), {}, BuildDir, "Test.exe",
                      cppb::BinaryFile::Type::Executable);
    if (!Executable.has_value()) {
        return Executable.error();
    }
    return 0;
}
