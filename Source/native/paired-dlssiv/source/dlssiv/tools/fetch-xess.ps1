<#
.SYNOPSIS
    Fetches Intel's XeSS headers and licence, which build-host.ps1 compiles against and which the repository does not carry.

.DESCRIPTION
    The helper uses Intel XeSS Super Resolution through Intel's public headers (external\xess\inc\xess) and loads
    libxess.dll at run time by its full path, with no import library. The headers, the licence and the DLL are Intel's,
    under the Intel Simplified Software License, and this repository does not redistribute them. This script takes them
    from Intel's own release zip instead.

    THE ZIP IS THE PIN. Intel publishes XeSS SDK 3.0.2 as one zip on GitHub. The script checks its size and SHA-256
    before it opens it, and then checks each file it extracts against the SHA-256 recorded here (text files are hashed
    with CRLF read as LF, so git's line-ending conversion cannot make a good file look wrong). A download that does not
    match is refused and not kept.

    WHAT IT WRITES, and nothing else:
        external\xess\inc\xess\*.h               the seven public headers of inc\xess
        external\xess\LICENSE-Intel-XeSS.txt     the zip's LICENSE.txt
        external\xess\libxess.dll                bin\libxess.dll, only with -Runtime (the helper loads it beside itself;
                                                 a player gets it from the mod's own download)
    All of them are git-ignored.

.PARAMETER FromLocal
    Use a copy already on this machine and download nothing: Intel's zip itself, a folder holding XeSS_SDK_3.0.2.zip, or
    a folder already laid out as external\xess (inc\xess\*.h and LICENSE-Intel-XeSS.txt). Every file is held to the same
    hashes as a download.

.PARAMETER Runtime
    Also extract bin\libxess.dll to external\xess\libxess.dll.

.PARAMETER Check
    Verify what is on disk and download nothing. Exits non-zero when a header or the licence is missing or is not the
    recorded file (with -Runtime, the DLL too).

.PARAMETER KeepZip
    Keep the downloaded zip in external\xess (it is git-ignored). By default it is deleted once the files are out.

.EXAMPLE
    .\tools\fetch-xess.ps1
    .\tools\fetch-xess.ps1 -Check
    .\tools\fetch-xess.ps1 -FromLocal D:\sdk\XeSS_SDK_3.0.2.zip
#>
param(
    [string]$FromLocal = '',
    [switch]$Runtime,
    [switch]$Check,
    [switch]$KeepZip
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$xessDir = Join-Path $root 'external\xess'

# Intel XeSS SDK 3.0.2, published 2026-07-24. The hashes below were read from that zip.
$zipName = 'XeSS_SDK_3.0.2.zip'
$zipSize = 76839749
$zipSha  = '88b8a373f30e33f3558a77a93e634f11b8132fc3047ea1a8edeead32b8471990'
$url     = 'https://github.com/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip'

# zip entry -> where it goes under external\xess, and the SHA-256 of its text with CRLF read as LF
$texts = [ordered]@{
    'inc/xess/xess.h'             = @('inc\xess\xess.h',             'd01a1be9be0672b17a093a7333f78016418efe2382c17cca699e463aee8ba5cc')
    'inc/xess/xess_d3d11.h'       = @('inc\xess\xess_d3d11.h',       'ce06710b2cbd3c99046c0e80d63e224c72ab3e26e29c4721d662acae6582e29a')
    'inc/xess/xess_d3d12.h'       = @('inc\xess\xess_d3d12.h',       'd43e3da0c241464e9c9dbe46be229250f4c94c697f2b2ea49f092f2fde5586af')
    'inc/xess/xess_d3d12_debug.h' = @('inc\xess\xess_d3d12_debug.h', '807c178e4c760cc1c9e23ede4400b0119e54830c3cd9aef36532038105284307')
    'inc/xess/xess_debug.h'       = @('inc\xess\xess_debug.h',       'dac7e3cf84b8b38af2edc659b0594bf16eeaef8493cf6a40926fb5c8a307da3e')
    'inc/xess/xess_vk.h'          = @('inc\xess\xess_vk.h',          '6b2b219c5c1592741625c65477698937829d0a74cb5f5db7a649ff7acd42b8ac')
    'inc/xess/xess_vk_debug.h'    = @('inc\xess\xess_vk_debug.h',    '993933c0cf64e0d5b03674aed83edd352bd024fb85bdfbfc6cd07456bac40c4e')
    'LICENSE.txt'                 = @('LICENSE-Intel-XeSS.txt',      'e15ca2a10f34d836cfff59922c35dd9277ae8068353abb588c851f9d16d4737d')
}
$dllEntry = 'bin/libxess.dll'
$dllDest  = 'libxess.dll'
$dllSize  = 77795704
$dllSha   = '251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7'

function Get-Sha256Bytes([byte[]]$b) {
    $h = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($h.ComputeHash($b)) -replace '-', '').ToLower() } finally { $h.Dispose() }
}

function Get-TextSha([byte[]]$b) {
    # the text's SHA-256 with every CRLF read as LF
    $ms = New-Object System.IO.MemoryStream
    for ($i = 0; $i -lt $b.Length; $i++) {
        if ($b[$i] -eq 13 -and ($i + 1) -lt $b.Length -and $b[$i + 1] -eq 10) { continue }
        $ms.WriteByte($b[$i])
    }
    return Get-Sha256Bytes $ms.ToArray()
}

function Get-FileSha([string]$path) { return (Get-FileHash -Path $path -Algorithm SHA256).Hash.ToLower() }

function Test-OnDisk {
    # -> the list of what is wrong; empty when everything this script writes is there and is the recorded file
    $bad = @()
    foreach ($k in $texts.Keys) {
        $p = Join-Path $xessDir $texts[$k][0]
        if (-not (Test-Path $p)) { $bad += "missing: $p"; continue }
        $got = Get-TextSha ([System.IO.File]::ReadAllBytes($p))
        if ($got -ne $texts[$k][1]) { $bad += "not the recorded file: $p (SHA-256 $got)" }
    }
    if ($Runtime) {
        $p = Join-Path $xessDir $dllDest
        if (-not (Test-Path $p)) { $bad += "missing: $p" }
        elseif ((Get-Item $p).Length -ne $dllSize -or (Get-FileSha $p) -ne $dllSha) { $bad += "not the recorded file: $p" }
    }
    return ,$bad
}

function Read-Entry($zip, [string]$name) {
    $e = $zip.GetEntry($name)
    if (-not $e) { throw "Intel's zip has no $name" }
    $s = $e.Open()
    try { $ms = New-Object System.IO.MemoryStream; $s.CopyTo($ms); return ,$ms.ToArray() } finally { $s.Dispose() }
}

function Install-FromZip([string]$zipPath) {
    $len = (Get-Item $zipPath).Length
    if ($len -ne $zipSize) { throw ("{0} is {1:N0} bytes; Intel's {2} is {3:N0}. Not used." -f $zipPath, $len, $zipName, $zipSize) }
    $sha = Get-FileSha $zipPath
    if ($sha -ne $zipSha) { throw "$zipPath has SHA-256 $sha; Intel's $zipName has $zipSha. Not used." }
    Write-Host ("  {0}: {1:N0} bytes, SHA-256 {2}, the recorded zip" -f $zipName, $len, $sha) -ForegroundColor Green
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        # every file is read and checked before any is written
        $out = [ordered]@{}
        foreach ($k in $texts.Keys) {
            $b = Read-Entry $zip $k
            $got = Get-TextSha $b
            if ($got -ne $texts[$k][1]) { throw "$k in the zip has SHA-256 $got; recorded $($texts[$k][1]). Nothing written." }
            $out[$texts[$k][0]] = $b
        }
        if ($Runtime) {
            $b = Read-Entry $zip $dllEntry
            if ($b.Length -ne $dllSize -or (Get-Sha256Bytes $b) -ne $dllSha) { throw "$dllEntry in the zip is not the recorded file. Nothing written." }
            $out[$dllDest] = $b
        }
    } finally { $zip.Dispose() }
    foreach ($rel in $out.Keys) {
        $p = Join-Path $xessDir $rel
        $d = Split-Path $p -Parent
        if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        [System.IO.File]::WriteAllBytes($p, $out[$rel])
        Write-Host "  wrote $p"
    }
}

