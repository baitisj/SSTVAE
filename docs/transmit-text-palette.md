# SSTVAE fork: right-click text palette for the Transmit composer

> **Status, 2026-09-19 -- Phases 0-6 are done and verified, bar one
> deferred item.** The work moved to Linux and the "Prerequisite /
> blocker" section below is history: read it as the reason for the
> move, not as a live problem. Verified on a full build (codec, Qt
> Widgets, Qt Multimedia and Hamlib all on -- note that the earlier
> `--no-codec` runs silently turned the GUI off with it, reporting 20
> passing tests where there are now 32):
>
> - **32/32 ctest**, including `overlay_render`, `overlay_editor`,
>   `text_palette` and `tx_panel`.
> - **`pytest --native`**: 392 passed, 9 skipped.
> - `check_includes.py` and `check_layering.py` clean.
>
> Every new assertion was mutation-tested. Two of them had no teeth
> until the fixture was fixed, and both are worth knowing: with three
> overlay items "down one layer" and "to the bottom" land in the same
> place, so a palette ignoring Shift entirely passed; and a size box
> pinned by `setMinimumWidth` passes an assertion on `sizeHint()` while
> still moving in the layout, because `QWidgetItem::sizeHint` bounds
> the hint by the widget's *maximum* size.
>
> **Nothing is outstanding.** Phase 6's wiki `Home` entry is **closed as
> not applicable to a fork** -- a fork cannot push to upstream's wiki and
> its own starts empty, so the entry would mean seeding a parallel wiki
> for one bullet. `CLAUDE.md`'s docs list does that job in the
> repository instead. The reasoning, and the condition under which the
> item returns, are in that phase.
>
> Pushed to `github.com/baitisj/SSTVAE`, branch
> `transmit-text-palette`; `origin` is left pointing at arodland and the
> fork is the `fork` remote, which the branch tracks.
>
> Two departures from this plan, both recorded where they land:
> `size` is shown in **frame pixels in the strip box as well as in the
> palette** (Phase 4 left the box on fractions and flagged the
> question; pixels was the call), and the font family is a nested
> `QMenu` rather than a `QComboBox` -- Phase 4's named fallback, taken
> up front because the combo's failure is style-dependent and so cannot
> be ruled out from one machine.

## Context

This directory is to become a fork of `github.com/arodland/SSTVAE` carrying UI
changes to the **Transmit** composer — the surface that adds and edits burned-in
overlay text on the picture about to be sent.

Today that surface is a static group box under the canvas
(`TransmitPanel::build_properties`, `native/gui/tx_panel.cpp:483`) offering
exactly six things: Text, Align, Size, Rotation, Colour, Remove. The supplied
mockup (`.colorfulvibe/attachments/mu8sbu7d-935x7y/1.html`) asks for far more —
bold/italic/underline, a font family, solid/linear/radial fill with a from- and
a to-colour, a stroke toggle with its own colour and width, wheel-scrolled size
and rotation with Shift as a coarse step, and layer ordering — presented as a
compact floating palette rather than a row of form fields.

**The agreed shape:** the palette is summoned by **right-clicking the selected
text item on the canvas**. It is a `QMenu` with three entries — Format, Style,
Layers — each opening a submenu that holds that mockup row's *live controls*,
not plain menu items. The existing "Selected item" box under the canvas stays
exactly as it is; the menu is a second, richer path to the same fields.

Two upstream invariants constrain everything below, and both survive by
construction here:

- **The editor previews `overlay::render()`'s own output**, so anything the
  palette can set must be a document field the renderer honours — never a Qt
  effect painted only in the preview.
- **The control strip's height is locked equal to the receive pane's**
  (`PaneContainer::equalise_strips`, guarded by `native/tests/test_tx_panel.cpp`).
  A popup costs no layout, so the strip does not move — but the guard must still
  pass after the wiring lands.

## Prerequisite / blocker

This machine has `git` but **no CMake, no Ninja, no Qt6, no compiler on PATH**
(checked: `which cmake qmake6` → nothing). Nothing in this plan can be built or
tested locally until that is fixed. Two ways forward, to settle before Phase 1:

