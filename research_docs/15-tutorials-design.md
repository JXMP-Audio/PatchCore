# 15 — Interactive tutorials

Status: design approved in chat (2026-11-02), spec waiting for the user's review. Follows
[10 — design guide](10-jxmp-design-guide.md) for the look and [04](04-sided-modules-patchbay-design.md) for
front/back sides. Work starts after the `standalone-audio` / `jxmp-set3` build (user rule: no builds or tests
until the user says "build").

## Goal

Someone who has never patched a modular synth opens **Help → Tutorials**, picks a lesson and is walked through
our JXMP modules step by step. The app watches what they do and moves on by itself. Success: a beginner finishes
lesson 1 and hears a sound from a patch they made and understand.

## Decisions (questions 2026-11-02)

| Topic | Decision |
|---|---|
| Interactivity | The app watches each task (cable, knob, module, side, sound) and moves on when it is done |
| Lessons in the first version | 1 First sound, 2 Filter and envelope, 3 Modulation |
| Who adds modules | Lesson 1: the user (learns the quick search); lessons 2–3: modules are already in the start patch |
| Language | English only (like the rest of the app) |
| Approach | Lessons are data files (`lesson.json` + `start.vcv`), one generic tutorial engine in the core |
| Plugin | Also in the VST3 plugins (Host Audio is the plugin's output there, lessons need no change) |
| Later lessons | Sequences and melody, drums and rhythm, effects and mix: new data files, no new code |

## What the user sees

**Starting.** Help menu → **Tutorials ▸** lists the lessons with a one-line description and a check mark when
done. On the very first start of the app a small notice offers "New to modular? Start the first lesson"
(Start / Not now); it is shown once.

**Opening a lesson.** Asks to save unsaved work (the same dialog as File → New), then opens the lesson's
`start.vcv` as a new **untitled** patch: Ctrl+S never overwrites the lesson, and the result can be saved as the
user's own patch. The output level of Host Audio is set low in every start patch.

**The step panel.** A floating box at the bottom right of the rack, draggable, styled like the dark menus:
lesson title, "Step 3 / 12", 2–4 sentences, buttons **Back**, **Next**, **Exit**.
- Explanation steps: **Next** is active.
- Task steps: instead of Next a line "Waiting for: …"; when the task is done the step is ticked green and the
  next one comes after ~0.8 s.
- Every task step has a **Show me** link that does the task for the user (adds the cable, sets the knob, turns
  the rack, adds the module), so nobody gets stuck.

**Highlight.** The target jack, knob or module gets a pulsing orange frame drawn above the rack and cables. When
the target is off screen the rack scrolls to it once at the start of the step. When the target is on the other
side, the step first says "Press Tab to turn the rack around" and waits for the turn.

**Ending.** **Exit** closes the panel at any time and leaves the patch open. After the last step a summary
("You learned: …") offers the next lesson. Finished lessons are stored in the PatchCore settings.

## Lesson files

```
plugins/JXMP/res/tutorials/index.json              order, id, title, one-line description
plugins/JXMP/res/tutorials/01-first-sound/lesson.json
plugins/JXMP/res/tutorials/01-first-sound/start.vcv
plugins/JXMP/res/tutorials/02-filter-envelope/…
plugins/JXMP/res/tutorials/03-modulation/…
```

The lessons live in the JXMP plugin's resources (they teach JXMP modules, and `src/Rack/res` is the Rack
submodule); the core finds them through `plugin::getPlugin("JXMP")`. `plugins/Makefile` bundles the folder like
other plugin resource folders (`RESOURCE_FILES += JXMP/res/tutorials`).

### References

- **Module**: `"module": "<slug>"` (e.g. `"VCO"`, `"VCA"`, `"HostAudio2"`), the first module of that model in
  the rack (lowest module id). No lesson of the first version uses two modules of one kind; tags for that case are
  left for later.
- **Port / param**: by the name the module configures (`configInput`/`configOutput`/`configParam`), e.g.
  `"output": "Saw"`, `"input": "In 1"`, `"param": "Cutoff frequency"`. Ports without a name are referenced by
  number: `"input": "#1"` (1-based). Names are not unique on a Patchbay (front and back are both "Ch 1"), so a
  port reference may add `"side": "front"` or `"back"`.

### Step

```json
{ "text": "Drag a cable from SAW on the oscillator to IN 1 on the amplifier.",
  "highlight": [{"module": "VCO", "output": "Saw"}, {"module": "VCA", "input": "In 1"}],
  "side": "back",
  "done": {"cable": {"from": {"module": "VCO", "output": "Saw"}, "to": {"module": "VCA", "input": "In 1"}}},
  "showMe": "auto" }
```

- `text` (required), at most 4 sentences.
- `highlight` (optional): references to frame.
- `side` (optional): the side the step needs; if the rack shows the other side, the panel asks for Tab first.
- `done` (required): the completion condition.
- `showMe` (optional): `"auto"` does what `done` says where possible; `"none"` hides the link.

