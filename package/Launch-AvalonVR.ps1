param([string]$GameRoot)
$ErrorActionPreference='Stop'
if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Start Steam before launching AvalonVR.' }
if (-not $GameRoot) {
 $steamPath=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
 $libraries=@()
 if ($steamPath) {
  $libraries+= $steamPath
  $libraryFile=Join-Path $steamPath 'steamapps\libraryfolders.vdf'
  if (Test-Path -LiteralPath $libraryFile) {
   foreach ($line in Get-Content -LiteralPath $libraryFile) {
    if ($line -match '"path"\s+"([^"]+)"') { $libraries+= $matches[1].Replace('\\','\') }
   }
  }
 }
 foreach ($library in ($libraries | Select-Object -Unique)) {
  $candidate=Join-Path $library 'steamapps\common\Tainted Grail FoA'
  if (Test-Path -LiteralPath (Join-Path $candidate 'Fall of Avalon.exe')) { $GameRoot=$candidate; break }
 }
}
if (-not $GameRoot) { throw 'Game not found. Run this script with -GameRoot followed by the game install directory.' }
if (Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) { throw 'Close the game normally before starting AvalonVR.' }
& "$PSScriptRoot\AvalonVRLauncher.exe" "$GameRoot\Fall of Avalon.exe" "$PSScriptRoot\AvalonVR.dll" '-force-d3d11' '-screen-fullscreen' '0' '-logFile' "$PSScriptRoot\AvalonVR-Player.log"
if ($LASTEXITCODE) { throw "AvalonVR launcher failed: $LASTEXITCODE" }
