#include "qrss_schedule.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <exception>
#include <system_error>
#include <utility>

#include "images/images.hpp"

namespace sstvae::gui::qrss_schedule {

namespace {

constexpr double WEEK_S = 7 * 86400.0;

// Send k of `e` needs the transmitter over [slot + a, slot + b], where
// slot = first_slot + k * P.
double busy_from(const Entry&) { return qrss_tx::audio_start(0.0) - START_LEAD_S; }
double busy_to(const Entry& e) {
    return qrss_tx::audio_end((qrss_tx::passes_for(e.mode) - 1) * qrss_tx::PASS_SPACING_S);
}

// The sends k of `o` whose busy span overlaps (from, to), clamped to the
// ones still to come: empty when lo > hi.
std::pair<int, int> overlapping(const Entry& o, double from, double to) {
    const double p = period_min(o) * 60.0;
    int lo = static_cast<int>(std::floor((from - o.first_slot - busy_to(o)) / p)) + 1;
    int hi = static_cast<int>(std::ceil((to - o.first_slot - busy_from(o)) / p)) - 1;
    lo = std::max(lo, o.next);
    if (o.count > 0) hi = std::min(hi, o.count - 1);
    return {lo, hi};
}

QDateTime utc(double t) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(t)), QTimeZone::utc());
}

std::string interval(int minutes) {
    if (minutes % 60 != 0) return "every " + std::to_string(minutes) + " minutes";
    const int h = minutes / 60;
    if (h == 1) return "every hour";
    if (h == 2) return "every 2 hours (alternating hours)";
    if (h % 24 == 0) return h == 24 ? "every day" : "every " + std::to_string(h / 24) + " days";
    return "every " + std::to_string(h) + " hours";
}

}  // namespace

int period_min(const Entry& e) {
    return e.every_min > 0 ? e.every_min : 30 * std::max(1, qrss_tx::passes_for(e.mode));
}

double send_slot(const Entry& e, int k) { return e.first_slot + k * period_min(e) * 60.0; }

bool finished(const Entry& e) { return e.count > 0 && e.next >= e.count; }

std::vector<qrss_tx::Pass> passes_of(const Entry& e, int k) {
    std::vector<qrss_tx::Pass> out;
    const double slot = send_slot(e, k);
    for (int j = 0; j < qrss_tx::passes_for(e.mode); ++j) {
        out.push_back({slot + j * qrss_tx::PASS_SPACING_S, j});
    }
    return out;
}

std::pair<double, double> busy(const Entry& e, int k) {
    const double slot = send_slot(e, k);
    return {slot + busy_from(e), slot + busy_to(e)};
}

std::string problem(const Entry& e) {
    const int n = qrss_tx::passes_for(e.mode);
    if (n == 0) return "unknown QRSS mode '" + e.mode + "'";
    if (!(e.freq_hz >= qrss_tx::FREQ_MIN_HZ && e.freq_hz <= qrss_tx::FREQ_MAX_HZ)) {
        return "the QRSS carrier must be between 300 and 2700 Hz";
    }
    if (e.first_slot <= 0.0 || std::fmod(e.first_slot, QUARTER_S) != 0.0) {
        return "a send starts on a quarter hour";
    }
    if (e.count < 0) return "the number of sends cannot be negative";
    if (e.every_min == 0) {
        if (e.count == 0) {
            return "back-to-back sends never leave the air: choose how many, or a gap "
                   "between them";
        }
        if (e.count * n > MAX_RUN_PASSES) {
            return "back to back, at most " + std::to_string(MAX_RUN_PASSES / n) +
                   " sends of mode " + e.mode + " (a day on the air)";
        }
        return {};
    }
    if (e.every_min < 0 || e.every_min % 15 != 0) {
        return "sends repeat a whole number of quarter hours apart";
    }
    if (e.every_min < 30 * n) {
        return "a mode " + e.mode + " send lasts " + std::to_string(30 * n) +
               " minutes, so it cannot repeat more often than that";
    }
    return {};
}

std::string describe(const Entry& e) {
    const std::string mode = "mode " + e.mode;
    if (e.count == 1) return mode + ", once";
    if (e.every_min == 0) return std::to_string(e.count) + " x " + mode + ", back to back";
    const std::string times = e.count == 0 ? "until removed" : std::to_string(e.count) + " times";
    return mode + " " + interval(e.every_min) + ", " + times;
}

