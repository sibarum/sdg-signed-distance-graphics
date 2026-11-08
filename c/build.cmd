@echo off
rem Builds the C rasterizer with MSVC and runs its conformance test, from a plain shell: loads the Visual Studio
rem environment (found with vswhere), then configures, builds and tests with the CMake Visual Studio carries.
rem
rem     build.cmd            from cmd or PowerShell
rem     ./build.cmd          from Git Bash
rem
rem Anywhere else, CMake alone: cmake -S . -B build && cmake --build build && ctest --test-dir build
setlocal
set "VSDIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSDIR%\vswhere.exe" (
    echo build: vswhere.exe not found; is Visual Studio installed? 1>&2
    exit /b 1
)
set "VCVARS="
pushd "%VSDIR%"
for /f "usebackq delims=" %%i in (`.\vswhere.exe -latest -products * -find VC\Auxiliary\Build\vcvars64.bat 2^>nul`) do set "VCVARS=%%i"
popd
if not defined VCVARS (
    echo build: no vcvars64.bat found; install the "Desktop development with C++" workload. 1>&2
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
ctest --test-dir build --output-on-failure
exit /b %ERRORLEVEL%
