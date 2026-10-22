# AvalonVR shader hunter

Load the affected scene and give the game keyboard focus. The hunter starts off.
The normal native stereo, DLSS, HUD/cursor, anchored menus and Stage29 save-reload
UI texture refresh stay active. No F9 diagnostic binding is present.

- **End:** enable the hunter, rebuild the cumulative list with observed shaders
  first, and suppress the selected shader's scene commands.
- **Page Down / Page Up:** restore the previous selection and suppress the next /
  previous shader. Hold either key to repeat every **0.2 seconds** after the
  immediate first step. Releasing stops it; holding both pauses navigation.
  The list wraps across PS, VS, CS, GS, HS and DS together.
- **Delete:** mark the current shader. The mark remains saved after changing the
  selection or disabling the tool. A diagnostic eye capture is requested.
- **Home:** restore the hunted selection and disable hunting. After a mark, this also
  requests a normal baseline eye capture and native input-state snapshot.

The hunter suppresses one selected shader identity at a time, across both eyes. Wait briefly
after each change. The headset label shows index/total, stage, identity, mark count
and suppressed commands. The HUD and label remain enabled; F5/F6/F7 no longer
change world orientation, UI visibility or UI orientation.

Stage42's separate `[Effects] DisableMarkedFogLens=1` removes the saved
CS7728ef9458651dad producer regardless of hunting. Home leaves that configured
removal active. Set it to0 for a native baseline; it reloads within about a
second during gameplay. The matched RGB output is cleared instead of left
stale. This single mark does not identify every fog/lens-flare shader.

When a suspect changes the broken lighting, press Delete, then Home. Stay on the
same view for a few seconds so the restored native command can be captured. Tell
the assistant whether the artifact disappeared, froze, or changed, and whether
objects or other effects also disappeared. Hiding a compute producer can leave
stale intermediate output; this is evidence to inspect, not a lighting fix.

Coverage accumulates across frames and contexts. New creations and observed
bindings append without moving the selection; End rebuilds the ordering after
changing areas. All seven shader creation forms (including geometry stream
output), seven graphics draw forms and both compute dispatch forms are hooked.
Immediate/deferred contexts and newer deferred-context creation entry points are
covered. The mod's own OpenXR/UI transfer shaders are excluded, and the native
UI capture remains outside scene suppression.

Bytecode created after injection has a stable FNV-1a identity. Shaders created
before observation remain huntable with explicitly labeled per-process object
tokens. Those tokens do not imply recovered bytecode or identity across runs.
The list includes captured creations not used in the current view; some selections
can therefore have no visible effect.

Private output is stored in `shader-hunter/` beside the running DLL:

- `shader-hunter-catalog.json`: cumulative identities, stages and observed counts.
- `shader-hunter-status.json`: selection, coverage and suppression counters.
- `shader-hunter-marks.jsonl`: persistent user marks.
- Unique `mark-*` folders: mark metadata, captured DXBC/disassembly/reflection when
  available, suppressed/restored resource bindings and selected-stage constant
  buffers. Frame pose/projection data, context type and eye label accompany them.

Native constant buffers are copied at most once per eye/outside-eye category per
mark phase. Immediate-context readbacks use asynchronous queries and no-wait
polling; deferred-context snapshots contain resource metadata only. Constant-buffer
offset/range metadata is retained. Disk writes, disassembly and reflection run on
a background writer. Full texture readback happens only for requested eye captures.
File names are unique across launches, and old marks are retained.

These diagnostics are private and should stay outside public runtime/source
archives. The hunter is a diagnosis tool; it does not automatically modify shaders
or choose which lighting effect is broken.

Private Stage39 adds `[Debug] ShadowConsumerDiagnostics=1`: F4 also records
read-only PS/CS candidates with a bound 104-byte-stride structured buffer, up to
32 records over two native stereo pairs and two records per shader/pair. Keep
hunting off with Home for a native baseline. Candidate folders include available
DXBC/reflection, CB ranges, bindings, tracked/game eye positions and asynchronous
104/240-byte-stride buffer copies. A matching bound resource does not prove shader
use; reflection/disassembly must establish that. Eye remains explicitly unknown
without a valid native eye marker. This flag is off in the public configuration.

Configured removals are separate from hunting. `[Effects] DisableMarkedFogLens`
controls the previously accepted RGB effect; optional `DisableGroundFog=1`
removes graphics draws using the two Stage57 local-fog marks
(VS326cadd68150c1f1 / PSafdb0f0cc22f9ef0). Ground-fog removal defaults to0.
Both settings reload within about one second while running. Home restores the
hunter selection and keeps these configured choices. The ground-fog option can
also remove other local fog volumes sharing either marked shader. It leaves
native atmospheric fog, compute passes and the quad fog-sampling correction in
place. Status JSON includes separate ground-fog draw and identity-match counters.
