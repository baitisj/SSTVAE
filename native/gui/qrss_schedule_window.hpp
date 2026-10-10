// The QRSS schedule window: add a picture to send later, again and again
// (gui/qrss_schedule.hpp), and see what is coming up. Opened by the
// "Schedule..." button beside Send.
//
// Times are quarter hours in UTC, as everywhere else in QRSS, with the
// local time beside each so "alternating hours" can be planned against
// the operator's own clock. The picture is a snapshot: the composition
// as it is when "Use the composition" is pressed (or when the window
// opens), or a file framed the way a loaded picture is. What is shown is
// what goes out, every time, which is what gives every send of an entry
// the same picture ID.

#ifndef SSTVAE_GUI_QRSS_SCHEDULE_WINDOW_HPP
#define SSTVAE_GUI_QRSS_SCHEDULE_WINDOW_HPP

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

#include "images/types.hpp"

class QComboBox;
class QDateEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;

namespace sstvae::gui {

class QrssScheduler;

class QrssScheduleWindow : public QWidget {
    Q_OBJECT

public:
    using Composition = std::function<std::optional<images::Picture>()>;
    struct Defaults {
        std::string mode = "A";
        double freq_hz = 1500.0;
    };

    QrssScheduleWindow(QrssScheduler* scheduler, Composition composition,
                       std::function<Defaults()> defaults, QWidget* parent = nullptr);

    // The form's current entry, as Add would add it (for tests).
    double chosen_slot() const;
    // Take a fresh snapshot of the composition into the form.
    void use_composition();
    void use_file(const QString& path);

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void build();
    void update_summary();
    void fill_times();
    void add();
    void set_selected_enabled(bool on);
    void remove_selected();
    QString local_and_utc(double slot) const;

    QrssScheduler* scheduler_ = nullptr;
    Composition composition_;
    std::function<Defaults()> defaults_;

    std::optional<images::Picture> picture_;
    QString picture_label_;

    QLabel* thumb_ = nullptr;
    QLabel* picture_name_ = nullptr;
    QComboBox* mode_ = nullptr;
    QSpinBox* freq_ = nullptr;
    QDateEdit* date_ = nullptr;
    QComboBox* time_ = nullptr;
    QSpinBox* count_ = nullptr;
    QComboBox* every_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* add_ = nullptr;
    QTreeWidget* table_ = nullptr;
    QPushButton* pause_ = nullptr;
    QPushButton* resume_ = nullptr;
    QPushButton* remove_ = nullptr;
    QListWidget* coming_ = nullptr;
    QTimer* clock_timer_ = nullptr;
};

}  // namespace sstvae::gui

#endif