std::string when(double slot, double today) {
    const QDateTime t = utc(slot);
    const bool same_day = t.date() == utc(today).date();
    return t.toString(same_day ? QStringLiteral("HH:mm") : QStringLiteral("ddd d MMM HH:mm"))
               .toStdString() +
           "Z";
}

double earliest_slot(double now) {
    const double offset = qrss_tx::audio_start(0.0) - START_LEAD_S;
    return std::ceil((now - offset) / QUARTER_S) * QUARTER_S;
}

std::vector<Upcoming> upcoming(const std::vector<Entry>& entries, double now,
                               double horizon_s, std::size_t max) {
    std::vector<Upcoming> out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (!e.enabled || !problem(e).empty()) continue;
        std::size_t taken = 0;
        for (int k = e.next; (e.count == 0 || k < e.count) && taken < max; ++k, ++taken) {
            const double slot = send_slot(e, k);
            if (slot > now + horizon_s) break;
            out.push_back({i, k, slot});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Upcoming& a, const Upcoming& b) { return a.slot < b.slot; });
    if (out.size() > max) out.resize(max);
    return out;
}

std::optional<Clash> clash(const Entry& e, const std::vector<Entry>& others, double now) {
    if (!problem(e).empty()) return std::nullopt;
    const double end = std::max(now, e.first_slot) + WEEK_S;
    std::optional<Clash> best;
    for (const Entry& o : others) {
        if (o.id == e.id || !o.enabled || finished(o) || !problem(o).empty()) continue;
        for (int k = e.next; e.count == 0 || k < e.count; ++k) {
            if (best && send_slot(e, k) >= best->slot) break;
            if (send_slot(e, k) > end) break;
            const auto [from, to] = busy(e, k);
            const auto [lo, hi] = overlapping(o, from, to);
            if (lo <= hi) {
                best = Clash{o.label, send_slot(e, k), send_slot(o, lo)};
                break;
            }
        }
    }
    return best;
}

std::optional<Upcoming> clash_with(const std::vector<Entry>& entries, double from, double to,
                                   double) {
    std::optional<Upcoming> best;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& o = entries[i];
        if (!o.enabled || finished(o) || !problem(o).empty()) continue;
        const auto [lo, hi] = overlapping(o, from, to);
        if (lo <= hi && (!best || send_slot(o, lo) < best->slot)) {
            best = Upcoming{i, lo, send_slot(o, lo)};
        }
    }
    return best;
}

std::string to_json(const std::vector<Entry>& entries) {
    QJsonArray list;
    for (const Entry& e : entries) {
        QJsonObject o;
        o[QStringLiteral("id")] = QString::fromStdString(e.id);
        o[QStringLiteral("picture")] = QString::fromStdString(e.picture);
        o[QStringLiteral("label")] = QString::fromStdString(e.label);
        o[QStringLiteral("mode")] = QString::fromStdString(e.mode);
        o[QStringLiteral("freq_hz")] = e.freq_hz;
        o[QStringLiteral("first_slot")] = e.first_slot;
        o[QStringLiteral("count")] = e.count;
        o[QStringLiteral("every_min")] = e.every_min;
        o[QStringLiteral("enabled")] = e.enabled;
        o[QStringLiteral("next")] = e.next;
        list.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("entries")] = list;
    return QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
}

std::vector<Entry> from_json(const std::string& text) {
    std::vector<Entry> out;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text));
    for (const QJsonValue& v : doc.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("id")).toString().toStdString();
        e.picture = o.value(QStringLiteral("picture")).toString().toStdString();
        e.label = o.value(QStringLiteral("label")).toString().toStdString();
        e.mode = o.value(QStringLiteral("mode")).toString(QStringLiteral("A")).toStdString();
        e.freq_hz = o.value(QStringLiteral("freq_hz")).toDouble(qrss_tx::FREQ_DEFAULT_HZ);
        e.first_slot = o.value(QStringLiteral("first_slot")).toDouble();
        e.count = o.value(QStringLiteral("count")).toInt(1);
        e.every_min = o.value(QStringLiteral("every_min")).toInt();
        e.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        e.next = std::max(0, o.value(QStringLiteral("next")).toInt());
        if (e.id.empty() || e.picture.empty() || !problem(e).empty()) continue;
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace sstvae::gui::qrss_schedule

