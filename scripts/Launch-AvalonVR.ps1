param([string]$GameRoot,[string]$BuildDirectory='build')
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
$buildPath=if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $sourceRoot $BuildDirectory }
$launcher=Join-Path $buildPath 'bin\RelWithDebInfo\Launch-AvalonVR.ps1'
if (-not (Test-Path -LiteralPath $launcher)) { throw 'Run scripts\Build.ps1 first.' }
& $launcher -GameRoot $GameRoot