### Conditions

| Condition | True when |
|---|---|
| `{"next": true}` | the user clicks Next (explanation step) |
| `{"module": ref}` | a module of that slug is in the rack |
| `{"cable": {"from": ref, "to": ref}}` | a cable connects the two ports; `to` or `from` may be left out ("anywhere") |
| `{"param": ref, "min": a, "max": b}` | the param value (in the displayed unit) is in [a, b] |
| `{"param": ref, "moved": d}` | the param moved by at least `d` (fraction of its range) since the step began |
| `{"side": "front"\|"back"}` | the rack shows that side |
| `{"signal": ref, "above": v}` | the peak voltage on the port over the last 0.3 s is above `v` volts |
| `{"wait": s}` | `s` seconds passed since the step began |
| `{"all": [...]}`, `{"any": [...]}` | combinations |

A condition (except `next` and `wait`) must hold for **0.3 s** without a break before the step counts as done, so
sweeping a knob through the range does not skip a step.

**Show me "auto"** per condition: `module` adds the module next to the last module of the lesson; `cable` adds
the cable; `param` sets the middle of [min, max] or moves the knob by `d`; `side` turns the rack; `all` does each
part; `any` does the first part; `signal` and `wait` have nothing to do (the link is hidden). Every Show me
action goes through Rack's history, so Ctrl+Z undoes it.

**Back** shows the previous step's text again; it does not undo the rack. On Next, a step whose condition already
holds is skipped.

## Architecture

### `src/patchcore/TutorialLogic.hpp` (pure logic, no Rack)

- `Lesson parseLesson(const std::string& json, std::string* error)`: parse and validate (required fields,
  known condition names, references well-formed). Uses jansson like the rest of Rack (the tests link the
  system `libjansson`).
- `struct RackState`: modules (slug, id), cables (from/to as module id + port name + side), params (module
  id + name → value, min, max), port peaks (module id + port → volts), the shown side, time.
- `struct Resolved`: the references resolved against a `RackState` (module id, or "missing").
- `bool holds(const Condition&, const RackState&, const StepContext&)`, where `StepContext` keeps the param
  values at the step start and the step start time.
- `struct Runner`: current step, the 0.3 s hold timer, Next / Back / skip, `update(const RackState&, double t)`
  returns what changed (step done, lesson done).

### `src/patchcore/Tutorial.cpp` (Rack side)

- Builds a `RackState` every frame from `APP->engine` and the scene: module slugs, cables,
  `paramQuantities` (displayed values), `portInfos` names, `PortWidget` sides through `portSide()`, the shown
  side through `isBackView()`. Port peaks: the max |voltage| of each referenced port, read in the UI step
  (`Port::getVoltage`), kept over 0.3 s; no change to module DSP.
- Resolves references to widgets for the highlight and Show me.
- `TutorialPanel`: a widget above the rack (like the Mixer overlay, but **not modal**, the rack stays usable),
  draggable, the buttons, the "Waiting for" line, the green tick, the summary.
- `TutorialHighlight`: pulsing orange frame (2 px, alpha 0.4–1.0 at ~1.5 Hz) around each highlighted widget, in
  rack coordinates, drawn after the rack. Scrolls the rack once per step when a target is off screen.
- Show me actions: add module (`createModule` like the quick search), add cable (`CableWidget` + history
  action), set param (`ParamQuantity::setValue` + history action), turn the rack (`setViewSide`).
- `openLesson(id)`: the File → New save prompt, then load `start.vcv` as an untitled patch (path cleared,
  history reset), then show the panel at step 1.

### Hooks

- `src/override/MenuBar.cpp`: Help menu → **Tutorials ▸** submenu from `index.json`, check marks from the
  settings; a lesson whose `lesson.json` does not parse is shown disabled.
- `src/patchcore/PatchCore.cpp` settings: `tutorialsDone` (array of lesson ids), `tutorialOffered` (bool).
- First start: when `tutorialOffered` is false, the notice strip (`createNoticeStrip`, the audio notice strip)
  shows "New to modular? Start the first lesson" with Start / Not now; either sets `tutorialOffered`.

### Errors

- Broken `lesson.json`: the lesson is disabled in the menu, the error goes to the log; the app keeps running.
- A referenced module was deleted: the panel says "The VCO is gone – press Show me to put it back"; Show me adds
  it again.
- A reference to a port or param that does not exist (e.g. after a rename): the step shows without that
  highlight and a warning goes to the log. The content test (below) is there to prevent it.

## Lesson content

Port and param names below are the configured names in `plugins/JXMP/src`. Every lesson is 10–12 steps and
about 8–10 minutes.

### 1 — First sound

Start patch: empty rack except Host Audio (level low) and a Patchbay4 whose front Ch 1 / Ch 2 are wired to Host
Audio inputs #1 / #2. Front side shown.

