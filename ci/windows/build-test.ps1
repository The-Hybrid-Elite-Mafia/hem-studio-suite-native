param([Parameter(Mandatory=$true)][ValidateSet('Validate','Configure','Build','Test')][string]$Phase)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $Root
New-Item -ItemType Directory -Force ci-logs | Out-Null
# PowerShell must inspect native exit codes explicitly; a piped compiler failure is fatal.
$PSNativeCommandUseErrorActionPreference = $false
function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$Log) {
    & $Program @Arguments 2>&1 | Tee-Object -FilePath $Log
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "$Program failed with exit code $code; see $Log" }
}
$Build = Join-Path $Root 'build'
$Juce = Join-Path $Root 'dependencies/JUCE'
if ($Phase -eq 'Validate') {
    if (-not $IsWindows) { throw 'This workflow requires real Windows; Linux/Wine is not accepted.' }
    foreach ($file in @('native/CMakeLists.txt','native/Source/Main.cpp','native/Tests/CoreMasterStageTests.cpp','native/Tests/VST3Fixture/CMakeLists.txt','native/Assets/studio_suite_master.png','native/Assets/ghost_brand_reference.png')) {
        if (-not (Test-Path $file -PathType Leaf)) { throw "Missing required source: $file" }
    }
    Invoke-Logged git @('rev-parse','HEAD') 'ci-logs/source-commit.txt'
    Invoke-Logged git @('status','--porcelain') 'ci-logs/source-status.txt'
    Get-CimInstance Win32_OperatingSystem | Format-List Caption,Version,BuildNumber,OSArchitecture | Out-File ci-logs/windows.txt
    @("RUNNER_OS=$env:RUNNER_OS", "RUNNER_ARCH=$env:RUNNER_ARCH", "ImageOS=$env:ImageOS", "ImageVersion=$env:ImageVersion") | Add-Content ci-logs/windows.txt
    Invoke-Logged cmake @('--version') 'ci-logs/cmake-version.txt'
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    Invoke-Logged $vswhere @('-products','*','-version','[17.0,18.0)','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-format','json') 'ci-logs/visual-studio.json'
    $manifest = Get-Content native/INTEGRATION-MANIFEST.json -Raw | ConvertFrom-Json
    foreach ($relative in @('Source/CoreBaseline/Engine.h','Source/CoreBaseline/Engine.cpp')) {
        $entry = @($manifest.files | Where-Object path -eq $relative)
        if ($entry.Count -ne 1) { throw "Baseline missing from original manifest: $relative" }
        $actual = (Get-FileHash (Join-Path 'native' $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $entry[0].sha256) { throw "Protected baseline was changed: $relative" }
    }
    'Protected CoreBaseline matches the imported source manifest.' | Set-Content ci-logs/baseline-integrity.txt
} elseif ($Phase -eq 'Configure') {
    $juceRevision = (& git -C $Juce rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $juceRevision -ne '3af3ce009f6a02f6fa651008fffb5b41743a9fab') { throw 'JUCE pin mismatch' }
    $juceRevision | Set-Content ci-logs/juce-commit.txt
    $fixture = Join-Path $Build 'Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3'
    Invoke-Logged cmake @('-S','native','-B',$Build,'-G','Visual Studio 17 2022','-A','x64',"-DJUCE_PATH=$Juce",'-DBUILD_TESTING=ON','-DHEM_DSP_TESTS_ONLY=OFF','-DHEM_ENABLE_ASIO=OFF','-DHEM_BUILD_VST3_TEST_FIXTURE=ON',"-DHEM_TEST_VST3_BUNDLE=$fixture",'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded') 'ci-logs/configure.log'
    Invoke-Logged ctest @('--test-dir',$Build,'-C','Release','--show-only=json-v1') 'ci-logs/test-inventory.json'
    $inventory = Get-Content ci-logs/test-inventory.json -Raw | ConvertFrom-Json
    $names = @($inventory.tests | ForEach-Object name | Sort-Object)
    $expected = @(Get-Content ci/windows/expected-tests.txt | Where-Object { $_.Trim() } | Sort-Object)
    $difference = @(Compare-Object $expected $names)
    if ($difference.Count -ne 0) { $difference | Out-String | Write-Host; throw 'The full expected 29-test CTest inventory is not present' }
} elseif ($Phase -eq 'Build') {
    # ALL_BUILD includes the exact HEMStudioSuite app, all tests, and HEMVST3TestFixture_VST3.
    # Two MSBuild workers limit private-runner RAM pressure and included-minute use.
    Invoke-Logged cmake @('--build',$Build,'--config','Release','--parallel','2','--','/verbosity:normal',"/bl:$Root/ci-logs/msbuild.binlog",'/fileLogger',"/fileLoggerParameters:LogFile=$Root/ci-logs/msbuild-diagnostic.log;Verbosity=diagnostic") 'ci-logs/build.log'
    if (-not (Test-Path (Join-Path $Build 'HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.exe'))) { throw 'The exact application executable was not built' }
    if (-not (Test-Path (Join-Path $Build 'Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3'))) { throw 'The owned VST3 fixture bundle was not built' }
} else {
    # Run every registered test, sequentially, with complete output and an individual timeout.
    Invoke-Logged ctest @('--test-dir',$Build,'-C','Release','--parallel','1','--verbose','--output-on-failure','--no-tests=error','--timeout','240','--output-junit',"$Root/ci-logs/ctest-junit.xml") 'ci-logs/ctest-full.log'
    $skips = @(Select-String -Path ci-logs/ctest-full.log -Pattern '\bSKIP:|\*\*\*Skipped')
    if ($skips.Count -ne 0) {
        $skips | Out-String | Set-Content ci-logs/skipped-coverage.txt
        throw 'Existing tests reported skipped coverage; see skipped-coverage.txt. This is not a full Windows pass.'
    }
}
