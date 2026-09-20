// The right-click style palette for a text item on the composer.
//
// A `QMenu` with three submenus -- Format, Style, Layers -- each holding
// one row of *live controls* rather than a list of menu items. It is a
// second, richer path to fields the "Selected item" box under the canvas
// also edits; that box is unchanged and both write the same document.
//
// **A popup rather than more of the control strip.** The two panes' strip
// heights are locked equal (`PaneContainer::equalise_strips`), so
// everything added below the canvas comes out of the picture on *both*
// sides. A popup costs no layout at all, which is why this shape was
// chosen over widening the box that already exists.
//
// **Everything here must be a document field the renderer honours.** The
// editor previews `overlay::render()`'s own output, so a control that
// painted an effect into the preview alone would be showing the operator
// something that is not going on the air. Every control below writes a
// field on `overlay::TextItem` and then re-renders.
//
// Two constructions in here are deliberate and neither is the obvious
// one:
//
// **No `QComboBox` inside a `QWidgetAction`.** Its popup is a second
// window over a menu that holds a mouse grab, and on some styles opening
// it dismisses the menu underneath. The font family is a nested `QMenu`
// instead -- which is the one popup a menu is *made* of, works on every
// style, and costs nothing for a list of four. The failure it avoids is
// style-dependent, so a machine where the combo happened to work would
// prove nothing about the next one.
//
// **The item is not stored.** Every edit asks the editor for its
// selection afresh, because the Layers row rotates items inside a
// `std::vector` -- a pointer captured when the menu opened would be
// pointing at a different item by the time the next control was touched.

#ifndef SSTVAE_GUI_TEXT_PALETTE_HPP
#define SSTVAE_GUI_TEXT_PALETTE_HPP

#include <QMenu>
#include <QPoint>
#include <QString>

#include <string>

#include "overlay/model.hpp"

class QAbstractButton;
class QEvent;
class QMenu;
class QObject;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;
class QWidget;

namespace sstvae::gui {

class OverlayEditor;

class TextPaletteMenu : public QMenu {
    Q_OBJECT

public:
    explicit TextPaletteMenu(OverlayEditor* editor, QWidget* parent = nullptr);

    // Open the palette over `item`, which must be the editor's current
    // selection -- `OverlayEditor::contextMenuEvent` selects what was
    // clicked before it emits, so that holds by construction. Anything
    // else is refused rather than guessed at, because editing an item
    // other than the selected one is silently wrong.
    //
    // An `ImageItem` gets the Layers submenu only. Ordering is the one
    // thing on here that means something for a picture inset, and the
    // alternative -- opening nothing -- makes a right-click look broken.
    void popup_for(overlay::Item* item, const QPoint& global_pos);

    // What the Layers row's labels say, from the modifiers held *now*.
    //
    // Shift turns "Bring forward" into "Bring to front", and the label
    // has to change before the click to be honest about what the click
    // will do -- so this is polled on a timer while the submenu is open
    // rather than read from the click event. Public because that makes
    // it the seam a test drives, the way `Waterfall::tick()` is a slot
    // so a frame can be rendered without waiting on a timer.
    void set_layer_modifiers(Qt::KeyboardModifiers modifiers);

signals:
    // Something on the palette changed the item, so the strip's own
    // property box can re-read the same fields. Deliberately not the
    // editor's `documentChanged`, which also fires on every mouse move
    // of a drag.
    void itemEdited();

protected:
    // The wheel steps on the size and rotation fields. A `QMenu` eats
    // wheel events to scroll itself, so these are intercepted before
    // either the menu or the spin box's own handler sees them.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The selected item, or null when the selection is gone or the
    // controls are being filled. Every edit goes through one of these
    // rather than through a pointer captured when the menu opened --
    // the Layers row rotates items inside a vector.
    overlay::Item* current_item();
    // The same, narrowed to a text item: null for a picture inset.
    overlay::TextItem* current_text();
    // Re-render, and tell the strip box to re-read. Called by every
    // control on this menu, and the only way any of them writes.
    void edited();
    // Fill every control from the item, with `loading_` set so their
    // change signals do not write the value straight back.
    void load_from(const overlay::TextItem& text);

    QMenu* build_format();
    QMenu* build_style();
    QMenu* build_layers();
    // A colour well: a button showing `getter`'s colour that writes
    // `setter` when it is changed.
    QPushButton* color_well(QWidget* parent, const QString& name,
                            const QString& tip);
    // A pointer to member rather than a reference into the item: the
    // colour dialog is modal and runs an event loop, so the item has to
    // be resolved again *after* it returns rather than held across it.
    void pick_color(QAbstractButton* well, std::string overlay::TextItem::* field);

    // One wheel notch on a field, in the units that field shows.
    void step_size(int notches, bool coarse);
    void step_rotation(int notches, bool coarse);

    void update_fill_mode_button(const QString& mode);
    void update_layer_labels();

    OverlayEditor* editor_ = nullptr;

    QMenu* format_menu_ = nullptr;
    QMenu* style_menu_ = nullptr;
    QMenu* layers_menu_ = nullptr;

    QToolButton* bold_ = nullptr;
    QToolButton* italic_ = nullptr;
    QToolButton* underline_ = nullptr;
    QPushButton* family_ = nullptr;
    QSpinBox* size_ = nullptr;

    QPushButton* fill_from_ = nullptr;
    QPushButton* fill_mode_ = nullptr;
    QPushButton* fill_to_ = nullptr;
    QToolButton* stroke_on_ = nullptr;
    QPushButton* stroke_color_ = nullptr;
    QSpinBox* stroke_width_ = nullptr;
    QSpinBox* rotation_ = nullptr;

    QPushButton* layer_up_ = nullptr;
    QPushButton* layer_down_ = nullptr;

    // Polls `QApplication::keyboardModifiers` while Layers is open.
    QTimer* shift_watch_ = nullptr;
    bool shifted_ = false;

    // What the stroke toggle restores when it is switched back on.
    // `stroke_width` is the only stroke field -- zero *is* the off
    // switch, deliberately, because two fields that can disagree about
    // whether there is a stroke is one field too many -- so turning it
    // off has to remember the width somewhere, and that somewhere is
    // here rather than in the document.
    double stroke_restore_ = 0.12;

    // Set while the controls are being filled from an item.
    bool loading_ = false;
};

}  // namespace sstvae::gui

#endif
