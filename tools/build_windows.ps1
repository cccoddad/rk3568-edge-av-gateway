# Builds, tests, and validates from a short Windows path for Ninja compatibility.
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$rkavProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..")) # Repository root.
$rkavCacheRoot = Join-Path $env:LOCALAPPDATA "rkav-gateway" # External short-path cache root.
$rkavSourceLink = Join-Path $rkavCacheRoot "src"
$rkavBuildDir = Join-Path $rkavCacheRoot ("build-" + $Configuration.ToLowerInvariant())

New-Item -ItemType Directory -Force -Path $rkavCacheRoot | Out-Null
if (Test-Path -LiteralPath $rkavSourceLink) {
    $rkavExisting = Get-Item -LiteralPath $rkavSourceLink -Force
    if ($rkavExisting.LinkType -ne "Junction") {
        throw "Build short path exists but is not a junction: $rkavSourceLink"
    }
    $rkavTarget = [System.IO.Path]::GetFullPath([string]$rkavExisting.Target)
    if ($rkavTarget -ne $rkavProjectRoot) {
        throw "Existing junction points to another repository: $rkavTarget"
    }
} else {
    New-Item -ItemType Junction -Path $rkavSourceLink -Target $rkavProjectRoot | Out-Null
}

& cmake -S $rkavSourceLink -B $rkavBuildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DRKAV_BUILD_TESTS=ON" `
    "-DRKAV_ENABLE_MOCK=ON"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& cmake --build $rkavBuildDir -j 4
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

& ctest --test-dir $rkavBuildDir --output-on-failure --timeout 15
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }

$rkavExecutable = Join-Path $rkavBuildDir "rkav-gateway.exe"
$rkavConfig = Join-Path $rkavSourceLink "config\mock.json"
& $rkavExecutable --validate-config --config $rkavConfig
if ($LASTEXITCODE -ne 0) { throw "Configuration validation failed" }

Write-Host "Build and tests passed. Executable: $rkavExecutable"
