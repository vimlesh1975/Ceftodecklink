param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [string]$Generator = "",
    [string]$Platform = "x64",

    [ValidateSet("Auto", "On", "Off")]
    [string]$DeckLink = "Auto"
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

function Get-LatestVisualStudio {
    param(
        [string[]]$Requires = @()
    )

    $vswhere = Get-VsWherePath
    if (-not $vswhere) {
        return $null
    }

    $arguments = @(
        "-latest",
        "-products", "*",
        "-version", "[17.0,19.0)",
        "-format", "json"
    )

    foreach ($require in $Requires) {
        $arguments += @("-requires", $require)
    }

    $json = & $vswhere @arguments
    if (-not $json) {
        return $null
    }

    $instances = $json | ConvertFrom-Json
    if ($instances -is [array]) {
        return $instances[0]
    }

    return $instances
}

function Get-VisualStudioGenerator {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallationVersion
    )

    $major = [int]($InstallationVersion.Split(".")[0])
    switch ($major) {
        18 { return "Visual Studio 18 2026" }
        17 { return "Visual Studio 17 2022" }
        default { throw "Unsupported Visual Studio major version: $major." }
    }
}

function Get-DeckLinkApiPath {
    $candidates = @(
        "$env:ProgramFiles\Blackmagic Design\Blackmagic Desktop Video\DeckLinkAPI64.dll",
        "${env:ProgramFiles(x86)}\Blackmagic Design\Blackmagic Desktop Video\DeckLinkAPI.dll"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

$vsWithCpp = Get-LatestVisualStudio -Requires @("Microsoft.VisualStudio.Component.VC.Tools.x86.x64")
$vsForTools = if ($vsWithCpp) { $vsWithCpp } else { Get-LatestVisualStudio }
$cmakePath = $null

if ($vsForTools) {
    $bundledCMake = Join-Path $vsForTools.installationPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $bundledCMake) {
        $cmakePath = $bundledCMake
    }
}

if (-not $cmakePath) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    $cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
}

if (-not $cmakePath) {
    throw "cmake was not found. Install CMake on PATH, or install Visual Studio 2022/2026 with CMake tools."
}

if (-not $Generator) {
    if (-not $vsWithCpp) {
        throw "Visual C++ build tools were not found. Install the Desktop development with C++ workload for Visual Studio 2022/2026."
    }

    $Generator = Get-VisualStudioGenerator -InstallationVersion $vsWithCpp.installationVersion
}

& (Join-Path $PSScriptRoot "ensure-webview2-sdk.ps1")

$deckLinkApiPath = Get-DeckLinkApiPath
$deckLinkEnabled = switch ($DeckLink) {
    "On" { "ON" }
    "Off" { "OFF" }
    default { if ($deckLinkApiPath) { "ON" } else { "OFF" } }
}

if ($DeckLink -eq "On" -and -not $deckLinkApiPath) {
    throw "DeckLink was requested, but the Blackmagic DeckLink API DLL was not found."
}

& $cmakePath -S . -B build -G $Generator -A $Platform -DCEFTOD_WITH_DECKLINK=$deckLinkEnabled
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

& $cmakePath --build build --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

Write-Host "Built build\$Configuration\CeftoDecklink.exe"
