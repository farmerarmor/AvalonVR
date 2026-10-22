# AvalonVR

Experimental native same-frame stereo VR mod for **Tainted Grail: The Fall of Avalon**. Includes OpenXR head tracking, native DLSS, adjustable fixed quad-view foveation, an aim-aligned crosshair, and menus anchored in space.

Supports **Steam build 25923610, game 1.30.101 (1.30a), Unity 6000.0.64f1, D3D11, Windows x64**. The launcher checks the executable, UnityPlayer and GameAssembly hashes and refuses unsupported builds. Other game builds are not supported by changing the hashes alone.

## Install and launch

1. Download the **runtime ZIP** from Releases and extract it to a writable folder outside the game directory.
2. Start Steam, connect the headset and start the chosen OpenXR runtime. Oculus/Quest Link and SteamVR through its tested Meta compatibility path have been checked; other headset drivers need testing.
3. Close the game if it is running, then run **Launch-AvalonVR.cmd**. The launcher detects Steam libraries. For a custom location, run `Launch-AvalonVR.ps1 -GameRoot "<game directory>"` in PowerShell.
4. Use the game's keyboard/mouse or gamepad controls. Give the game keyboard focus for mod hotkeys.

Keep `AvalonVR.dll`, `AvalonVRLauncher.exe`, **`AvalonVRCharacterPools.dll`**, and `AvalonVR.ini` together. The character helper must be available before game startup. The package requires the Microsoft Visual C++ x64 runtime. It leaves original game files unchanged. To play flat or roll back, close the game and launch it normally from Steam.

When replacing an earlier development package, extract the new release to its own folder. Use the included INI as the starting point, then copy preferred graphics settings; old character-pool/loading settings can omit required fixes. Each release keeps its own configuration.

## Rendering and performance

Dedicated HDRP cameras render both eyes in the **same game simulation frame**. This is not alternating-eye rendering or depth-generated stereo. Predicted OpenXR poses drive view/projection matrices; native mouse/gamepad weapon and interaction aiming remain unchanged. Motion controllers are not implemented.

The main-menu/loading path uses the game's desktop output on a VR panel. Gameplay switches to native stereo after loading. The normal desktop render is skipped in gameplay for performance, so its mirror can remain frozen. HUD and menus are captured once; menus anchor in space and include the native mouse cursor. F2 recenters and reanchors them. The crosshair follows the native aim ray when switching spells, melee and unequipped hands; it does not predict projectile drop or homing.

The release defaults to native **DLSS Performance**, 100% runtime-recommended output, and **fixed quad views**. Four native views are composed locally into two OpenXR eye images: a lower-resolution wide view and a sharper central region for each eye. This is fixed foveation; it does not use eye tracking or require an OpenXR quad-view extension.

Default focus size is 40% horizontally/vertically and peripheral resolution 25%. Ctrl +/- changes focus size; Shift +/- changes strength. Larger strength means lower peripheral resolution. Main keyboard and numpad keys work and repeat when held. Successful size/strength changes save after release. Mip matching, shared exposure and matched fog sampling correct the tested foliage and brightness/fog seams. Use Ctrl+F12 to compare with ordinary stereo.

In `AvalonVR.ini`, `[Performance] UpscalerPreset` selects 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance. F11 cycles these; F10 compares with full internal resolution. Output resolution is independent of the DLSS input resolution. Disable NVIDIA App model overrides when reproducing the tested bundled-DLSS baseline; the mod does not replace the game's DLSS library or alter driver settings.

Rotating temporary staging uploads reduce a verified Unity buffer-remap stall while preserving native GPU buffers and partial writes. Two short same-torch comparisons improved delivered app frames from about 73 to 90 per second on the test system, while the Oculus CPU-duration HUD remained about 6.6-6.9 ms. This is a scene-specific result, not a general FPS guarantee. `[Performance] AsyncBufferUploads=0` plus restart restores native uploads.

The character-pool capacity floor, loading-camera guard and native cleanup path are enabled. They passed repeated island/mainland tests after earlier builds lost or stretched character bodies. Already-dead NPCs were also seen unloading/reappearing with their interaction prompts; the cause of that separate behavior was not established.

