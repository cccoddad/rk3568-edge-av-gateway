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
$rkavDependencyRoot = Join-Path $rkavCacheRoot "dependencies"
$rkavJpegArchive = Join-Path $rkavDependencyRoot "libjpeg-turbo-3.1.4.1.tar.gz"
$rkavJpegSource = Join-Path $rkavDependencyRoot "libjpeg-turbo-3.1.4.1"
$rkavJpegSha256 = "ecae8008e2cc9ade2f2c1bb9d5e6d4fb73e7c433866a056bd82980741571a022"

New-Item -ItemType Directory -Force -Path $rkavCacheRoot | Out-Null
New-Item -ItemType Directory -Force -Path $rkavDependencyRoot | Out-Null

# MinGW CMake lacks Windows CA trust anchors; use system curl and verify the pinned SHA-256.
if (-not (Test-Path -LiteralPath $rkavJpegArchive)) {
    $rkavCurlArguments = @(
        "--ssl-no-revoke", "--fail", "--location", "--retry", "5", "--retry-all-errors",
        "--connect-timeout", "20", "--max-time", "600",
        "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.4.1/libjpeg-turbo-3.1.4.1.tar.gz",
        "--output", $rkavJpegArchive
    )
    & curl.exe @rkavCurlArguments
    if ($LASTEXITCODE -ne 0) { throw "libjpeg-turbo download failed" }
}
$rkavActualJpegHash = (Get-FileHash -LiteralPath $rkavJpegArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($rkavActualJpegHash -ne $rkavJpegSha256) {
    throw "libjpeg-turbo archive hash mismatch: $rkavActualJpegHash"
}
if (-not (Test-Path -LiteralPath (Join-Path $rkavJpegSource "CMakeLists.txt"))) {
    Push-Location $rkavDependencyRoot
    try {
        & cmake -E tar xzf $rkavJpegArchive
        if ($LASTEXITCODE -ne 0) { throw "libjpeg-turbo extraction failed" }
    } finally {
        Pop-Location
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $rkavJpegSource "CMakeLists.txt"))) {
    throw "libjpeg-turbo source directory is incomplete: $rkavJpegSource"
}
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
    "-DRKAV_ENABLE_MOCK=ON" `
    "-DRKAV_WITH_JPEG=ON" `
    "-DFETCHCONTENT_SOURCE_DIR_LIBJPEG_TURBO=$rkavJpegSource" `
    "-DRKAV_WITH_V4L2=OFF"
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
