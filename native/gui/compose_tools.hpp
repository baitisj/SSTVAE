// What the Transmit pane's composer and the QRSS Editor share: the tool
// palette's hand-drawn icons, the picture file filter, and where the
// built-in overlay templates live. One copy, so the two windows offer
// the same tools and the same templates.

#ifndef SSTVAE_GUI_COMPOSE_TOOLS_HPP
#define SSTVAE_GUI_COMPOSE_TOOLS_HPP

#include <QIcon>

#include <filesystem>

class QWidget;

namespace sstvae::gui::compose {

extern const char* const IMAGE_FILTER;

QIcon text_tool_icon(const QWidget* metrics);
QIcon image_tool_icon(const QWidget* metrics);
QIcon last_rx_tool_icon(const QWidget* metrics);
QIcon rect_tool_icon(const QWidget* metrics);

// The built-in templates' directory (see the .cpp for where an installed
// app keeps them).
std::filesystem::path builtin_templates_dir();

}  // namespace sstvae::gui::compose

#endif
