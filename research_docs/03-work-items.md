# 03 — Work items (backlog)

Legend: **P** = phase · **S/M/L** = rough size · ☐ open · ☑ done

## Research & design
- ☑ R-1 Review the Reason product pages and capture UI concepts → `01-reason-ui-research.md`
- ☑ R-2 Draft the UI plan → `02-ui-plan.md`
- ☑ R-3 HTML mockups: front rack, back rack, browser + transport → `mockups/`
- ☐ R-4 Once `src/Rack` has finished cloning, confirm the file names in the plan's technical table (S)
- ☐ R-5 Take screenshots of the current Cardinal UI as a baseline for comparison (S)
- ☐ R-6 Decide the final color tokens and materials; export them as `theme-tokens.json` (S)
- ☐ R-7 Review the mockups with the user and pick the direction for open questions (fold UX, container UX) (S)

## P0 — Foundations
- ☐ F-1 `ThemeTokens` struct plus a settings key `patchcore.theme` (Classic | Dark) (M)
- ☐ F-2 Asset folder `src/Cardinal/res/patchcore/` with rail, screw, ear and case SVGs (M)
- ☐ F-3 Framebuffer-cached decoration layer helper (S)
- ☐ F-4 A per-frame timing overlay, used to measure regressions (S)

## P1 — Rack realism
- ☐ K-1 Rack frame: 19" ears and side posts drawn around the rack area (M)
- ☐ K-2 Rails above and below each 3U row, with screw holes every HP (M)
- ☐ K-3 Recess / empty-slot rendering (dark cavity with depth gradient) (S)
- ☐ K-4 Module mount screws plus a heuristic to detect screws already in the panel (M)
- ☐ K-5 Contact shadow and edge ambient occlusion per module (S)
- ☐ K-6 Optional panel sheen / vignette (setting 0–100%) (S)
- ☐ K-7 Cables 2.0: tube shading, plug heads, sag and wobble physics (L)
- ☐ K-8 Cable color by port type (optional) (S)
- ☐ K-9 New selection outline and lift shadow (S)

## P2 — Workspace chrome
- ☐ W-1 TopBar widget that replaces MenuBar (keeps all existing menu items) (M)
- ☐ W-2 LCD capsule widget (patch name, tempo, position) (S)
- ☐ W-3 TransportBar: play/stop/rec, BPM, signature, position, DSP and level meters (M)
- ☐ W-4 Docked Browser panel: search, locations, tags, list with thumbnails, preview (L)
- ☐ W-5 Drag from the browser into the rack, with an insertion indicator (M)
- ☐ W-6 Navigator minimap with a draggable viewport (M)
- ☐ W-7 Shortcut map (`Tab`, `B`, `F`, `Space`, `N`) and its documentation (S)

## P3 — Back panel
- ☑ B-1 Front/back state on the Scene, plus the flip animation (M)
- ☑ B-2 Generated back panel per module (plate, name, jacks, labels from PortInfo) (L)
- ☑ B-3 ~~Hide all cables on the front~~ replaced by S-1: each cable lives on the side of its sockets
- ☑ B-3b ~~Hide every `PortWidget` on the front~~ replaced by S-1: only back sockets of JXMP modules are hidden and capped
- ☑ B-4 Patching interactions on the back panel (M)

## Sided modules ([04](04-sided-modules-patchbay-design.md), plan [05](05-plan-sided-modules-patchbay.md))
- ☑ S-1 `SidedModuleWidget`: basic modules keep front sockets, JXMP modules patch on the back (M)
- ☑ S-2 JXMP plugin with Patchbay 4/8/16 and smart jacks (L)
- ☐ S-3 Serial plate on the back of JXMP modules ([06](06-jxmp-voice-modules-design.md)) (S)

## Cables ([07](07-realistic-cables-design.md)) — cancelled, original cables stay
- ☐ C-1 Rope simulation with tests (M)
- ☐ C-2 3.5 mm and 6.3 mm plugs, shadow, plug-in animation (M)
- ☐ C-3 Cable types Standard / LED / Braided / Coiled, defaults and right-click menu, saved in the patch (M)
- ☐ C-4 Stackable plugs and towers (L)
- ☐ C-5 6.3 mm sockets on JXMP back panels (S)

## JXMP voice ([06](06-jxmp-voice-modules-design.md))
- ☐ V-1 Common base: panel, widget, serial (M)
- ☐ V-2 JX-AMP (VCA) (S) · ☐ V-3 JX-ENV (ADSR) (M) · ☐ V-4 JX-OSC (VCO) (L) · ☐ V-5 JX-FLT (VCF) (M)

## P4 — Workflow
- ☐ X-1 Fold a row or module to a 1U strip, saved in patch metadata (M)
- ☐ X-2 Container / "combinator" group with 4 macros (L)
- ☐ X-3 Auto-route a newly added module (M)
- ☐ X-4 Inspector tool window (M)
- ☐ X-5 (stretch) Mixer overview (L)
