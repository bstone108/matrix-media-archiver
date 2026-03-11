Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path
$versionFile = Join-Path $rootDir "VERSION.txt"
if (-not (Test-Path $versionFile)) {
    throw "Version file not found: $versionFile"
}
$version = (Get-Content $versionFile -Raw).Trim()

$windowsArch = if ($env:WINDOWS_ARCH) { $env:WINDOWS_ARCH.ToLowerInvariant() } else { "x64" }
switch ($windowsArch) {
    "x64" {
        $cmakeArch = "x64"
        $rustTarget = if ($env:RUST_TARGET) { $env:RUST_TARGET } else { "x86_64-pc-windows-msvc" }
    }
    "arm64" {
        $cmakeArch = "ARM64"
        $rustTarget = if ($env:RUST_TARGET) { $env:RUST_TARGET } else { "aarch64-pc-windows-msvc" }
    }
    default {
        throw "Unsupported WINDOWS_ARCH '$windowsArch'. Expected x64 or arm64."
    }
}

$buildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $rootDir ".work/windows/build-msvc-$windowsArch" }
$stageDir = if ($env:STAGE_DIR) { $env:STAGE_DIR } else { Join-Path $rootDir ".work/windows/stage-msvc-$windowsArch" }
$buildsDir = if ($env:BUILDS_DIR) { $env:BUILDS_DIR } else { Join-Path $rootDir "builds" }
$archivePath = Join-Path $buildsDir "MatrixMediaArchiverQt-$version-windows-$windowsArch.zip"

New-Item -ItemType Directory -Force -Path $buildsDir | Out-Null

rustup target add $rustTarget

$cmakeConfigureArgs = @(
    "-S", $rootDir,
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", $cmakeArch,
    "-DMATRIX_MEDIA_ARCHIVER_BACKEND_RUST_TARGET=$rustTarget"
)
& cmake @cmakeConfigureArgs

$cmakeBuildArgs = @(
    "--build", $buildDir,
    "--config", "Release"
)
& cmake @cmakeBuildArgs

$ctestArgs = @(
    "--test-dir", $buildDir,
    "--build-config", "Release",
    "--output-on-failure"
)
& ctest @ctestArgs

$releaseDir = Join-Path $buildDir "Release"
$appExe = Join-Path $releaseDir "MatrixMediaArchiverQt.exe"
$backendExe = Join-Path $releaseDir "matrix_media_archiver_backend.exe"
if (-not (Test-Path $appExe)) {
    throw "Built app not found at $appExe"
}
if (-not (Test-Path $backendExe)) {
    throw "Built Rust backend not found at $backendExe"
}

$windeployqt = (Get-Command windeployqt.exe -ErrorAction Stop).Source

if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

Copy-Item $appExe $stageDir
Copy-Item $backendExe $stageDir

& $windeployqt `
    --release `
    --compiler-runtime `
    --no-translations `
    --dir $stageDir `
    (Join-Path $stageDir "MatrixMediaArchiverQt.exe")

if (Test-Path $archivePath) {
    Remove-Item -Force $archivePath
}

$sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($sevenZip) {
    Push-Location $stageDir
    try {
        & $sevenZip.Source a -bd -mmt=1 -tzip -mx=9 -mfb=258 -mpass=15 $archivePath *
    } finally {
        Pop-Location
    }
} else {
    Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $archivePath -CompressionLevel Optimal
}

Write-Host "Created $archivePath"
