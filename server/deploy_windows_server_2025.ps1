param(
    [Parameter(Mandatory = $true)]
    [string]$DashscopeApiKey,

    [string]$InstallRoot = "C:\FJ-ker",
    [int]$Port = 8080,
    [string]$BindHost = "127.0.0.1",
    [string]$PublicBaseUrl = "",
    [string]$ApiToken = "",
    [string]$QwenModel = "qwen3.7-plus",
    [int]$MaxPages = 20,
    [int]$MaxUploadBytes = 6000000,
    [string]$TaskName = "FJ-ker Server",
    [string[]]$AllowedRemoteAddress = @("Any"),
    [string]$PythonInstallerUrl = "https://www.python.org/ftp/python/3.11.9/python-3.11.9-amd64.exe",
    [switch]$OpenFirewall,
    [switch]$AcceptPublicHttpRisk,
    [switch]$SkipPythonInstall,
    [switch]$SkipPlaywrightBrowserInstall
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message"
}

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $adminRole = [Security.Principal.WindowsBuiltInRole]::Administrator
    if (-not $principal.IsInRole($adminRole)) {
        throw "Run this script from an elevated/admin SSH PowerShell session."
    }
}

function Resolve-FullPath {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    return [IO.Path]::GetFullPath($Path)
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = ""
    )
    if ($WorkingDirectory) {
        Push-Location $WorkingDirectory
    }
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$FilePath exited with code $LASTEXITCODE"
        }
    }
    finally {
        if ($WorkingDirectory) {
            Pop-Location
        }
    }
}

function Test-Python311 {
    param([string]$PythonExe)
    if (-not (Test-Path -LiteralPath $PythonExe)) {
        return $false
    }
    & $PythonExe -c "import sys; raise SystemExit(0 if sys.version_info[:2] == (3, 11) else 1)" 2>$null
    return $LASTEXITCODE -eq 0
}

function Find-Python311 {
    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($pyLauncher) {
        $fromLauncher = (& py -3.11 -c "import sys; print(sys.executable)" 2>$null)
        if ($LASTEXITCODE -eq 0 -and $fromLauncher) {
            $candidate = $fromLauncher.Trim()
            if (Test-Python311 $candidate) {
                return $candidate
            }
        }
    }

    $pathPython = Get-Command python -ErrorAction SilentlyContinue
    if ($pathPython -and (Test-Python311 $pathPython.Source)) {
        return $pathPython.Source
    }

    $candidates = @(
        "$env:ProgramFiles\Python311\python.exe",
        "$env:LocalAppData\Programs\Python\Python311\python.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Python311 $candidate) {
            return $candidate
        }
    }

    return ""
}

function Install-Python311 {
    if ($SkipPythonInstall) {
        throw "Python 3.11 was not found and -SkipPythonInstall was set."
    }

    Write-Step "Downloading and installing Python 3.11"
    $installer = Join-Path $env:TEMP "python-3.11-amd64.exe"
    Invoke-WebRequest -Uri $PythonInstallerUrl -OutFile $installer -UseBasicParsing
    $args = @(
        "/quiet",
        "InstallAllUsers=1",
        "PrependPath=1",
        "Include_launcher=1",
        "Include_pip=1",
        "Include_test=0"
    )
    $process = Start-Process -FilePath $installer -ArgumentList $args -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Python installer exited with code $($process.ExitCode)."
    }
}

function New-ApiToken {
    $bytes = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $rng.GetBytes($bytes)
    }
    finally {
        $rng.Dispose()
    }
    return [Convert]::ToBase64String($bytes).TrimEnd("=").Replace("+", "-").Replace("/", "_")
}