function Install-FromFolder([string]$dir) {
    # a folder laid out as external\xess: the same checks, then a copy
    $out = [ordered]@{}
    foreach ($k in $texts.Keys) {
        $src = Join-Path $dir $texts[$k][0]
        if (-not (Test-Path $src) -and $k -eq 'LICENSE.txt') { $src = Join-Path $dir 'LICENSE.txt' }
        if (-not (Test-Path $src)) { throw "missing in ${dir}: $($texts[$k][0])" }
        $b = [System.IO.File]::ReadAllBytes($src)
        $got = Get-TextSha $b
        if ($got -ne $texts[$k][1]) { throw "$src has SHA-256 $got; recorded $($texts[$k][1]). Nothing written." }
        $out[$texts[$k][0]] = $b
    }
    if ($Runtime) {
        $src = Join-Path $dir $dllDest
        if (-not (Test-Path $src)) { throw "missing in ${dir}: $dllDest" }
        if ((Get-Item $src).Length -ne $dllSize -or (Get-FileSha $src) -ne $dllSha) { throw "$src is not the recorded file. Nothing written." }
    }
    foreach ($rel in $out.Keys) {
        $p = Join-Path $xessDir $rel
        $d = Split-Path $p -Parent
        if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        [System.IO.File]::WriteAllBytes($p, $out[$rel])
        Write-Host "  wrote $p"
    }
    if ($Runtime) { Copy-Item (Join-Path $dir $dllDest) (Join-Path $xessDir $dllDest) -Force; Write-Host "  wrote $(Join-Path $xessDir $dllDest)" }
}

