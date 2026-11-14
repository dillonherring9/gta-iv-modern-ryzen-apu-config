<#
.SYNOPSIS
    Fetches the NVIDIA DLSS SDK files that build-host.ps1 needs and that the repository does not carry: the NGX headers,
    the static-CRT import library and NVIDIA's licence.

.DESCRIPTION
    build-host.ps1 compiles against external\ngx\include\nvsdk_ngx*.h and links external\ngx\lib\nvsdk_ngx_s.lib. The
    NVIDIA DLSS SDK licence permits distributing the SDK incorporated into a built application, and forbids subjecting
    it to an open source licence (section 3.e) or distributing it as a stand-alone product (section 3.b). So this
    repository does not carry any of NVIDIA's files, and this script takes them from NVIDIA's public repository
    (github.com/NVIDIA/DLSS) instead.

    THE HASHES ARE THE PIN, NOT THE REF. The files were taken from NVIDIA/DLSS main on 2026-09-03, and main moves. Every
    file is checked against the hash of the file this project was built and tested against, so an upstream change is
    detected rather than silently compiled. Text files are hashed with CRLF read as LF, so git's line-ending conversion
    cannot make a good file look wrong. To move to a newer SDK on purpose, pass -AllowDifferentHash and then update the
    hashes below.

    WHAT IT WRITES, and nothing else (all git-ignored):
        external\ngx\include\nvsdk_ngx*.h          six headers (the core and the DLSS-D definitions the helpers include)
        external\ngx\lib\nvsdk_ngx_s.lib           the static-CRT import library
        external\ngx\LICENSE-NVIDIA-DLSS-SDK.txt   NVIDIA's LICENSE.txt

.PARAMETER Ref
    The git ref in NVIDIA/DLSS to fetch from. Defaults to main.

.PARAMETER AllowDifferentHash
    Keep files whose hash is not the recorded one. With -Check, pass the check anyway.

.PARAMETER Check
    Verify what is on disk and download nothing. build-host.ps1 calls this, so it EXITS NON-ZERO when a file is missing,
    the library is not a COFF archive, or a hash is not the recorded one (unless -AllowDifferentHash).

.PARAMETER FromLocal
    Use a copy already on this machine and download nothing: a folder laid out as external\ngx (include\, lib\,
    LICENSE-NVIDIA-DLSS-SDK.txt) or as NVIDIA's repository (include\, lib\Windows_x86_64\x64\, LICENSE.txt). Every file
    is held to the same hashes as a download.

.EXAMPLE
    .\tools\fetch-ngx.ps1
    .\tools\fetch-ngx.ps1 -Check
    .\tools\fetch-ngx.ps1 -FromLocal D:\src\DLSS
#>
param(
    [string]$Ref = 'main',
    [switch]$AllowDifferentHash,
    [switch]$Check,
    [string]$FromLocal = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$ngxDir = Join-Path $root 'external\ngx'

# what this project was built and tested against (NVIDIA/DLSS main, 2026-09-03)
# destination under external\ngx -> path in NVIDIA's repository, SHA-256 of the text with CRLF read as LF
$texts = [ordered]@{
    'include\nvsdk_ngx.h'              = @('include/nvsdk_ngx.h',              'f6014a256f9d75ccec1278ac6e23d596b398a76cc3960048ca1a274b378b1989')
    'include\nvsdk_ngx_defs.h'         = @('include/nvsdk_ngx_defs.h',         'ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e')
    'include\nvsdk_ngx_defs_dlssd.h'   = @('include/nvsdk_ngx_defs_dlssd.h',   'd2fde340db2189c89bce093bc1edd7b3579df48decac329218a98a9c5fd46018')
    'include\nvsdk_ngx_helpers.h'      = @('include/nvsdk_ngx_helpers.h',      '2d5661f8b5ab55e1223e485f24146274d48077e09051873826b653d4384fe7d8')
    'include\nvsdk_ngx_params.h'       = @('include/nvsdk_ngx_params.h',       '943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc')
    'include\nvsdk_ngx_params_dlssd.h' = @('include/nvsdk_ngx_params_dlssd.h', 'd41e3ba8f1d72583b55273092c99bf426dc4a0d46de827229e25d5b1325a370e')
    'LICENSE-NVIDIA-DLSS-SDK.txt'      = @('LICENSE.txt',                      '21b5daec892b12bea692e66bc8fe45cf5902ccaf3a7b831e78050d8859881c37')
}
$libRel   = 'lib\nvsdk_ngx_s.lib'
$libUp    = 'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib'
$libMd5   = '16933B6EAA970224BCE203C8BE71B532'
$libSize  = 4740514
$base     = "https://raw.githubusercontent.com/NVIDIA/DLSS/$Ref"

function Get-Sha256Bytes([byte[]]$b) {
    $h = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($h.ComputeHash($b)) -replace '-', '').ToLower() } finally { $h.Dispose() }
}

function Get-TextSha([byte[]]$b) {
    $ms = New-Object System.IO.MemoryStream
    for ($i = 0; $i -lt $b.Length; $i++) {
        if ($b[$i] -eq 13 -and ($i + 1) -lt $b.Length -and $b[$i + 1] -eq 10) { continue }
        $ms.WriteByte($b[$i])
    }
    return Get-Sha256Bytes $ms.ToArray()
}

function Test-LibBytes([byte[]]$b) {
    # 'ok', or why not; the magic first: a 404 page saved to disk is still a file
    if ($b.Length -lt 1048576) { return "too small ($($b.Length) bytes); this is not an import library" }
    $magic = [System.Text.Encoding]::ASCII.GetString($b, 0, 8)
    if ($magic -ne "!<arch>`n") { return "not a COFF archive" }
    return 'ok'
}