function Copy-DeploymentTree {
    param(
        [string]$Source,
        [string]$Destination
    )

    $excludedNames = @(".git", ".agents", ".codex", ".venv", ".pytest_cache", ".test_deps", "__pycache__")
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    Get-ChildItem -LiteralPath $Source -Force | Where-Object {
        $excludedNames -notcontains $_.Name
    } | ForEach-Object {
        $target = Join-Path $Destination $_.Name
        if ($_.PSIsContainer) {
            Copy-DeploymentTree -Source $_.FullName -Destination $target
        }
        else {
            Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        }
    }
}

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Content
    )
    $encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Protect-SecretFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    & icacls.exe $Path /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to tighten ACL for $Path."
    }
}

function Wait-Health {
    param([string]$Uri)
    for ($i = 0; $i -lt 45; $i++) {
        try {
            $response = Invoke-RestMethod -Uri $Uri -TimeoutSec 2 -UseBasicParsing
            if ($response.ok -eq $true) {
                return $true
            }
        }
        catch {
            Start-Sleep -Seconds 1
        }
    }
    return $false
}

Assert-Admin

if ($OpenFirewall -and -not $AcceptPublicHttpRisk) {
    throw "Opening a public HTTP API requires -AcceptPublicHttpRisk. The API uses a token, but traffic is still plaintext unless you add HTTPS."
}

if ($OpenFirewall -and ($BindHost -eq "127.0.0.1" -or $BindHost -eq "localhost")) {
    throw "-OpenFirewall requires a non-local BindHost such as 0.0.0.0."
}

if (-not $ApiToken) {
    $ApiToken = New-ApiToken
}

$sourceRepoRoot = Resolve-FullPath (Split-Path -Parent $PSScriptRoot)
$installRootFull = Resolve-FullPath $InstallRoot

Write-Step "Preparing install directory: $installRootFull"
New-Item -ItemType Directory -Force -Path $installRootFull | Out-Null

