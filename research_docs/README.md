# PatchCore research docs

A Reason-inspired, realistic frontend for Cardinal (a VCV Rack fork).

| File | What |
|---|---|
| [01-reason-ui-research.md](01-reason-ui-research.md) | What Reason's product pages show, plus its signature UI concepts mapped to Cardinal |
| [02-ui-plan.md](02-ui-plan.md) | Design principles, layout, phased feature set, code touch-points, risks |
| [03-work-items.md](03-work-items.md) | Backlog (research → P0 foundations → P1 rack realism → P2 chrome → P3 back panel → P4 workflow) |
| [04-sided-modules-patchbay-design.md](04-sided-modules-patchbay-design.md) | Design: basic modules keep front cables, JXMP modules patch on the back, Patchbay 4/8/16 bridges the two |
| [05-plan-sided-modules-patchbay.md](05-plan-sided-modules-patchbay.md) | Implementation plan for 04, task by task |
| [06-jxmp-voice-modules-design.md](06-jxmp-voice-modules-design.md) | Design: JXMP rules (naming, serial plate) and the voice set VCO/VCF/VCA/ADSR |
| [07-realistic-cables-design.md](07-realistic-cables-design.md) | **Cancelled.** Design: realistic cables (kept for reference) |
| [08-plan-realistic-cables.md](08-plan-realistic-cables.md) | **Cancelled.** Implementation plan for 07 |
| [mockups/index.html](mockups/index.html) | Interactive HTML mockup. Open it in a browser |
| [mockups/screens/](mockups/screens/) | `front.png`, `back.png`, `play.png` captures at 1600×940 |

## Using the mockup

Open `mockups/index.html` in a desktop browser.

- `Tab` flips between the front and back of the rack. The back shows the jacks, serial
  stickers and power headers, with outputs in amber.
- `B` toggles the docked browser. `F` folds the row under the mouse. `Space` starts
  and stops the transport.
- Sockets and cables exist only on the back, as in Reason. The front panel shows
  only knobs, sliders, buttons, LEDs and displays. On the back, drag from one jack to another
  to patch, and double-click a jack to remove its cables. On the front, drag a knob
  vertically to turn it.
- To add a module, drag it from the browser onto a rack row, or use **Add to rack**.
  If there is no room, the row is outlined in red.
- Deep links: `index.html#back`, `#nobrowser` and `#play` can be combined.

All module names and brands in the mockup are made up and serve only as placeholders.