1. What a modular synth is: modules make or change voltages, cables carry them. (`next`)
2. Add an oscillator: right-click the rack or press Enter, type "VCO". (`module VCO`)
3. Add an amplifier the same way, type "VCA". (`module VCA`)
4. JXMP jacks are on the back: press Tab. (`side back`)
5. Cable VCO **Saw** → VCA **In 1**. (`cable`)
6. Cable VCA **Out 1** → Patchbay **Ch 1** (back): the way out to the speaker. (`all`: `cable` +
   `signal` HostAudio2 `#1` above 0.05 V)
7. Press Tab, turn VCA **Channel 1 level**: the amplifier sets the volume. (`side front`, then `param moved 0.2`)
8. Turn VCO **Frequency**: the pitch. (`param moved 0.1`)
9. Move the cable from **Saw** to **Sqr**, then to **Sin**: the wave shape is the timbre. (`cable` from Sqr,
   then `cable` from Sin — two steps)
10. Add a scope ("Scope") and cable VCO **Sin** to Scope **In 1**: now you see the wave. (`all`: `module Scope` +
    `cable`)
11. Summary: oscillator = source, VCA = volume, cable = signal, voltage = sound. (`next`)

### 2 — Filter and envelope

Start patch: VCO, VCF, ADSR, VCA, Clock, Scope, Patchbay4 + Host Audio wired as in lesson 1; no JXMP cables.
Back side shown.

1. Subtractive synthesis: start with a bright wave and take away. (`next`)
2. VCO **Saw** → VCF **In**, VCF **Out** → VCA **In 1**, VCA **Out 1** → Patchbay **Ch 1** (back). (`all` of
   three `cable`s + `signal`)
3. Turn VCF **Cutoff frequency** down and up: dark and bright. (`param moved 0.3`)
4. Turn up **Resonance**: the filter sings at its edge. (`param` with `min` at half of its range, in its displayed unit)
5. Clock **/4** → ADSR **Gate**: a gate is "a key held down". (`cable`)
6. ADSR **Env** → VCA **CV 1**: the drone becomes notes. (`cable`)
7. Turn ADSR **Attack** and **Release**: short and long notes. (`all` of two `param moved 0.2`)
8. Turn **Decay** and **Sustain**, and cable ADSR **Env** → Scope **In 1** to see the shape. (`all`)
9. A second cable ADSR **Env** → VCF **Env**, then turn up VCF **Envelope amount**: every note opens the filter.
   (`all`: `cable` + `param moved 0.2`)
10. Summary: VCO → VCF → VCA; the envelope shapes the volume and the colour. (`next`)

### 3 — Modulation

Start patch: the finished patch of lesson 2, plus LFO and Attenuverter. Back side shown.

1. CV is a voltage that turns a knob for you. (`next`)
2. LFO **Tri** → VCF **Freq CV**, then turn up VCF **Cutoff CV**: the filter breathes. (`all`)
3. Turn LFO **Rate**: slow and fast. (`param moved 0.2`)
4. LFO **Sin** → VCO **FM**, a little **Frequency modulation**: vibrato. (`all`: `cable` + `param` with a small `min`)
5. Through the Attenuverter: LFO **Sin** → Attenuverter **In 1**, **Out 1** → VCO **FM**; **Row 1 gain** makes it
   smaller or turns it upside down. (`all`)
6. Move the cable at Attenuverter **In 1** from LFO **Sin** to LFO **Sqr**, **Row 1 gain** up: a trill between two notes. (`all`)
7. LFO **Sin** → Scope **In 1**, VCA **Out 1** → Scope **In 2**: the slow wave and the sound side by side.
   (`all`)
8. Summary: audio and CV are the same voltages at different speeds; any output can drive any input. (`next`)

## Testing

Pure-logic tests in `src/patchcore/tests/test_tutorial.cpp`, run by `src/patchcore/tests/run.sh`:

- Parsing: valid lesson; rejects a missing `done`, an unknown condition, a bad reference.
- Each condition against hand-made `RackState`s (true and false cases), including `#n` port numbers and
  the Patchbay `side`.
- The 0.3 s hold; `moved` measured from the step start; Next / Back / skip of a step that already holds.
- **Content check**: every `lesson.json` in `plugins/JXMP/res/tutorials` parses; every module reference exists in
  `plugins/JXMP/plugin.json` (or is `HostAudio2`); every port/param name exists in the module's source
  (`configInput` / `configOutput` / `configParam` / `configSwitch` names; `string::f("In %d", …)` patterns match
  any number); every `start.vcv` loads as JSON and holds the modules the lesson expects at start. A failure names
  the lesson, step and reference ("lesson 2 step 6: VCA has no input 'CV 1'").

By hand with the user (workspace 3), after the build: each lesson once done normally and once only through Show
me, in the standalone and in the VST3 plugin in a host.

## Out of scope (first version)

- Lessons 4–6 (sequences, drums, effects and mix): later, data only.
- Translations; voice-over; videos.
- Undoing the rack with Back.
- Tutorials for third-party modules.