if ($sourceRepoRoot.TrimEnd("\") -ine $installRootFull.TrimEnd("\")) {
    Write-Step "Copying project files from $sourceRepoRoot"
    Copy-DeploymentTree -Source $sourceRepoRoot -Destination $installRootFull
}
else {
    Write-Step "Using current project directory in place"
}

$serverRoot = Join-Path $installRootFull "server"
$venvPython = Join-Path $serverRoot ".venv\Scripts\python.exe"
$logRoot = Join-Path $installRootFull "logs"
$serverLog = Join-Path $logRoot "server.log"
$runnerPath = Join-Path $serverRoot "run_deployed.ps1"
$envPath = Join-Path $serverRoot ".env"
$infoPath = Join-Path $serverRoot "DEPLOYMENT_INFO.txt"

if (-not (Test-Path -LiteralPath $serverRoot)) {
    throw "Server directory was not found at $serverRoot."
}

New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

Write-Step "Locating Python 3.11"
$pythonExe = Find-Python311
if (-not $pythonExe) {
    Install-Python311
    $pythonExe = Find-Python311
}
if (-not $pythonExe) {
    throw "Python 3.11 is still unavailable after installation."
}
Write-Host "Python: $pythonExe"

Write-Step "Creating virtual environment"
if (-not (Test-Path -LiteralPath $venvPython)) {
    Invoke-Checked -FilePath $pythonExe -Arguments @("-m", "venv", (Join-Path $serverRoot ".venv"))
}

Write-Step "Installing Python dependencies"
Invoke-Checked -FilePath $venvPython -Arguments @("-m", "pip", "install", "--upgrade", "pip", "setuptools", "wheel") -WorkingDirectory $serverRoot
Invoke-Checked -FilePath $venvPython -Arguments @("-m", "pip", "install", "-e", ".") -WorkingDirectory $serverRoot

if (-not $SkipPlaywrightBrowserInstall) {
    Write-Step "Installing Playwright Chromium"
    try {
        Invoke-Checked -FilePath $venvPython -Arguments @("-m", "playwright", "install", "chromium") -WorkingDirectory $serverRoot
    }
    catch {
        Write-Warning "Playwright Chromium install failed. The server can still use the Pillow fallback renderer. Error: $($_.Exception.Message)"
    }
}

Write-Step "Writing .env"
$envContent = @"
DASHSCOPE_API_KEY=$DashscopeApiKey
QWEN_MODEL=$QwenModel
FJKER_API_TOKEN=$ApiToken
SERVER_HOST=$BindHost
SERVER_PORT=$Port
MAX_PAGES=$MaxPages
MAX_UPLOAD_BYTES=$MaxUploadBytes
"@
Write-Utf8NoBom -Path $envPath -Content $envContent
Protect-SecretFile -Path $envPath

Write-Step "Writing deployed runner"
$runnerContent = @"
`$ErrorActionPreference = "Stop"
Set-Location "$serverRoot"
`$env:SERVER_HOST = "$BindHost"
`$env:SERVER_PORT = "$Port"
& "$venvPython" -m uvicorn app.main:app --host "$BindHost" --port $Port *> "$serverLog"
"@
Write-Utf8NoBom -Path $runnerPath -Content $runnerContent

Write-Step "Registering scheduled task: $TaskName"
$existingTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existingTask) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

$taskAction = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument ('-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $runnerPath) `
    -WorkingDirectory $serverRoot
$taskTrigger = New-ScheduledTaskTrigger -AtStartup
$taskPrincipal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -RunLevel Highest
Register-ScheduledTask -TaskName $TaskName -Action $taskAction -Trigger $taskTrigger -Principal $taskPrincipal | Out-Null
Start-ScheduledTask -TaskName $TaskName

Write-Step "Waiting for local health check"
$healthUri = "http://127.0.0.1:$Port/health"
if (-not (Wait-Health -Uri $healthUri)) {
    Write-Warning "Health check failed. Recent server log:"
    if (Test-Path -LiteralPath $serverLog) {
        Get-Content -LiteralPath $serverLog -Tail 80
    }
    throw "Deployment task started, but $healthUri did not become healthy."
}

if ($OpenFirewall) {
    Write-Step "Opening Windows Firewall TCP $Port"
    $ruleName = "FJ-ker API $Port"
    $existingRule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
    if ($existingRule) {
        Remove-NetFirewallRule -DisplayName $ruleName
    }
    $firewallArgs = @{
        DisplayName = $ruleName
        Direction = "Inbound"
        Action = "Allow"
        Protocol = "TCP"
        LocalPort = $Port
    }
    if ($AllowedRemoteAddress.Count -gt 0 -and -not ($AllowedRemoteAddress.Count -eq 1 -and $AllowedRemoteAddress[0] -eq "Any")) {
        $firewallArgs.RemoteAddress = $AllowedRemoteAddress
    }
    New-NetFirewallRule @firewallArgs | Out-Null
}
else {
    Write-Warning "Firewall was not opened. Use -OpenFirewall -AcceptPublicHttpRisk if this server must be reachable by public IP."
}

if (-not $PublicBaseUrl) {
    $PublicBaseUrl = "http://<YOUR_PUBLIC_IP>:$Port"
}

$infoContent = @"
FJ-ker deployment complete

InstallRoot=$installRootFull
ServerRoot=$serverRoot
HealthUrl=http://127.0.0.1:$Port/health
PublicBaseUrl=$PublicBaseUrl
TaskName=$TaskName
LogFile=$serverLog

Firmware settings:
#define SERVER_BASE_URL "$PublicBaseUrl"
#define FJKER_API_TOKEN "$ApiToken"

Keep this file private. It contains the device API token.
"@
Write-Utf8NoBom -Path $infoPath -Content $infoContent
Protect-SecretFile -Path $infoPath

Write-Host ""
Write-Host "Deployment complete."
Write-Host "Local health: $healthUri"
Write-Host "Public base URL for firmware: $PublicBaseUrl"
Write-Host "Deployment info: $infoPath"
Write-Host ""
Write-Host "Add these to firmware/include/secrets.h:"
Write-Host "#define SERVER_BASE_URL `"$PublicBaseUrl`""
Write-Host "#define FJKER_API_TOKEN `"$ApiToken`""
