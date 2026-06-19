#requires -version 5.1
[CmdletBinding()]
param(
    [string]$PhysicalAdapterName = '',
    [string]$TapName = 'OpenOS-TAP',
    [string]$OpenVpnPackageId = 'OpenVPNTechnologies.OpenVPN'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "=== $Message ===" -ForegroundColor Cyan
}

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-PrimaryAdapter {
    $configs = Get-NetIPConfiguration | Where-Object {
        $_.NetAdapter.Status -eq 'Up' -and
        $_.IPv4Address -and
        $_.IPv4DefaultGateway -and
        $_.NetAdapter.InterfaceDescription -notmatch 'TAP|OpenVPN|TUN|Wintun|Loopback|VMware|VirtualBox|Hyper-V|WSL|Npcap'
    }
    $cfg = $configs | Select-Object -First 1
    if ($cfg) {
        return $cfg.InterfaceAlias
    }

    $fallback = Get-NetAdapter | Where-Object {
        $_.Status -eq 'Up' -and
        $_.InterfaceDescription -notmatch 'TAP|OpenVPN|TUN|Wintun|Loopback|VMware|VirtualBox|Hyper-V|WSL|Npcap'
    } | Select-Object -First 1
    if ($fallback) {
        return $fallback.Name
    }
    return $null
}

if (-not (Test-Admin)) {
    Write-Error 'Please run this script as Administrator.'
    exit 1
}

Write-Step 'Check QEMU'
$qemu = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
if (-not $qemu) {
    Write-Error 'qemu-system-i386 was not found. Please install QEMU or add it to PATH.'
    exit 1
}
Write-Host "QEMU: $($qemu.Source)"
& qemu-system-i386 --version | Select-Object -First 1

Write-Step 'Check physical adapter'
if ([string]::IsNullOrWhiteSpace($PhysicalAdapterName)) {
    $PhysicalAdapterName = Find-PrimaryAdapter
}
if ([string]::IsNullOrWhiteSpace($PhysicalAdapterName)) {
    Write-Host 'Could not auto-detect a primary physical adapter. Current adapters:' -ForegroundColor Yellow
    Get-NetAdapter | Sort-Object Name | Format-Table -Auto Name,InterfaceDescription,Status,LinkSpeed
    Write-Error 'Please specify -PhysicalAdapterName manually.'
    exit 1
}
$physical = Get-NetAdapter -Name $PhysicalAdapterName -ErrorAction SilentlyContinue
if (-not $physical) {
    Write-Host "Adapter '$PhysicalAdapterName' was not found. Current adapters:" -ForegroundColor Yellow
    Get-NetAdapter | Sort-Object Name | Format-Table -Auto Name,InterfaceDescription,Status,LinkSpeed
    Write-Error 'Please specify a valid -PhysicalAdapterName.'
    exit 1
}
Write-Host "Physical adapter: $($physical.Name) / $($physical.InterfaceDescription) / $($physical.Status)"

Write-Step 'Check TAP-Windows Adapter V9'
# QEMU tap on Windows needs the legacy TAP-Windows Adapter V9 device.
# Do NOT use OpenVPN Data Channel Offload, Wintun, or TUN devices here.
$tap = Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object {
    $_.InterfaceDescription -eq 'TAP-Windows Adapter V9'
} | Select-Object -First 1

if (-not $tap) {
    Write-Host 'No TAP-Windows Adapter V9 found. Trying to install OpenVPN by winget.' -ForegroundColor Yellow
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Error 'winget was not found. Please install OpenVPN Community Edition manually and include TAP-Windows Adapter V9.'
        exit 1
    }

    & winget install --id $OpenVpnPackageId --exact --accept-package-agreements --accept-source-agreements

    Write-Host 'Waiting for TAP-Windows Adapter V9...'
    Start-Sleep -Seconds 8
    $tap = Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object {
        $_.InterfaceDescription -eq 'TAP-Windows Adapter V9'
    } | Select-Object -First 1
}

if (-not $tap) {
    Write-Error 'TAP-Windows Adapter V9 was still not found. Please confirm that TAP-Windows Adapter V9 is installed.'
    exit 1
}

Write-Host "TAP-Windows Adapter V9: $($tap.Name) / $($tap.InterfaceDescription) / $($tap.Status)"
if ($tap.Name -ne $TapName) {
    if (Get-NetAdapter -Name $TapName -ErrorAction SilentlyContinue) {
        Write-Host "Adapter name '$TapName' is already used by a non-TAP adapter. Rename it to OpenVPN-DCO first." -ForegroundColor Yellow
        Rename-NetAdapter -Name $TapName -NewName 'OpenVPN-DCO' -ErrorAction Stop
        Start-Sleep -Seconds 2
    }
    Write-Host "Rename TAP-Windows Adapter V9 to $TapName"
    Rename-NetAdapter -Name $tap.Name -NewName $TapName -ErrorAction Stop
    Start-Sleep -Seconds 2
}

Write-Step 'Check network bridge'
Get-NetAdapter | Where-Object { $_.Name -match 'Bridge' -or $_.InterfaceDescription -match 'Bridge' } | Format-Table -Auto Name,InterfaceDescription,Status,MacAddress

Write-Step 'Create Windows Network Bridge manually'
Write-Host 'The Network Connections window will be opened.' -ForegroundColor Yellow
Write-Host "1. Hold Ctrl and select these two adapters: $PhysicalAdapterName and $TapName"
Write-Host '2. Right click and choose Bridge Connections.'
Write-Host '3. Wait until a Network Bridge appears and network is restored.'
Write-Host ''
Write-Host 'Network may disconnect temporarily during bridge creation.'
Start-Process ncpa.cpl

Write-Step 'Generate QEMU bridge run script'
$runScript = 'E:\openos\scripts\windows\run-openos-bridge.ps1'
$runLines = @(
    '#requires -version 5.1',
    '$ErrorActionPreference = ''Stop''',
    'Set-Location ''E:\openos''',
    'qemu-system-i386 -m 128M `',
    '  -drive file=target/openos.img,format=raw `',
    ('  -netdev tap,id=net0,ifname=''' + $TapName + ''',script=no,downscript=no `'),
    '  -device e1000,netdev=net0,mac=52:54:00:12:34:56 `',
    '  -display gtk'
)
$runLines | Set-Content -Path $runScript -Encoding UTF8
Write-Host "Generated: $runScript"

Write-Step 'Suggested OpenOS bridge network settings'
$ipconf = Get-NetIPConfiguration -InterfaceAlias $PhysicalAdapterName
$addr = $ipconf.IPv4Address.IPAddress
$prefix = $ipconf.IPv4Address.PrefixLength
$gw = $ipconf.IPv4DefaultGateway.NextHop
$dns = ($ipconf.DNSServer.ServerAddresses | Where-Object { $_ -match '^\d+\.\d+\.\d+\.\d+$' } | Select-Object -First 1)
Write-Host "Windows IP: $addr/$prefix"
Write-Host "Gateway: $gw"
Write-Host "DNS: $dns"
Write-Host 'Configure OpenOS to an unused IP in the same subnet, for example:'
Write-Host 'IP:      192.168.1.240'
Write-Host 'Mask:    255.255.255.0'
Write-Host "Gateway: $gw"
Write-Host "DNS:     $gw or 223.5.5.5"

Write-Step 'Done'
Write-Host 'After creating the bridge, run QEMU as Administrator:'
Write-Host "powershell -ExecutionPolicy Bypass -File $runScript"
