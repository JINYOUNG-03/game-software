# Included dependency sources

`glfw/` and `glew/` are complete, unmodified source-release trees downloaded from the official release URLs in
`DEPENDENCIES.json`. Builds use local files only; no submodule checkout, package manager or network download is required.
`.gitattributes` preserves upstream bytes. The migration check verifies the sorted file-tree hashes.

The repository-owned `GLFW.Static.vcxproj`, `GLEW.Static.vcxproj` and `StaticLibrary.props` compile the Windows
sources for Debug/Release and Win32/x64. They use the same static MSVC runtime as the game and never inherit its C++ PCH.
The root CMakeLists provides an alternative Windows MSVC/clang-cl build using the same sources.

Upstream source licenses remain in `glfw/LICENSE.md` and `glew/LICENSE.txt` and are copied beside the built game.
The private fallback font and its SIL Open Font License are in `resource/Fonts` and are deployed together.
Malgun Gothic is a system font and is not redistributed by this repository.

See `../GLFW_MIGRATION.md` for build steps, supported scope and verification limits.