namespace sstvae::gui {

namespace qs = qrss_schedule;

QrssScheduler::QrssScheduler(std::filesystem::path file, Starter starter, Clock clock,
                             int tick_ms, QObject* parent)
    : QObject(parent), file_(std::move(file)), starter_(std::move(starter)),
      clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = [] { return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0; };
    }
    load();
    if (tick_ms > 0) {
        timer_ = new QTimer(this);
        timer_->setInterval(tick_ms);
        connect(timer_, &QTimer::timeout, this, &QrssScheduler::tick);
        timer_->start();
        // The first look is as soon as the event loop runs, so a send due
        // when the app starts is not a tick late.
        QTimer::singleShot(0, this, &QrssScheduler::tick);
    }
}

QrssScheduler::~QrssScheduler() = default;

std::filesystem::path QrssScheduler::picture_dir() const {
    return file_.parent_path() / "qrss_schedule";
}

void QrssScheduler::load() {
    QFile f(QString::fromStdString(file_.string()));
    if (!f.open(QIODevice::ReadOnly)) return;
    entries_ = qs::from_json(f.readAll().toStdString());
}

void QrssScheduler::save() {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    QSaveFile f(QString::fromStdString(file_.string()));
    const std::string text = qs::to_json(entries_);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(text.data(), static_cast<qint64>(text.size())) !=
            static_cast<qint64>(text.size()) ||
        !f.commit()) {
        emit logged(2, tr("QRSS schedule: could not save %1")
                              .arg(QString::fromStdString(file_.string())));
    }
}

qs::Entry* QrssScheduler::find(const std::string& id) {
    for (qs::Entry& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::string QrssScheduler::add(qs::Entry entry, const images::Picture& picture) {
    const double now = clock_();
    entry.next = 0;
    entry.enabled = true;
    if (std::string why = qs::problem(entry); !why.empty()) return why;
    const double earliest = qs::earliest_slot(now);
    if (entry.first_slot < earliest) {
        return "that is too soon: a send added now can start at " + qs::when(earliest, now) +
               " at the earliest";
    }
    if (const auto c = qs::clash(entry, entries_, now)) {
        return "its " + qs::when(c->slot, now) + " send would overlap the " +
               qs::when(c->other_slot, now) + " send of \"" + c->other +
               "\"; the transmitter sends one thing at a time";
    }
    do {
        entry.id = QUuid::createUuid().toString(QUuid::Id128).left(12).toStdString();
    } while (find(entry.id) != nullptr);
    std::error_code ec;
    std::filesystem::create_directories(picture_dir(), ec);
    const std::filesystem::path png = picture_dir() / (entry.id + ".png");
    try {
        images::save_png(picture, png.string());
    } catch (const std::exception& e) {
        return std::string("could not save the picture: ") + e.what();
    }
    entry.picture = png.string();
    if (entry.label.empty()) entry.label = "picture " + entry.id.substr(0, 4);
    entries_.push_back(entry);
    save();
    emit logged(0, tr("QRSS schedule: added \"%1\", %2 at %3 Hz, first send %4")
                           .arg(QString::fromStdString(entry.label),
                                QString::fromStdString(qs::describe(entry)))
                           .arg(entry.freq_hz, 0, 'f', 0)
                           .arg(QString::fromStdString(qs::when(entry.first_slot, now))));
    emit changed();
    return {};
}

void QrssScheduler::remove(const std::string& id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const qs::Entry& e) { return e.id == id; });
    if (it == entries_.end()) return;
    std::error_code ec;
    const std::filesystem::path png(it->picture);
    if (png.parent_path() == picture_dir()) std::filesystem::remove(png, ec);
    const QString label = QString::fromStdString(it->label);
    entries_.erase(it);
    save();
    emit logged(0, active_ == id
                           ? tr("QRSS schedule: removed \"%1\"; the send on the air carries "
                                "on (Cancel stops it)")
                                 .arg(label)
                           : tr("QRSS schedule: removed \"%1\"").arg(label));
    emit changed();
}

std::string QrssScheduler::set_enabled(const std::string& id, bool on) {
    qs::Entry* e = find(id);
    if (e == nullptr || e->enabled == on) return {};
    if (on) {
        qs::Entry trial = *e;
        trial.enabled = true;
        if (const auto c = qs::clash(trial, entries_, clock_())) {
            return "its " + qs::when(c->slot, clock_()) + " send would overlap the " +
                   qs::when(c->other_slot, clock_()) + " send of \"" + c->other + "\"";
        }
    }
    e->enabled = on;
    QString note;
    if (on) {
        // What fell due while it was paused was not missed: it was not
        // wanted.
        const double now = clock_();
        e->next = std::max(e->next, first_startable(*e, now));
        note = qs::finished(*e) ? tr("; every send is past")
                                : tr("; next send %1").arg(QString::fromStdString(
                                      qs::when(qs::send_slot(*e, e->next), now)));
    }
    save();
    emit logged(0, (on ? tr("QRSS schedule: resumed \"%1\"%2") : tr("QRSS schedule: paused \"%1\"%2"))
                       .arg(QString::fromStdString(e->label), note));
    emit changed();
    if (on) tick();
    return {};
}

