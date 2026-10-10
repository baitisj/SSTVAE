#include "qrss_schedule_window.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDateEdit>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimeZone>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include "crop_dialog.hpp"
#include "images/images.hpp"
#include "qrss_schedule.hpp"
#include "qrss_tx.hpp"
#include "style.hpp"

namespace sstvae::gui {

namespace qs = qrss_schedule;

namespace {

constexpr int THUMB_W = 160;
constexpr int THUMB_H = 120;
constexpr double DAY_S = 86400.0;

QDateTime utc_of(double t) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(t)), QTimeZone::utc());
}

QString every_label(int minutes) {
    if (minutes == 0) return QObject::tr("Back to back");
    if (minutes == 120) return QObject::tr("2 hours (alternating hours)");
    if (minutes % 1440 == 0) {
        return minutes == 1440 ? QObject::tr("1 day") : QObject::tr("%1 days").arg(minutes / 1440);
    }
    if (minutes % 60 == 0) {
        return minutes == 60 ? QObject::tr("1 hour") : QObject::tr("%1 hours").arg(minutes / 60);
    }
    return QObject::tr("%1 hours").arg(minutes / 60.0, 0, 'g', 3);
}

QString pass_word(int n) { return n == 1 ? QObject::tr("pass") : QObject::tr("passes"); }

QString duration_text(int minutes) {
    if (minutes < 60) return QObject::tr("%1 minutes").arg(minutes);
    if (minutes % 60 == 0) {
        return minutes == 60 ? QObject::tr("1 hour") : QObject::tr("%1 hours").arg(minutes / 60);
    }
    return QObject::tr("%1 hours").arg(minutes / 60.0, 0, 'g', 3);
}

}  // namespace

QrssScheduleWindow::QrssScheduleWindow(QrssScheduler* scheduler, Composition composition,
                                       std::function<Defaults()> defaults, QWidget* parent)
    : QWidget(parent, Qt::Window), scheduler_(scheduler),
      composition_(std::move(composition)), defaults_(std::move(defaults)) {
    setObjectName(QStringLiteral("qrss_schedule_window"));
    setWindowTitle(tr("QRSS schedule"));
    build();
    const Defaults d = defaults_ ? defaults_() : Defaults{};
    mode_->setCurrentIndex(std::max(0, mode_->findData(QString::fromStdString(d.mode))));
    freq_->setValue(static_cast<int>(std::lround(d.freq_hz)));
    const double first = qs::earliest_slot(scheduler_->now());
    const QDateTime t = utc_of(first);
    date_->setMinimumDate(utc_of(scheduler_->now()).date());
    date_->setDate(t.date());
    fill_times();
    time_->setCurrentIndex(time_->findData(t.time().msecsSinceStartOfDay() / 1000));
    connect(scheduler_, &QrssScheduler::changed, this, &QrssScheduleWindow::refresh);
    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(30000);
    connect(clock_timer_, &QTimer::timeout, this, [this] {
        refresh();
        update_summary();
    });
    clock_timer_->start();
    refresh();
    update_summary();
    resize(760, 780);
}

