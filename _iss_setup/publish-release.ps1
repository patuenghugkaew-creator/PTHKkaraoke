# Publishes a new version of PTHK Karaoke to GitHub Releases.
# Every installed copy checks there and offers the update automatically.
#
# Before running:
#   1. Raise the version in ..\version.h (e.g. v.1.1.1 / 1,1,1), build, and copy the new files into deploy.
#   2. Have a GitHub token with "Contents: Read and write" on the PTHKkaraoke repository:
#        $env:GITHUB_TOKEN = "github_pat_..."
#
# Run:  powershell -ExecutionPolicy Bypass -File publish-release.ps1 -Notes "What changed"
param(
    [string]$Notes = "",
    [switch]$SkipBuild        # use an installer that is already in ..\dist
)

$ErrorActionPreference = "Stop"
$Repo = "patuenghugkaew-creator/PTHKkaraoke"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here

if (-not $env:GITHUB_TOKEN) { throw "Set GITHUB_TOKEN first (see the top of this file)." }

# check the token before the long installer build
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
try {
    $r = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo" -Headers @{
        Authorization = "Bearer $env:GITHUB_TOKEN"; Accept = "application/vnd.github+json"; "User-Agent" = "PTHK-publish" }
} catch { throw "Token ไม่ถูกต้องหรือหมดอายุ - สร้างใหม่แล้วตั้ง `$env:GITHUB_TOKEN อีกครั้ง" }
if (-not $r.permissions.push) { throw "Token นี้ไม่มีสิทธิ์เขียน repo $Repo (ต้องให้สิทธิ์ Contents: Read and write)" }
Write-Host "Token OK"

# version from version.h  ("v.1.1.0\0" -> 1.1.0)
$verLine = Select-String -Path "$Root\version.h" -Pattern 'VER_PRODUCTVERSION_STR\s+"v\.([0-9.]+)' | Select-Object -First 1
if (-not $verLine) { throw "Version not found in version.h" }
$Version = $verLine.Matches[0].Groups[1].Value
$Tag = "v$Version"
Write-Host "Version: $Version"

# the program in deploy must really be this version (otherwise installed copies would update forever)
$exeText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("D:\REA\PTHK-Build\deploy\PTHKKaraoke.exe"))
if (-not $exeText.Contains("v.$Version")) { throw "deploy\PTHKKaraoke.exe is not version $Version - rebuild (delete MainWindow.obj) and copy it first." }

# 1. installer
$Setup = "$Root\dist\PTHK-Karaoke-$Version-x64-setup.exe"
if (-not $SkipBuild) {
    $iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    if (-not (Test-Path $iscc)) { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
    & $iscc "/DAppVersion=$Version" "$Here\pthk-karaoke-x64.iss"
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
}
if (-not (Test-Path $Setup)) { throw "Installer not found: $Setup" }

# 2. checksum (the program refuses a download that does not match)
$Sha = (Get-FileHash $Setup -Algorithm SHA256).Hash.ToLower()
Write-Host "SHA256: $Sha"

# 3. release + installer upload
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$H = @{ Authorization = "Bearer $env:GITHUB_TOKEN"; Accept = "application/vnd.github+json"; "User-Agent" = "PTHK-publish" }
$body = @{
    tag_name = $Tag
    name     = "PTHK Karaoke $Version"
    body     = ($Notes + "`n`nSHA256: $Sha").Trim()
    draft    = $false
    prerelease = $false
} | ConvertTo-Json
$rel = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$Repo/releases" -Headers $H `
        -Body ([Text.Encoding]::UTF8.GetBytes($body)) -ContentType "application/json; charset=utf-8"
$name = [IO.Path]::GetFileName($Setup)
$up = "https://uploads.github.com/repos/$Repo/releases/$($rel.id)/assets?name=$name"
Invoke-RestMethod -Method Post -Uri $up -Headers $H -InFile $Setup -ContentType "application/octet-stream" | Out-Null

Write-Host ""
Write-Host "Published: $($rel.html_url)"
Write-Host "Installed copies will offer version $Version at their next check."
