$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$Python = Join-Path $Root ".venv\Scripts\python.exe"
if (-not (Test-Path $Python)) {
    throw "未找到虚拟环境。请先运行 install.ps1。"
}

$HostValue = $env:SERVER_HOST
if (-not $HostValue) {
    $HostValue = "0.0.0.0"
}

$PortValue = $env:SERVER_PORT
if (-not $PortValue) {
    $PortValue = "8080"
}

Write-Host "局域网 IPv4 地址："
Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
    Select-Object -ExpandProperty IPAddress |
    ForEach-Object { Write-Host "  http://$($_):$PortValue" }

& $Python -m uvicorn app.main:app --host $HostValue --port ([int]$PortValue)
