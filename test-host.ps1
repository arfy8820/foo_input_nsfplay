param([string]$FoobarDirectory='C:\Program Files\foobar2000',[string]$PlatformToolset='v145')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$testRoot=Join-Path $root 'build/host-validation'
$fixtures=Join-Path $testRoot 'fixtures'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$oldTest=$env:NSFPLAY_TEST_DIR
try {
    $env:TEMP=Join-Path $root 'build/temp';$env:TMP=$env:TEMP
    & $msbuild (Join-Path $root 'host_smoke.vcxproj') /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$PlatformToolset" /p:TrackFileAccess=false /p:ForceRebuild=true /p:BuildProjectReferences=false
    if($LASTEXITCODE -ne 0){throw 'Host smoke-test build failed'}
    New-Item -ItemType Directory -Force $testRoot,$fixtures,(Join-Path $testRoot 'components') | Out-Null
    $testExe=Join-Path $testRoot 'foobar2000.exe'
    Get-Process foobar2000 -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $testExe} | Stop-Process
    Get-ChildItem -LiteralPath $FoobarDirectory -File | Where-Object {$_.Extension -eq '.dll' -or ($_.Extension -eq '.exe' -and $_.Name -ne 'uninstall.exe')} | Copy-Item -Destination $testRoot -Force
    Copy-Item -LiteralPath (Join-Path $FoobarDirectory 'runtime') -Destination $testRoot -Recurse -Force
    foreach($file in @('foo_ui_std.dll','foo_input_std.dll','foo_converter.dll')) {Copy-Item -LiteralPath (Join-Path $FoobarDirectory ('components/'+$file)) -Destination (Join-Path $testRoot 'components') -Force}
    foreach($file in @('foo_input_nsfplay.dll','foo_nsfplay_test.dll')) {Copy-Item -LiteralPath (Join-Path $root ('build/x64/Release/'+$file)) -Destination (Join-Path $testRoot 'components') -Force}
    [IO.File]::WriteAllText((Join-Path $testRoot 'portable_mode_enabled'),'')
    Copy-Item -LiteralPath (Join-Path $root 'tests/fixtures/smoke.nsfe') -Destination $fixtures -Force
    Copy-Item -LiteralPath (Join-Path $root 'tests/fixtures/smoke.nsf') -Destination (Join-Path $fixtures 'host-write.nsf') -Force
    [IO.File]::WriteAllText((Join-Path $fixtures 'host-smoke.txt'),'')
    $runningMarker=Join-Path $testRoot 'profile/running'
    if(Test-Path -LiteralPath $runningMarker){Remove-Item -LiteralPath $runningMarker -Force}
    $env:NSFPLAY_TEST_DIR=$fixtures
    $process=Start-Process -FilePath $testExe -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    do {if($process.WaitForExit(1000)){break}} while([DateTime]::UtcNow -lt $deadline)
    if(!$process.HasExited) {Stop-Process -Id $process.Id}
    [string]$result=[IO.File]::ReadAllText((Join-Path $fixtures 'host-smoke.txt'))
    Write-Host $result
    if(!$result.Contains('RESULT: PASS')){throw 'Isolated host test failed or did not reach its test callback. See build/host-validation/fixtures/host-smoke.txt.'}
} finally {$env:TEMP=$oldTemp;$env:TMP=$oldTmp;$env:NSFPLAY_TEST_DIR=$oldTest}
