# Installs deskstrate-host.
#
# Default (needs an elevated PowerShell): the "deskstrate-host" service, started at boot, so it
# also works at the sign-in and lock screens. Copies the exe to %ProgramFiles%\Deskstrate (not
# a user-writable folder, since it runs as SYSTEM), registers the service with restart on
# failure, and adds a firewall rule for UDP 18200 from the local subnet.
#
# -User (no admin): the standalone tray app for the current user only, started at logon from
# a Startup shortcut. It can't act at the sign-in or lock screen, and Windows Firewall asks to
# let it listen the first time (allow Private networks).
#
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-Exe path\to\deskstrate-host.exe] [-User] [-Uninstall]
# -Uninstall removes both kinds.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "deskstrate-host.exe"),
    [switch]$User,
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

$service = "deskstrate-host"
$rule = "Deskstrate host (UDP 18200)"
$svcDir = Join-Path $env:ProgramFiles "Deskstrate"
$userDir = Join-Path $env:LOCALAPPDATA "Deskstrate"
$shortcut = Join-Path ([Environment]::GetFolderPath("Startup")) "Deskstrate host.lnk"
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

function Remove-UserInstall {
    Get-Process deskstrate-host -ErrorAction SilentlyContinue |
        Where-Object { $_.SessionId -eq (Get-Process -Id $PID).SessionId } |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Remove-Item $shortcut, (Join-Path $userDir "deskstrate-host.exe") -ErrorAction SilentlyContinue
}

function Remove-ServiceInstall {
    if (Get-Service $service -ErrorAction SilentlyContinue) {
        Stop-Service $service -Force -ErrorAction SilentlyContinue
        sc.exe delete $service | Out-Null
    }
    Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    Remove-Item (Join-Path $svcDir "deskstrate-host.exe") -ErrorAction SilentlyContinue
}

if ($Uninstall) {
    Remove-UserInstall
    if ($admin) { Remove-ServiceInstall } elseif (Get-Service $service -ErrorAction SilentlyContinue) {
        Write-Warning "Run elevated to remove the service too."
    }
    Write-Host "Removed. Logs stay in $userDir and $env:ProgramData\Deskstrate."
    return
}

if (-not (Test-Path $Exe)) { throw "No ${Exe}: build it (see host/README.md) or pass -Exe." }

if ($User) {
    if (Get-Service $service -ErrorAction SilentlyContinue) {
        throw "The Deskstrate service is installed and owns UDP 18200. Uninstall it first (elevated: install.ps1 -Uninstall)."
    }
    Remove-UserInstall
    Start-Sleep -Milliseconds 500
    New-Item -ItemType Directory -Force $userDir | Out-Null
    $target = Join-Path $userDir "deskstrate-host.exe"
    Copy-Item $Exe $target -Force
    $lnk = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
    $lnk.TargetPath = $target
    $lnk.WorkingDirectory = $userDir
    $lnk.Save()
    Start-Process $target
    Write-Host "Installed for $env:USERNAME and running (tray icon). Log: $userDir\host.log"
    return
}

if (-not $admin) { throw "Installing the service needs an elevated PowerShell (or use -User for the tray app)." }

Remove-UserInstall  # the tray app would hold the port
if (Get-Service $service -ErrorAction SilentlyContinue) {
    Stop-Service $service -Force
    Start-Sleep -Milliseconds 500
}
New-Item -ItemType Directory -Force $svcDir | Out-Null
$target = Join-Path $svcDir "deskstrate-host.exe"
Copy-Item $Exe $target -Force

if (-not (Get-Service $service -ErrorAction SilentlyContinue)) {
    New-Service -Name $service -DisplayName "Deskstrate host" `
        -Description "Follows the Deskstrate core: display layout and Screens off for this PC." `
        -BinaryPathName "`"$target`" --service" -StartupType Automatic | Out-Null
    sc.exe failure $service reset= 86400 actions= restart/5000/restart/5000/restart/30000 | Out-Null
}
if (-not (Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $rule -Direction Inbound -Program $target -Protocol UDP `
        -LocalPort 18200 -RemoteAddress LocalSubnet -Action Allow | Out-Null
}
Start-Service $service
Write-Host "Service installed and running. Logs: $env:ProgramData\Deskstrate\service.log and agent.log"
