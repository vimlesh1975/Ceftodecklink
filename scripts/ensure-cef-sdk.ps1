param(
    [string]$Version = "149.0.3+gd84bb73+chromium-149.0.7827.115",
    [string]$Platform = "windows64",
    [string]$Distribution = "minimal",
    [string]$ExpectedSha1 = "17736a1c36482749bc165d1291baa974d270e0ea"
)

$ErrorActionPreference = "Stop"

$thirdPartyRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "third_party\cef"
$packageBaseName = "cef_binary_${Version}_${Platform}_${Distribution}"
$packageFile = Join-Path $thirdPartyRoot "$packageBaseName.tar.bz2"
$sdkRoot = Join-Path $thirdPartyRoot $packageBaseName
$packageUrl = "https://cef-builds.spotifycdn.com/$packageBaseName.tar.bz2"

$headerPath = Join-Path $sdkRoot "include\cef_app.h"
$libPath = Join-Path $sdkRoot "Release\libcef.lib"
$dllPath = Join-Path $sdkRoot "Release\libcef.dll"

if ((Test-Path -LiteralPath $headerPath) -and
    (Test-Path -LiteralPath $libPath) -and
    (Test-Path -LiteralPath $dllPath)) {
    Write-Host "CEF SDK already present at $sdkRoot"
    Write-Output $sdkRoot
    return
}

New-Item -ItemType Directory -Force -Path $thirdPartyRoot | Out-Null

if (-not (Test-Path -LiteralPath $packageFile)) {
    Write-Host "Downloading CEF $Version $Platform $Distribution..."
    Invoke-WebRequest -Uri $packageUrl -OutFile $packageFile
}

$actualSha1 = (Get-FileHash -Path $packageFile -Algorithm SHA1).Hash.ToLowerInvariant()
if ($actualSha1 -ne $ExpectedSha1.ToLowerInvariant()) {
    throw "CEF package SHA1 mismatch. Expected $ExpectedSha1, got $actualSha1."
}

if (Test-Path -LiteralPath $sdkRoot) {
    Remove-Item -LiteralPath $sdkRoot -Recurse -Force
}

Write-Host "Extracting CEF package..."
tar -xf $packageFile -C $thirdPartyRoot

if (-not ((Test-Path -LiteralPath $headerPath) -and
          (Test-Path -LiteralPath $libPath) -and
          (Test-Path -LiteralPath $dllPath))) {
    throw "CEF package extraction did not contain the expected native files."
}

Write-Host "CEF SDK ready at $sdkRoot"
Write-Output $sdkRoot
