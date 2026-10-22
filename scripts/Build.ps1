param([string]$BuildDirectory='build',[switch]$GpuValidation)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
$buildPath=if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $sourceRoot $BuildDirectory }
$validation=if ($GpuValidation) { 'ON' } else { 'OFF' }
& cmake -S $sourceRoot -B $buildPath -G 'Visual Studio 17 2022' -A x64 "-DAVALON_GPU_VALIDATION=$validation"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build $buildPath --config RelWithDebInfo -j 4
if ($LASTEXITCODE) { throw 'Build failed' }
& ctest --test-dir $buildPath -C RelWithDebInfo --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
$payload=Join-Path $buildPath 'bin\RelWithDebInfo'
foreach ($file in @('Launch-AvalonVR.ps1','Launch-AvalonVR.cmd','README.md')) {
 Copy-Item -LiteralPath (Join-Path $sourceRoot "package\$file") -Destination $payload -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $payload 'AvalonVR.ini'))) {
 Copy-Item -LiteralPath (Join-Path $sourceRoot 'package\AvalonVR.ini') -Destination $payload
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'LICENSE'),(Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md') -Destination $payload -Force
$licenses=New-Item -ItemType Directory -Path (Join-Path $payload 'licenses') -Force
$licenseFiles=@{
 'vendor\minhook\LICENSE.txt'='MinHook-LICENSE.txt'
 'vendor\openxr\LICENSE'='OpenXR-LICENSE.txt'
 'vendor\openxr\jsoncpp-LICENSE.txt'='JsonCpp-LICENSE.txt'
 'vendor\nvapi\License.txt'='NVAPI-LICENSE.txt'
}
foreach ($entry in $licenseFiles.GetEnumerator()) {
 Copy-Item -LiteralPath (Join-Path $sourceRoot $entry.Key) -Destination (Join-Path $licenses.FullName $entry.Value) -Force
}
Write-Output "Build and checks passed. Runtime output: $payload"
