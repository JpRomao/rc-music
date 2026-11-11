# Existing, validated Windows host paths. Copy to ignored .tools/windows.psd1
# and adjust locations on another host. No dependency is downloaded or installed.
# Relative paths are resolved from the repository, not this data file.
@{
    CMake = 'C:/Program Files/CMake/bin/cmake.exe'
    CTest = 'C:/Program Files/CMake/bin/ctest.exe'
    Ninja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    Compiler = 'C:/Program Files/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.44.35207/bin/HostX64/x64/cl.exe'
    ClangFormat = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe'
    QmlFormat = 'out/m0/toolchains/qt-6.11.2-msvc2022_64/bin/qmlformat.exe'
    VisualStudioRoot = 'C:/Program Files/Microsoft Visual Studio/18/BuildTools'
    QtPrefix = 'q/f75eb9bea8280844c6d1a2b3f4/sdk'
    QtMultimediaPackage = 'out/product/qt-multimedia-6.11.2-rcmusic2/lib/cmake/Qt6Multimedia'
    QtHost = 'out/m0/toolchains/qt-6.11.2-msvc2022_64'
    TagLibPrefix = 'out/product/taglib-2.3.2-msvc144/install'
    Catch2Source = 'out/m0/toolchains/Catch2-3.15.3'
}
