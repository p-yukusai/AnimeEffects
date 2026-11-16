# Install a Qt 6 release for Windows from download.qt.io.
#
# WHY THIS EXISTS AND aqtinstall DOES NOT: from Qt 6.12 the Windows repository
# moved each architecture's Updates.xml into its own folder
# (qt6_6120/qt6_6120_msvc2022_64/Updates.xml). aqtinstall 3.3.0 - the newest
# PyPI release, and therefore what jurplel/install-qt-action installs - still
# builds the old path (qt6_6120/qt6_6120/Updates.xml) and fails with
# "Failed to locate XML data for Qt version '6.12.0'". Linux kept the old
# layout, so this only affects Windows.
#
# The script reads the repository's own Updates.xml, so it downloads exactly
# what the Qt installer would: no file names with build timestamps are
# hard-coded here. The legacy layout is still accepted, so the script keeps
# working if (or when) the repository changes back.
#
# Layout on disk matches a normal Qt install, so CMAKE_PREFIX_PATH winds up as
#   <Prefix>\<Version>\<Arch>
#
# Requires Python (py7zr is installed on demand) and curl.exe (Windows 10+).
#
# Usage:
#   scripts\install-qt-windows.ps1 -Version 6.12.0 -Arch win64_msvc2022_64 -Prefix C:\Qt
#
# -Modules overrides the add-on set; its default is what this tree needs.

[CmdletBinding()]
param(
    [string]$Version = '6.12.0',
    [string]$Arch = 'win64_msvc2022_64',
    [string]$Prefix = 'C:\Qt',
    # The add-on packages this application needs: Canvas Painter is an add-on in
    # 6.12 (not part of qtdeclarative), and Qt Quick pulls TaskTree through its
    # QmlAssetDownloader plugin at configure time.
    [string[]]$Modules = @('qtcanvaspainter', 'qtmultimedia', 'qt5compat', 'qtimageformats', 'qtshadertools', 'qttasktree')
)

$ErrorActionPreference = 'Stop'

$versionNoDots = $Version.Replace('.', '')
# The repository's per-architecture folder drops the toolkit prefix: the aqt
# architecture "win64_msvc2022_64" lives in "qt6_6120_msvc2022_64".
$archFolder = $Arch -replace '^win64_', ''
$repoRoot = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_$versionNoDots"
$legacy = "$repoRoot/qt6_$versionNoDots"
$split = "$repoRoot/qt6_${versionNoDots}_$archFolder"

function Test-Url([string]$Url) {
    # HEAD first (cheap), GET as the fallback: some mirrors answer 405 to HEAD.
    foreach ($method in 'Head', 'Get') {
        try {
            $r = Invoke-WebRequest -Uri $Url -UseBasicParsing -Method $method -TimeoutSec 30
            if ($r.StatusCode -eq 200) { return $true }
        } catch { }
    }
    return $false
}

if (Test-Url "$split/Updates.xml") {
    $base = $split
} elseif (Test-Url "$legacy/Updates.xml") {
    $base = $legacy
} else {
    throw "No Qt $Version repository for $Arch under $repoRoot (tried the split and the legacy layout)."
}
Write-Output "Qt repository: $base"

# The packages this build needs: the desktop base (qtbase, qtdeclarative, qtsvg,
# qttools, translations) plus one add-on package per requested module.
$wanted = @("qt.qt6.$versionNoDots.$Arch")
foreach ($m in $Modules) {
    $wanted += "qt.qt6.$versionNoDots.addons.$m.$Arch"
}

$updates = (Invoke-WebRequest -Uri "$base/Updates.xml" -UseBasicParsing -TimeoutSec 60).Content
$packages = [regex]::Matches($updates, '(?s)<PackageUpdate>(.*?)</PackageUpdate>') | ForEach-Object {
    $body = $_.Groups[1].Value
    [pscustomobject]@{
        Name     = [regex]::Match($body, '<Name>([^<]*)</Name>').Groups[1].Value
        Version  = [regex]::Match($body, '<Version>([^<]*)</Version>').Groups[1].Value
        Archives = [regex]::Match($body, '<DownloadableArchives>([^<]*)</DownloadableArchives>').Groups[1].Value
    }
}

$archives = @()
foreach ($name in $wanted) {
    $pkg = $packages | Where-Object { $_.Name -eq $name } | Select-Object -First 1
    if (-not $pkg) {
        throw "Qt $Version has no package '$name'. Available packages include: " +
              (($packages | Select-Object -First 12 | ForEach-Object { $_.Name }) -join ', ')
    }
    foreach ($a in ($pkg.Archives -split ',')) {
        $a = $a.Trim()
        # The repository stores each archive under the package's build id:
        # "6.12.0-0-202609280346" + "qtbase-Windows-...7z". DownloadableArchives
        # lists only the second half.
        if ($a) { $archives += [pscustomobject]@{ Package = $name; File = "$($pkg.Version)$a" } }
    }
}

$target = Join-Path (Join-Path $Prefix $Version) $Arch
$cache = Join-Path (Join-Path $Prefix '_packages') $Version
New-Item -ItemType Directory -Force -Path $target, $cache | Out-Null

foreach ($a in $archives) {
    $out = Join-Path $cache $a.File
    if (-not (Test-Path $out) -or (Get-Item $out).Length -eq 0) {
        Write-Output "downloading $($a.File)"
        curl.exe -sSL --fail -o $out "$base/$($a.Package)/$($a.File)"
        if ($LASTEXITCODE -ne 0) { throw "download failed: $base/$($a.Package)/$($a.File)" }
    } else {
        Write-Output "cached      $($a.File)"
    }
}

# py7zr rather than 7z.exe: 7-Zip is not part of Windows and the script must run
# on a stock machine. It is only installed when it is missing.
python -c 'import py7zr' *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Output 'installing py7zr'
    python -m pip install --quiet --disable-pip-version-check py7zr
    if ($LASTEXITCODE -ne 0) { throw 'py7zr could not be installed' }
}

$extract = @'
import sys, py7zr
for path, dest in zip(sys.argv[1::2], sys.argv[2::2]):
    print('extracting', path)
    with py7zr.SevenZipFile(path) as archive:
        archive.extractall(path=dest)
'@
$extractScript = Join-Path $cache 'extract.py'
Set-Content -Path $extractScript -Value $extract -Encoding Ascii

$pairs = @()
foreach ($a in $archives) {
    $pairs += (Join-Path $cache $a.File)
    $pairs += $target
}
& python $extractScript @pairs
if ($LASTEXITCODE -ne 0) { throw 'extraction failed' }

Write-Output "Qt $Version ($Arch) installed in $target"
Write-Output "Add to the environment: QT_ROOT_DIR=$target"
