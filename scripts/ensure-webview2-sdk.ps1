param(
    [string]$Version = "1.0.3537.50"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$packageRoot = Join-Path $repoRoot "third_party\webview2"
$sdkRoot = Join-Path $packageRoot "sdk"
$headerPath = Join-Path $sdkRoot "build\native\include\WebView2.h"
$loaderLibPath = Join-Path $sdkRoot "build\native\x64\WebView2Loader.dll.lib"

if ((Test-Path -LiteralPath $headerPath) -and (Test-Path -LiteralPath $loaderLibPath)) {
    Write-Host "WebView2 SDK already present at $sdkRoot"
    return
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

$packageFile = Join-Path $packageRoot "Microsoft.Web.WebView2.$Version.nupkg"
$packageUrl = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$Version"

if (-not (Test-Path -LiteralPath $packageFile)) {
    Write-Host "Downloading Microsoft.Web.WebView2 $Version..."
    Invoke-WebRequest -Uri $packageUrl -OutFile $packageFile
}

if (Test-Path -LiteralPath $sdkRoot) {
    $resolvedSdkRoot = (Resolve-Path -LiteralPath $sdkRoot).Path
    $resolvedPackageRoot = (Resolve-Path -LiteralPath $packageRoot).Path
    if (-not $resolvedSdkRoot.StartsWith($resolvedPackageRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace unexpected SDK path: $resolvedSdkRoot"
    }

    Remove-Item -LiteralPath $sdkRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $sdkRoot | Out-Null
$zipPackageFile = Join-Path $packageRoot "Microsoft.Web.WebView2.$Version.zip"
Copy-Item -LiteralPath $packageFile -Destination $zipPackageFile -Force
Expand-Archive -LiteralPath $zipPackageFile -DestinationPath $sdkRoot -Force

if (-not ((Test-Path -LiteralPath $headerPath) -and (Test-Path -LiteralPath $loaderLibPath))) {
    throw "WebView2 SDK download did not contain the expected native files."
}

Write-Host "WebView2 SDK ready at $sdkRoot"
