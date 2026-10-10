#include "qrss_page.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "qrss_window.hpp"

namespace sstvae::gui {

QrssPage::QrssPage(QrssWindow* signals_view, QWidget* parent)
    : QWidget(parent), signals_(signals_view) {
    setObjectName(QStringLiteral("qrss_page"));
    auto* box = new QVBoxLayout(this);

    auto* row = new QHBoxLayout;
    listen_ = new QPushButton(this);
    listen_->setObjectName(QStringLiteral("qrss_listen"));
    listen_->setToolTip(tr("Starts the radio's audio and the QRSS listener. The audio also "
                           "feeds the SSTVAE decoder, so this is the same switch as Start "
                           "receiving on the SSTVAE tab."));
    connect(listen_, &QPushButton::clicked, this, [this] { emit listenRequested(!listening_); });
    row->addWidget(listen_);
    listen_state_ = new QLabel(this);
    listen_state_->setObjectName(QStringLiteral("qrss_listen_state"));
    row->addWidget(listen_state_, 1);
    schedule_summary_ = new QLabel(this);
    schedule_summary_->setObjectName(QStringLiteral("qrss_schedule_summary"));
    schedule_summary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(schedule_summary_, 1);
    auto* schedule = new QPushButton(tr("Sched&ule..."), this);
    schedule->setObjectName(QStringLiteral("qrss_schedule_button"));
    schedule->setToolTip(tr("Prepare pictures and schedule QRSS sends: once, back to back, "
                            "every few hours."));
    connect(schedule, &QPushButton::clicked, this, &QrssPage::scheduleRequested);
    row->addWidget(schedule);
    box->addLayout(row);

    if (signals_ != nullptr) {
        signals_->setParent(this, Qt::Widget);
        box->addWidget(signals_, 1);
        signals_->show();
    }
    set_listening(false);
}

void QrssPage::set_listening(bool on) {
    listening_ = on;
    listen_->setText(on ? tr("&Stop receiving") : tr("&Start receiving"));
    listen_state_->setText(on ? tr("Receiving: the QRSS listener hears the radio.")
                              : tr("Not receiving: start it to hear QRSS signals."));
}

void QrssPage::set_schedule_summary(const QString& text) { schedule_summary_->setText(text); }

}  // namespace sstvae::gui