## Optional effects

- `[Effects] DisableGroundFog=1` removes local fog-volume draws using the two marked shaders. Default 0 keeps them. Other local fog volumes sharing those shaders can also be removed.
- `[Effects] DisableMarkedFogLens=1` is the previously accepted removal of the marked fog/lens RGB effect. Set0 to restore it.
- Both effect options reload within about one second while running. Home restores shader hunting while retaining configured effect choices.
- `[Render] ShadowFix=1` enables player-centered cascades. It improves the tested head-dependent fading but shortens the visible shadow range. Default 0 retains normal shadows. Ctrl+F8 compares the modes for the current session.

## Controls

| Key | Action |
| --- | --- |
| F1 | Toggle stereo rendering |
| F2 | Recenter head position/yaw and reanchor menus |
| F3 | Toggle the desktop menu panel during gameplay |
| F4 | Save a private diagnostic eye/UI capture beside the DLL; may briefly stall |
| F8 | Toggle desktop rendering during gameplay |
| F10 | Selected upscaler versus full internal resolution |
| F11 | Next native DLSS preset and resolution label |
| Ctrl+F12 | Fixed quad views versus ordinary stereo |
| Ctrl+Shift+F12 | Show/hide the focus-region border |
| Ctrl +/- | Adjust focus-region size; hold to repeat |
| Shift +/- | Adjust foveation strength; hold to repeat |
| Ctrl+F8 | Optional shadow workaround comparison |
| Ctrl+Shift+F8 | Matched versus native fog sampling |
| Ctrl+Alt+F12 | Peripheral mip matching comparison |
| Ctrl+Alt+F11 | Shared versus independent exposure comparison |
| Ctrl+F11 | Batched versus separate eye-transfer setup |
| Insert | DLSS scheduling comparison |
| End | Enable shader hunting |
| Page Up / Page Down | Select/suppress a shader; hold to repeat every 0.2 seconds |
| Delete | Save the selected shader mark and diagnostic inputs |
| Home | Restore the hunter selection and disable hunting |

F5/F6/F7 are no longer mod toggles. F9 retains the game's normal quick-load action. Most hotkeys affect the current session; only focus-size/strength tuning saves automatically. Other INI settings require restart unless stated above. See [shader hunter documentation](tools/SHADER-HUNTER.md).

## Test scope and limitations

Headset checks covered upright stereo/tracking, HUD and pause/inventory menus, cursor alignment, aim-aligned crosshair, menu anchoring/reloads, corrected depth-dependent lighting, repeated character travel, quad tuning, mip/exposure/fog corrections and optional marked ground-fog removal. Broad stability, all dialogue/cutscenes, projectile impact alignment and other headset drivers remain unvalidated. The remaining native shadow fading is why the shadow workaround is optional. The earlier NVIDIA VRS/hidden-area-mask experiments did not establish a useful gain and are not enabled or advertised as working features.

F4 and shader hunting create private diagnostics in the mod folder. Do not upload personal captures, logs, dumps or game binaries with bug reports unless you intend to share them. The repository and release contain none of those artifacts.

## Build

Requires Visual Studio 2022 C++ tools, Windows SDK, CMake 3.24+ and PowerShell. From the source root:

```powershell
.\scripts\Build.ps1
```

This builds all three runtime components and runs 10 standard checks. The portable launcher/configuration is copied into `build/bin/RelWithDebInfo`; an existing INI is retained. `scripts/Launch-AvalonVR.ps1` launches that output, with optional `-GameRoot` and `-BuildDirectory` arguments.

`-GpuValidation` also builds standalone GPU harnesses. Some take user-captured shader fixtures; copyrighted game bytecode is not included. Prior validation of the shipped code passed 80 ground-fog, 88 hunter and 67 marked-effect checks per protected/unprotected context, plus 73 upload GPU checks. These complement the headset results and do not prove broad stability.

Project code is under the MIT license. Bundled dependencies retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is an unofficial mod, unaffiliated with the game's developers or publishers.
