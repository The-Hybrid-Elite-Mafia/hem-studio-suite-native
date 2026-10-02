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
    Get-CimInstance Win32_ComputerSystem | Format-List NumberOfLogicalProcessors,TotalPhysicalMemory | Out-File ci-logs/runner-capacity.txt
    Invoke-Logged cmake @('--version') 'ci-logs/cmake-version.txt'
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    Invoke-Logged $vswhere @('-products','*','-version','[18.0,19.0)','-requires','Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64','-format','json') 'ci-logs/visual-studio.json'
    $instances = @(Get-Content ci-logs/visual-studio.json -Raw | ConvertFrom-Json)
    if ($instances.Count -eq 0) { throw 'VS 2026 with the MSVC 14.44/v143 component is not installed on this windows-2025 image; do not silently change runner or toolset' }
    $manifest = Get-Content native/INTEGRATION-MANIFEST.json -Raw | ConvertFrom-Json
    foreach ($relative in @('Source/CoreBaseline/Engine.h','Source/CoreBaseline/Engine.cpp')) {
        $entry = @($manifest.files | Where-Object path -eq $relative)
        if ($entry.Count -ne 1) { throw "Baseline missing from original manifest: $relative" }
        $actual = (Get-FileHash (Join-Path 'native' $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
        "$relative expected=$($entry[0].sha256) actual=$actual" | Add-Content ci-logs/baseline-hashes.txt
        if ($actual -ne $entry[0].sha256) { throw "Protected baseline was changed: $relative" }
    }
    'Protected CoreBaseline matches the imported source manifest.' | Set-Content ci-logs/baseline-integrity.txt
} elseif ($Phase -eq 'Configure') {
    $juceRevision = (& git -C $Juce rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $juceRevision -ne '3af3ce009f6a02f6fa651008fffb5b41743a9fab') { throw 'JUCE pin mismatch' }
    $juceRevision | Set-Content ci-logs/juce-commit.txt
    $fixture = Join-Path $Build 'Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3'
    Invoke-Logged cmake @('-S','native','-B',$Build,'-G','Visual Studio 18 2026','-A','x64','-T','v143,version=14.44,host=x64',"-DJUCE_PATH=$Juce",'-DBUILD_TESTING=ON','-DHEM_DSP_TESTS_ONLY=OFF','-DHEM_ENABLE_ASIO=OFF','-DHEM_BUILD_VST3_TEST_FIXTURE=ON',"-DHEM_TEST_VST3_BUNDLE=$fixture",'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded') 'ci-logs/configure.log'
    Invoke-Logged ctest @('--test-dir',$Build,'-C','Release','--show-only=json-v1') 'ci-logs/test-inventory.json'
    $inventory = Get-Content ci-logs/test-inventory.json -Raw | ConvertFrom-Json
    $names = @($inventory.tests | ForEach-Object name | Sort-Object)
    $expected = @(Get-Content ci/windows/expected-tests.txt | Where-Object { $_.Trim() } | Sort-Object)
    $difference = @(Compare-Object $expected $names)
    if ($difference.Count -ne 0) { $difference | Out-String | Write-Host; throw 'The full expected 29-test CTest inventory is not present' }
} elseif ($Phase -eq 'Build') {
    # ALL_BUILD includes the exact HEMStudioSuite app, all tests, and HEMVST3TestFixture_VST3.
    # Two MSBuild workers limit private-runner RAM pressure and included-minute use.
        # JUCE adds bare /MP. _CL_ is appended by MSVC after command arguments,
    # so /MP1 prevents two MSBuild projects from each spawning a full CPU pool.
    $previousCl = $env:_CL_
    try {
        $env:_CL_ = ("$previousCl /MP1").Trim()
        'MSBuild project workers=2; appended MSVC _CL_ override=/MP1; effective compiler flags may emit D9025.' | Set-Content ci-logs/compiler-concurrency.txt
        # Catch the two observed Windows readback regressions first. Their build
        # objects stay in this same build tree and are reused by ALL_BUILD below.
                foreach ($target in @('hem_offline_worker_tests','hem_studio_branding_tests')) {
            Invoke-Logged cmake @('--build',$Build,'--config','Release','--target',$target,'--parallel','2','--','/verbosity:normal',"/bl:$Root/ci-logs/msbuild-focused-$target.binlog",'/fileLogger',"/fileLoggerParameters:LogFile=$Root/ci-logs/msbuild-focused-$target-diagnostic.log;Verbosity=diagnostic") "ci-logs/build-focused-$target.log"
        }
        Invoke-Logged ctest @('--test-dir',$Build,'-C','Release','--verbose','--output-on-failure','--no-tests=error','--timeout','240','-R','^(hem_offline_worker_tests|hem_studio_branding_tests)$','--output-junit',"$Root/ci-logs/ctest-focused.xml") 'ci-logs/ctest-focused.log'
        Invoke-Logged cmake @('--build',$Build,'--config','Release','--parallel','2','--','/verbosity:normal',"/bl:$Root/ci-logs/msbuild.binlog",'/fileLogger',"/fileLoggerParameters:LogFile=$Root/ci-logs/msbuild-diagnostic.log;Verbosity=diagnostic") 'ci-logs/build.log'
    } finally { $env:_CL_ = $previousCl }
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