1. Install CMake + Ninja + Qt 6 + MSVC (or MSYS2 toolchain) locally. Note that
   `tools/build_native.sh` is bash and resolves `.venv/bin/python`, which is
   `.venv/Scripts/python` on Windows — expect to invoke `cmake`/`ctest` directly
   rather than through the script.
2. Push each phase to the fork and let `.github/workflows/ci.yml` (which already
   builds `ubuntu-latest`, `macos-latest`, `windows-latest`) be the gate.

Every phase below ends with "build + ctest green"; that step is where this
blocker bites.

---

## Phase 0 — Land the fork

Working directory already holds `CLAUDE.md`, `.claude/` and `.colorfulvibe/`, so
a direct `git clone .` will not work.

1. `git clone https://github.com/arodland/SSTVAE` into a temp dir, then move its
   contents (including `.git/`) into `C:\Users\baitisj\Documents\sstvae`,
   keeping `.claude/` and `.colorfulvibe/`.
2. **CLAUDE.md merge, both kept.** Upstream's `CLAUDE.md` (2482 lines — commands,
   architecture, the native port, hard-won gotchas) becomes the file. Append the
   current fork-local `CLAUDE.md` verbatim as a clearly marked final section,
   e.g. `## Fork directives (agent workflow)`, so neither set of rules is lost.
   Save the pre-merge fork file to `docs/` only if you want the provenance; the
   merged file is what loads.
3. `.gitignore`: add `.colorfulvibe/` and `.claude/settings.local.json`.
4. `origin` stays pointed at upstream until you create the GitHub fork; do
   nothing that pushes.
5. **Baseline gate:** build and run the suite *before* any change, so a later
   failure is attributable. Record the pass count.

---

## Phase 1 — Document format v2 (4 files)

Files: `native/core/overlay/model.hpp`, `native/core/overlay/model.cpp`,
`sstvae/overlay/model.py`, `tests/test_native_overlay.py`.

Add to `overlay::TextItem`, every default chosen so a version-1 document renders
**exactly** as it does today:

| field | type | default | note |
|---|---|---|---|
| `bold` | `bool` | `false` | |
| `italic` | `bool` | `false` | |
| `underline` | `bool` | `false` | |
| `font_family` | `std::string` | `""` | family name or a generic keyword (`sans-serif`, `serif`, `monospace`, `cursive`). **`font` (a path) wins when both are set** — a template shipping its own face must keep it. |
| `fill_mode` | `std::string` | `"solid"` | `solid` \| `linear` \| `radial` |
| `color2` | `std::string` | `"#38bdf8"` | the to-colour; unread when solid |
| `fill_angle` | `double` | `45.0` | degrees, linear only |

**No new field for the stroke toggle.** `stroke_width` is already a fraction of
the glyph size and `0` already means "no stroke" (`draw_text` guards on
`stroke > 0.0`). The toggle reads and writes that. Do **not** change the
existing `stroke_width = 0.12` default — new text items keep the stroke they
have today; only the mockup's own default differs.

`model.cpp`:
- `Reader` has `get()` overloads for `std::string` and `double` only — add a
  `bool` overload, following the same note-and-keep-the-default shape.
- Extend `read_text`'s reads and its `report_unknown` list; extend the
  `to_json` text object with the same keys.
- **`DOC_VERSION = 2`.** `from_json` already throws on a version above what the
  build understands, so an older build refuses a v2 document outright rather
  than silently dropping a gradient.

`sstvae/overlay/model.py` — **mandatory, not optional.**
`tests/test_native_overlay.py` asserts `cpp.DOC_VERSION == DOC_VERSION` and
compares `json.loads(cpp.round_trip(...))` against Python's `to_dict()`, so
bumping only the C++ side turns that suite red. Mirror the same seven fields and
the same defaults on Python's `TextItem`, and bump its `DOC_VERSION` to 2.
`sstvae/overlay/render.py` is **not** extended — it is used only by the Python
tests, and defaults keep it faithful; say so in its module docstring.

