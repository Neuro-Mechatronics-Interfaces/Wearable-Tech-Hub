<#
.SYNOPSIS
    Configure and build the Wearable Tech Hub firmware for Pico 2 W.

.DESCRIPTION
    Wraps the CMake configure + build steps into one command.
    Run from any directory — the script locates the repo root from its own
    location ($PSScriptRoot).

.PARAMETER PicoSdkPath
    Path to a Pico SDK checkout.  Defaults to the PICO_SDK_PATH environment
    variable.

.PARAMETER PicoToolchainPath
    Path to the ARM GNU toolchain bin/ directory (e.g. the folder that contains
    arm-none-eabi-gcc.exe).  Defaults to PICO_TOOLCHAIN_PATH env var.

.PARAMETER BuildDir
    CMake binary / build directory.  Defaults to <repo-root>\build.

.PARAMETER BuildType
    CMake build type: Release (default) | Debug | RelWithDebInfo | MinSizeRel.

.PARAMETER Generator
    CMake generator.  Defaults to "Ninja".
    Use "Visual Studio 17 2022" if you prefer the VS generator.

.PARAMETER Board
    Pico board name passed to PICO_BOARD.  Defaults to pico2_w.

.PARAMETER GpioButtons
    Pass -GpioButtons to compile in the physical button / LED support
    (HUB_ENABLE_GPIO_BUTTONS).  Off by default.

.PARAMETER ConfigureOnly
    Stop after CMake configure; do not build.

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER VsInstallationPath
    Explicit path to a Visual Studio installation root.  If omitted the script
    finds the latest VS2022 install via vswhere.

.PARAMETER NoVsDevShell
    Skip importing the Visual Studio developer environment.  Use when already
    running inside a VS developer prompt.

.PARAMETER SkipPublish
    Do not copy the built UF2 to the release folder and do not increment the
    VERSION file.  Useful for test / Debug builds where you don't want to cut
    a new numbered release.

.EXAMPLE
    # Minimal — uses PICO_SDK_PATH / PICO_TOOLCHAIN_PATH env vars:
    .\build.ps1

