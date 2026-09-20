// The right-click text palette.
//
// What a palette *looks* like needs eyes. What it writes does not, and
// the failures available here are all of the silent kind: a control
// that reads a field and writes it straight back on open, a wheel step
// that is the platform's rather than the documented one, a "clear
// style" that also clears somebody's callsign. Each of those leaves a
// palette that still looks and behaves like a working one.

#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include "check.hpp"
#include "images/types.hpp"
#include "overlay/model.hpp"
#include "overlay_editor.hpp"
#include "text_palette.hpp"

using namespace sstvae;

namespace {

images::Picture grey(int w, int h) {
    images::Picture picture(w, h);
    std::fill(picture.rgb.begin(), picture.rgb.end(), std::uint8_t{128});
    return picture;
}

struct Fixture {
    std::unique_ptr<gui::OverlayEditor> editor;
    std::unique_ptr<gui::TextPaletteMenu> palette;

    Fixture() : editor(new gui::OverlayEditor()) {
        editor->resize(500, 400);
        editor->set_base_image(grey(overlay::CANVAS_W, overlay::CANVAS_H));
        palette.reset(new gui::TextPaletteMenu(editor.get()));
    }

    // Add a text item (which selects it) and open the palette over it,
    // then close the popup again -- the controls keep whatever the open
    // loaded into them, and a test driving a visible menu would be
    // testing the window manager.
    overlay::TextItem* open_on_text(const std::string& text = "N0CALL") {
        editor->add_text(text);
        open();
        return std::get_if<overlay::TextItem>(editor->selected_item());
    }

    void open() {
        palette->popup_for(editor->selected_item(), QPoint(10, 10));
        palette->close();
    }

    // The text of the item at a given depth -- index 0 is the bottom
    // layer, since `Doc::items` is drawn back to front.
    std::string text_at(int index) {
        return std::get<overlay::TextItem>(editor->doc().items[index]).text;
    }

    // The whole stack as one string, so a wrong order fails with the
    // order it got rather than with "false".
    std::string stack() {
        std::string out;
        for (int i = 0; i < static_cast<int>(editor->doc().items.size()); ++i) {
            out += text_at(i);
        }
        return out;
    }

