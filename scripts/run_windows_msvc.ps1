$ErrorActionPreference = "Stop"

$QtPrefix = if ($env:QT_PREFIX) { $env:QT_PREFIX } else { "D:/Qt/Qt6.8.3/6.8.3/msvc2022_64" }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-windows-msvc" }
$ViewerArgs = @()

for ($i = 0; $i -lt $args.Count; $i++) {
    switch ($args[$i]) {
        "-QtPrefix" {
            $i++
            if ($i -ge $args.Count) { throw "-QtPrefix requires a value." }
            $QtPrefix = $args[$i]
        }
        "-BuildDir" {
            $i++
            if ($i -ge $args.Count) { throw "-BuildDir requires a value." }
            $BuildDir = $args[$i]
        }
        default {
            $ViewerArgs += $args[$i]
        }
    }
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$ExePath = Join-Path $BuildPath "Release/FEModelViewer.exe"
$QtBin = Join-Path $QtPrefix "bin"
$OccBin = Join-Path $RepoRoot "third_party/windows/opencascade/win64/vc14/bin"

if (-not (Test-Path $ExePath)) {
    throw "FEModelViewer.exe not found at '$ExePath'. Run scripts/build_windows_msvc.ps1 first."
}

if (-not (Test-Path (Join-Path $QtBin "Qt6Core.dll"))) {
    throw "Qt runtime not found under '$QtBin'. Pass -QtPrefix or set QT_PREFIX."
}

if (-not (Test-Path (Join-Path $OccBin "TKernel.dll"))) {
    throw "Windows OpenCASCADE runtime not found under '$OccBin'."
}

$env:Path = "$BuildPath/Release;$QtBin;$OccBin;$env:Path"
& $ExePath @ViewerArgs
exit $LASTEXITCODE
