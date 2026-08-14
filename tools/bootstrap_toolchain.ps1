param(
    [string]$ToolchainRoot,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
Set-StrictMode -Version Latest

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $ToolchainRoot) {
    $ToolchainRoot = Join-Path $RepoRoot ".toolchain"
}
$ToolchainRoot = [System.IO.Path]::GetFullPath($ToolchainRoot)
$Cache = Join-Path $ToolchainRoot "cache"
$Staging = Join-Path $ToolchainRoot "staging"

$packages = @(
    [ordered]@{
        Name = "llvm-mingw"
        Version = "20260616"
        Archive = "llvm-mingw-20260616-ucrt-x86_64.zip"
        Url = "https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip"
        Sha256 = "b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35"
        Install = "llvm-mingw"
        Layout = "single-directory"
        Sentinel = "bin\clang++.exe"
    },
    [ordered]@{
        Name = "cmake"
        Version = "4.3.3"
        Archive = "cmake-4.3.3-windows-x86_64.zip"
        Url = "https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip"
        Sha256 = "935ade9e5e8723583c07f44c5592cea2a1c8f65c56ca7e07b34c025c880e0bd6"
        Install = "cmake"
        Layout = "single-directory"
        Sentinel = "bin\cmake.exe"
    },
    [ordered]@{
        Name = "ninja"
        Version = "1.13.2"
        Archive = "ninja-win.zip"
        Url = "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip"
        Sha256 = "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65"
        Install = "ninja"
        Layout = "flat"
        Sentinel = "ninja.exe"
    }
)

function Assert-ManagedPath {
    param([string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    $prefix = $ToolchainRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside toolchain root: $full"
    }
    return $full
}

function Remove-ManagedDirectory {
    param([string]$Path)
    $full = Assert-ManagedPath $Path
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

function Get-VerifiedArchive {
    param($Package)
    $archivePath = Join-Path $Cache $Package.Archive
    if (Test-Path -LiteralPath $archivePath) {
        $actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -eq $Package.Sha256) { return $archivePath }
        Remove-Item -LiteralPath $archivePath -Force
    }

    $partial = "$archivePath.partial"
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    Invoke-WebRequest -Headers @{"User-Agent" = "Dao-Kernel-Toolchain"} -Uri $Package.Url -OutFile $partial
    $actual = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Package.Sha256) {
        Remove-Item -LiteralPath $partial -Force
        throw "SHA-256 mismatch for $($Package.Archive): $actual"
    }
    Move-Item -LiteralPath $partial -Destination $archivePath
    return $archivePath
}

New-Item -ItemType Directory -Force -Path $ToolchainRoot, $Cache, $Staging | Out-Null

foreach ($package in $packages) {
    $installPath = Assert-ManagedPath (Join-Path $ToolchainRoot $package.Install)
    $sentinel = Join-Path $installPath $package.Sentinel
    if ((Test-Path -LiteralPath $sentinel) -and -not $Force) {
        Write-Output "$($package.Name) $($package.Version): present"
        continue
    }

    $archivePath = Get-VerifiedArchive $package
    $stagePath = Assert-ManagedPath (Join-Path $Staging $package.Name)
    Remove-ManagedDirectory $stagePath
    Remove-ManagedDirectory $installPath
    New-Item -ItemType Directory -Force -Path $stagePath | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $stagePath -Force

    if ($package.Layout -eq "single-directory") {
        $roots = @(Get-ChildItem -LiteralPath $stagePath -Directory)
        if ($roots.Count -ne 1) { throw "Unexpected archive layout for $($package.Name)" }
        Move-Item -LiteralPath $roots[0].FullName -Destination $installPath
    } else {
        New-Item -ItemType Directory -Force -Path $installPath | Out-Null
        Get-ChildItem -LiteralPath $stagePath -Force | Move-Item -Destination $installPath
    }
    Remove-ManagedDirectory $stagePath
    if (-not (Test-Path -LiteralPath $sentinel)) {
        throw "Installation sentinel missing for $($package.Name): $sentinel"
    }
    Write-Output "$($package.Name) $($package.Version): installed"
}

$manifest = [ordered]@{
    schema = 1
    generated_at = (Get-Date).ToUniversalTime().ToString("o")
    root = $ToolchainRoot
    packages = @($packages | ForEach-Object {
        [ordered]@{
            name = $_.Name
            version = $_.Version
            archive = $_.Archive
            url = $_.Url
            sha256 = $_.Sha256
            path = (Join-Path $ToolchainRoot $_.Install)
        }
    })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ToolchainRoot "manifest.json") -Encoding UTF8
Write-Output "toolchain ready: $ToolchainRoot"