Tests: extend `tests/test_native_overlay.py` with (a) a v2 document with every
new field set round-tripping unchanged, (b) a v1 document (no style keys)
loading with the documented defaults and **no** notes.

---

## Phase 2 — Renderer (2 files)

Files: `native/core/overlay/render.cpp`, `native/tests/test_overlay_render.cpp`.

- `font_for` (`render.cpp:79`): set bold/italic from the item; resolve
  `font_family` when `font`'s path yields no family. Keep `setPixelSize` — the
  comment there explains why point sizes are wrong for this document.
- **Underline needs explicit geometry.** `QPainterPath::addText` adds glyph
  outlines only; a font's underline is a decoration Qt draws separately, so
  `setUnderline(true)` alone renders nothing through this path. In `text_path`,
  append a rect per line at `QFontMetricsF::underlinePos()` with
  `lineWidth()` thickness, into the *same* path — so stroke and fill both cover
  it, as they must for it to look like part of the text.
- `draw_text` (`render.cpp:165`): replace the final
  `painter.fillPath(path, color_of(item.color, Qt::white))` with a brush chosen
  by `fill_mode`. `color` is the from-stop in every mode, which is what keeps v1
  documents identical. Linear: a `QLinearGradient` across the layout bbox at
  `fill_angle`. Radial: a `QRadialGradient` centred on the bbox, radius half its
  diagonal. Build the gradient in the same (already translated/rotated) painter
  space the path is in, so the fill rotates with the text rather than sliding
  across it.
- `item_bbox` (`render.cpp:341`): include the underline's descent in the
  returned box, or the selection handle clips it.

Tests in `test_overlay_render.cpp`:
- A default `TextItem` renders **pixel-identical** to one with `fill_mode`,
  `color2`, `fill_angle`, `font_family` and the three flags left at their
  defaults — the v1-compatibility claim, stated as an assertion.
- Bold changes ink coverage; underline adds ink below the baseline that the
  same item without it does not have.
- Linear fill: sample two points near opposite edges of the bbox and require
  they differ, and that solid at the same colours does not differ.
- Radial differs from linear for the same stop pair.

---

## Phase 3 — Editor: layering + the right-click hook (3 files)

Files: `native/gui/overlay_editor.hpp`, `native/gui/overlay_editor.cpp`,
`native/tests/test_overlay_editor.cpp`.

`Doc::items` is already "drawn back to front" and the editor already owns
selection by index, so layering is a reorder plus keeping `selected_` on the
item that moved:

```cpp
void raise_selected();      // swap with the item above; no-op at the top
void lower_selected();
void raise_to_top();        // Shift
void lower_to_bottom();
```

Each mirrors `remove_selected` (`overlay_editor.cpp:152`): mutate `doc_`, keep
`selected_` pointing at the same item, `emit documentChanged()`. They must also
emit `selectionChanged(selected_item())` — the item's *address* inside the
vector changes, and `TransmitPanel` caches nothing but does dereference it.

Right-click:

```cpp
void contextMenuEvent(QContextMenuEvent* event) override;
signals:
    void contextMenuRequested(overlay::Item* item, const QPoint& global_pos);
```

Hit-test the cursor exactly as `mousePressEvent` does (`overlay_editor.cpp:322`
— grip first, then `hit_test`); if it lands on an item, `select()` it first so
right-clicking an *unselected* item selects and then opens, then emit. A
right-click on empty canvas emits nothing and lets the base class run.
`mousePressEvent` keeps its `event->button() != Qt::LeftButton` early return, so
the right button never starts a drag.

Tests: each of the four layer moves reorders `doc().items` and leaves the same
item selected; top/bottom are no-ops at the ends; a synthesised right-click over
an item selects it and emits `contextMenuRequested`, and one over empty canvas
does not.

---

## Phase 4 — The palette menu (5 files)

Files: **new** `native/gui/text_palette.hpp`, **new**
`native/gui/text_palette.cpp`, `native/CMakeLists.txt` (add to the `gui/` source
list at line ~437), `native/tests/CMakeLists.txt`, **new**
`native/tests/test_text_palette.cpp`.