.EXAMPLE
    # Explicit paths, enable GPIO buttons, clean rebuild:
    .\build.ps1 `
        -PicoSdkPath      C:\MyRepos\RPi\pico-sdk `
        -PicoToolchainPath C:\Toolchains\arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin `
        -GpioButtons -Clean

.EXAMPLE
    # Configure only, Debug build:
    .\build.ps1 -BuildType Debug -ConfigureOnly

.EXAMPLE
    # Use Visual Studio generator instead of Ninja:
    .\build.ps1 -Generator "Visual Studio 17 2022"
#>

[CmdletBinding()]
param(
    [string]$PicoSdkPath        = $env:PICO_SDK_PATH,
    [string]$PicoToolchainPath  = $env:PICO_TOOLCHAIN_PATH,
    [string]$BuildDir,
    [string]$BuildType          = "Release",
    [string]$Generator          = "Ninja",
    [string]$Board              = "pico2_w",
    [switch]$GpioButtons,
    [switch]$ConfigureOnly,
    [switch]$Clean,
    [string]$VsInstallationPath,
    [switch]$NoVsDevShell,
    [switch]$SkipPublish
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# ── Paths ─────────────────────────────────────────────────────────────────────
$script:RepoRoot = $PSScriptRoot   # This script lives at the repo root

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $script:RepoRoot "build"
}

# ── Version ───────────────────────────────────────────────────────────────────
# VERSION is a plain-text file containing "major.minor" (e.g. "1.1").
# Each successful build publishes the UF2 under that version tag and then
# auto-increments the minor component so the next build gets a fresh label.
# To start a new major (e.g. v2.0) simply edit VERSION before building.
$script:VersionFile = Join-Path $script:RepoRoot "VERSION"
if (Test-Path $script:VersionFile) {
    $script:CurrentVersion = (Get-Content $script:VersionFile -Raw).Trim()
} else {
    $script:CurrentVersion = "1.0"
}
$script:VerParts = $script:CurrentVersion -split '\.'
$script:VerMajor = [int]$script:VerParts[0]
$script:VerMinor = [int]$script:VerParts[1]

# ── Helpers ───────────────────────────────────────────────────────────────────
function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Fail {
    param([string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Invoke-External {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $script:RepoRoot
    )
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            Fail "Command failed (exit $LASTEXITCODE): $FilePath $($Arguments -join ' ')"
        }
    } finally {
        Pop-Location
    }
}

function Get-CommandPath {
    param([Parameter(Mandatory)][string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) { return $null }
    return $cmd.Source
}

function Add-ToPathFront {
    param([Parameter(Mandatory)][string]$PathEntry)
    $parts = if ($env:PATH) { $env:PATH -split ";" } else { @() }
    if ($parts -notcontains $PathEntry) {
        $env:PATH = "$PathEntry;$env:PATH"
    }
}

function Import-VsDevShell {
    param([string]$PreferredInstallPath)

    if ($NoVsDevShell) { return }
    if (Get-CommandPath "cl.exe") {
        Write-Host "(VS build environment already active)" -ForegroundColor DarkGray
        return
    }

    $vsDevCmd = $null

    if (-not [string]::IsNullOrWhiteSpace($PreferredInstallPath)) {
        $candidate = Join-Path $PreferredInstallPath "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) { $vsDevCmd = $candidate }
    }

    if ($null -eq $vsDevCmd) {
        $vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vsWhere)) {
            Fail "vswhere.exe not found.  Install VS 2022 Build Tools or run from a VS developer prompt."
        }
        $installPath = & $vsWhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
            Fail "Could not locate a Visual Studio installation with Desktop C++ tools."
        }
        $candidate = Join-Path $installPath.Trim() "Common7\Tools\VsDevCmd.bat"
        if (-not (Test-Path $candidate)) {
            Fail "VsDevCmd.bat not found under '$installPath'."
        }
        $vsDevCmd = $candidate
    }

    Write-Step "Importing Visual Studio build environment"
    $output = cmd.exe /s /c "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { Fail "Failed to import VS environment from '$vsDevCmd'." }

    foreach ($line in $output) {
        if ($line -match "^([^=]+)=(.*)$") {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Resolve-RequiredDir {
    param([string]$PathValue, [string]$Description)
    if ([string]::IsNullOrWhiteSpace($PathValue)) { Fail "$Description was not provided." }
    $resolved = Resolve-Path $PathValue -ErrorAction SilentlyContinue
    if ($null -eq $resolved) { Fail "$Description does not exist: $PathValue" }
    return $resolved.Path
}

function Find-Uf2 {
    param([string]$BuildDirectory)
    $hits = @(Get-ChildItem -Path $BuildDirectory -Filter "wth_firmware.uf2" -Recurse -ErrorAction SilentlyContinue |
              Sort-Object FullName)
    if ($hits.Count -gt 0) { return $hits[0].FullName }
    return $null
}

# ── Preflight ─────────────────────────────────────────────────────────────────
Write-Step "Preparing build environment"
Import-VsDevShell -PreferredInstallPath $VsInstallationPath

$PicoSdkPath = Resolve-RequiredDir -PathValue $PicoSdkPath -Description "PICO_SDK_PATH"
$env:PICO_SDK_PATH = $PicoSdkPath

if (-not [string]::IsNullOrWhiteSpace($PicoToolchainPath)) {
    $PicoToolchainPath = Resolve-RequiredDir -PathValue $PicoToolchainPath -Description "PICO_TOOLCHAIN_PATH"
    $env:PICO_TOOLCHAIN_PATH = $PicoToolchainPath
    Add-ToPathFront -PathEntry $PicoToolchainPath
}

$sdkImport = Join-Path $PicoSdkPath "external\pico_sdk_import.cmake"
if (-not (Test-Path $sdkImport)) {
    Fail "PICO_SDK_PATH does not look like a Pico SDK checkout (missing $sdkImport)."
}

$cmakePath = Get-CommandPath "cmake.exe"
if ($null -eq $cmakePath) { $cmakePath = Get-CommandPath "cmake" }
if ($null -eq $cmakePath) { Fail "cmake not found on PATH." }

if ($Generator -eq "Ninja") {
    $ninjaPath = Get-CommandPath "ninja.exe"
    if ($null -eq $ninjaPath) { $ninjaPath = Get-CommandPath "ninja" }
    if ($null -eq $ninjaPath) { Fail "Ninja not found on PATH.  Install Ninja or omit -Generator Ninja." }
}

$armGcc = Get-CommandPath "arm-none-eabi-gcc.exe"
if ($null -eq $armGcc) { $armGcc = Get-CommandPath "arm-none-eabi-gcc" }
if ($null -eq $armGcc) {
    Fail "arm-none-eabi-gcc not found.  Set -PicoToolchainPath or add the ARM toolchain to PATH."
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "PICO_SDK_PATH       = $PicoSdkPath"       -ForegroundColor DarkGray
if (-not [string]::IsNullOrWhiteSpace($PicoToolchainPath)) {
    Write-Host "PICO_TOOLCHAIN_PATH = $PicoToolchainPath" -ForegroundColor DarkGray
}
Write-Host "CMake               = $cmakePath"           -ForegroundColor DarkGray
Write-Host "ARM GCC             = $armGcc"              -ForegroundColor DarkGray
Write-Host "Generator           = $Generator"           -ForegroundColor DarkGray
Write-Host "BuildType           = $BuildType"           -ForegroundColor DarkGray
Write-Host "BuildDir            = $BuildDir"            -ForegroundColor DarkGray
Write-Host "GpioButtons         = $($GpioButtons.IsPresent)" -ForegroundColor DarkGray
Write-Host "Version             = $script:CurrentVersion" -ForegroundColor DarkGray
Write-Host "SkipPublish         = $($SkipPublish.IsPresent)" -ForegroundColor DarkGray

# ── Clean ─────────────────────────────────────────────────────────────────────
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Removing build directory"
    Remove-Item -Recurse -Force $BuildDir
}

# ── Configure ─────────────────────────────────────────────────────────────────
Write-Step "Configuring (cmake)"
$configArgs = @(
    "-S", $script:RepoRoot,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DHUB_BUILD=ON",
    "-DHUB_GPIO_BUTTONS=$( if ($GpioButtons) { 'ON' } else { 'OFF' } )",
    "-DPICO_BOARD=$Board",
    "-DCMAKE_BUILD_TYPE=$BuildType"
)
if (-not [string]::IsNullOrWhiteSpace($PicoToolchainPath)) {
    $configArgs += "-DPICO_TOOLCHAIN_PATH=$PicoToolchainPath"
}
Invoke-External -FilePath $cmakePath -Arguments $configArgs

if ($ConfigureOnly) {
    Write-Host "`nConfigure complete (skipping build, -ConfigureOnly was set)." -ForegroundColor Yellow
    exit 0
}

