param(
    [string]$JobId = "",
    [string]$BaseUrl = "http://115.28.131.113:8080",
    [string]$Token = $env:FJKER_API_TOKEN,
    [string]$OutputDir = "",
    [int]$Scale = 3,
    [switch]$Wait,
    [int]$TimeoutSeconds = 240,
    [int]$PollSeconds = 2,
    [switch]$OpenFolder,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$PageWidth = 384
$PageHeight = 168
$RowStride = 48
$PageBytes = 8064

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
}

function Add-SystemDrawing {
    try {
        Add-Type -AssemblyName System.Drawing
    }
    catch {
        throw "System.Drawing is unavailable. Run this script from Windows PowerShell on your PC."
    }
}

function Normalize-BaseUrl {
    param([string]$Url)
    return $Url.TrimEnd("/")
}

function Get-AuthHeaders {
    param([string]$DeviceToken)
    if (-not $DeviceToken) {
        return @{}
    }
    return @{ "X-FJ-KER-TOKEN" = $DeviceToken }
}

function Get-JobStatus {
    param(
        [string]$Url,
        [string]$Job,
        [hashtable]$Headers
    )
    return Invoke-RestMethod -Method Get -Uri "$Url/jobs/$Job" -Headers $Headers
}

function Wait-ReadyJob {
    param(
        [string]$Url,
        [string]$Job,
        [hashtable]$Headers
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        $status = Get-JobStatus -Url $Url -Job $Job -Headers $Headers
        if ($status.status -eq "ready") {
            return $status
        }
        if ($status.status -eq "error") {
            throw "Job $Job failed: $($status.error)"
        }
        if (-not $Wait -or (Get-Date) -gt $deadline) {
            throw "Job $Job is not ready yet. Current status: $($status.status). Re-run with -Wait to poll."
        }
        Write-Host "Job status: $($status.status); waiting $PollSeconds seconds..."
        Start-Sleep -Seconds $PollSeconds
    }
}

function Convert-PageBinToPng {
    param(
        [string]$BinPath,
        [string]$PngPath,
        [int]$ImageScale
    )
    $bytes = [IO.File]::ReadAllBytes($BinPath)
    if ($bytes.Length -ne $PageBytes) {
        throw "$BinPath has $($bytes.Length) bytes; expected $PageBytes bytes."
    }

    if ($ImageScale -lt 1) {
        $ImageScale = 1
    }

    $source = New-Object System.Drawing.Bitmap $PageWidth, $PageHeight
    try {
        $black = [System.Drawing.Color]::Black
        $white = [System.Drawing.Color]::White
        for ($y = 0; $y -lt $PageHeight; $y++) {
            $base = $y * $RowStride
            for ($x = 0; $x -lt $PageWidth; $x++) {
                $b = $bytes[$base + [int][Math]::Floor($x / 8)]
                $bit = ($b -shr (7 - ($x % 8))) -band 1
                if ($bit -eq 1) {
                    $source.SetPixel($x, $y, $black)
                }
                else {
                    $source.SetPixel($x, $y, $white)
                }
            }
        }

        if ($ImageScale -eq 1) {
            $source.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
            return
        }

        $scaled = New-Object System.Drawing.Bitmap ($PageWidth * $ImageScale), ($PageHeight * $ImageScale)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($scaled)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
                $graphics.DrawImage($source, 0, 0, $scaled.Width, $scaled.Height)
            }
            finally {
                $graphics.Dispose()
            }
            $scaled.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $scaled.Dispose()
        }
    }
    finally {
        $source.Dispose()
    }
}