int QrssScheduler::first_startable(const qs::Entry& e, double now) const {
    const double p = qs::period_min(e) * 60.0;
    const double x = (now + qs::MIN_LEAD_S - qrss_tx::audio_start(e.first_slot)) / p;
    int k = x < 0.0 ? 0 : static_cast<int>(std::floor(x)) + 1;
    if (e.count > 0) k = std::min(k, e.count);
    return k;
}

void QrssScheduler::run_finished() {
    if (active_.empty()) return;
    active_.clear();
    emit changed();
}

void QrssScheduler::tick() {
    const double now = clock_();
    bool dirty = false;
    // Sends whose audio is too close (or past) to start: missed.
    for (qs::Entry& e : entries_) {
        if (!e.enabled || qs::finished(e)) continue;
        const int k = first_startable(e, now);
        if (k <= e.next) continue;
        const int missed = k - e.next;
        const std::string last = qs::when(qs::send_slot(e, k - 1), now);
        const std::string key_prefix = e.id + "/";
        std::string why = "the app was not running then";
        if (refused_.rfind(key_prefix, 0) == 0) why = refused_why_;
        emit logged(1, tr("QRSS schedule: \"%1\" missed %2 send(s), the last at %3 (%4)")
                              .arg(QString::fromStdString(e.label))
                              .arg(missed)
                              .arg(QString::fromStdString(last), QString::fromStdString(why)));
        e.next = k;
        dirty = true;
    }
    // The earliest send that is due.
    qs::Entry* due = nullptr;
    for (qs::Entry& e : entries_) {
        if (!e.enabled || qs::finished(e)) continue;
        if (now < qrss_tx::audio_start(qs::send_slot(e, e.next)) - qs::START_LEAD_S) continue;
        if (due == nullptr || qs::send_slot(e, e.next) < qs::send_slot(*due, due->next)) due = &e;
    }
    if (due != nullptr) {
        qs::Entry& e = *due;
        const int n = qrss_tx::passes_for(e.mode);
        const int m = e.every_min == 0 ? std::min(e.count - e.next, qs::MAX_RUN_PASSES / n) : 1;
        std::vector<qrss_tx::Pass> passes;
        for (int k = e.next; k < e.next + m; ++k) {
            for (const qrss_tx::Pass& p : qs::passes_of(e, k)) passes.push_back(p);
        }
        const std::string key = e.id + "/" + std::to_string(e.next);
        std::string why;
        const Start result = starter_(e, passes, &why);
        const QString label = QString::fromStdString(e.label);
        const QString at = QString::fromStdString(qs::when(qs::send_slot(e, e.next), now));
        if (result == Start::Started) {
            const QString which =
                e.count == 1 ? QString()
                : m == 1     ? tr(", send %1%2").arg(e.next + 1).arg(
                               e.count > 0 ? tr(" of %1").arg(e.count) : QString())
                             : tr(", sends %1-%2 of %3").arg(e.next + 1).arg(e.next + m).arg(e.count);
            emit logged(0, tr("QRSS schedule: starting \"%1\" (%2) at %3%4")
                                   .arg(label, QString::fromStdString(qs::describe(e)), at, which));
            e.next += m;
            active_ = e.id;
            refused_.clear();
            dirty = true;
        } else if (result == Start::Busy) {
            if (refused_ != key) {
                emit logged(0, tr("QRSS schedule: the %1 send of \"%2\" is waiting: %3")
                                       .arg(at, label, QString::fromStdString(why)));
                refused_ = key;
                refused_why_ = why;
            }
        } else {
            emit logged(2, tr("QRSS schedule: the %1 send of \"%2\" was not sent: %3")
                                  .arg(at, label, QString::fromStdString(why)));
            e.next += 1;
            dirty = true;
        }
    }
    if (dirty) {
        save();
        emit changed();
    }
}

}  // namespace sstvae::gui
