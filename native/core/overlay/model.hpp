// The overlay document: what to draw on top of a picture.
//
// The document only -- rendering lives with the editor in Phase 3. Two
// design choices carry over from `sstvae/overlay/model.py`, and both
// exist so that *templates* (saving an overlay and reapplying it to
// tomorrow's picture) stay a UI-only change rather than a redesign:
//
// **Coordinates are normalized** to 0..1 of the canvas and sizes are
// fractions of it, so a document is resolution-independent.
//
// **Image insets are late-bound references, not pasted bitmaps.**
// `ImageItem::source` is "last_rx" or a path, resolved at render time.
// A template saying "inset the most recent received picture, bottom
// left" therefore still means that next week -- which is the entire
// point, and impossible if the editor flattened the bitmap in at
// composition time.

#ifndef SSTVAE_OVERLAY_MODEL_HPP
#define SSTVAE_OVERLAY_MODEL_HPP

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "images/types.hpp"

namespace sstvae::overlay {

// The overlay's coordinate space is the transmitted frame itself, so
// what the editor shows is what goes on the air.
inline constexpr int CANVAS_W = images::IMG_W;
inline constexpr int CANVAS_H = images::IMG_H;

// **2 since the editor grew a style palette**: weight, slant, underline,
// a font family and a gradient fill. The bump is deliberate rather than
// additive-and-silent. `from_json` refuses a document newer than the
// build understands, so an older build meeting one of these says so
// instead of quietly drawing a gradient as a flat colour -- which is the
// failure a template is least able to survive, because a template is
// opened by builds its author never saw.
inline constexpr int DOC_VERSION = 2;

// Resolved at render time rather than stored, so the reference stays
// meaningful in a saved template.
inline constexpr const char* SOURCE_LAST_RX = "last_rx";

// A run of burned-in text.
//
// `text` may contain newlines: a station's callsign, grid and name are
// one item, not three stacked by hand.
struct TextItem {
    std::string text;
    double x = 0.03;
    double y = 0.03;
    // Cap height as a fraction of canvas height, so text scales with
    // the frame.
    double size = 0.08;
    std::string color = "#ffffff";
    std::string stroke_color = "#000000";
    // Fraction of the glyph size. **Zero is the off switch**, and the
    // only one: the palette's stroke toggle writes 0 here rather than
    // carrying a separate `stroke` flag, because two fields that can
    // disagree about whether there is a stroke is one field too many.
    double stroke_width = 0.12;
    // Font path; empty = the default face.
    std::string font;
    // Which point of the text box (x, y) positions, in PIL's two-letter
    // convention ("la" = left/ascender, "mm" = middle/middle). This is
    // what lets a template pin text to a corner without knowing how
    // long the string will be.
    std::string anchor = "la";
    std::string align = "left";   // between lines, once there is more than one
    double line_spacing = 0.15;   // extra gap, fraction of size
    double rotation = 0.0;        // degrees, counter-clockwise

    // --- style, added in document version 2 --------------------------
    //
    // **Every default here reproduces a version-1 document exactly.**
    // That is not a courtesy to old files; it is what lets `color` go on
    // meaning what it always meant (the fill, and now the *first* stop
    // of a gradient) instead of a v1 document rendering as a surprise.
    bool bold = false;
    bool italic = false;
    bool underline = false;
    // A font *family*, as opposed to `font`, which is a path.
    //
    // The two are not alternatives with a winner picked at random:
    // `font` wins when both are set, because a template that ships its
    // own face is naming the exact file it needs, and a family name is
    // only ever a request the system may answer with something else.
    // Empty means "no request" -- the default face.
    //
    // Either a real family ("DejaVu Sans") or one of the generic
    // keywords "sans-serif", "serif", "monospace", "cursive".
    std::string font_family;
    // "solid", "linear" or "radial". Anything else is treated as solid,
    // so a document from a build that grows a fourth mode still draws.
    std::string fill_mode = "solid";
    // The gradient's far stop. Unread when the fill is solid, but kept
    // in the document regardless: toggling a gradient off and on again
    // in the editor must not lose the colour it was set to.
    std::string color2 = "#38bdf8";
    // Degrees, clockwise from left-to-right. Linear fills only.
    double fill_angle = 45.0;
};

// A picture inset -- typically the last received image, so an operator
// can send back what they just got.
struct ImageItem {
    std::string source = SOURCE_LAST_RX;  // "last_rx" or a filesystem path
    double x = 0.68;
    double y = 0.68;
    double width = 0.28;    // fraction of canvas width; height follows aspect
    double border = 0.004;  // fraction of canvas width; 0 for none
    std::string border_color = "#ffffff";
    double opacity = 1.0;
    double rotation = 0.0;
    std::string anchor = "la";
};

using Item = std::variant<TextItem, ImageItem>;

// An ordered list of items, drawn back to front.
struct Doc {
    std::vector<Item> items;
    int version = DOC_VERSION;

    bool empty() const { return items.empty(); }
};

// What was skipped while loading. Unknown item kinds and unknown fields
// are ignored for forward compatibility, but -- as with settings --
// ignoring quietly makes a hand-edited document's typo invisible.
struct Note {
    std::string where;
    std::string problem;
};

// Parse a document.
//
// Unlike `settings::load`, this *can* fail, and deliberately: a
// document is a file the operator explicitly opened, so malformed JSON
// or a version this build cannot understand is worth reporting rather
// than silently replacing with an empty overlay. Unknown items and
// fields within a readable document are still skipped and noted.
Doc from_json(const std::string& text, std::vector<Note>* notes = nullptr);

std::string to_json(const Doc& doc, int indent = 2);

}  // namespace sstvae::overlay

#endif
