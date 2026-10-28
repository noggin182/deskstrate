# Installs deskstrate-host for the current user: copies it to %LOCALAPPDATA%\Deskstrate,
# starts it at logon (a shortcut in the Startup folder; no admin needed) and starts it now.
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-Exe path\to\deskstrate-host.exe] [-Uninstall]
# The first run asks Windows Firewall to let it listen on UDP 18200: allow Private networks.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "deskstrate-host.exe"),
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

$dir = Join-Path $env:LOCALAPPDATA "Deskstrate"
$target = Join-Path $dir "deskstrate-host.exe"
$shortcut = Join-Path ([Environment]::GetFolderPath("Startup")) "Deskstrate host.lnk"

Get-Process deskstrate-host -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

if ($Uninstall) {
    Remove-Item $shortcut, $target -ErrorAction SilentlyContinue
    Write-Host "Removed. The log stays in $dir."
    return
}

if (-not (Test-Path $Exe)) { throw "No ${Exe}: build it (see host/README.md) or pass -Exe." }
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item $Exe $target -Force

$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut($shortcut)
$lnk.TargetPath = $target
$lnk.WorkingDirectory = $dir
$lnk.Save()

Start-Process $target
Write-Host "Installed and running (tray icon). Log: $dir\host.log"