void QrssScheduleWindow::build() {
    auto* outer = new QVBoxLayout(this);

    // --- adding a send ---------------------------------------------------
    auto* add_box = new QGroupBox(tr("Add a send"), this);
    auto* form = new QFormLayout(add_box);

    thumb_ = new QLabel(add_box);
    thumb_->setObjectName(QStringLiteral("schedule_thumb"));
    thumb_->setFixedSize(THUMB_W, THUMB_H);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setFrameShape(QFrame::StyledPanel);
    picture_name_ = new QLabel(add_box);
    picture_name_->setObjectName(QStringLiteral("schedule_picture_name"));
    picture_name_->setWordWrap(true);
    auto* use_comp = new QPushButton(tr("Use the composition"), add_box);
    use_comp->setObjectName(QStringLiteral("schedule_use_composition"));
    use_comp->setToolTip(tr("Snapshot the picture on the Transmit pane, overlay and all. "
                            "Later edits there do not change a scheduled send."));
    connect(use_comp, &QPushButton::clicked, this, &QrssScheduleWindow::use_composition);
    auto* open_file = new QPushButton(tr("Open file..."), add_box);
    connect(open_file, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Picture to schedule"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All files (*)"));
        if (!path.isEmpty()) use_file(path);
    });
    framing_button_ = new QPushButton(tr("Framing..."), add_box);
    framing_button_->setObjectName(QStringLiteral("schedule_framing"));
    framing_button_->setToolTip(tr("Choose which part of the file goes on the air"));
    framing_button_->setEnabled(false);
    connect(framing_button_, &QPushButton::clicked, this, &QrssScheduleWindow::choose_framing);
    auto* file_row = new QHBoxLayout;
    file_row->addWidget(open_file);
    file_row->addWidget(framing_button_);
    auto* pic_side = new QVBoxLayout;
    pic_side->addWidget(picture_name_);
    pic_side->addWidget(use_comp);
    pic_side->addLayout(file_row);
    pic_side->addStretch(1);
    auto* pic_row = new QHBoxLayout;
    pic_row->addWidget(thumb_);
    pic_row->addLayout(pic_side, 1);
    form->addRow(tr("Picture:"), pic_row);

    mode_ = new QComboBox(add_box);
    mode_->setObjectName(QStringLiteral("schedule_mode"));
    for (const char* m : {"A", "B", "C"}) {
        const std::string s(m);
        mode_->addItem(tr("QRSS CE Mode %1 - %2 min (%3 %4)")
                           .arg(QString::fromStdString(s))
                           .arg(qrss_tx::minutes_for(s))
                           .arg(qrss_tx::passes_for(s))
                           .arg(pass_word(qrss_tx::passes_for(s))),
                       QString::fromStdString(s));
    }
    form->addRow(tr("Mode:"), mode_);

    freq_ = new QSpinBox(add_box);
    freq_->setObjectName(QStringLiteral("schedule_freq"));
    freq_->setRange(static_cast<int>(qrss_tx::FREQ_MIN_HZ), static_cast<int>(qrss_tx::FREQ_MAX_HZ));
    freq_->setSingleStep(10);
    freq_->setSuffix(tr(" Hz"));
    freq_->setToolTip(tr("The audio frequency of the carrier, 300-2700 Hz, as on the "
                         "QRSS carrier slider."));
    form->addRow(tr("Carrier:"), freq_);

    date_ = new QDateEdit(add_box);
    date_->setObjectName(QStringLiteral("schedule_date"));
    date_->setCalendarPopup(true);
    date_->setDisplayFormat(QStringLiteral("ddd d MMM yyyy"));
    date_->setToolTip(tr("The UTC date of the first send."));
    time_ = new QComboBox(add_box);
    time_->setObjectName(QStringLiteral("schedule_time"));
    time_->setToolTip(tr("Sends start on a quarter hour, UTC (Z); your local time is in "
                         "brackets."));
    auto* when_row = new QHBoxLayout;
    when_row->addWidget(date_);
    when_row->addWidget(time_, 1);
    form->addRow(tr("First send (UTC):"), when_row);

    count_ = new QSpinBox(add_box);
    count_->setObjectName(QStringLiteral("schedule_count"));
    count_->setRange(0, 999);
    count_->setSpecialValueText(tr("Until removed"));
    count_->setValue(2);
    count_->setToolTip(tr("How many times to send the picture. Every send is the same "
                          "picture ID, so receivers add them together."));
    every_ = new QComboBox(add_box);
    every_->setObjectName(QStringLiteral("schedule_every"));
    for (int m : qs::EVERY_CHOICES_MIN) every_->addItem(every_label(m), m);
    every_->setToolTip(tr("From the start of one send to the start of the next. Back to "
                          "back starts the next send the half hour after the last pass "
                          "of the one before."));
    auto* repeat_row = new QHBoxLayout;
    repeat_row->addWidget(count_);
    repeat_row->addWidget(new QLabel(tr("sends, every"), add_box));
    repeat_row->addWidget(every_, 1);
    form->addRow(tr("Repeat:"), repeat_row);

    summary_ = new QLabel(add_box);
    summary_->setObjectName(QStringLiteral("schedule_summary"));
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(summary_);
    add_ = new QPushButton(tr("&Add to schedule"), add_box);
    add_->setObjectName(QStringLiteral("schedule_add"));
    connect(add_, &QPushButton::clicked, this, &QrssScheduleWindow::add);
    form->addRow(style::row(add_box, {add_}));
    outer->addWidget(add_box);

    for (QComboBox* c : {mode_, time_, every_}) {
        connect(c, &QComboBox::currentIndexChanged, this, &QrssScheduleWindow::update_summary);
    }
    for (QSpinBox* s : {freq_, count_}) {
        connect(s, &QSpinBox::valueChanged, this, &QrssScheduleWindow::update_summary);
    }
    connect(date_, &QDateEdit::dateChanged, this, [this] {
        fill_times();
        update_summary();
    });

    // --- what is scheduled ------------------------------------------------
    auto* list_box = new QGroupBox(tr("Scheduled"), this);
    auto* list_layout = new QVBoxLayout(list_box);
    table_ = new QTreeWidget(list_box);
    table_->setObjectName(QStringLiteral("schedule_table"));
    table_->setRootIsDecorated(false);
    table_->setIconSize(QSize(48, 36));
    table_->setHeaderLabels({tr("Picture"), tr("Repeats"), tr("Carrier"), tr("Next send"),
                             tr("Status")});
    table_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->header()->setStretchLastSection(true);
    table_->setMinimumHeight(table_->fontMetrics().height() * 8);
    list_layout->addWidget(table_);
    pause_ = new QPushButton(tr("Pause"), list_box);
    pause_->setObjectName(QStringLiteral("schedule_pause"));
    resume_ = new QPushButton(tr("Resume"), list_box);
    resume_->setObjectName(QStringLiteral("schedule_resume"));
    remove_ = new QPushButton(tr("Remove"), list_box);
    remove_->setObjectName(QStringLiteral("schedule_remove"));
    remove_->setToolTip(tr("Take it off the schedule. A send already on the air carries on; "
                           "Cancel on the Transmit pane stops it."));
    connect(pause_, &QPushButton::clicked, this, [this] { set_selected_enabled(false); });
    connect(resume_, &QPushButton::clicked, this, [this] { set_selected_enabled(true); });
    connect(remove_, &QPushButton::clicked, this, &QrssScheduleWindow::remove_selected);
    connect(table_, &QTreeWidget::itemSelectionChanged, this, &QrssScheduleWindow::refresh);
    list_layout->addWidget(style::row(list_box, {pause_, resume_, remove_}));
    outer->addWidget(list_box, 1);

    auto* coming_box = new QGroupBox(tr("Coming up"), this);
    auto* coming_layout = new QVBoxLayout(coming_box);
    coming_ = new QListWidget(coming_box);
    coming_->setObjectName(QStringLiteral("schedule_coming"));
    coming_layout->addWidget(coming_);
    outer->addWidget(coming_box, 1);

    outer->addWidget(style::note(
        tr("Scheduled sends go out only while this app is running, through the same radio, "
           "level and callsign as Send. Receiving pauses from a few minutes before each send "
           "until it ends."),
        this));
}

void QrssScheduleWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // The composition the operator is looking at, unless a file was chosen.
    if (!picture_ || !file_source_) use_composition();
    refresh();
    update_summary();
}

void QrssScheduleWindow::use_composition() {
    std::optional<images::Picture> p = composition_ ? composition_() : std::nullopt;
    file_source_.reset();
    file_path_.clear();
    framing_button_->setEnabled(false);
    if (!p) {
        picture_.reset();
        picture_label_.clear();
        thumb_->setPixmap(QPixmap());
        thumb_->setText(tr("No picture"));
        picture_name_->setText(tr("The Transmit pane has no picture yet: choose one there, "
                                  "or open a file."));
        update_summary();
        return;
    }
    picture_ = std::move(p);
    picture_label_ = tr("composition %1").arg(utc_of(scheduler_->now()).toString(
                         QStringLiteral("d MMM HH:mm'Z'")));
    thumb_->setPixmap(style::to_pixmap(*picture_).scaled(THUMB_W, THUMB_H, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));
    picture_name_->setText(tr("The composition on the Transmit pane, as it is now."));
    update_summary();
}

void QrssScheduleWindow::use_file(const QString& path) {
    images::Picture loaded;
    try {
        loaded = images::load(path.toStdString());
    } catch (const std::exception& e) {
        picture_name_->setText(tr("Could not open %1: %2").arg(path, QString::fromUtf8(e.what())));
        return;
    }
    file_source_ = std::move(loaded);
    file_path_ = path;
    framing_ = images::Framing{};
    // The Transmit pane asks only when the picture is not 4:3; here any
    // size but the one sent asks, so a scheduled picture is never
    // rescaled or cropped without being seen first. Cancel keeps the
    // default framing (the centre, full width), as there.
    if (file_source_->width != images::IMG_W || file_source_->height != images::IMG_H) {
        CropDialog dialog(*file_source_, framing_, this);
        if (dialog.exec() == QDialog::Accepted) framing_ = dialog.framing();
    }
    apply_framing();
}

