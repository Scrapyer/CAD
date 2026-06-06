param(
    [string]$QtPrefix = $(if ($env:QT_PREFIX) { $env:QT_PREFIX } else { "D:/Qt/Qt6.8.3/6.8.3/msvc2022_64" }),
    [string]$BuildDir = $(if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-windows-msvc" }),
    [string]$InstallPrefix = $(if ($env:INSTALL_PREFIX) { $env:INSTALL_PREFIX } else { "install-windows-msvc" }),
    [switch]$Clean,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$InstallPath = Join-Path $RepoRoot $InstallPrefix
$QtBin = Join-Path $QtPrefix "bin"
$OccBin = Join-Path $RepoRoot "third_party/windows/opencascade/win64/vc14/bin"

if (-not (Test-Path (Join-Path $QtPrefix "lib/cmake/Qt6/Qt6Config.cmake"))) {
    throw "Qt 6 CMake config not found under '$QtPrefix'. Pass -QtPrefix or set QT_PREFIX."
}

if (-not (Test-Path (Join-Path $OccBin "TKernel.dll"))) {
    throw "Windows OpenCASCADE runtime not found under '$OccBin'."
}

if ($Clean -and (Test-Path $BuildPath)) {
    Remove-Item -LiteralPath $BuildPath -Recurse -Force
}

cmake -S $RepoRoot -B $BuildPath `
    -DCMAKE_PREFIX_PATH=$QtPrefix `
    -DCMAKE_INSTALL_PREFIX=$InstallPath

cmake --build $BuildPath --config Release --parallel

if (-not $SkipTests) {
    $env:Path = "$BuildPath/Release;$QtBin;$OccBin;$env:Path"
    ctest --test-dir $BuildPath -C Release --output-on-failure
}
