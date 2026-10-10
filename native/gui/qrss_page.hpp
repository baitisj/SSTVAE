// The main window's QRSSTVAE tab: the QRSS signals (gui/qrss_window.hpp)
// with a row above them for the two things that tab is used for besides
// watching tiles -- receiving audio at all, and the transmit schedule.
//
// Receiving is the receive pane's: one sound-card stream feeds both the
// SSTVAE decoder and the QRSS listener, so Start here and Start on the
// SSTVAE tab are one switch, shown the same in both places. The page only
// asks for it (`listenRequested`) and shows what it is told
// (`set_listening`), so it holds no audio of its own and a test can drive
// it without a sound card.

#ifndef SSTVAE_GUI_QRSS_PAGE_HPP
#define SSTVAE_GUI_QRSS_PAGE_HPP

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

namespace sstvae::gui {

class QrssWindow;

class QrssPage : public QWidget {
    Q_OBJECT

public:
    // Takes `signals_` into its layout.
    explicit QrssPage(QrssWindow* signals_view, QWidget* parent = nullptr);

    QrssWindow* signals_view() const { return signals_; }

public slots:
    void set_listening(bool on);
    void set_schedule_summary(const QString& text);

signals:
    // Start (true) or stop (false) receiving.
    void listenRequested(bool on);
    void scheduleRequested();

private:
    QrssWindow* signals_ = nullptr;
    QPushButton* listen_ = nullptr;
    QLabel* listen_state_ = nullptr;
    QLabel* schedule_summary_ = nullptr;
    bool listening_ = false;
};

}  // namespace sstvae::gui

#endif
