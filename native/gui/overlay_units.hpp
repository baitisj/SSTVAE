// How a document size is shown to the operator.
//
// The document stores every size as a *fraction* of the canvas. That is
// what makes a saved overlay mean the same thing at any resolution, and
// it is not a number anyone composing a picture thinks in: "0.08" is a
// cap height of 38 px on the frame that goes on the air. Both surfaces
// that offer a size control -- the strip's "Selected item" box and the
// right-click palette -- show pixels of the 640x480 transmitted frame.
//
// **One conversion, here.** Two copies can round differently, and the
// symptom is a size that changes by a pixel when you look at it in the
// other control -- which then writes that pixel back into the document.
//
// **The axis is not the same for both item kinds, and that is not
// cosmetic.** A text item's `size` is cap height as a fraction of the
// canvas *height*; an image inset's `width` is a fraction of its
// *width*. Converting an inset against 480 gives a number that is
// wrong by the aspect ratio and looks entirely plausible.

#ifndef SSTVAE_GUI_OVERLAY_UNITS_HPP
#define SSTVAE_GUI_OVERLAY_UNITS_HPP

#include <algorithm>
#include <cmath>
#include <variant>

#include "overlay/model.hpp"

namespace sstvae::gui::units {

// The editor's own drag clamps (`OverlayEditor::mouseMoveEvent`), so a
// size reachable by dragging a corner is typeable in the box and the
// other way round. Kept as the fractions they are, and converted below.
inline constexpr double TEXT_SIZE_MIN = 0.01;
inline constexpr double TEXT_SIZE_MAX = 1.5;
inline constexpr double IMAGE_WIDTH_MIN = 0.02;
inline constexpr double IMAGE_WIDTH_MAX = 2.0;

struct Range {
    int min;
    int max;
};

// Rounded *inward*, so neither end of the range is a value the document
// would refuse: 0.01 of 480 is 4.8 px, and offering 4 would write back
// a size below the clamp.
inline Range size_range(const overlay::Item& item) {
    if (std::holds_alternative<overlay::TextItem>(item)) {
        return Range{static_cast<int>(std::ceil(TEXT_SIZE_MIN * overlay::CANVAS_H)),
                     static_cast<int>(std::floor(TEXT_SIZE_MAX * overlay::CANVAS_H))};
    }
    return Range{static_cast<int>(std::ceil(IMAGE_WIDTH_MIN * overlay::CANVAS_W)),
                 static_cast<int>(std::floor(IMAGE_WIDTH_MAX * overlay::CANVAS_W))};
}

// The pixel value a control should show for this item's size.
inline int size_px(const overlay::Item& item) {
    if (const auto* text = std::get_if<overlay::TextItem>(&item)) {
        return static_cast<int>(std::lround(text->size * overlay::CANVAS_H));
    }
    return static_cast<int>(
        std::lround(std::get<overlay::ImageItem>(item).width * overlay::CANVAS_W));
}

// Write a pixel value back as the fraction the document holds.
inline void set_size_px(overlay::Item& item, int px) {
    const Range range = size_range(item);
    const double clamped = std::clamp(px, range.min, range.max);
    if (auto* text = std::get_if<overlay::TextItem>(&item)) {
        text->size = clamped / static_cast<double>(overlay::CANVAS_H);
    } else {
        std::get<overlay::ImageItem>(item).width =
            clamped / static_cast<double>(overlay::CANVAS_W);
    }
}

}  // namespace sstvae::gui::units

#endif