function Write-Gallery {
    param(
        [string]$Directory,
        [string[]]$PngFiles,
        [string]$Job
    )
    $items = foreach ($file in $PngFiles) {
        $name = [IO.Path]::GetFileName($file)
        "<section><h2>$name</h2><img src=`"png/$name`" alt=`"$name`"></section>"
    }
    $html = @"
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>FJ-ker job $Job pages</title>
  <style>
    body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; background: #f6f6f6; }
    section { margin: 0 0 24px; padding: 16px; background: white; border: 1px solid #ddd; }
    img { image-rendering: pixelated; border: 1px solid #bbb; max-width: 100%; background: white; }
  </style>
</head>
<body>
  <h1>FJ-ker job $Job pages</h1>
  $($items -join "`n  ")
</body>
</html>
"@
    [IO.File]::WriteAllText((Join-Path $Directory "index.html"), $html, [Text.UTF8Encoding]::new($false))
}

function New-SelfTestBin {
    param([string]$Path)
    $bytes = New-Object byte[] $PageBytes
    for ($x = 0; $x -lt $PageWidth; $x++) {
        $y = [int][Math]::Floor($x * $PageHeight / $PageWidth)
        $idx = $y * $RowStride + [int][Math]::Floor($x / 8)
        $bytes[$idx] = $bytes[$idx] -bor (0x80 -shr ($x % 8))
    }
    for ($x = 0; $x -lt $PageWidth; $x++) {
        $idx = [int][Math]::Floor($x / 8)
        $bytes[$idx] = $bytes[$idx] -bor (0x80 -shr ($x % 8))
    }
    for ($y = 0; $y -lt $PageHeight; $y++) {
        $idx = $y * $RowStride
        $bytes[$idx] = $bytes[$idx] -bor 0x80
    }
    [IO.File]::WriteAllBytes($Path, $bytes)
}

Add-SystemDrawing

if ($SelfTest) {
    $testDir = Join-Path $env:TEMP "fjker-page-selftest"
    New-Item -ItemType Directory -Force -Path $testDir | Out-Null
    $binPath = Join-Path $testDir "sample.bin"
    $pngPath = Join-Path $testDir "sample.png"
    New-SelfTestBin -Path $binPath
    Convert-PageBinToPng -BinPath $binPath -PngPath $pngPath -ImageScale $Scale
    Write-Host "Self-test PNG written to: $pngPath"
    return
}

if (-not $JobId) {
    throw "Missing -JobId. Example: .\tools\download_job_pages.ps1 -JobId 7bb3"
}

if (-not $Token) {
    $Token = Read-Host "Paste FJKER_API_TOKEN"
}

$BaseUrl = Normalize-BaseUrl $BaseUrl
$headers = Get-AuthHeaders $Token

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-Location) ("fjker_job_{0}" -f $JobId)
}

$binDir = Join-Path $OutputDir "bin"
$pngDir = Join-Path $OutputDir "png"
New-Item -ItemType Directory -Force -Path $binDir, $pngDir | Out-Null

Write-Step "Checking job $JobId at $BaseUrl"
$status = Wait-ReadyJob -Url $BaseUrl -Job $JobId -Headers $headers
$pageCount = [int]$status.pages
if ($pageCount -le 0) {
    throw "Job $JobId is ready but has no pages."
}

Write-Step "Downloading and converting $pageCount pages"
$digits = [Math]::Max(2, $pageCount.ToString().Length)
$pngFiles = @()
for ($i = 0; $i -lt $pageCount; $i++) {
    $stem = "page_{0}" -f ($i.ToString("D$digits"))
    $binPath = Join-Path $binDir "$stem.bin"
    $pngPath = Join-Path $pngDir "$stem.png"
    $uri = "$BaseUrl/jobs/$JobId/pages/$i"

    Write-Host "  page $i -> $pngPath"
    Invoke-WebRequest -Method Get -Uri $uri -Headers $headers -OutFile $binPath | Out-Null
    Convert-PageBinToPng -BinPath $binPath -PngPath $pngPath -ImageScale $Scale
    $pngFiles += $pngPath
}

Write-Gallery -Directory $OutputDir -PngFiles $pngFiles -Job $JobId

Write-Host ""
Write-Host "Done."
Write-Host "Output directory: $OutputDir"
Write-Host "Gallery: $((Resolve-Path (Join-Path $OutputDir 'index.html')).Path)"

if ($OpenFolder) {
    Start-Process $OutputDir
}
