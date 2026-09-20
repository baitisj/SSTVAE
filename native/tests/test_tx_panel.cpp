// The transmit pane's control strip, which must not change height.
//
// **This is the guard for a bug that broke the other pane.** The
// "Selected item" row used to hide itself when nothing was selected,
// while the comment where it is built said the opposite in bold. Two
// things followed, and the second is why this file exists:
//
//   1. The row is ~90 px, so the canvas jumped under the pointer on
//      every select and deselect -- on a composing surface, where the
//      thing being clicked is the thing that moves.
//
//   2. `PaneContainer::equalise_strips` -- the entire mechanism that
//      makes the received image and the composed image the same size --
//      runs only from `set_control_strips` and from a resize. Nothing
//      re-runs it when a strip's *content* changes height. So from the
//      first click on the canvas, both strips held minimums computed
//      for a layout that no longer existed and the two images silently
//      stopped matching, until something happened to resize the window.
//
// `test_pane_container.cpp` cannot catch this: it drives the container
// with stand-in panes whose strips are static, which is right for what
// it tests and blind to what this tests. The check has to be on the
// real panel.
//
// Deliberately a height *invariant* rather than a "the box is visible"
// assertion: what the layout cares about is the number, and hiding the
// row by some other means later would break the panes the same way.

#include <QApplication>
#include <QContextMenuEvent>
#include <QLabel>
#include <QLayout>
#include <QPoint>
#include <QPushButton>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

#include <cmath>
#include <string>
#include <variant>

#include "app_state.hpp"
#include "check.hpp"
#include "flow_layout.hpp"
#include "overlay/model.hpp"
#include "overlay/render.hpp"
#include "overlay_editor.hpp"
#include "text_palette.hpp"
#include "tx_panel.hpp"

using namespace sstvae;
using namespace sstvae::gui;

