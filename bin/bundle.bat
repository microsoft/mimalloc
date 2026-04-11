@echo off
rem ------------------------------------------------------------------
rem Build a binary release
rem ------------------------------------------------------------------
setlocal

mkdir "out/bundle"
mkdir "out/bundle/prefix-x64"
mkdir "out/bundle/prefix-x86"

set MI_TAG=
for /F "tokens=*" %%x in ('git describe --tag 2^> nul') do (set MI_TAG=%%x)
set MI_COMMIT=
for /F "tokens=*" %%x in ('git rev-parse HEAD 2^> nul') do (set MI_COMMIT=%%x)

echo Tag   : %MI_TAG%
echo Commit: %MI_COMMIT%

rem (not needed as it is already done on other platforms)
rem curl --proto =https --tlsv1.2 -f -L -o "https://github.com/microsoft/mimalloc/archive/%MI_COMMIT%.tar.gz" "out/bundle/mimalloc-%MI_TAG%-source.tar.gz"

rem Generate x64

echo.
echo Build: debug x64
cmake . -B "out/bundle/debug-x64" -A x64 -T ClangCl -DCMAKE_BUILD_TYPE=Debug
cmake --build "out/bundle/debug-x64" --config Debug
ctest --test-dir "out/bundle/debug-x64" -C Debug
cmake --install "out/bundle/debug-x64" --prefix "out/bundle/prefix-x64" --config Debug

echo.
echo Build: release x64
cmake . -B "out/bundle/release-x64" -A x64 -T ClangCl -DCMAKE_BUILD_TYPE=Release -DMI_OPT_ARCH=ON
cmake --build "out/bundle/release-x64" --config Release
ctest --test-dir "out/bundle/release-x64" -C Release
cmake --install "out/bundle/release-x64" --prefix "out/bundle/prefix-x64" --config Release

echo.
echo Build: secure x64
cmake . -B "out/bundle/secure-x64" -A x64 -T ClangCl -DCMAKE_BUILD_TYPE=Release -DMI_OPT_ARCH=ON -DMI_SECURE=ON
cmake --build "out/bundle/secure-x64" --config Release
ctest --test-dir "out/bundle/secure-x64" -C Release
cmake --install "out/bundle/secure-x64" --prefix "out/bundle/prefix-x64" --config Release

echo.
echo Archive: x64
cd "out/bundle/prefix-x64"
tar -czvf "../mimalloc-%MI_TAG%-windows-x64.tar.gz" .
cd ../../..


rem Now generate x86

echo.
echo Build: debug x86
cmake . -B "out/bundle/debug-x86" -A Win32 -T ClangCl -DCMAKE_BUILD_TYPE=Debug
cmake --build "out/bundle/debug-x86" --config Debug
ctest --test-dir "out/bundle/debug-x86" -C Debug
cmake --install "out/bundle/debug-x86" --prefix "out/bundle/prefix-x86" --config Debug

echo.
echo Build: release x86
cmake . -B "out/bundle/release-x86" -A Win32 -T ClangCl -DCMAKE_BUILD_TYPE=Release -DMI_OPT_ARCH=ON
cmake --build "out/bundle/release-x86" --config Release
ctest --test-dir "out/bundle/release-x86" -C Release
cmake --install "out/bundle/release-x86" --prefix "out/bundle/prefix-x86" --config Release

echo.
echo Build: secure x86
cmake . -B "out/bundle/secure-x86" -A Win32 -T ClangCl -DCMAKE_BUILD_TYPE=Release -DMI_OPT_ARCH=ON -DMI_SECURE=ON
cmake --build "out/bundle/secure-x86" --config Release
ctest --test-dir "out/bundle/secure-x86" -C Release
cmake --install "out/bundle/secure-x86" --prefix "out/bundle/prefix-x86" --config Release

echo.
echo Archive: x86
cd "out/bundle/prefix-x86"
tar -czvf "../mimalloc-%MI_TAG%-windows-x86.tar.gz" .
cd ../../..

echo.
echo Done