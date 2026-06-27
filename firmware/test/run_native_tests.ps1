param(
  [string]$Compiler = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$testRoot = $PSScriptRoot
$firmwareRoot = Split-Path -Parent $testRoot
$buildRoot = Join-Path $env:TEMP "fjker-native-tests"

if ([string]::IsNullOrWhiteSpace($Compiler)) {
  $candidate = Get-Command g++ -ErrorAction SilentlyContinue
  if (-not $candidate) {
    $candidate = Get-Command clang++ -ErrorAction SilentlyContinue
  }
  if (-not $candidate) {
    throw "No C++ compiler found. Install g++ or clang++, or pass -Compiler <path>."
  }
  $Compiler = $candidate.Source
}

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$tests = @(
  @{
    Name = "test_dither"
    Files = @(
      "test\test_native\test_dither.cpp",
      "src\dither.cpp"
    )
  },
  @{
    Name = "test_app_state"
    Files = @(
      "test\test_native\test_app_state.cpp",
      "src\app.cpp",
      "src\page_store.cpp",
      "src\diagnostics.cpp"
    )
  },
  @{
    Name = "test_page_store"
    Files = @(
      "test\test_native\test_page_store.cpp",
      "src\page_store.cpp",
      "src\diagnostics.cpp"
    )
  },
  @{
    Name = "test_diagnostics"
    Files = @(
      "test\test_native\test_diagnostics.cpp",
      "src\diagnostics.cpp"
    )
  }
)

foreach ($test in $tests) {
  $exe = Join-Path $buildRoot ($test.Name + ".exe")
  $sources = @()
  foreach ($file in $test.Files) {
    $sources += (Join-Path $firmwareRoot $file)
  }

  $compileArgs = @(
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-I", (Join-Path $firmwareRoot "src"),
    "-I", (Join-Path $firmwareRoot "include"),
    "-o", $exe
  ) + $sources

  Write-Host "Compiling $($test.Name)"
  & $Compiler @compileArgs
  if ($LASTEXITCODE -ne 0) {
    throw "Compile failed for $($test.Name)"
  }

  Write-Host "Running $($test.Name)"
  & $exe
  if ($LASTEXITCODE -ne 0) {
    throw "Test failed: $($test.Name)"
  }
}

Write-Host "All native tests passed"