namespace {

// The strip's height at the width it will actually have.
//
// Not `sizeHint()`: the rows inside wrap, so the height is a function
// of the width, and a hint taken at an unconstrained width reports one
// line -- the same trap `PaneContainer::strip_height_for_width` records.
int strip_height(QWidget* strip) {
    QLayout* layout = strip->layout();
    if (layout != nullptr && layout->hasHeightForWidth() && strip->width() > 0) {
        return layout->minimumHeightForWidth(strip->width());
    }
    return strip->sizeHint().height();
}

void test_the_strip_height_survives_a_selection() {
    // `AppState`'s constructor loads settings and opens the log; it
    // does not fetch a model, and `TransmitPanel` with no codec simply
    // does not arm the optimizer. So this needs no network and no
    // radio.
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    panel->setGeometry(0, 0, 900, 700);
    host.show();
    QCoreApplication::processEvents();

    QWidget* strip = panel->control_strip();
    const int idle = strip_height(strip);
    check::is_true(idle > 0, "the strip has a height to begin with");

    // Adding a text item selects it, which is the transition that used
    // to reveal the properties row.
    auto* editor = panel->findChild<OverlayEditor*>();
    check::is_true(editor != nullptr, "the panel has an overlay editor");
    editor->add_text(std::string("KD8XYZ"));
    QCoreApplication::processEvents();
    check::equal(strip_height(strip), idle,
                 "the strip is the same height with an item selected");

    // And back to nothing selected, which is the transition that used
    // to hide the row again. Through `remove_selected`, which is a real
    // gesture (the Remove button, or Delete) and reaches the same state
    // as clicking empty canvas -- `select` itself is private, and this
    // test has no business reaching past the panel's own surface.
    editor->remove_selected();
    QCoreApplication::processEvents();
    check::equal(strip_height(strip), idle,
                 "the strip is the same height with nothing selected");
}

// The level caption, slider and readout are one item of the send bar.
//
// The send bar is a `FlowLayout`, which wraps *between* items, so three
// separate items could put the slider on one line and the number it is
// showing on the next -- or strand "Level:" above the thing it names.
// None of the three means anything alone: the slider has no scale
// printed on it, so the readout is the only way to know where it is
// set.
//
// **Measured before writing this: no reachable width actually splits
// them today.** Swept 380 to 900 px in 2 px steps against the
// un-grouped layout and the readout never left the slider's line -- the
// worst difference was 1 px of integer centring. The trio is ~285 px at
// its narrowest and the pane's floor is 380, so it fits on the first
// line every time, and the wrap lands after Send and Cancel instead.
//
// So this asserts the *structure*, not a break it cannot reproduce. A
// same-line check would pass with or without the fix, which by this
// project's standards is worse than no check at all. What is worth
// holding is that the three stay one item: the property currently
// rests on a coincidence between the mode combo's width, the slider's
// 80 px minimum and the pane floor, and a longer translation of
// "Level:" is all it would take to break it.
void test_the_level_controls_are_one_flow_item() {
    AppState state;
    QWidget host;
    host.resize(1400, 700);
    auto* panel = new TransmitPanel(&state, &host);
    host.show();

    auto* slider = panel->findChild<QSlider*>();
    check::is_true(slider != nullptr, "the panel has a level slider");

    // By its text rather than a stored pointer: this test has no
    // business reaching into the panel's members, and "the label
    // showing decibels" is what the operator is looking at.
    QLabel* readout = nullptr;
    for (QLabel* label : panel->findChildren<QLabel*>()) {
        if (label->text().endsWith(QLatin1String(" dB"))) {
            check::is_true(readout == nullptr, "exactly one dB readout");
            readout = label;
        }
    }
    check::is_true(readout != nullptr, "the panel has a dB readout");
    if (slider == nullptr || readout == nullptr) return;

    QWidget* group = slider->parentWidget();
    check::is_true(group == readout->parentWidget(),
                   "the readout shares a container with the slider");
    // And that container is not the wrapping row itself, which is what
    // it would be if the three were added to the send bar directly.
    // dynamic_cast, not qobject_cast: FlowLayout carries no Q_OBJECT
    // (it has no signals of its own), and qobject_cast static_asserts
    // on that rather than falling back.
    check::is_true(group != nullptr &&
                       dynamic_cast<FlowLayout*>(group->layout()) == nullptr,
                   "their container does not wrap between them");
}

// Editing defers the composite rebuild instead of doing it inline.
//
// `schedule_optimization` renders the whole 640x480 composite and
// converts it to float **on the GUI thread** -- measured at 6.37 ms for
// a three-item composition -- and `OverlayEditor::documentChanged` is
// emitted on every mouse move of a drag. The debounce inside
// `Speculative` does not help with that: it keeps the *worker* from
// starting a run per edit, and by the time it is consulted the
// expensive part has already happened.
//
// Asserted on the timer rather than on elapsed time: what is being
// checked is that the work was deferred and coalesced, which is a
// structural property. A stopwatch here would be a latency test
// pretending to be a correctness one.
void test_an_edit_defers_the_rebuild() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    host.show();
    QCoreApplication::processEvents();

    auto* debounce = panel->findChild<QTimer*>(QStringLiteral("edit_debounce"));
    check::is_true(debounce != nullptr, "the panel has an edit debounce");
    if (debounce == nullptr) return;
    check::is_true(debounce->isSingleShot(),
                   "the debounce is single-shot, so a burst is one rebuild");
    check::is_true(debounce->interval() > 0,
                   "and has an interval, so it actually defers");

    auto* editor = panel->findChild<OverlayEditor*>();
    check::is_true(editor != nullptr, "the panel has an overlay editor");
    if (editor == nullptr) return;

    // A burst, as a drag produces. Every one of these restarts the same
    // timer; none of them may rebuild.
    for (int i = 0; i < 20; ++i) editor->refresh_item();
    check::is_true(debounce->isActive(),
                   "a burst of edits leaves one deferred rebuild, not none");
}

// A rebuild consumes the pending edit rather than leaving it queued.
//
// This is the primitive `send()` uses to flush, and the reason it must
// exist. The generation counter in `Speculative` is what guarantees the
// latents on the air describe the picture on the air, and it can only
// know about an edit that reached it. An edit made within the debounce
// window of a Send click has not -- so unless Send rebuilds first,
// `request_send()` waits on the *previous* generation, finds a result
// already ready for it, and transmits the current composition with
// latents refined for the one before it. That failure is invisible by
// construction: refined latents for the wrong picture still decode to a
// picture.
//
// **What this does not cover, deliberately: that `send()` calls it.**
// Driving `send()` here would open a modal message box -- no picture
// chosen, and no codec loaded -- and a modal in a headless test is a
// hang, which this suite treats as worse than a gap. The call is the
// first statement of `send()` and carries the reasoning above beside
// it. A test for it belongs wherever a panel is driven with a real
// codec, which this file is not.
void test_a_rebuild_consumes_the_pending_edit() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    host.show();
    QCoreApplication::processEvents();

    auto* debounce = panel->findChild<QTimer*>(QStringLiteral("edit_debounce"));
    auto* editor = panel->findChild<OverlayEditor*>();
    check::is_true(debounce != nullptr && editor != nullptr,
                   "the panel has a debounce and an editor");
    if (debounce == nullptr || editor == nullptr) return;

    editor->refresh_item();
    check::is_true(debounce->isActive(), "an edit is pending");

    panel->schedule_optimization();
    check::is_true(!debounce->isActive(),
                   "a rebuild clears the pending edit rather than repeating it");
}

