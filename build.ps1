# Build NextGenDiskCache 2.3.0 (DLL) into package\SKSE\Plugins.
# Robust across Visual Studio 2019/2022/2026: the CMake generator is derived
# from whatever vswhere reports. No downloads: every dependency is in deps\.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

# --- Visual Studio toolchain (version-agnostic) ------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    # Some shells (e.g. Git Bash) do not forward the "ProgramFiles(x86)" env
    # var to child processes; fall back to the canonical absolute location.
    $vswhere = "${env:SystemDrive}\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio Build Tools" }
$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vs) { throw "Visual Studio with C++ MSBuild tools not found" }
$vsVer = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationVersion
$vsMajor = [int]($vsVer.Split('.')[0])
switch ($vsMajor) {
    18 { $generator = "Visual Studio 18 2026" }
    17 { $generator = "Visual Studio 17 2022" }
    16 { $generator = "Visual Studio 16 2019" }
    default { throw "Unsupported Visual Studio major version: $vsMajor ($vsVer)" }
}
Write-Host "Using $generator (VS $vsVer at $vs)"
$vsDev = Join-Path $vs "Common7\Tools\VsDevCmd.bat"

$cmd = @"
call "$vsDev" -arch=amd64 -host_arch=amd64 >nul
cmake -S . -B build -G "$generator" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1
"@
$tmp = Join-Path $env:TEMP "ngdc_build.cmd"
Set-Content -Path $tmp -Value $cmd -Encoding ASCII
& cmd.exe /c $tmp
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

$dll = Join-Path $here "package\SKSE\Plugins\NextGenDiskCache.dll"
if (-not (Test-Path $dll)) { throw "DLL missing after build: $dll" }
Get-Item $dll | Format-List FullName, Length, LastWriteTime
Write-Host "BUILD OK: $dll"