A file of its own rather than more of `tx_panel.cpp`, which is already 1284
lines.

```cpp
class TextPaletteMenu : public QMenu {
    Q_OBJECT
public:
    TextPaletteMenu(OverlayEditor* editor, QWidget* parent = nullptr);
    void popup_for(overlay::Item* item, const QPoint& global_pos);
signals:
    void itemEdited();   // so the strip box can re-read the same fields
};
```

Three submenus, each holding a single `QWidgetAction` whose widget is one
horizontal row of real controls — the mockup's rows, one for one:

- **Format** — Bold / Italic / Underline toggle buttons; font-family combo
  (Sans-Serif, Serif, Monospace, Cursive, mapped to `font_family`); size field.
- **Style** — Clear style; the split fill control (from-colour, to-colour, and a
  centre button cycling SOLID → LINEAR → RADIAL with the mode shown beside it);
  stroke on/off toggle, stroke colour, stroke width; the rotation field.
- **Layers** — up / down, calling the Phase 3 API; Shift turns them into
  to-top / to-bottom, with the label changing to say so.

Behaviour to get right, all of it load-bearing:

- **Loading guard.** Copy `TransmitPanel::loading_properties_`
  (`tx_panel.cpp:986`) exactly: `popup_for` fills the widgets from the item with
  the guard set, so their change signals do not write straight back.
- **Every edit** mutates the `overlay::Item*` in place, calls
  `editor_->refresh_item()`, and emits `itemEdited()`.
- **Wheel steps.** Size: ±1 px / ±10 px with Shift. Rotation: ±1° / snap to 15°
  with Shift, matching the mockup's `rotInput` handler. *Assumption worth
  flagging:* the menu shows **size in pixels of the 480-line transmit canvas**
  (`size × overlay::CANVAS_H`), because that is the unit the mockup is built
  around; the strip box keeps showing the fraction, whose tooltip already
  explains why it is one. Say the word and both become fractions.
- **A `QMenu` eats wheel events** (it uses them to scroll a tall menu). Install
  an event filter on the size and rotation fields that handles and *accepts*
  the wheel, so the submenu neither scrolls nor closes.
- **Risk: a `QComboBox` popup inside a `QWidgetAction` can dismiss the menu** on
  some styles. If the font-family combo does, fall back to a `QPushButton` with
  its own child `QMenu` of families — same result, no nested popup.
- **Shift tracking** for the Layers row: watch `QApplication::keyboardModifiers`
  on a short timer while the submenu is open, or filter key events on it —
  do not try to read modifiers only at click time, because the label has to
  change *before* the click to be honest about what it will do.
- **Clear style** resets `color`, `color2`, `fill_mode`, `fill_angle`,
  `stroke_color`, `stroke_width`, `bold`, `italic`, `underline` and
  `font_family` to `TextItem` defaults, and leaves `text`, `x`, `y`, `size`,
  `rotation`, `align`, `anchor` and `line_spacing` alone.
- Reuse `TransmitPanel::set_color_swatch`'s approach for the colour wells
  (`tx_panel.cpp:948`) — it already solves the HiDPI pixmap and the
  don't-rebuild-an-identical-icon guard. Lift it into `gui/style.hpp` rather
  than copying it.
- The menu is for **text items only**: an `ImageItem` right-click should open
  nothing (or a Layers-only menu — pick one; Layers-only is the more useful and
  costs a conditional).

Optional and cheap: a `--text-palette` flag in `native/apps/sstvae_gui_shot.cpp`
that pops the menu and grabs it, since a popup does not appear in a normal
widget render. Worth it — "is this laid out well" has no test oracle, which is
exactly why that tool exists.

Tests in `test_text_palette.cpp`: wheel with and without Shift produces the
documented steps and clamps at the range ends; the fill-mode button cycles
solid → linear → radial → solid and writes `fill_mode`; the stroke toggle sets
`stroke_width` to 0 and restores the previous value; clear-style resets exactly
the listed fields and no others (assert the untouched ones explicitly).

