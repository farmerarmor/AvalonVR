# 0.1.0 - 2026-10-22

First public experimental release, based on the headset-accepted Stage58 runtime.

- Native same-frame stereo/OpenXR tracking with native DLSS per view.
- Aim-aligned crosshair, native cursor, anchored menus and stale UI-texture recovery after reloads.
- Fixed quad views with saved size/strength tuning, matched texture mips, shared exposure and matched fog sampling.
- Consistent eye clip-plane/depth parameters for the marked lighting and shadow artifacts.
- Character-pool headroom, native destruction cleanup and a loading-camera guard for repeated island/mainland travel.
- Rotating staging uploads for the verified Unity upload stall, with native fallback.
- Optional marked ground-fog removal and a separately configurable marked fog/lens effect removal.
- Optional player-centered shadow workaround, disabled by default because of its shorter visible range.
- Shader hunter with 0.2-second held-key navigation and saved marks.

This remains experimental. General stability, all dialogue/cutscenes, projectile impact alignment and other headset drivers need further testing. No motion-controller support or eye-tracked foveation is included. See README for supported build, controls and test scope.