function Get-Md5Bytes([byte[]]$b) {
    $h = [System.Security.Cryptography.MD5]::Create()
    try { return ([BitConverter]::ToString($h.ComputeHash($b)) -replace '-', '') } finally { $h.Dispose() }
}

function Test-File([string]$rel, [byte[]]$b) {
    # 'ok', 'hash' (a good file of another version) or why it is no good
    if ($rel -eq $libRel) {
        $s = Test-LibBytes $b
        if ($s -ne 'ok') { return $s }
        if ((Get-Md5Bytes $b) -ne $libMd5 -or $b.Length -ne $libSize) { return 'hash' }
        return 'ok'
    }
    if ((Get-TextSha $b) -ne $texts[$rel][1]) { return 'hash' }
    return 'ok'
}

function Get-AllRel { return @($texts.Keys) + @($libRel) }

function Test-OnDisk {
    $bad = @()
    foreach ($rel in (Get-AllRel)) {
        $p = Join-Path $ngxDir $rel
        if (-not (Test-Path $p)) { $bad += "missing: $p"; continue }
        $s = Test-File $rel ([System.IO.File]::ReadAllBytes($p))
        if ($s -eq 'hash') { $bad += "not the recorded file: $p" }
        elseif ($s -ne 'ok') { $bad += "${p}: $s" }
    }
    return ,$bad
}

function Write-Rel([string]$rel, [byte[]]$b) {
    $p = Join-Path $ngxDir $rel
    $d = Split-Path $p -Parent
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
    [System.IO.File]::WriteAllBytes($p, $b)
    Write-Host "  wrote $p"
}

# ---------------------------------------------------------------- check
$bad = Test-OnDisk
if ($Check) {
    if ($bad.Count -eq 0) {
        Write-Host "NVIDIA DLSS SDK files present and match the recorded build." -ForegroundColor Green
        exit 0
    }
    $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    if ($AllowDifferentHash -and -not ($bad | Where-Object { $_ -notlike 'not the recorded file:*' })) {
        Write-Host "  -AllowDifferentHash was given, so this check passes anyway." -ForegroundColor Yellow
        exit 0
    }
    Write-Host ""
    Write-Host "  They are not in the repository on purpose (the NVIDIA DLSS SDK licence). Fetch them with:"
    Write-Host "      .\tools\fetch-ngx.ps1" -ForegroundColor Cyan
    exit 1
}
if ($bad.Count -eq 0) {
    Write-Host "NVIDIA DLSS SDK files present and match the recorded build. Nothing to do." -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- gather every file first, write only when all are good
$got = [ordered]@{}
if ($FromLocal) {
    if (-not (Test-Path $FromLocal -PathType Container)) { Write-Host "No such folder: $FromLocal" -ForegroundColor Red; exit 1 }
    Write-Host "Taking the NVIDIA DLSS SDK files from $FromLocal (nothing is downloaded)." -ForegroundColor Cyan
    foreach ($rel in (Get-AllRel)) {
        $up = if ($rel -eq $libRel) { $libUp } else { $texts[$rel][0] }
        $cands = @((Join-Path $FromLocal $rel), (Join-Path $FromLocal ($up -replace '/', '\')))
        $src = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $src) { Write-Host "  missing in ${FromLocal}: $rel" -ForegroundColor Red; exit 1 }
        $got[$rel] = [System.IO.File]::ReadAllBytes($src)
    }
} else {
    Write-Host "Fetching the NVIDIA DLSS SDK files from NVIDIA/DLSS ($Ref)." -ForegroundColor Cyan
    $oldProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        foreach ($rel in (Get-AllRel)) {
            $up = if ($rel -eq $libRel) { $libUp } else { $texts[$rel][0] }
            $tmp = [System.IO.Path]::GetTempFileName()
            try {
                Invoke-WebRequest -Uri "$base/$up" -OutFile $tmp -UseBasicParsing
                $got[$rel] = [System.IO.File]::ReadAllBytes($tmp)
            } finally { Remove-Item $tmp -Force -ErrorAction SilentlyContinue }
            Write-Host "  fetched $up"
        }
    } catch {
        Write-Host "  download FAILED: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "  If this is a 404, the ref '$Ref' may not carry that path any more. You can also clone"
        Write-Host "  https://github.com/NVIDIA/DLSS and run .\tools\fetch-ngx.ps1 -FromLocal <the clone>."
        $ProgressPreference = $oldProgress
        exit 1
    }
    $ProgressPreference = $oldProgress
}

$wrong = @()
foreach ($rel in $got.Keys) {
    $s = Test-File $rel $got[$rel]
    if ($s -eq 'hash') { $wrong += $rel }
    elseif ($s -ne 'ok') { Write-Host "  ${rel}: $s. Nothing written." -ForegroundColor Red; exit 1 }
}
if ($wrong.Count -and -not $AllowDifferentHash) {
    Write-Host ""
    Write-Host "  THESE ARE NOT THE FILES THIS PROJECT WAS BUILT AGAINST:" -ForegroundColor Yellow
    $wrong | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
    Write-Host "  NVIDIA/DLSS main moves; the hashes in this script are the only pin. Nothing written."
    Write-Host "  To take them anyway: .\tools\fetch-ngx.ps1 -AllowDifferentHash, then update the hashes in this script."
    exit 1
}
foreach ($rel in $got.Keys) { Write-Rel $rel $got[$rel] }
Write-Host ""
Write-Host "NVIDIA DLSS SDK files in place. They stay untracked: do not commit them." -ForegroundColor Green
exit 0