---

## Phase 5 — Wire it into the panel (3 files)

Files: `native/gui/tx_panel.hpp`, `native/gui/tx_panel.cpp`,
`native/tests/test_tx_panel.cpp`.

- Construct `TextPaletteMenu` in `build_ui` (`tx_panel.cpp:322`) as a child of
  the panel; connect `editor_->contextMenuRequested` → `popup_for`.
- Connect `palette_->itemEdited` → `on_selection(editor_->selected_item())`, so
  the strip box shows what the menu just changed. **Not** `documentChanged` →
  `on_selection`: that signal fires on every mouse move of a drag, which is the
  path `EDIT_DEBOUNCE_MS` and `set_color_swatch`'s guard both exist to protect.
- `on_selection` (`tx_panel.cpp:993`) needs no new fields, but must keep working
  when the menu is the thing that changed them.

Tests: the existing strip-height invariant still passes with the menu
constructed (this is the guard the whole Phase-4 popup design is chosen to
satisfy); an edit made through the menu is visible in the strip box's widgets.

---

## Phase 6 — Docs (2 files, plus one closed as not applicable)

- `CLAUDE.md` (merged, upstream section): note the v2 document and the palette
  under "The native port".
- `docs/gui-review.md`: it already carries before/after transmit-tab shots; add
  the palette, with a fresh `sstvae-gui-shot --transmit` image.
- ~~**The wiki's `Home` page.**~~ **Closed as not applicable to a fork**
  (2026-09-19, baitisj), after the fork was pushed to
  `github.com/baitisj/SSTVAE` and the question could actually be asked.

  The rule it came from is real: upstream's CLAUDE.md requires every
  `docs/*.md` to be listed under "Also in the repository" on the wiki's `Home`.
  What the plan did not know is what honouring it from a fork would cost.
  Pushing to `arodland/SSTVAE.wiki.git` needs write access to **arodland's**
  repository, which a fork does not grant — so the entry would have to go to
  the fork's own wiki, and **a fork's wiki starts empty rather than inheriting
  upstream's eight pages**, and has to be enabled in Settings before it exists
  at all. So "add one bullet to Home" is really "stand up a parallel wiki and
  seed a Home page", which is a different and much larger thing, and one that
  then has *two* Home pages drifting apart with nothing in CI watching either.

  What the entry was for — "this doc exists, and here is what it covers" — is
  already served by `CLAUDE.md`'s own docs list, which lives in the repository,
  travels with the branch, and is read by everyone who works here. That is
  strictly better than a wiki page nothing checks.

  **If this branch is ever contributed upstream, the item comes back**, and
  then it is the one-line edit it was always meant to be: an
  `arodland/SSTVAE/blob/master/docs/…` bullet, added by someone with write
  access, in the same sitting as the merge.

---

## Verification

Per phase, from the repo root:

```bash
tools/build_native.sh --test        # cmake + ctest + pytest --native
ctest --test-dir native/build -R 'overlay_render|overlay_editor|tx_panel|text_palette' -V
pytest tests/test_native_overlay.py tests/test_overlay.py -v
python tools/check_layering.py      # core/overlay/ must not gain QtWidgets
```

`text_palette` lives in `native/gui/`, which is the only place QtWidgets is
allowed — `check_layering.py` enforces that, and Phase 2's renderer work must
stay QtGui-only.

End to end, by hand:

1. `native/build/sstvae-gui`, Transmit tab, choose a picture, **Add text**.
2. Right-click the text: the three submenus open and each control changes the
   *preview*, which is the renderer's own output — so what is seen is what would
   go on the air.
3. Scroll over size and rotation, with and without Shift; the submenu must not
   scroll or close.
4. Layers: with two text items overlapping, up/down reorders and Shift jumps to
   top/bottom.
5. Confirm the two panes' pictures are still the same size (the shot tool prints
   `strips: receive N transmit N` and whether they match).
6. Save an overlay, reload it, and confirm every style field survives; open a
   pre-change (v1) document and confirm it loads clean with no notes.
