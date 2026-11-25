param(
    [string]$FoobarDirectory,
    [string]$PlatformToolset='v145',
    [ValidateSet('x64', 'Win32')][string]$Platform='x64'
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
if (!$FoobarDirectory) {
    $programFiles = $env:ProgramFiles
    if ($Platform -eq 'Win32' -and ${env:ProgramFiles(x86)}) { $programFiles = ${env:ProgramFiles(x86)} }
    $FoobarDirectory = Join-Path $programFiles 'foobar2000'
}
$testDirectory = if ($Platform -eq 'Win32') { 'build/host-validation-0.3-Win32' } else { 'build/host-validation-0.3' }
$testRoot=Join-Path $root $testDirectory
$fixtures=Join-Path $testRoot 'fixtures'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$oldTest=$env:NSFPLAY_TEST_DIR
try {
    $env:TEMP=Join-Path $root 'build/temp';$env:TMP=$env:TEMP
    & $msbuild (Join-Path $root 'host_smoke.vcxproj') /m /nr:false /nologo /v:minimal /p:Configuration=Release "/p:Platform=$Platform" "/p:PlatformToolset=$PlatformToolset" /p:TrackFileAccess=false /p:ForceRebuild=true /p:BuildProjectReferences=false
    if($LASTEXITCODE -ne 0){throw 'Host smoke-test build failed'}
    New-Item -ItemType Directory -Force $testRoot,$fixtures,(Join-Path $testRoot 'components') | Out-Null
    $testExe=Join-Path $testRoot 'foobar2000.exe'
    Get-Process foobar2000 -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $testExe} | Stop-Process
    Get-ChildItem -LiteralPath $FoobarDirectory -File | Where-Object {$_.Extension -eq '.dll' -or ($_.Extension -eq '.exe' -and $_.Name -ne 'uninstall.exe')} | Copy-Item -Destination $testRoot -Force
    foreach($folder in @('runtime','doc','icons','themes')) {
        $source = Join-Path $FoobarDirectory $folder
        if (Test-Path -LiteralPath $source -PathType Container) { Copy-Item -LiteralPath $source -Destination $testRoot -Recurse -Force }
    }
    foreach($file in @('foo_ui_std.dll','foo_input_std.dll','foo_converter.dll','foo_dsp_std.dll','foo_dsp_eq.dll','foo_fileops.dll','foo_freedb2.dll','foo_out_upnp.dll','foo_unpack.dll')) {
        $source = Join-Path $FoobarDirectory "components/$file"
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination (Join-Path $testRoot 'components') -Force }
    }
    foreach($file in @('foo_input_nsfplay.dll','foo_nsfplay_test.dll')) {Copy-Item -LiteralPath (Join-Path $root "build/$Platform/Release/$file") -Destination (Join-Path $testRoot 'components') -Force}
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
    if(!$result.Contains('RESULT: PASS')){throw "Isolated host test failed or did not reach its test callback. See $(Join-Path $fixtures 'host-smoke.txt')."}
} finally {$env:TEMP=$oldTemp;$env:TMP=$oldTmp;$env:NSFPLAY_TEST_DIR=$oldTest}