void QrssScheduleWindow::choose_framing() {
    if (!file_source_) return;
    CropDialog dialog(*file_source_, framing_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    framing_ = dialog.framing();
    apply_framing();
}

void QrssScheduleWindow::apply_framing() {
    if (!file_source_) return;
    try {
        picture_ = images::fit(*file_source_, framing_);
    } catch (const std::exception& e) {
        picture_.reset();
        picture_name_->setText(tr("Could not frame %1: %2")
                                   .arg(QFileInfo(file_path_).fileName(),
                                        QString::fromUtf8(e.what())));
        update_summary();
        return;
    }
    picture_label_ = QFileInfo(file_path_).fileName();
    thumb_->setPixmap(style::to_pixmap(*picture_).scaled(THUMB_W, THUMB_H, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));
    // The same caption the Transmit pane gives a loaded picture: what
    // was done to it to make 640 x 480.
    const images::Picture& src = *file_source_;
    QString caption = tr("%1, %2x%3").arg(picture_label_).arg(src.width).arg(src.height);
    const bool four_by_three = src.width * images::IMG_H == src.height * images::IMG_W;
    if (framing_.zoom < 1.0) {
        caption += tr(", padded to 4:3");
    } else if (!four_by_three || framing_.zoom > 1.0) {
        caption += tr(", cropped to 4:3");
    }
    if (src.width < images::MIN_W || src.height < images::MIN_H) {
        caption += tr(" (small, so upscaled)");
    }
    picture_name_->setText(caption);
    framing_button_->setEnabled(true);
    update_summary();
}

QString QrssScheduleWindow::local_and_utc(double slot) const {
    const QDateTime local = utc_of(slot).toLocalTime();
    return tr("%1 (%2 local)")
        .arg(QString::fromStdString(qs::when(slot, scheduler_->now())),
             local.toString(QStringLiteral("HH:mm")));
}

void QrssScheduleWindow::fill_times() {
    const QVariant keep = time_->currentData();
    const QSignalBlocker block(time_);
    time_->clear();
    const QDateTime midnight(date_->date(), QTime(0, 0), QTimeZone::utc());
    for (int q = 0; q < 96; ++q) {
        const QDateTime t = midnight.addSecs(q * 900);
        time_->addItem(tr("%1Z (%2 local)")
                           .arg(t.toString(QStringLiteral("HH:mm")),
                                t.toLocalTime().toString(QStringLiteral("HH:mm"))),
                       q * 900);
    }
    time_->setCurrentIndex(keep.isValid() ? std::max(0, time_->findData(keep)) : 0);
}

double QrssScheduleWindow::chosen_slot() const {
    const QDateTime midnight(date_->date(), QTime(0, 0), QTimeZone::utc());
    return static_cast<double>(midnight.toSecsSinceEpoch()) + time_->currentData().toInt();
}

namespace {

qs::Entry form_entry(const QComboBox* mode, const QSpinBox* freq, double slot,
                     const QSpinBox* count, const QComboBox* every) {
    qs::Entry e;
    e.mode = mode->currentData().toString().toStdString();
    e.freq_hz = freq->value();
    e.first_slot = slot;
    e.count = count->value();
    e.every_min = every->currentData().toInt();
    return e;
}

}  // namespace

void QrssScheduleWindow::update_summary() {
    const qs::Entry e = form_entry(mode_, freq_, chosen_slot(), count_, every_);
    const double now = scheduler_->now();
    QString why;
    if (!picture_) why = tr("choose a picture first.");
    if (why.isEmpty()) why = QString::fromStdString(qs::problem(e));
    if (why.isEmpty() && e.first_slot < qs::earliest_slot(now)) {
        why = tr("that is too soon: the earliest a send added now can start is %1.")
                  .arg(local_and_utc(qs::earliest_slot(now)));
    }
    if (why.isEmpty()) {
        if (const auto c = qs::clash(e, scheduler_->entries(), now)) {
            why = tr("the %1 send would overlap the %2 send of \"%3\": the transmitter sends "
                     "one thing at a time.")
                      .arg(QString::fromStdString(qs::when(c->slot, now)),
                           QString::fromStdString(qs::when(c->other_slot, now)),
                           QString::fromStdString(c->other));
        }
    }
    add_->setEnabled(why.isEmpty());
    if (!why.isEmpty()) {
        summary_->setText(tr("Can't add yet: %1").arg(why));
        return;
    }
    const int n = qrss_tx::passes_for(e.mode);
    QStringList times;
    const int shown = e.count == 0 ? 3 : std::min(e.count, 4);
    for (int k = 0; k < shown; ++k) times << local_and_utc(qs::send_slot(e, k));
    QString when = times.join(tr(", "));
    if (e.count == 0 || e.count > shown) when += tr(", ...");
    const int sends = e.count;
    QString total;
    if (sends > 0) {
        total = tr(" In all: %1 %2, %3 on the air.")
                    .arg(sends * n)
                    .arg(pass_word(sends * n))
                    .arg(duration_text(sends * n * 30));
    }
    QString text = tr("%1. %2 at %3.%4")
                       .arg(QString::fromStdString(qs::describe(e)),
                            e.count == 1 ? tr("Sends") : tr("Sends start"), when, total);
    if (e.count != 1) {
        text += tr(" Every send has the same picture ID, so receivers add them up.");
    }
    summary_->setText(text.left(1).toUpper() + text.mid(1));
}

void QrssScheduleWindow::add() {
    if (!picture_) return;
    qs::Entry e = form_entry(mode_, freq_, chosen_slot(), count_, every_);
    e.label = picture_label_.toStdString();
    const std::string why = scheduler_->add(e, *picture_);
    if (!why.empty()) {
        summary_->setText(tr("Can't add: %1").arg(QString::fromStdString(why)));
        return;
    }
    summary_->setText(tr("Added \"%1\": %2.")
                          .arg(picture_label_, QString::fromStdString(qs::describe(e))));
    add_->setEnabled(false);
}

void QrssScheduleWindow::set_selected_enabled(bool on) {
    const QList<QTreeWidgetItem*> sel = table_->selectedItems();
    if (sel.isEmpty()) return;
    const std::string why =
        scheduler_->set_enabled(sel.front()->data(0, Qt::UserRole).toString().toStdString(), on);
    if (!why.empty()) summary_->setText(tr("Can't resume: %1").arg(QString::fromStdString(why)));
}

void QrssScheduleWindow::remove_selected() {
    const QList<QTreeWidgetItem*> sel = table_->selectedItems();
    if (sel.isEmpty()) return;
    scheduler_->remove(sel.front()->data(0, Qt::UserRole).toString().toStdString());
}

void QrssScheduleWindow::refresh() {
    const double now = scheduler_->now();
    const auto& entries = scheduler_->entries();
    QString selected;
    if (!table_->selectedItems().isEmpty()) {
        selected = table_->selectedItems().front()->data(0, Qt::UserRole).toString();
    }
    {
        const QSignalBlocker block(table_);
        table_->clear();
        for (const qs::Entry& e : entries) {
            auto* item = new QTreeWidgetItem(table_);
            const QString id = QString::fromStdString(e.id);
            item->setData(0, Qt::UserRole, id);
            item->setText(0, QString::fromStdString(e.label));
            const QPixmap pix(QString::fromStdString(e.picture));
            if (!pix.isNull()) {
                item->setIcon(0, QIcon(pix.scaled(48, 36, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation)));
            }
            item->setText(1, QString::fromStdString(qs::describe(e)));
            item->setText(2, tr("%1 Hz").arg(e.freq_hz, 0, 'f', 0));
            item->setText(3, qs::finished(e) ? tr("none") : local_and_utc(qs::send_slot(e, e.next)));
            QString status;
            if (scheduler_->active() == e.id) {
                status = tr("Sending");
            } else if (qs::finished(e)) {
                status = tr("Done");
            } else if (!e.enabled) {
                status = tr("Paused");
            } else {
                status = e.next == 0   ? tr("Scheduled")
                         : e.count > 0 ? tr("%1 of %2 done").arg(e.next).arg(e.count)
                                       : tr("%1 done").arg(e.next);
            }
            item->setText(4, status);
            if (id == selected) item->setSelected(true);
        }
    }
    const QList<QTreeWidgetItem*> sel = table_->selectedItems();
    const qs::Entry* chosen = nullptr;
    if (!sel.isEmpty()) {
        const std::string id = sel.front()->data(0, Qt::UserRole).toString().toStdString();
        for (const qs::Entry& e : entries) {
            if (e.id == id) chosen = &e;
        }
    }
    pause_->setEnabled(chosen != nullptr && chosen->enabled && !qs::finished(*chosen));
    resume_->setEnabled(chosen != nullptr && !chosen->enabled && !qs::finished(*chosen));
    remove_->setEnabled(chosen != nullptr);

    coming_->clear();
    for (const qs::Upcoming& u : qs::upcoming(entries, now, 7 * DAY_S, 12)) {
        const qs::Entry& e = entries[u.entry];
        const QString which = e.count == 1 ? QString()
                              : e.count == 0
                                  ? tr(", send %1").arg(u.send + 1)
                                  : tr(", send %1 of %2").arg(u.send + 1).arg(e.count);
        coming_->addItem(tr("%1: \"%2\", mode %3 at %4 Hz%5")
                             .arg(local_and_utc(u.slot), QString::fromStdString(e.label),
                                  QString::fromStdString(e.mode))
                             .arg(e.freq_hz, 0, 'f', 0)
                             .arg(which));
    }
    if (coming_->count() == 0) coming_->addItem(tr("Nothing scheduled in the next week."));
}

}  // namespace sstvae::gui