    template <typename T>
    T* find(const char* name) {
        return palette->findChild<T*>(QString::fromLatin1(name));
    }
};

void wheel(QWidget* widget, int notches,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPointF at(5, 5);
    QWheelEvent event(at, widget->mapToGlobal(at.toPoint()), QPoint(0, 0),
                      QPoint(0, notches * 120), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}

// A text item's fields as the document holds them, for the "and nothing
// else moved" half of the clear-style test.
std::string positional(const overlay::TextItem& text) {
    return text.text + "|" + std::to_string(text.x) + "|" +
           std::to_string(text.y) + "|" + std::to_string(text.size) + "|" +
           std::to_string(text.rotation) + "|" + text.align + "|" +
           text.anchor + "|" + std::to_string(text.line_spacing) + "|" +
           text.font;
}

// The wheel steps are the palette's, not the platform's.
//
// A `QSpinBox` already answers a wheel with its own `singleStep`, and a
// `QMenu` answers one by scrolling itself -- so "it responds to the
// wheel" is not evidence that either of the documented steps is what
// happened. Both are asserted in the field's own unit *and* in the
// document, because the field is in pixels of the transmitted frame
// and the document is in fractions of it: a conversion dropped in
// either direction leaves the spin box looking perfectly correct.
void test_the_wheel_steps_are_the_documented_ones() {
    Fixture fix;
    overlay::TextItem* text = fix.open_on_text();
    check::is_true(text != nullptr, "palette: a text item to edit");
    if (text == nullptr) return;
    auto* size = fix.find<QSpinBox>("palette_size");
    auto* rotation = fix.find<QSpinBox>("palette_rotation");
    check::is_true(size != nullptr && rotation != nullptr,
                   "palette: it has a size and a rotation field");
    if (size == nullptr || rotation == nullptr) return;

    // 0.08 of a 480-line canvas.
    check::equal(size->value(), 38, "palette: size opens in canvas pixels");

    wheel(size, 1);
    check::equal(size->value(), 39, "palette: one notch is one pixel");
    check::is_true(std::abs(text->size - 39.0 / overlay::CANVAS_H) < 1e-12,
                   "palette: and it reaches the document as a fraction");

    wheel(size, 1, Qt::ShiftModifier);
    check::equal(size->value(), 49, "palette: shift is ten pixels");
    wheel(size, -1, Qt::ShiftModifier);
    check::equal(size->value(), 39, "palette: and back down");

    // Clamped at the ends rather than wrapping or running past.
    wheel(size, 100, Qt::ShiftModifier);
    check::equal(size->value(), 720, "palette: size stops at the top");
    wheel(size, 1);
    check::equal(size->value(), 720, "palette: and stays there");
    wheel(size, -1000);
    check::equal(size->value(), 5, "palette: and at the bottom");

    check::equal(rotation->value(), 0, "palette: rotation opens at zero");
    wheel(rotation, 1);
    check::equal(rotation->value(), 1, "palette: one notch is one degree");
    check::equal(text->rotation, 1.0, "palette: and reaches the document");

    // Shift is a *destination*, not a bigger step: from 1 degree the
    // next multiple of 15 upward is 15, and downward is 0. A "+15" step
    // would give 16 and -14, and both still look like a working snap
    // until you try to square something up.
    wheel(rotation, 1, Qt::ShiftModifier);
    check::equal(rotation->value(), 15, "palette: shift snaps up to 15");
    wheel(rotation, -1, Qt::ShiftModifier);
    check::equal(rotation->value(), 0, "palette: and back down to 0");
    wheel(rotation, -1, Qt::ShiftModifier);
    check::equal(rotation->value(), -15, "palette: through zero as well");

    wheel(rotation, 1000);
    check::equal(rotation->value(), 180, "palette: rotation stops at 180");
    wheel(rotation, -1000);
    check::equal(rotation->value(), -180, "palette: and at -180");
}

// Opening the palette must not be an edit.
//
// Every control is filled from the item when the menu opens, and each
// of those `setValue`/`setChecked` calls fires the same signal a real
// edit does. Without the loading guard the menu writes the item back to
// itself on open -- which is invisible for a field that round-trips,
// and *not* invisible for the size, which the palette shows in whole
// canvas pixels. A size of 0.0801 would come back as 38/480.
void test_opening_the_palette_edits_nothing() {
    Fixture fix;
    fix.editor->add_text("N0CALL");
    auto* text = std::get_if<overlay::TextItem>(fix.editor->selected_item());
    check::is_true(text != nullptr, "palette: a text item");
    if (text == nullptr) return;
    // Deliberately not a whole number of canvas pixels: 38.448.
    text->size = 0.0801;
    text->rotation = 7.5;
    const std::string before = positional(*text);

    int edits = 0;
    QObject::connect(fix.palette.get(), &gui::TextPaletteMenu::itemEdited,
                     [&edits] { ++edits; });
    fix.open();

    check::equal(edits, 0, "palette: opening it is not an edit");
    check::equal(positional(*text), before,
                 "palette: and the item is byte for byte what it was");
}

// Solid -> linear -> radial -> solid, and it writes the document.
void test_the_fill_mode_button_cycles() {
    Fixture fix;
    overlay::TextItem* text = fix.open_on_text();
    auto* button = fix.find<QPushButton>("palette_fill_mode");
    check::is_true(text != nullptr && button != nullptr,
                   "palette: it has a fill-mode button");
    if (text == nullptr || button == nullptr) return;

    check::equal(text->fill_mode, std::string("solid"), "palette: opens solid");
    check::equal(button->text().toStdString(), std::string("SOLID"),
                 "palette: and says so");

    button->click();
    check::equal(text->fill_mode, std::string("linear"), "palette: solid -> linear");
    check::equal(button->text().toStdString(), std::string("LINEAR"),
                 "palette: the label follows");
    button->click();
    check::equal(text->fill_mode, std::string("radial"), "palette: linear -> radial");
    button->click();
    check::equal(text->fill_mode, std::string("solid"), "palette: radial -> solid");
}

// The stroke toggle is the only control on `stroke_width`, and zero is
// the off switch -- so turning it off has to remember the width
// somewhere or the operator loses it. It must remember the width they
// *chose*, not the default, which is the version of this that passes a
// test written with the default still in place.
void test_the_stroke_toggle_keeps_the_width() {
    Fixture fix;
    overlay::TextItem* text = fix.open_on_text();
    auto* on = fix.find<QToolButton>("palette_stroke_on");
    auto* width = fix.find<QSpinBox>("palette_stroke_width");
    check::is_true(text != nullptr && on != nullptr && width != nullptr,
                   "palette: it has a stroke toggle and width");
    if (text == nullptr || on == nullptr || width == nullptr) return;

    check::is_true(on->isChecked(), "palette: a new item has a stroke");
    check::equal(width->value(), 12, "palette: 0.12 of the glyph size");

    // A width the operator chose, which is what has to come back.
    width->setValue(30);
    check::is_true(std::abs(text->stroke_width - 0.30) < 1e-12,
                   "palette: the width reaches the document");

    on->setChecked(false);
    check::equal(text->stroke_width, 0.0, "palette: off is a width of zero");
    check::equal(width->value(), 0, "palette: and the field says zero");
    check::is_true(!width->isEnabled(), "palette: with nothing to set");

    on->setChecked(true);
    check::is_true(std::abs(text->stroke_width - 0.30) < 1e-12,
                   "palette: back on restores the width that was chosen");
    check::equal(width->value(), 30, "palette: and the field with it");
    check::is_true(width->isEnabled(), "palette: enabled again");
}

// Clear style resets the appearance and *only* the appearance.
//
// The untouched fields are asserted explicitly rather than left
// implied: the obvious way to write this button is `*text =
// overlay::TextItem{}` with the text copied back, which passes every
// assertion about the style fields and silently moves the item to the
// top-left corner at the default size.
void test_clear_style_resets_the_style_fields_only() {
    Fixture fix;
    fix.editor->add_text("W1AW/KH6");
    auto* text = std::get_if<overlay::TextItem>(fix.editor->selected_item());
    check::is_true(text != nullptr, "palette: a text item");
    if (text == nullptr) return;

    // Style, all of it away from its default.
    text->bold = true;
    text->italic = true;
    text->underline = true;
    text->font_family = "monospace";
    text->fill_mode = "radial";
    text->color = "#ff0000";
    text->color2 = "#00ff00";
    text->fill_angle = 120.0;
    text->stroke_color = "#0000ff";
    text->stroke_width = 0.4;
    // And everything that is not style, equally away from its default.
    text->x = 0.4;
    text->y = 0.6;
    text->size = 0.2;
    text->rotation = 33.0;
    text->align = "center";
    text->anchor = "mm";
    text->line_spacing = 0.5;
    text->font = "/fonts/sent-with-the-template.ttf";
    const std::string untouched = positional(*text);

    fix.open();
    auto* clear = fix.find<QPushButton>("palette_clear");
    check::is_true(clear != nullptr, "palette: it has a clear-style button");
    if (clear == nullptr) return;
    clear->click();

    const overlay::TextItem fresh;
    check::equal(text->bold, fresh.bold, "clear: bold");
    check::equal(text->italic, fresh.italic, "clear: italic");
    check::equal(text->underline, fresh.underline, "clear: underline");
    check::equal(text->font_family, fresh.font_family, "clear: font family");
    check::equal(text->fill_mode, fresh.fill_mode, "clear: fill mode");
    check::equal(text->color, fresh.color, "clear: colour");
    check::equal(text->color2, fresh.color2, "clear: the far stop");
    check::equal(text->fill_angle, fresh.fill_angle, "clear: fill angle");
    check::equal(text->stroke_color, fresh.stroke_color, "clear: stroke colour");
    check::equal(text->stroke_width, fresh.stroke_width, "clear: stroke width");

    check::equal(positional(*text), untouched,
                 "clear: and nothing else moved -- text, place, size, "
                 "rotation, align, anchor, line spacing, font file");
}

// An image inset gets Layers and nothing else.
//
// Ordering is the one thing on this menu that means anything for a
// picture; bold and a gradient do not. Opening nothing at all would be
// the other defensible choice and is the worse one -- a right-click
// that does nothing reads as broken.
void test_an_image_item_gets_layers_only() {
    Fixture fix;
    fix.editor->add_image_inset("/nonexistent/inset.png");
    fix.open();

    auto* format = fix.find<QMenu>("palette_format");
    auto* style = fix.find<QMenu>("palette_style");
    auto* layers = fix.find<QMenu>("palette_layers");
    check::is_true(format != nullptr && style != nullptr && layers != nullptr,
                   "palette: it has all three submenus");
    if (format == nullptr || style == nullptr || layers == nullptr) return;
    check::is_true(!format->menuAction()->isVisible(),
                   "palette: no Format for a picture");
    check::is_true(!style->menuAction()->isVisible(),
                   "palette: no Style either");
    check::is_true(layers->menuAction()->isVisible(),
                   "palette: but Layers, which is what a picture has");

    // And back again, so hiding them is not a one-way door.
    fix.editor->add_text("N0CALL");
    fix.open();
    check::is_true(format->menuAction()->isVisible(),
                   "palette: a text item gets Format back");
    check::is_true(style->menuAction()->isVisible(), "palette: and Style");
}

// The Layers row acts on the editor, and Shift changes both what the
// buttons say and what they do.
//
// The label has to change *before* the click -- a control that silently
// does something other than what it says is worse than one that cannot
// do it at all -- which is why the modifier is a polled state rather
// than something read off the click event.
void test_the_layer_row_reorders_and_shift_relabels() {
    // **Four items, and the selection starts two from each end.** With
    // three, every shifted move is also reachable in one step -- from
    // the middle of a stack of three, "down one" and "to the bottom"
    // land in the same place, so a palette that ignored Shift entirely
    // passed. This is the fixture that tells them apart.
    Fixture fix;
    fix.editor->add_text("A");
    fix.editor->add_text("B");
    fix.editor->add_text("C");
    fix.editor->add_text("D");
    fix.open();  // on D, which is on top
    check::equal(fix.stack(), std::string("ABCD"), "layers: added back to front");

    auto* up = fix.find<QPushButton>("palette_layer_up");
    auto* down = fix.find<QPushButton>("palette_layer_down");
    check::is_true(up != nullptr && down != nullptr,
                   "palette: it has layer buttons");
    if (up == nullptr || down == nullptr) return;

    const std::string plain_up = up->text().toStdString();
    const std::string plain_down = down->text().toStdString();

    down->click();
    check::equal(fix.stack(), std::string("ABDC"),
                 "layers: down moves the selection one layer");

    fix.palette->set_layer_modifiers(Qt::ShiftModifier);
    check::is_true(up->text().toStdString() != plain_up,
                   "layers: shift says the button will do something else");
    check::is_true(down->text().toStdString() != plain_down,
                   "layers: both of them");

    // Two layers to go, so one step short of the bottom is "ADBC" and
    // the jump is "DABC" -- the assertion that made Shift load-bearing.
    down->click();
    check::equal(fix.stack(), std::string("DABC"),
                 "layers: with shift held, down goes all the way");
    up->click();
    check::equal(fix.stack(), std::string("ABCD"),
                 "layers: and up goes all the way back");

    fix.palette->set_layer_modifiers(Qt::NoModifier);
    check::equal(up->text().toStdString(), plain_up,
                 "layers: releasing shift says so again");
    check::equal(down->text().toStdString(), plain_down, "layers: on both");
    down->click();
    check::equal(fix.stack(), std::string("ABDC"),
                 "layers: and it is one layer at a time again");
}

// The palette edits the editor's selection, so it refuses to open over
// anything else. `OverlayEditor::contextMenuEvent` selects what was
// right-clicked before it emits, so this never fires in the app -- it
// is the assertion that keeps that a requirement rather than a habit.
void test_it_refuses_an_item_that_is_not_the_selection() {
    Fixture fix;
    fix.editor->add_text("A");
    fix.editor->add_text("B");  // selected
    overlay::Item* other = &const_cast<overlay::Doc&>(fix.editor->doc()).items[0];

    fix.palette->popup_for(other, QPoint(10, 10));
    check::is_true(!fix.palette->isVisible(),
                   "palette: it does not open over an unselected item");
    fix.palette->popup_for(nullptr, QPoint(10, 10));
    check::is_true(!fix.palette->isVisible(), "palette: nor over nothing");

    fix.palette->popup_for(fix.editor->selected_item(), QPoint(10, 10));
    check::is_true(fix.palette->isVisible(), "palette: but it does over the selection");
    fix.palette->close();
}

// Bold, italic and underline are document fields, not a Qt font effect
// painted into the preview -- which is the property that keeps what the
// operator arranges and what goes on the air the same picture.
void test_the_weight_toggles_write_the_document() {
    Fixture fix;
    overlay::TextItem* text = fix.open_on_text();
    if (text == nullptr) return;
    auto* bold = fix.find<QToolButton>("palette_bold");
    auto* italic = fix.find<QToolButton>("palette_italic");
    auto* underline = fix.find<QToolButton>("palette_underline");
    check::is_true(bold != nullptr && italic != nullptr && underline != nullptr,
                   "palette: it has the three weight toggles");
    if (bold == nullptr || italic == nullptr || underline == nullptr) return;

    int edits = 0;
    QObject::connect(fix.palette.get(), &gui::TextPaletteMenu::itemEdited,
                     [&edits] { ++edits; });

    bold->setChecked(true);
    check::is_true(text->bold, "palette: bold reaches the document");
    check::is_true(!text->italic && !text->underline,
                   "palette: and only bold -- the three are not one flag");
    italic->setChecked(true);
    underline->setChecked(true);
    check::is_true(text->italic && text->underline, "palette: and the other two");
    bold->setChecked(false);
    check::is_true(!text->bold && text->italic,
                   "palette: turning one off leaves the others");
    check::equal(edits, 4, "palette: each is one edit, announced once");
}

}  // namespace

int main(int argc, char** argv) {
    check::report_crashes_instead_of_prompting();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QApplication app(argc, argv);

    test_the_wheel_steps_are_the_documented_ones();
    test_opening_the_palette_edits_nothing();
    test_the_fill_mode_button_cycles();
    test_the_stroke_toggle_keeps_the_width();
    test_clear_style_resets_the_style_fields_only();
    test_an_image_item_gets_layers_only();
    test_the_layer_row_reorders_and_shift_relabels();
    test_it_refuses_an_item_that_is_not_the_selection();
    test_the_weight_toggles_write_the_document();

    return check::report("text palette");
}
