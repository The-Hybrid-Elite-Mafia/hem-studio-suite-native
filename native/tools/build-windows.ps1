param(
    [Parameter(Mandatory=$true)][string]$JucePath,
    [string]$BuildDirectory = "build-windows",
    [switch]$EnableAsio,
    [string]$AsioSdkPath = ""
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "CMake is required; this script does not install tools" }
if (-not (Test-Path (Join-Path $JucePath "CMakeLists.txt"))) { throw "Supply the JUCE 8.0.10 source directory" }
$Build = Join-Path $Root $BuildDirectory
$Arguments = @("-S", $Root, "-B", $Build, "-G", "Visual Studio 17 2022", "-A", "x64", "-DJUCE_PATH=$JucePath", "-DHEM_ENABLE_ASIO=OFF")
if ($EnableAsio) { $Arguments = $Arguments | Where-Object { $_ -ne "-DHEM_ENABLE_ASIO=OFF" }; $Arguments += @("-DHEM_ENABLE_ASIO=ON", "-DHEM_ASIO_SDK_PATH=$AsioSdkPath") }
& cmake @Arguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
& cmake --build $Build --config Release --target HEMStudioSuite
if ($LASTEXITCODE -ne 0) { throw "Native Windows compilation failed" }
Write-Host "Windows application compiled. Tests and hardware acceptance must still be run on this machine."