# ---------------------------------------------------------------- check
if ($Check) {
    $bad = Test-OnDisk
    if ($bad.Count -eq 0) {
        Write-Host "Intel XeSS files present and recorded ($($texts.Count) text files$(if ($Runtime) { ' and libxess.dll' }))." -ForegroundColor Green
        exit 0
    }
    $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host ""
    Write-Host "  They are not in the repository on purpose (Intel's licence). Fetch them with:"
    Write-Host "      .\tools\fetch-xess.ps1" -ForegroundColor Cyan
    exit 1
}

# ---------------------------------------------------------------- already here?
$bad = Test-OnDisk
if ($bad.Count -eq 0) {
    Write-Host "Intel XeSS files present and recorded. Nothing to do." -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- from a local copy
if ($FromLocal) {
    if (-not (Test-Path $FromLocal)) { Write-Host "No such file or folder: $FromLocal" -ForegroundColor Red; exit 1 }
    try {
        if (Test-Path $FromLocal -PathType Leaf) { Install-FromZip $FromLocal }
        elseif (Test-Path (Join-Path $FromLocal $zipName)) { Install-FromZip (Join-Path $FromLocal $zipName) }
        else { Install-FromFolder $FromLocal }
    } catch { Write-Host "  $($_.Exception.Message)" -ForegroundColor Red; exit 1 }
    $bad = Test-OnDisk
    if ($bad.Count -ne 0) { $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }; exit 1 }
    Write-Host "Intel XeSS files in place, from the local copy. They stay untracked: do not commit them." -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- download
Write-Host "Fetching Intel XeSS SDK 3.0.2 (about 77 MB)." -ForegroundColor Cyan
Write-Host "  from $url"
if (-not (Test-Path $xessDir)) { New-Item -ItemType Directory -Path $xessDir -Force | Out-Null }
$tmp = Join-Path $xessDir ($zipName + '.download')
if (Test-Path $tmp) { Remove-Item $tmp -Force }
$oldProgress = $ProgressPreference
$ProgressPreference = 'SilentlyContinue'
try {
    Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
} catch {
    Write-Host "  download FAILED: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "  You can download $zipName by hand from https://github.com/intel/xess/releases (v3.0.2) and run"
    Write-Host "      .\tools\fetch-xess.ps1 -FromLocal <the zip>" -ForegroundColor Cyan
    if (Test-Path $tmp) { Remove-Item $tmp -Force }
    $ProgressPreference = $oldProgress
    exit 1
}
$ProgressPreference = $oldProgress
try { Install-FromZip $tmp } catch {
    Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
    Remove-Item $tmp -Force
    exit 1
}
if ($KeepZip) { Move-Item $tmp (Join-Path $xessDir $zipName) -Force } else { Remove-Item $tmp -Force }
$bad = Test-OnDisk
if ($bad.Count -ne 0) { $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }; exit 1 }
Write-Host ""
Write-Host "Intel XeSS files fetched. They stay untracked: do not commit them." -ForegroundColor Green