// The colour button's size never moves.
//
// **This is the platform-independent form of a Windows-only CI
// failure.** `set_color_swatch` paints the current colour onto the
// button as an icon, and it used to do so for the first time when a
// text item was first selected -- a QPushButton grows when it is handed
// an icon, measured 80x22 without and 80x24 with. The button therefore
// got 2 px taller at that moment and stayed there.
//
// On Linux that was invisible: a taller sibling on the same line of the
// wrapping row absorbed it, and the strip stayed 143. On Windows this
// button *is* the tallest thing on its line, so the strip went 185 to
// 189 and `test_the_strip_height_survives_a_selection` failed there and
// nowhere else.
//
// Asserting on the strip alone would leave that a Windows-only check --
// green on the machine in front of whoever breaks it next. Asserting on
// the button says the same thing everywhere.
void test_the_colour_button_does_not_change_size() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    panel->setGeometry(0, 0, 900, 700);
    host.show();
    QCoreApplication::processEvents();

    QPushButton* colour = nullptr;
    for (QPushButton* button : panel->findChildren<QPushButton*>()) {
        if (button->text().startsWith(QLatin1String("Colour"))) colour = button;
    }
    check::is_true(colour != nullptr, "the panel has a colour button");
    if (colour == nullptr) return;
    const QSize idle = colour->sizeHint();

    auto* editor = panel->findChild<OverlayEditor*>();
    check::is_true(editor != nullptr, "the panel has an overlay editor");
    // A *text* item, which is the only kind with a colour and therefore
    // the only one that ever painted a swatch.
    editor->add_text(std::string("KD8XYZ"));
    QCoreApplication::processEvents();
    check::is_true(colour->sizeHint() == idle,
                   "the colour button is the same size with a text item selected");

    editor->remove_selected();
    QCoreApplication::processEvents();
    check::is_true(colour->sizeHint() == idle,
                   "and the same size again with nothing selected");
}

// The size box is in pixels of the transmitted frame, per item kind.
//
// **The axis is the part that can be wrong and look right.** A text
// item's `size` is cap height as a fraction of the canvas *height*
// (480); an image inset's `width` is a fraction of its *width* (640).
// One control serves both, so converting an inset against 480 gives a
// number that is wrong by the aspect ratio, is entirely plausible on
// screen, and writes itself back into the document the moment the
// operator touches the field.
//
// Both directions are asserted, because a conversion used for display
// only and a conversion used for write-back are two different bugs and
// each leaves the other looking correct.
void test_the_size_box_is_in_frame_pixels() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    panel->setGeometry(0, 0, 900, 700);
    host.show();
    QCoreApplication::processEvents();

    auto* editor = panel->findChild<OverlayEditor*>();
    auto* size = panel->findChild<QSpinBox*>(QStringLiteral("item_size_px"));
    check::is_true(editor != nullptr && size != nullptr,
                   "size: the panel has an editor and a size box");
    if (editor == nullptr || size == nullptr) return;

    editor->add_text("N0CALL");
    QCoreApplication::processEvents();
    // 0.08 of a 480-line canvas.
    check::equal(size->value(), 38, "size: a text item reads in canvas pixels");
    check::equal(size->minimum(), 5, "size: the range is the drag clamp, 0.01");
    check::equal(size->maximum(), 720, "size: up to 1.5 of the height");

    size->setValue(96);
    const auto& text = std::get<overlay::TextItem>(editor->doc().items.front());
    check::is_true(std::abs(text.size - 0.2) < 1e-12,
                   "size: and writes back as a fraction of the height");

    editor->remove_selected();
    editor->add_image_inset("/nonexistent/inset.png");
    QCoreApplication::processEvents();
    // 0.28 of a 640-wide canvas -- 179, not the 134 that the text
    // item's axis would give.
    check::equal(size->value(), 179, "size: an inset reads against the width");
    check::equal(size->minimum(), 13, "size: with the inset's own clamp, 0.02");
    check::equal(size->maximum(), 1280, "size: up to 2.0 of the width");

    size->setValue(320);
    const auto& image = std::get<overlay::ImageItem>(editor->doc().items.front());
    check::is_true(std::abs(image.width - 0.5) < 1e-12,
                   "size: and writes back as a fraction of the width");
}

