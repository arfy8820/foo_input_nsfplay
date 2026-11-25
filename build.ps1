param(
    [string]$PlatformToolset,
    [string]$FoobarDirectory,
    [switch]$SkipPreferencesTests,
    [ValidateSet('x64', 'Win32')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Install Visual Studio C++ Build Tools (VS2022 or later).' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'No Visual Studio x86/x64 C++ toolchain found.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
if (!$FoobarDirectory) {
    $programFiles = $env:ProgramFiles
    if ($Platform -eq 'Win32' -and ${env:ProgramFiles(x86)}) { $programFiles = ${env:ProgramFiles(x86)} }
    $FoobarDirectory = Join-Path $programFiles 'foobar2000'
}
if (!$PlatformToolset) {
    $toolsetPaths = Get-ChildItem (Join-Path $installation 'MSBuild/Microsoft/VC') -Filter PlatformToolsets -Recurse -Directory |
        Where-Object { $_.Parent.Name -eq $Platform }
    $toolsets = @($toolsetPaths | Get-ChildItem -Directory | Select-Object -ExpandProperty Name -Unique)
    if ($toolsets -contains 'v143') { $PlatformToolset = 'v143' }
    elseif ($toolsets -contains 'v145') { $PlatformToolset = 'v145' }
    else { throw 'No supported v143/v145 toolset found. Pass -PlatformToolset explicitly.' }
}
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$previousPath = $env:PATH
try {
    # Keep compiler temporary files within this checkout, including in managed sandboxes.
    $env:TEMP = Join-Path $projectRoot 'build/temp'
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force $env:TEMP | Out-Null
    $options = @('/m','/nr:false','/nologo','/v:minimal','/p:Configuration=Release',"/p:Platform=$Platform","/p:PlatformToolset=$PlatformToolset",'/p:TrackFileAccess=false','/p:ForceRebuild=true')
    & $msbuild (Join-Path $projectRoot 'foo_input_nsfplay.vcxproj') @options
    if ($LASTEXITCODE -ne 0) { throw 'Component build failed.' }
    & $msbuild (Join-Path $projectRoot 'engine_smoke.vcxproj') @options /p:BuildProjectReferences=false
    if ($LASTEXITCODE -ne 0) { throw 'Smoke-test build failed.' }
    $output = Join-Path $projectRoot "build/$Platform/Release"
    & (Join-Path $output 'engine_smoke.exe') (Join-Path $projectRoot 'tests/fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'Engine smoke tests failed.' }
    if (!$SkipPreferencesTests) {
        $shared = Join-Path $FoobarDirectory 'shared.dll'
        if (!(Test-Path -LiteralPath $shared)) {
            throw "Preferences regression tests need $Platform foobar2000 shared.dll. Pass -FoobarDirectory with its location, or use -SkipPreferencesTests."
        }
        $FoobarDirectory = (Resolve-Path -LiteralPath $FoobarDirectory).Path
        $shared = Join-Path $FoobarDirectory 'shared.dll'
        $image = [IO.File]::ReadAllBytes($shared)
        $pe = [BitConverter]::ToInt32($image, 0x3c)
        $machine = [BitConverter]::ToUInt16($image, $pe + 4)
        $expected = if ($Platform -eq 'Win32') { 0x014c } else { 0x8664 }
        if ($machine -ne $expected) { throw "shared.dll in $FoobarDirectory does not match the $Platform build." }
        & $msbuild (Join-Path $projectRoot 'preferences_smoke.vcxproj') @options /p:BuildProjectReferences=false
        if ($LASTEXITCODE -ne 0) { throw 'Preferences regression-test build failed.' }
        $env:PATH = $FoobarDirectory + ';' + $previousPath
        $testProfile = if ($Platform -eq 'Win32') { 'build/preferences-test-profile-Win32' } else { 'build/preferences-test-profile' }
        & (Join-Path $output 'preferences_smoke.exe') (Join-Path $projectRoot $testProfile)
        if ($LASTEXITCODE -ne 0) { throw 'Preferences regression tests failed.' }
        $env:PATH = $previousPath
    } else { Write-Warning 'Preferences regression tests explicitly skipped.' }

    $stage = Join-Path $projectRoot "build/package-$Platform"
    $dllStage = if ($Platform -eq 'Win32') { $stage } else { Join-Path $stage 'x64' }
    New-Item -ItemType Directory -Force $dllStage,(Join-Path $stage 'notices') | Out-Null
    Copy-Item -LiteralPath (Join-Path $output 'foo_input_nsfplay.dll') -Destination $dllStage
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $projectRoot 'USER_GUIDE.md') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $projectRoot 'VALIDATION.md') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $projectRoot '../bbbradsmith/nsfplay/readme.txt') -Destination (Join-Path $stage 'notices/nsfplay-readme.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot '../SDK-2026-10-01/sdk-license.txt') -Destination (Join-Path $stage 'notices/sdk-license.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot '../SDK-2026-10-01/pfc/pfc-license.txt') -Destination (Join-Path $stage 'notices/pfc-license.txt')
    $dist = Join-Path $projectRoot 'dist'
    New-Item -ItemType Directory -Force $dist | Out-Null
    $packageName = if ($Platform -eq 'Win32') { 'foo_input_nsfplay-x86' } else { 'foo_input_nsfplay' }
    $zip = Join-Path $dist "$packageName.zip"
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $stream = [IO.File]::Open($zip, [IO.FileMode]::Create)
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($item in Get-ChildItem -LiteralPath $stage -Recurse -File) {
            $entry = $item.FullName.Substring($stage.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $item.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
    $package = Join-Path $dist "$packageName.fb2k-component"
    Move-Item -LiteralPath $zip -Destination $package -Force
    $dllDist = if ($Platform -eq 'Win32') { Join-Path $dist 'Win32' } else { $dist }
    New-Item -ItemType Directory -Force $dllDist | Out-Null
    Copy-Item -LiteralPath (Join-Path $output 'foo_input_nsfplay.dll') -Destination $dllDist
    Write-Host "Built and tested: $package"
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:PATH = $previousPath
}
