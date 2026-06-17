param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [string]$Generator = "Visual Studio 17 2022",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

function Get-VsWherePath {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }

if (-not $cmakePath) {
    $vswhere = Get-VsWherePath
    if ($vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version "[17.0,18.0)" -property installationPath
        if ($vsPath) {
            $bundledCMake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $bundledCMake) {
                $cmakePath = $bundledCMake
            }
        }
    }
}

if (-not $cmakePath) {
    throw "cmake was not found. Install Visual Studio 2022 with CMake tools, or add cmake.exe to PATH."
}

& (Join-Path $PSScriptRoot "ensure-webview2-sdk.ps1")

& $cmakePath -S . -B build -G $Generator -A $Platform
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

& $cmakePath --build build --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

Write-Host "Built build\$Configuration\CeftoDecklink.exe"
