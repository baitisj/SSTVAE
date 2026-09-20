#include "text_palette.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>

#include <cmath>
#include <string>
#include <variant>

#include "overlay_editor.hpp"
#include "overlay_units.hpp"
#include "style.hpp"

namespace sstvae::gui {
namespace {

// Stroke width is a fraction of the glyph size, which is a percentage
// everywhere an operator meets it.
constexpr int STROKE_MAX_PCT = 50;

// The generic families, plus "Default" for the empty string. Real family
// names are not offered: this is a menu, and the set that renders the
// same on three platforms is exactly these four.
struct Family {
    const char* label;
    const char* value;
};
constexpr Family FAMILIES[] = {
    {"Default", ""},
    {"Sans-Serif", "sans-serif"},
    {"Serif", "serif"},
    {"Monospace", "monospace"},
    {"Cursive", "cursive"},
};

QString family_label(const std::string& value) {
    for (const Family& family : FAMILIES) {
        if (value == family.value) return QMenu::tr(family.label);
    }
    // A document may name a real face; show it rather than lying about
    // it being the default.
    return QString::fromStdString(value);
}

// One wheel event in notches, sign preserved. A high-resolution wheel
// reports less than a notch per event, and rounding that to zero makes
// the field simply not respond on a trackpad.
int notches_of(const QWheelEvent* wheel) {
    const int dy = wheel->angleDelta().y();
    if (dy == 0) return 0;
    const int whole = dy / 120;
    return whole != 0 ? whole : (dy > 0 ? 1 : -1);
}

QToolButton* toggle(QWidget* parent, const QString& label, const QString& name,
                    const QString& tip) {
    auto* button = new QToolButton(parent);
    button->setObjectName(name);
    button->setText(label);
    button->setCheckable(true);
    button->setToolTip(tip);
    return button;
}

// A submenu holding one row of live controls.
//
// `QWidgetAction` owns the row, and the row is parented to the menu so
// `findChild` reaches it before the menu has ever been shown --
// `QWidgetActionPrivate::defaultWidget` is a `QPointer`, so the double
// ownership is safe in either destruction order.
QMenu* row_menu(QMenu* parent, const QString& title, const QString& name,
                QWidget* row) {
    auto* menu = new QMenu(title, parent);
    menu->setObjectName(name);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(row);
    menu->addAction(action);
    parent->addMenu(menu);
    return menu;
}

QHBoxLayout* row_layout(QWidget* row) {
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    return layout;
}

}  // namespace

TextPaletteMenu::TextPaletteMenu(OverlayEditor* editor, QWidget* parent)
    : QMenu(parent), editor_(editor) {
    setObjectName(QStringLiteral("text_palette"));
    format_menu_ = build_format();
    style_menu_ = build_style();
    layers_menu_ = build_layers();
    update_layer_labels();
}

// --- building ----------------------------------------------------------------

QMenu* TextPaletteMenu::build_format() {
    auto* row = new QWidget(this);
    auto* menu = row_menu(this, tr("&Format"),
                          QStringLiteral("palette_format"), row);
    auto* layout = row_layout(row);

    bold_ = toggle(row, tr("B"), QStringLiteral("palette_bold"), tr("Bold"));
    italic_ = toggle(row, tr("I"), QStringLiteral("palette_italic"), tr("Italic"));
    underline_ =
        toggle(row, tr("U"), QStringLiteral("palette_underline"), tr("Underline"));
    // Not a font effect painted into the preview: each of these is a
    // field on the item that `overlay::render` honours, which is what
    // keeps the preview and the transmission the same picture.
    for (QToolButton* button : {bold_, italic_, underline_}) {
        connect(button, &QToolButton::toggled, this, [this, button](bool on) {
            overlay::TextItem* text = current_text();
            if (text == nullptr) return;
            if (button == bold_) text->bold = on;
            else if (button == italic_) text->italic = on;
            else text->underline = on;
            edited();
        });
        layout->addWidget(button);
    }

    // **A button with its own menu, not a `QComboBox`.** See the header:
    // a combo's popup over a menu that holds a mouse grab dismisses the
    // menu on some styles, and "some styles" is not something a machine
    // in front of anyone can rule out for the others.
    family_ = new QPushButton(row);
    family_->setObjectName(QStringLiteral("palette_family"));
    family_->setToolTip(tr("Font family. A document that names its own font "
                           "file keeps it -- the file wins over a family."));
    auto* families = new QMenu(family_);
    for (const Family& family : FAMILIES) {
        QAction* action = families->addAction(tr(family.label));
        const std::string value = family.value;
        connect(action, &QAction::triggered, this, [this, value] {
            overlay::TextItem* text = current_text();
            if (text == nullptr) return;
            text->font_family = value;
            family_->setText(family_label(value));
            edited();
        });
    }
    family_->setMenu(families);
    layout->addWidget(family_);

    layout->addWidget(new QLabel(tr("Size"), row));
    size_ = new QSpinBox(row);
    size_->setObjectName(QStringLiteral("palette_size"));
    // Pixels of the transmitted frame, the same unit the strip box
    // shows, through the same conversion -- see `overlay_units.hpp`.
    const units::Range size_range = units::size_range(overlay::TextItem{});
    size_->setRange(size_range.min, size_range.max);
    size_->setSuffix(tr(" px"));
    size_->setToolTip(tr("Cap height, in pixels of the 640x480 transmitted "
                         "frame. Scroll to change it; hold Shift for 10 px."));
    size_->installEventFilter(this);
    connect(size_, &QSpinBox::valueChanged, this, [this](int value) {
        overlay::Item* item = current_item();
        if (item == nullptr) return;
        units::set_size_px(*item, value);
        edited();
    });
    layout->addWidget(size_);
    menu->installEventFilter(this);
    return menu;
}

QMenu* TextPaletteMenu::build_style() {
    auto* row = new QWidget(this);
    auto* menu = row_menu(this, tr("&Style"),
                          QStringLiteral("palette_style"), row);
    auto* layout = row_layout(row);

    auto* clear = new QPushButton(tr("Clear"), row);
    clear->setObjectName(QStringLiteral("palette_clear"));
    clear->setToolTip(tr("Back to the default colours, weight and fill. The "
                         "text, its place, size and rotation are left alone."));
    connect(clear, &QPushButton::clicked, this, [this] {
        overlay::TextItem* text = current_text();
        if (text == nullptr) return;
        // **Appearance only.** Everything positional -- and the text
        // itself -- survives, so "clear style" is never the button that
        // loses somebody's callsign. `font` survives too: a document
        // naming its own face is naming a file it needs, which is not a
        // style choice this button made.
        const overlay::TextItem fresh;
        text->color = fresh.color;
        text->color2 = fresh.color2;
        text->fill_mode = fresh.fill_mode;
        text->fill_angle = fresh.fill_angle;
        text->stroke_color = fresh.stroke_color;
        text->stroke_width = fresh.stroke_width;
        text->bold = fresh.bold;
        text->italic = fresh.italic;
        text->underline = fresh.underline;
        text->font_family = fresh.font_family;
        load_from(*text);
        edited();
    });
    layout->addWidget(clear);

    // The split fill control: from-stop, the mode, to-stop. `color` is
    // the from-stop in every mode, which is what makes a solid fill and
    // a gradient's first stop the same field -- and therefore what makes
    // a version-1 document render unchanged.
    fill_from_ = color_well(row, QStringLiteral("palette_fill_from"),
                            tr("Fill colour, and a gradient's first stop."));
    connect(fill_from_, &QPushButton::clicked, this,
            [this] { pick_color(fill_from_, &overlay::TextItem::color); });
    layout->addWidget(fill_from_);

    fill_mode_ = new QPushButton(row);
    fill_mode_->setObjectName(QStringLiteral("palette_fill_mode"));
    fill_mode_->setToolTip(tr("Solid, linear gradient or radial gradient."));
    connect(fill_mode_, &QPushButton::clicked, this, [this] {
        overlay::TextItem* text = current_text();
        if (text == nullptr) return;
        // solid -> linear -> radial -> solid. Anything else in the
        // document (a mode a later build understands) lands on solid,
        // which is also how the renderer reads it.
        if (text->fill_mode == "solid") text->fill_mode = "linear";
        else if (text->fill_mode == "linear") text->fill_mode = "radial";
        else text->fill_mode = "solid";
        update_fill_mode_button(QString::fromStdString(text->fill_mode));
        edited();
    });
    layout->addWidget(fill_mode_);

    fill_to_ = color_well(row, QStringLiteral("palette_fill_to"),
                          tr("A gradient's far stop. Kept while the fill is "
                             "solid, so turning a gradient off and on again "
                             "does not lose it."));
    connect(fill_to_, &QPushButton::clicked, this,
            [this] { pick_color(fill_to_, &overlay::TextItem::color2); });
    layout->addWidget(fill_to_);

    // **The stroke toggle has no field of its own.** `stroke_width` is
    // already a fraction of the glyph size and zero already means no
    // stroke, so this writes that. Two fields that can disagree about
    // whether there is a stroke is one field too many.
    stroke_on_ = toggle(row, tr("Stroke"), QStringLiteral("palette_stroke_on"),
                        tr("Outline the glyphs."));
    connect(stroke_on_, &QToolButton::toggled, this, [this](bool on) {
        overlay::TextItem* text = current_text();
        if (text == nullptr) return;
        text->stroke_width = on ? stroke_restore_ : 0.0;
        loading_ = true;
        stroke_width_->setValue(
            static_cast<int>(std::lround(text->stroke_width * 100.0)));
        stroke_width_->setEnabled(on);
        loading_ = false;
        edited();
    });
    layout->addWidget(stroke_on_);

    stroke_color_ = color_well(row, QStringLiteral("palette_stroke_color"),
                               tr("Outline colour."));
    connect(stroke_color_, &QPushButton::clicked, this, [this] {
        pick_color(stroke_color_, &overlay::TextItem::stroke_color);
    });
    layout->addWidget(stroke_color_);

    stroke_width_ = new QSpinBox(row);
    stroke_width_->setObjectName(QStringLiteral("palette_stroke_width"));
    stroke_width_->setRange(0, STROKE_MAX_PCT);
    stroke_width_->setSuffix(tr("%"));
    stroke_width_->setToolTip(tr("Outline width, as a percentage of the glyph "
                                 "size, so it scales with the text."));
    stroke_width_->installEventFilter(this);
    connect(stroke_width_, &QSpinBox::valueChanged, this, [this](int value) {
        overlay::TextItem* text = current_text();
        if (text == nullptr) return;
        text->stroke_width = value / 100.0;
        // What the toggle restores. Remembered from the last width the
        // operator actually chose, rather than snapping back to the
        // default every time.
        if (value > 0) stroke_restore_ = text->stroke_width;
        edited();
    });
    layout->addWidget(stroke_width_);

    layout->addWidget(new QLabel(tr("Rotation"), row));
    rotation_ = new QSpinBox(row);
    rotation_->setObjectName(QStringLiteral("palette_rotation"));
    rotation_->setRange(-180, 180);
    rotation_->setSuffix(tr("°"));
    rotation_->setToolTip(tr("Degrees. Scroll to change it; hold Shift to snap "
                             "to the next multiple of 15."));
    rotation_->installEventFilter(this);
    connect(rotation_, &QSpinBox::valueChanged, this, [this](int value) {
        overlay::TextItem* text = current_text();
        if (text == nullptr) return;
        text->rotation = value;
        edited();
    });
    layout->addWidget(rotation_);
    menu->installEventFilter(this);
    return menu;
}

QMenu* TextPaletteMenu::build_layers() {
    auto* row = new QWidget(this);
    auto* menu = row_menu(this, tr("&Layers"),
                          QStringLiteral("palette_layers"), row);
    auto* layout = row_layout(row);

    layer_up_ = new QPushButton(row);
    layer_up_->setObjectName(QStringLiteral("palette_layer_up"));
    layer_down_ = new QPushButton(row);
    layer_down_->setObjectName(QStringLiteral("palette_layer_down"));
    // **No `itemEdited`, and no `refresh_item`.** A reorder is the
    // editor's own mutation: it re-renders and emits `selectionChanged`
    // with the item's new address, which is already what makes the strip
    // box re-read. Adding a second announcement here would refill those
    // widgets twice for one click.
    connect(layer_up_, &QPushButton::clicked, this, [this] {
        if (shifted_) editor_->raise_to_top();
        else editor_->raise_selected();
    });
    connect(layer_down_, &QPushButton::clicked, this, [this] {
        if (shifted_) editor_->lower_to_bottom();
        else editor_->lower_selected();
    });
    layout->addWidget(layer_up_);
    layout->addWidget(layer_down_);

    // Shift changes what these buttons *do*, so it has to change what
    // they say before the click rather than at it. Nothing delivers a
    // modifier change to a widget that has no focus, and a menu's
    // widgets do not take focus, so this is a poll -- cheap, and only
    // while the submenu is open.
    shift_watch_ = new QTimer(this);
    shift_watch_->setObjectName(QStringLiteral("palette_shift_watch"));
    shift_watch_->setInterval(50);
    connect(shift_watch_, &QTimer::timeout, this,
            [this] { set_layer_modifiers(QApplication::keyboardModifiers()); });
    connect(menu, &QMenu::aboutToShow, this, [this] {
        set_layer_modifiers(QApplication::keyboardModifiers());
        shift_watch_->start();
    });
    connect(menu, &QMenu::aboutToHide, shift_watch_, &QTimer::stop);
    menu->installEventFilter(this);
    return menu;
}

QPushButton* TextPaletteMenu::color_well(QWidget* parent, const QString& name,
                                         const QString& tip) {
    auto* well = new QPushButton(parent);
    well->setObjectName(name);
    well->setToolTip(tip);
    // Its swatch now, before anything is selected: a button grows when
    // it is first handed an icon, and a control that changes size when
    // the operator selects something is a layout that moves under them.
    // An invalid colour paints nothing.
    style::set_color_swatch(well, QColor());
    return well;
}

// --- editing -----------------------------------------------------------------

overlay::Item* TextPaletteMenu::current_item() {
    // Null while the controls are being filled from an item, so their
    // change signals do not write the value straight back -- the same
    // guard, for the same reason, as `TransmitPanel::editing_item`.
    if (loading_) return nullptr;
    return editor_->selected_item();
}

overlay::TextItem* TextPaletteMenu::current_text() {
    overlay::Item* item = current_item();
    if (item == nullptr) return nullptr;
    return std::get_if<overlay::TextItem>(item);
}

void TextPaletteMenu::edited() {
    editor_->refresh_item();
    emit itemEdited();
}

void TextPaletteMenu::load_from(const overlay::TextItem& text) {
    loading_ = true;
    bold_->setChecked(text.bold);
    italic_->setChecked(text.italic);
    underline_->setChecked(text.underline);
    family_->setText(family_label(text.font_family));
    size_->setValue(units::size_px(text));

    style::set_color_swatch(fill_from_, QColor(QString::fromStdString(text.color)));
    style::set_color_swatch(fill_to_, QColor(QString::fromStdString(text.color2)));
    update_fill_mode_button(QString::fromStdString(text.fill_mode));

    const bool stroked = text.stroke_width > 0.0;
    stroke_on_->setChecked(stroked);
    stroke_width_->setValue(static_cast<int>(std::lround(text.stroke_width * 100.0)));
    stroke_width_->setEnabled(stroked);
    // So the toggle restores what this item had rather than the default.
    if (stroked) stroke_restore_ = text.stroke_width;
    style::set_color_swatch(stroke_color_,
                            QColor(QString::fromStdString(text.stroke_color)));

    rotation_->setValue(static_cast<int>(std::lround(text.rotation)));
    loading_ = false;
}

void TextPaletteMenu::update_fill_mode_button(const QString& mode) {
    if (mode == QLatin1String("linear")) {
        fill_mode_->setText(tr("LINEAR"));
    } else if (mode == QLatin1String("radial")) {
        fill_mode_->setText(tr("RADIAL"));
    } else {
        fill_mode_->setText(tr("SOLID"));
    }
}

void TextPaletteMenu::update_layer_labels() {
    layer_up_->setText(shifted_ ? tr("Bring to front") : tr("Bring forward"));
    layer_down_->setText(shifted_ ? tr("Send to back") : tr("Send backward"));
}

void TextPaletteMenu::set_layer_modifiers(Qt::KeyboardModifiers modifiers) {
    const bool shifted = (modifiers & Qt::ShiftModifier) != Qt::NoModifier;
    if (shifted == shifted_) return;
    shifted_ = shifted;
    update_layer_labels();
}

void TextPaletteMenu::pick_color(QAbstractButton* well,
                                 std::string overlay::TextItem::* field) {
    overlay::TextItem* text = current_text();
    if (text == nullptr) return;
    const QColor start(QString::fromStdString(text->*field));

    // **Close the palette first.** The dialog is modal and this menu
    // holds a mouse grab; releasing it here rather than relying on the
    // style to do it keeps the behaviour the same on all three
    // platforms. The dialog then runs its own event loop, which is why
    // the item is resolved again below rather than held across it.
    hide();
    const QColor chosen = QColorDialog::getColor(start, parentWidget());
    if (!chosen.isValid()) return;
    text = current_text();
    if (text == nullptr) return;
    text->*field = chosen.name().toStdString();
    style::set_color_swatch(well, chosen);
    edited();
}

void TextPaletteMenu::popup_for(overlay::Item* item, const QPoint& global_pos) {
    // The editor selects what was right-clicked before it emits, so this
    // holds by construction. Refused rather than guessed at when it does
    // not: editing an item other than the selected one is silently
    // wrong, and silently wrong is the worst thing available here.
    if (item == nullptr || item != editor_->selected_item()) return;

    auto* text = std::get_if<overlay::TextItem>(item);
    // An image inset gets Layers only -- ordering is the one thing on
    // here that means anything for a picture, and opening nothing at all
    // reads as a broken right-click.
    format_menu_->menuAction()->setVisible(text != nullptr);
    style_menu_->menuAction()->setVisible(text != nullptr);
    if (text != nullptr) load_from(*text);
    popup(global_pos);
}

// --- the wheel ---------------------------------------------------------------

void TextPaletteMenu::step_size(int notches, bool coarse) {
    size_->setValue(size_->value() + notches * (coarse ? 10 : 1));
}

void TextPaletteMenu::step_rotation(int notches, bool coarse) {
    if (!coarse) {
        rotation_->setValue(rotation_->value() + notches);
        return;
    }
    // Shift snaps to the next multiple of 15 in the direction of travel
    // -- a destination rather than a step, so a rotation nudged to 7
    // degrees comes back to 15 or to 0 rather than to 22 or -8.
    const int direction = notches > 0 ? 1 : -1;
    for (int i = 0; i < std::abs(notches); ++i) {
        const double turns = rotation_->value() / 15.0;
        const int next = direction > 0
                             ? static_cast<int>(std::floor(turns)) * 15 + 15
                             : static_cast<int>(std::ceil(turns)) * 15 - 15;
        rotation_->setValue(next);
    }
}

bool TextPaletteMenu::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::Wheel) {
        return QMenu::eventFilter(watched, event);
    }
    auto* wheel = static_cast<QWheelEvent*>(event);

    // A wheel over a submenu arrives at the menu, which would scroll
    // itself; a wheel over a field may arrive at the field or at the
    // line edit inside it. Resolve to a widget, then walk up.
    QWidget* target = qobject_cast<QWidget*>(watched);
    if (auto* menu = qobject_cast<QMenu*>(watched)) {
        target = menu->childAt(wheel->position().toPoint());
    }
    const int notches = notches_of(wheel);
    const bool coarse = (wheel->modifiers() & Qt::ShiftModifier) != Qt::NoModifier;
    for (QWidget* widget = target; widget != nullptr;
         widget = widget->parentWidget()) {
        if (widget == size_) {
            if (notches != 0) step_size(notches, coarse);
            event->accept();
            return true;
        }
        if (widget == rotation_) {
            if (notches != 0) step_rotation(notches, coarse);
            event->accept();
            return true;
        }
        if (widget == stroke_width_) {
            if (notches != 0) {
                stroke_width_->setValue(stroke_width_->value() + notches);
            }
            event->accept();
            return true;
        }
    }

    // Anywhere else on a submenu: swallowed. These menus are one row
    // each and never scrollable, so the only thing a wheel could do here
    // is move the palette out from under the pointer.
    if (qobject_cast<QMenu*>(watched) != nullptr) {
        event->accept();
        return true;
    }
    return QMenu::eventFilter(watched, event);
}

}  // namespace sstvae::gui
