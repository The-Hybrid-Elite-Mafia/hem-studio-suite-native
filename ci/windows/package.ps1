$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $Root
$exe = Join-Path $Root 'build/HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.exe'
if (-not (Test-Path $exe -PathType Leaf)) { throw 'Exact HEMStudioSuite executable missing' }
$Package = Join-Path $Root 'dist/HEM-Studio-Suite-Windows-x64-Release'
New-Item -ItemType Directory -Force $Package,(Join-Path $Package 'notices') | Out-Null
Copy-Item $exe $Package
# StudioArtwork embeds both PNG assets in the binary. JUCE and the C/C++ runtime
# are linked statically; verify the PE imports rather than guessing which DLLs ship.
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = (& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Cannot locate VS 2022 dependency inspection tool' }
$dumpbin = Get-ChildItem "$vs/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe" | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe missing' }
$imports = & $dumpbin.FullName /dependents $exe 2>&1
$code = $LASTEXITCODE
$imports | Set-Content ci-logs/application-dependencies.txt
if ($code -ne 0) { throw "dumpbin failed: $code" }
$dlls = @($imports | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] } } | Sort-Object -Unique)
if ($dlls.Count -eq 0) { throw 'No PE dependencies found; inspection is inconclusive' }
$systemDlls = @('KERNEL32.dll','USER32.dll','GDI32.dll','WINSPOOL.DRV','COMDLG32.dll','ADVAPI32.dll','SHELL32.dll','OLE32.dll','OLEAUT32.dll','UUID.dll','WINMM.dll','VERSION.dll','IMM32.dll','SHLWAPI.dll','WS2_32.dll','WININET.dll','WINHTTP.dll','DWMAPI.dll','DXGI.dll','D2D1.dll','DWRITE.dll','D3D11.dll','COMCTL32.dll','CRYPT32.dll','UXTHEME.dll','SETUPAPI.dll','MSIMG32.dll','RPCRT4.dll','SHCORE.dll','BCRYPT.dll','USERENV.dll','WTSAPI32.dll','IPHLPAPI.dll','MMDEVAPI.dll','AVRT.dll','AUTHZ.dll','NORMALIZ.dll','MF.dll','MFPLAT.dll','MFREADWRITE.dll','PROPSYS.dll','CRYPTBASE.dll','WINTRUST.dll','NTDLL.dll')
foreach ($dll in $dlls) {
    if ($dll -notin $systemDlls -and $dll -notmatch '^(api-ms-win-|ext-ms-win-)') { throw "Unresolved/non-system runtime dependency needs an explicit packaging decision: $dll" }
}
Copy-Item native/Documentation/RELEASE-LICENSE-CHECK.md (Join-Path $Package 'notices')
Copy-Item dependencies/JUCE/LICENSE.md (Join-Path $Package 'notices/JUCE-LICENSE.md')
Copy-Item dependencies/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt (Join-Path $Package 'notices/VST3-SDK-LICENSE.txt')
@"
HEM Studio Suite: private Windows x64 Release engineering build
Source commit: $env:GITHUB_SHA
Build run: $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
Runner: windows-2025. Toolchain: Visual Studio 2022 MSVC x64, JUCE 8.0.10.
Exact application: Hybrid Elite Mafia - Studio Suite.exe
Assets: both StudioArtwork images are embedded in the executable.
Dependencies: static JUCE/MSVC runtime; PE import verification in runtime-dependencies.txt.
No installer, ASIO driver, third-party plugin, SDK source, or fixture binary is shipped.
The 29 original registered tests must pass before this bundle is uploaded.
Native -0.95 dBTP independent acceptance: NOT VERIFIED (original gate/vector source absent).
Physical audio devices, ASIO, full realtime acceptance and 808/listening tests: NOT VALIDATED.
Private use only. Distribution/licensing remains subject to notices/RELEASE-LICENSE-CHECK.md.
"@ | Set-Content (Join-Path $Package 'BUILD-INFO.txt')
Copy-Item ci-logs/application-dependencies.txt (Join-Path $Package 'runtime-dependencies.txt')
# Smoke-test the packaged executable itself on this real Windows runner.
$process = $null
try {
    $process = Start-Process -FilePath (Join-Path $Package 'Hybrid Elite Mafia - Studio Suite.exe') -WorkingDirectory $Package -PassThru
    Start-Sleep -Seconds 8
    $process.Refresh()
    if ($process.HasExited) { throw "Packaged app exited prematurely: $($process.ExitCode)" }
    if ($process.MainWindowHandle -eq 0) { throw 'Packaged app did not create a native top-level window' }
    if (-not $process.Responding) { throw 'Packaged app window is not responding' }
    "PASS: packaged application remained running and created a responding native window. Physical audio NOT VALIDATED." | Set-Content ci-logs/windows-launch-smoke.txt
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        $null = $process.CloseMainWindow()
        if (-not $process.WaitForExit(10000)) { Stop-Process -Id $process.Id }
    }
}
Copy-Item ci-logs/windows-launch-smoke.txt $Package
$rows = @(Get-ChildItem $Package -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($Package,$_.FullName).Replace('\','/')
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$relative
})
$rows | Set-Content (Join-Path $Package 'SHA256SUMS.txt')
Get-Content (Join-Path $Package 'SHA256SUMS.txt') | Tee-Object -FilePath ci-logs/package-sha256.txt