# ── Build ─────────────────────────────────────────────────────────────────────
Write-Step "Building (cmake --build)"
$buildArgs = @("--build", $BuildDir)
if ($Generator -match "Visual Studio") {
    $buildArgs += @("--config", $BuildType)
}
Invoke-External -FilePath $cmakePath -Arguments $buildArgs

# ── Report ────────────────────────────────────────────────────────────────────
$uf2 = Find-Uf2 -BuildDirectory $BuildDir
Write-Host ""
if ($null -ne $uf2) {
    Write-Host "==> Build complete" -ForegroundColor Green
    Write-Host "    UF2: $uf2" -ForegroundColor Green
    Write-Host ""
    Write-Host "Flash: hold BOOTSEL, plug in USB, then copy the UF2 to the RPI-RP2 drive." -ForegroundColor DarkGray

    if (-not $SkipPublish) {
        # ── Publish release ───────────────────────────────────────────────────
        # Copy the UF2 into the configurator's public/release/ tree if that
        # directory exists (i.e. this repo is checked out as a submodule inside
        # the parent Mudra repo).  Silently skipped when cloned standalone.
        $releaseRoot = Join-Path $script:RepoRoot "..\configurator\frontend\public\release"
        $releaseRoot = [System.IO.Path]::GetFullPath($releaseRoot)
        if (Test-Path $releaseRoot) {
            Write-Step "Publishing release v$script:CurrentVersion"
            $releaseDir = Join-Path $releaseRoot "v$script:CurrentVersion"
            if (-not (Test-Path $releaseDir)) {
                New-Item -ItemType Directory -Path $releaseDir | Out-Null
            }
            $dest = Join-Path $releaseDir "wth_firmware.uf2"
            Copy-Item -Path $uf2 -Destination $dest -Force
            Write-Host "    Published: $dest" -ForegroundColor Green
        } else {
            Write-Host "    (release publish skipped — configurator tree not found)" -ForegroundColor DarkGray
        }

        # ── Increment minor version ───────────────────────────────────────────
        $nextVersion = "$script:VerMajor.$($script:VerMinor + 1)"
        Set-Content -Path $script:VersionFile -Value $nextVersion -NoNewline
        Write-Host "    Version $script:CurrentVersion → $nextVersion (VERSION updated)" -ForegroundColor Cyan
        Write-Host "    Edit VERSION to change the major version or reset the counter." -ForegroundColor DarkGray
    }
} else {
    Write-Host "==> Build finished" -ForegroundColor Green
    $artifacts = @(Get-ChildItem -Path $BuildDir -Recurse -ErrorAction SilentlyContinue |
                   Where-Object { $_.Name -match '^wth_firmware\.(elf|bin|hex)$' })
    foreach ($a in $artifacts) {
        Write-Host "    $($a.Extension.TrimStart('.').ToUpper()): $($a.FullName)" -ForegroundColor Green
    }
    if ($artifacts.Count -eq 0) {
        Write-Host "    No wth_firmware artifacts found under '$BuildDir'." -ForegroundColor Yellow
    }
}