// Where a canvas point lands inside the editor widget, mirroring its
// letterboxing: same aspect, centred. The editor's own `canvas_rect` is
// private, and rightly so -- this is a test of the panel's wiring, not
// a licence to reach into the widget.
QPoint canvas_point(QWidget* editor, double canvas_x, double canvas_y) {
    const double aspect =
        static_cast<double>(overlay::CANVAS_W) / overlay::CANVAS_H;
    int w = editor->width();
    int h = static_cast<int>(std::lround(w / aspect));
    if (h > editor->height()) {
        h = editor->height();
        w = static_cast<int>(std::lround(h * aspect));
    }
    const int x0 = (editor->width() - w) / 2;
    const int y0 = (editor->height() - h) / 2;
    return QPoint(
        x0 + static_cast<int>(std::lround(canvas_x * w / overlay::CANVAS_W)),
        y0 + static_cast<int>(std::lround(canvas_y * h / overlay::CANVAS_H)));
}

// A right-click on the canvas opens the palette.
//
// The end of the chain the two halves are built as: the editor
// hit-tests, selects what was hit and emits, and the panel is what
// joins that to the menu. Each half is tested where it lives and
// neither says anything about the connection between them -- which is
// one line, and one line that can be left out with every other test in
// the tree still green.
void test_a_right_click_opens_the_palette() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    panel->setGeometry(0, 0, 900, 700);
    host.show();
    QCoreApplication::processEvents();

    auto* editor = panel->findChild<OverlayEditor*>();
    auto* palette = panel->findChild<TextPaletteMenu*>();
    check::is_true(editor != nullptr && palette != nullptr,
                   "right-click: the panel has an editor and a palette");
    if (editor == nullptr || palette == nullptr) return;

    editor->add_text(std::string("KD8XYZ"));
    QCoreApplication::processEvents();
    const overlay::Bbox box =
        overlay::item_bbox(overlay::CANVAS_W, overlay::CANVAS_H,
                           editor->doc().items.front(), nullptr);
    const QPoint at =
        canvas_point(editor, box.x + box.w / 2.0, box.y + box.h / 2.0);

    QContextMenuEvent event(QContextMenuEvent::Mouse, at, editor->mapToGlobal(at));
    QApplication::sendEvent(editor, &event);
    QCoreApplication::processEvents();
    check::is_true(palette->isVisible(),
                   "right-click: on an item, the palette opens");
    palette->close();

    // And empty canvas opens nothing -- the editor emits nothing there,
    // so this is the panel's half of that agreement.
    const QPoint empty = canvas_point(editor, overlay::CANVAS_W - 2,
                                      overlay::CANVAS_H / 2);
    QContextMenuEvent away(QContextMenuEvent::Mouse, empty,
                           editor->mapToGlobal(empty));
    QApplication::sendEvent(editor, &away);
    QCoreApplication::processEvents();
    check::is_true(!palette->isVisible(),
                   "right-click: on empty canvas, nothing opens");
}

