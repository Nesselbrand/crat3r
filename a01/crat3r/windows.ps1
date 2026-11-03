[CmdletBinding()]
param(
    [ValidateSet('Setup', 'Build', 'Run')]
    [string]$Action = 'Setup',
    [string]$VulkanSdk = $env:VULKAN_SDK,
    [string]$ClionHome,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed (exit code $LASTEXITCODE)."
    }
}

if (-not $ClionHome) {
    $ClionHome = Get-ChildItem "$env:ProgramFiles/JetBrains" -Directory -Filter 'CLion*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $ClionHome -or -not (Test-Path "$ClionHome/bin/mingw/bin/g++.exe")) {
    throw 'CLion with bundled MinGW is required. Pass -ClionHome if installed elsewhere.'
}
if (-not $VulkanSdk -and (Test-Path 'C:/VulkanSDK')) {
    $VulkanSdk = Get-ChildItem 'C:/VulkanSDK' -Directory |
        Where-Object { Test-Path "$($_.FullName)/Include/vulkan/vulkan.h" } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $VulkanSdk) {
    throw 'Vulkan SDK missing. Install the Windows x64 SDK from https://vulkan.lunarg.com/sdk/home/ and retry, or pass -VulkanSdk <path>.'
}
foreach ($component in @('Include/vulkan/vulkan.h', 'Bin/glslangValidator.exe', 'Bin/glslc.exe')) {
    if (-not (Test-Path "$VulkanSdk/$component")) { throw "Vulkan SDK component missing: $VulkanSdk/$component" }
}

$cmake = "$ClionHome/bin/cmake/win/x64/bin/cmake.exe"
$ninja = "$ClionHome/bin/ninja/win/x64/ninja.exe"
foreach ($tool in @($cmake, $ninja)) {
    if (-not (Test-Path $tool)) { throw "Tool missing: $tool" }
}
$env:VULKAN_SDK = (Resolve-Path $VulkanSdk).Path
$env:PATH = "$ClionHome/bin/mingw/bin;$env:VULKAN_SDK/Bin;$env:PATH"
$build = Join-Path $PSScriptRoot "build/windows-$($Configuration.ToLowerInvariant())"

Push-Location $PSScriptRoot
try {
    Invoke-Checked $cmake @('-S', '.', '-B', $build, '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_C_COMPILER=$ClionHome/bin/mingw/bin/gcc.exe",
        "-DCMAKE_CXX_COMPILER=$ClionHome/bin/mingw/bin/g++.exe")
    Invoke-Checked $cmake @('--build', $build, '--target', 'a01', '--parallel', '4')
    if ($Action -eq 'Run') {
        Invoke-Checked (Join-Path $build 'a01.exe') @()
    } else {
        Write-Host "Build ready. Start with: .\windows.ps1 Run"
    }
} finally {
    Pop-Location
}