// The palette edits the same fields the strip box shows, so the box
// has to re-read what the menu changed.
//
// Both halves matter and each hides the other's failure. If the two
// were wired to *different* items the box would simply show the wrong
// number, and if `itemEdited` were not connected at all the box would
// show a stale one -- which looks identical to "the edit did not
// happen" and sends the next person to debug the palette.
void test_an_edit_through_the_palette_reaches_the_strip_box() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    panel->setGeometry(0, 0, 900, 700);
    host.show();
    QCoreApplication::processEvents();

    auto* editor = panel->findChild<OverlayEditor*>();
    auto* palette = panel->findChild<TextPaletteMenu*>();
    auto* box_size = panel->findChild<QSpinBox*>(QStringLiteral("item_size_px"));
    check::is_true(editor != nullptr && palette != nullptr && box_size != nullptr,
                   "palette: the panel builds one, beside its size box");
    if (editor == nullptr || palette == nullptr || box_size == nullptr) return;

    editor->add_text(std::string("KD8XYZ"));
    QCoreApplication::processEvents();
    check::equal(box_size->value(), 38, "palette: the box shows the item's size");

    // Open it the way the editor does, then edit through it.
    palette->popup_for(editor->selected_item(), QPoint(50, 50));
    palette->close();
    auto* menu_size = panel->findChild<QSpinBox*>(QStringLiteral("palette_size"));
    check::is_true(menu_size != nullptr, "palette: it has a size field");
    if (menu_size == nullptr) return;
    check::equal(menu_size->value(), 38,
                 "palette: opening it shows the same number the box does");

    menu_size->setValue(96);
    QCoreApplication::processEvents();
    const auto& text = std::get<overlay::TextItem>(editor->doc().items.front());
    check::is_true(std::abs(text.size - 0.2) < 1e-12,
                   "palette: the edit reaches the document");
    check::equal(box_size->value(), 96,
                 "palette: and the strip box re-reads it");
}

// The palette costs the control strip no height at all.
//
// **This is the reason the whole feature is a popup.** The two panes'
// strips are locked to the same height (`PaneContainer::equalise_strips`)
// so the received picture and the composed one are the same size, and
// everything added under the canvas is paid for on both sides. A menu
// is a window of its own: it is in no layout, and an edit made through
// it must not move the strip either -- `on_selection` runs on every one
// of those edits, and that is the slot whose refilling used to change
// the row's height.
void test_the_palette_costs_the_strip_nothing() {
    AppState state;
    QWidget host;
    host.resize(900, 700);
    auto* panel = new TransmitPanel(&state, &host);
    panel->setGeometry(0, 0, 900, 700);
    host.show();
    QCoreApplication::processEvents();

    auto* palette = panel->findChild<TextPaletteMenu*>();
    QWidget* strip = panel->control_strip();
    check::is_true(palette != nullptr, "palette: the panel has one");
    if (palette == nullptr) return;

    // In no layout, on either level. A QMenu is a popup and cannot be
    // laid out anyway; this says so, so that adding it as a widget
    // later fails here rather than in the receive pane's geometry.
    check::is_true(panel->layout() == nullptr ||
                       panel->layout()->indexOf(palette) < 0,
                   "palette: it is not in the panel's layout");
    check::is_true(strip->layout() == nullptr ||
                       strip->layout()->indexOf(palette) < 0,
                   "palette: nor in the strip's");
    check::is_true(!palette->isVisible(), "palette: and it starts closed");

    const int idle = strip_height(strip);
    auto* editor = panel->findChild<OverlayEditor*>();
    check::is_true(editor != nullptr, "palette: the panel has an editor");
    if (editor == nullptr) return;
    editor->add_text(std::string("KD8XYZ"));
    QCoreApplication::processEvents();

    palette->popup_for(editor->selected_item(), QPoint(50, 50));
    palette->close();
    QCoreApplication::processEvents();
    check::equal(strip_height(strip), idle,
                 "palette: opening it does not move the strip");

    auto* menu_size = panel->findChild<QSpinBox*>(QStringLiteral("palette_size"));
    if (menu_size != nullptr) {
        // The longest number the field can hold, since the strip box
        // shows the same value and a wider number is the way an edit
        // could push the row.
        menu_size->setValue(720);
        QCoreApplication::processEvents();
        check::equal(strip_height(strip), idle,
                     "palette: nor does editing through it");
    }
}

}  // namespace

int main(int argc, char** argv) {
    check::report_crashes_instead_of_prompting();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    test_the_strip_height_survives_a_selection();
    test_the_level_controls_are_one_flow_item();
    test_the_colour_button_does_not_change_size();
    test_an_edit_defers_the_rebuild();
    test_a_rebuild_consumes_the_pending_edit();
    test_the_size_box_is_in_frame_pixels();
    test_a_right_click_opens_the_palette();
    test_an_edit_through_the_palette_reaches_the_strip_box();
    test_the_palette_costs_the_strip_nothing();
    return check::report("transmit panel");
}
