// Scheduled QRSS sends (gui/qrss_schedule.*): which quarter hours a
// schedule asks for, what may not overlap, and the scheduler's clock --
// due, started, waiting on a busy transmitter, missed -- against a fake
// clock and a fake transmitter. Then the window, offscreen: the form
// says what it will do and Add adds it, and a file that is not 640 x 480
// is framed in the Transmit pane's dialog first.

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "crop_dialog.hpp"
#include "images/images.hpp"
#include "images/types.hpp"
#include "qrss_schedule.hpp"
#include "qrss_schedule_window.hpp"

using namespace sstvae;
using namespace sstvae::gui;
namespace qs = sstvae::gui::qrss_schedule;

namespace {

constexpr double Q = 1791594000.0;    // 2026-10-10T01:00Z, a quarter hour
constexpr double H = 3600.0;

qs::Entry entry(const std::string& mode, double first, int count, int every_min,
                const std::string& label = "test") {
    qs::Entry e;
    e.id = label;
    e.label = label;
    e.picture = "/nowhere/" + label + ".png";
    e.mode = mode;
    e.first_slot = first;
    e.count = count;
    e.every_min = every_min;
    return e;
}

void test_the_sends_of_a_schedule() {
    // "2 x A": mode A twice, back to back: part 1 of the picture, twice.
    const qs::Entry aa = entry("A", Q, 2, 0);
    check::is_true(qs::problem(aa).empty(), "schedule/2xA: a valid schedule");
    check::equal(qs::period_min(aa), 30, "schedule/2xA: back to back is half an hour for A");
    std::vector<qrss_tx::Pass> p = qs::passes_of(aa, 1);
    check::is_true(p.size() == 1 && p[0].slot == Q + 1800.0 && p[0].segment == 0,
                   "schedule/2xA: the second send is the next half hour, part 1 again");
    check::equal(qs::describe(aa), std::string("2 x mode A, back to back"),
                 "schedule/2xA: described as Jeff wrote it");

    // Mode B every other hour, until removed.
    const qs::Entry b = entry("B", Q, 0, 120);
    check::is_true(qs::problem(b).empty(), "schedule/B: alternating hours is valid");
    p = qs::passes_of(b, 2);
    check::is_true(p.size() == 2 && p[0].slot == Q + 4 * H && p[1].slot == Q + 4 * H + 1800.0 &&
                       p[1].segment == 1,
                   "schedule/B: the third send, both of its parts");
    check::equal(qs::describe(b),
                 std::string("mode B every 2 hours (alternating hours), until removed"),
                 "schedule/B: described");
    const auto [from, to] = qs::busy(b, 0);
    check::is_true(from == Q - 11.0 - qs::START_LEAD_S && to == Q + 1800.0 + 1786.7,
                   "schedule/B: busy from the start lead to the end of the second pass");
    check::equal(qs::describe(entry("C", Q, 1, 0)), std::string("mode C, once"),
                 "schedule/C: a single send");
    check::equal(qs::describe(entry("A", Q, 5, 90)),
                 std::string("mode A every 90 minutes, 5 times"), "schedule/A: a count");
}

void test_what_cannot_be_scheduled() {
    check::is_true(!qs::problem(entry("A", Q, 0, 0)).empty(),
                   "schedule/problem: back to back until removed never leaves the air");
    check::is_true(!qs::problem(entry("A", Q, 49, 0)).empty(),
                   "schedule/problem: more than a day back to back");
    check::is_true(qs::problem(entry("A", Q, 48, 0)).empty(), "schedule/problem: a day is fine");
    check::is_true(!qs::problem(entry("C", Q, 17, 0)).empty(),
                   "schedule/problem: 17 x C is 51 passes");
    check::is_true(!qs::problem(entry("B", Q, 3, 30)).empty(),
                   "schedule/problem: an hour-long send every half hour overlaps itself");
    check::is_true(qs::problem(entry("A", Q, 3, 45)).empty(),
                   "schedule/problem: A every 45 minutes is fine");
    check::is_true(!qs::problem(entry("A", Q, 3, 50)).empty(),
                   "schedule/problem: not a whole number of quarter hours");
    check::is_true(!qs::problem(entry("A", Q + 60.0, 1, 0)).empty(),
                   "schedule/problem: off the quarter hour");
    qs::Entry far = entry("A", Q, 1, 0);
    far.freq_hz = 2800.0;
    check::is_true(!qs::problem(far).empty(), "schedule/problem: carrier out of the band");
    check::is_true(qs::earliest_slot(Q - 11.0 - qs::START_LEAD_S) == Q &&
                       qs::earliest_slot(Q - 11.0 - qs::START_LEAD_S + 1.0) == Q + 900.0,
                   "schedule/earliest: the first quarter hour with the start lead to spare");
}

void test_clashes() {
    const std::vector<qs::Entry> one{entry("A", Q, 1, 0, "first")};
    check::is_true(qs::clash(entry("A", Q + 1800.0, 1, 0, "next"), one, Q - H).has_value(),
                   "schedule/clash: another picture straight after one cannot start in time");
    check::is_true(!qs::clash(entry("A", Q + 2700.0, 1, 0, "later"), one, Q - H).has_value(),
                   "schedule/clash: a quarter hour's gap is enough");
    // A every 2 h from Q against A every 3 h from Q + 1 h: they meet at
    // Q + 4 h, the second send of the latter.
    const std::vector<qs::Entry> even{entry("A", Q, 0, 120, "even")};
    const auto c = qs::clash(entry("A", Q + H, 0, 180, "thirds"), even, Q - H);
    check::is_true(c && c->slot == Q + 4 * H && c->other_slot == Q + 4 * H &&
                       c->other == "even",
                   "schedule/clash: two repeating schedules meet where they meet");
    std::vector<qs::Entry> paused = even;
    paused[0].enabled = false;
    check::is_true(!qs::clash(entry("A", Q + H, 0, 180), paused, Q - H),
                   "schedule/clash: a paused entry is no obstacle");
    // A send by hand that runs over a scheduled one.
    const auto w = qs::clash_with(even, Q + 1.5 * H, Q + 2.5 * H, Q);
    check::is_true(w && w->send == 1 && w->slot == Q + 2 * H,
                   "schedule/clash: a send by hand over the 03:00Z send");
    check::is_true(!qs::clash_with(even, Q + 0.5 * H, Q + 1.5 * H, Q),
                   "schedule/clash: but not in the gap");
}

void test_upcoming_and_the_file() {
    std::vector<qs::Entry> list{entry("B", Q + H, 3, 180, "b"), entry("A", Q, 2, 0, "aa")};
    list[1].next = 1;
    const std::vector<qs::Upcoming> u = qs::upcoming(list, Q - H, 7 * 86400.0, 4);
    check::is_true(u.size() == 4 && u[0].entry == 1 && u[0].slot == Q + 1800.0 &&
                       u[1].entry == 0 && u[1].slot == Q + H && u[2].slot == Q + 4 * H &&
                       u[3].slot == Q + 7 * H,
                   "schedule/upcoming: what is left of each, in time order");
    list[0].enabled = false;
    list[0].freq_hz = 987.0;
    const std::vector<qs::Entry> back = qs::from_json(qs::to_json(list));
    check::is_true(back.size() == 2 && back[0].id == "b" && !back[0].enabled &&
                       back[0].freq_hz == 987.0 && back[0].every_min == 180 &&
                       back[0].count == 3 && back[1].next == 1 && back[1].first_slot == Q &&
                       back[1].mode == "A",
                   "schedule/file: every field survives the round trip");
    check::is_true(qs::from_json("not json").empty(), "schedule/file: a broken file is empty");
}

struct Fake {
    double now = Q - H;
    QrssScheduler::Start answer = QrssScheduler::Start::Started;
    std::string why;
    std::vector<std::vector<qrss_tx::Pass>> runs;
    std::vector<std::pair<int, std::string>> log;
};

QrssScheduler* make(Fake& fake, const std::filesystem::path& file) {
    auto* s = new QrssScheduler(
        file,
        [&fake](const qs::Entry&, const std::vector<qrss_tx::Pass>& passes, std::string* why) {
            if (fake.answer == QrssScheduler::Start::Started) fake.runs.push_back(passes);
            *why = fake.why;
            return fake.answer;
        },
        [&fake] { return fake.now; }, 0);
    QObject::connect(s, &QrssScheduler::logged, [&fake](int sev, const QString& m) {
        fake.log.push_back({sev, m.toStdString()});
    });
    return s;
}

bool logged(const Fake& f, const std::string& part) {
    for (const auto& [sev, m] : f.log) {
        if (m.find(part) != std::string::npos) return true;
    }
    return false;
}

void test_the_scheduler_starts_what_is_due(const std::filesystem::path& dir) {
    Fake fake;
    const std::filesystem::path file = dir / "a" / "qrss_schedule.json";
    std::unique_ptr<QrssScheduler> s(make(fake, file));
    const images::Picture pic(64, 48);

    qs::Entry aa = entry("A", Q, 2, 0, "two A");
    aa.id.clear();
    aa.picture.clear();
    check::is_true(s->add(aa, pic).empty(), "scheduler/add: 2 x A at 01:00Z");
    check::is_true(s->entries().size() == 1 &&
                       std::filesystem::exists(s->entries()[0].picture) &&
                       std::filesystem::exists(file),
                   "scheduler/add: the picture and the schedule are on disk");
    check::is_true(!s->add(entry("A", Q + 1800.0, 1, 0, "clash"), pic).empty(),
                   "scheduler/add: an overlapping send is refused");
    fake.now = Q - 11.0 - 200.0 + 900.0;
    check::is_true(!s->add(entry("A", Q + 900.0, 1, 0, "soon"), pic).empty(),
                   "scheduler/add: one with no time to start is refused");
    fake.now = Q - H;

    s->tick();
    check::is_true(fake.runs.empty(), "scheduler/tick: nothing due an hour early");
    fake.now = Q - 11.0 - qs::START_LEAD_S - 1.0;
    s->tick();
    check::is_true(fake.runs.empty(), "scheduler/tick: or a second before its lead");
    fake.now += 2.0;
    s->tick();
    check::is_true(fake.runs.size() == 1 && fake.runs[0].size() == 2 &&
                       fake.runs[0][0].slot == Q && fake.runs[0][1].slot == Q + 1800.0 &&
                       fake.runs[0][1].segment == 0,
                   "scheduler/tick: both sends handed over as one run of two passes");
    check::is_true(qs::finished(s->entries()[0]) && s->active() == s->entries()[0].id,
                   "scheduler/tick: the entry is done and on the air");
    s->tick();
    check::equal(fake.runs.size(), std::size_t{1}, "scheduler/tick: and started once");
    s->run_finished();
    check::is_true(s->active().empty(), "scheduler/tick: until the transmitter finishes");

    // A busy transmitter: waited for once in the log, then missed.
    fake.log.clear();
    check::is_true(s->add(entry("B", Q + 3 * H, 2, 180, "b"), pic).empty(),
                   "scheduler/busy: B at 04:00Z and 07:00Z");
    fake.answer = QrssScheduler::Start::Busy;
    fake.why = "the transmitter is busy";
    fake.now = Q + 3 * H - 11.0 - 200.0;
    s->tick();
    s->tick();
    int waiting = 0;
    for (const auto& [sev, m] : fake.log) waiting += m.find("is waiting") != std::string::npos;
    check::equal(waiting, 1, "scheduler/busy: logged once, not every tick");
    fake.now = Q + 3 * H - 11.0 - qs::MIN_LEAD_S + 1.0;
    s->tick();
    check::is_true(s->entries()[1].next == 1 && logged(fake, "missed 1 send") &&
                       logged(fake, "the transmitter is busy"),
                   "scheduler/busy: too late to start is missed, with the reason");

    // A failure (no callsign, say) costs that send and no more.
    fake.answer = QrssScheduler::Start::Failed;
    fake.why = "no callsign";
    fake.now = Q + 6 * H - 11.0 - 200.0;
    s->tick();
    check::is_true(s->entries()[1].next == 2 && logged(fake, "was not sent: no callsign"),
                   "scheduler/failed: reported and passed over");

    // Another listener of the same file sees it all.
    Fake other;
    std::unique_ptr<QrssScheduler> again(make(other, file));
    check::is_true(again->entries().size() == 2 && again->entries()[1].next == 2 &&
                       qs::finished(again->entries()[0]),
                   "scheduler/file: progress survives a restart");
    const std::string png = again->entries()[0].picture;
    again->remove(again->entries()[0].id);
    check::is_true(again->entries().size() == 1 && !std::filesystem::exists(png),
                   "scheduler/remove: the entry and its picture go");
}

void test_missed_and_paused(const std::filesystem::path& dir) {
    Fake fake;
    const std::filesystem::path file = dir / "b" / "qrss_schedule.json";
    std::unique_ptr<QrssScheduler> s(make(fake, file));
    const images::Picture pic(64, 48);
    check::is_true(s->add(entry("A", Q, 0, 60, "hourly"), pic).empty(),
                   "scheduler/missed: hourly from 01:00Z");
    // The app was not running from 00:00 to 03:20: three sends missed.
    fake.now = Q + 2 * H + 1200.0;
    s->tick();
    check::is_true(s->entries()[0].next == 3 && logged(fake, "missed 3 send(s)") &&
                       logged(fake, "the app was not running"),
                   "scheduler/missed: logged as one line, the next is 04:00Z");
    check::is_true(fake.runs.empty(), "scheduler/missed: and nothing started late");
    fake.log.clear();
    s->set_enabled(s->entries()[0].id, false);
    fake.now = Q + 6 * H;
    s->tick();
    check::is_true(s->entries()[0].next == 3 && !logged(fake, "missed"),
                   "scheduler/paused: a paused entry neither sends nor misses");
    check::is_true(s->set_enabled(s->entries()[0].id, true).empty(),
                   "scheduler/paused: resumed");
    check::is_true(s->entries()[0].next == 7 && !logged(fake, "missed") &&
                       logged(fake, "next send 08:00Z"),
                   "scheduler/paused: what fell due while paused is skipped quietly");
}

void test_the_window(const std::filesystem::path& dir) {
    Fake fake;
    std::unique_ptr<QrssScheduler> s(make(fake, dir / "c" / "qrss_schedule.json"));
    QrssScheduleWindow w(
        s.get(), [] { return std::optional<images::Picture>(images::Picture(640, 480)); },
        [] { return QrssScheduleWindow::Defaults{"B", 1234.0}; });
    w.show();
    QApplication::processEvents();
    auto* mode = w.findChild<QComboBox*>(QStringLiteral("schedule_mode"));
    auto* freq = w.findChild<QSpinBox*>(QStringLiteral("schedule_freq"));
    auto* count = w.findChild<QSpinBox*>(QStringLiteral("schedule_count"));
    auto* every = w.findChild<QComboBox*>(QStringLiteral("schedule_every"));
    auto* summary = w.findChild<QLabel*>(QStringLiteral("schedule_summary"));
    auto* add = w.findChild<QPushButton*>(QStringLiteral("schedule_add"));
    auto* table = w.findChild<QTreeWidget*>(QStringLiteral("schedule_table"));
    auto* coming = w.findChild<QListWidget*>(QStringLiteral("schedule_coming"));
    check::is_true(mode && freq && count && every && summary && add && table && coming,
                   "window: every control is there");
    if (!(mode && freq && count && every && summary && add && table && coming)) return;
    check::is_true(mode->currentData().toString() == QStringLiteral("B") && freq->value() == 1234,
                   "window: mode and carrier start as the Transmit pane has them");
    check::is_true(w.chosen_slot() == qs::earliest_slot(fake.now),
                   "window: the first send starts at the earliest quarter hour");
    check::is_true(summary->text().contains(QStringLiteral("2 x mode B, back to back")) &&
                       add->isEnabled(),
                   "window: the default is two sends back to back");
    mode->setCurrentIndex(mode->findData(QStringLiteral("A")));
    count->setValue(0);
    every->setCurrentIndex(every->findData(120));
    check::is_true(summary->text().contains(QStringLiteral("(alternating hours), until removed")),
                   "window: every other hour, until removed");
    every->setCurrentIndex(every->findData(0));
    check::is_true(!add->isEnabled() && summary->text().startsWith(QStringLiteral("Can't add")),
                   "window: back to back forever is refused, and says why");
    count->setValue(2);
    add->click();
    QApplication::processEvents();
    check::is_true(s->entries().size() == 1 && s->entries()[0].mode == "A" &&
                       s->entries()[0].count == 2 && s->entries()[0].freq_hz == 1234.0,
                   "window: Add adds what the form says");
    check::equal(table->topLevelItemCount(), 1, "window: and it is listed");
    check::equal(coming->count(), 2, "window: with its two sends coming up");
    freq->setValue(1235);
    check::is_true(!add->isEnabled() && summary->text().contains(QStringLiteral("would overlap")),
                   "window: the same send again would clash, and says so");
}

// A solid red picture file of the given size.
std::string red_png(const std::filesystem::path& dir, int w, int h) {
    images::Picture p(w, h);
    for (std::size_t i = 0; i < p.rgb.size(); i += 3) p.rgb[i] = 255;
    const std::string path = (dir / ("red-" + std::to_string(w) + "x" + std::to_string(h) +
                                     ".png")).string();
    images::save_png(p, path);
    return path;
}

// Answers the framing dialog, if one opens, the next time events are
// processed: zooms all the way out (the whole picture, padded) and
// presses OK. Counts how many opened.
void answer_framing(int* opened) {
    QTimer::singleShot(0, [opened] {
        auto* dialog = qobject_cast<CropDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        ++*opened;
        if (auto* view = dialog->findChild<CropView*>()) {
            images::Framing all;
            all.zoom = view->min_zoom();
            view->set_framing(all);
        }
        dialog->accept();
    });
}

void test_a_file_is_framed_first(const std::filesystem::path& dir) {
    Fake fake;
    std::unique_ptr<QrssScheduler> s(make(fake, dir / "f" / "qrss_schedule.json"));
    QrssScheduleWindow w(
        s.get(), [] { return std::optional<images::Picture>(images::Picture(640, 480)); },
        [] { return QrssScheduleWindow::Defaults{"A", 1500.0}; });
    w.show();
    QApplication::processEvents();
    auto* name = w.findChild<QLabel*>(QStringLiteral("schedule_picture_name"));
    auto* framing = w.findChild<QPushButton*>(QStringLiteral("schedule_framing"));
    auto* add = w.findChild<QPushButton*>(QStringLiteral("schedule_add"));
    check::is_true(name && framing && add, "window/file: the picture controls are there");
    if (!(name && framing && add)) return;
    check::is_true(!framing->isEnabled(), "window/file: nothing to frame in the composition");

    int opened = 0;
    answer_framing(&opened);
    w.use_file(QString::fromStdString(red_png(dir, 640, 480)));
    QApplication::processEvents();
    check::equal(opened, 0, "window/file: 640 x 480 goes straight in");
    check::is_true(framing->isEnabled(), "window/file: but can still be framed");

    answer_framing(&opened);
    w.use_file(QString::fromStdString(red_png(dir, 800, 450)));
    QApplication::processEvents();
    check::equal(opened, 1, "window/file: 16:9 opens the framing dialog");
    check::is_true(name->text().contains(QStringLiteral("800x450, padded to 4:3")),
                   "window/file: and says what it did");
    check::is_true(add->isEnabled(), "window/file: ready to add");
    add->click();
    QApplication::processEvents();
    check::equal(s->entries().size(), std::size_t{1}, "window/file: added");
    if (s->entries().size() != 1) return;
    const images::Picture sent = images::load(s->entries()[0].picture);
    auto red = [&sent](int x, int y) {
        return sent.rgb[(static_cast<std::size_t>(y) * sent.width + x) * 3] > 200;
    };
    check::is_true(sent.width == 640 && sent.height == 480 && !red(320, 10) && red(320, 240) &&
                       red(5, 240) && red(634, 240),
                   "window/file: what is scheduled is the framing chosen, whole and padded");

    answer_framing(&opened);
    w.use_file(QString::fromStdString(red_png(dir, 1280, 960)));
    QApplication::processEvents();
    check::equal(opened, 2, "window/file: a 4:3 picture of another size asks too");

    answer_framing(&opened);
    framing->click();
    QApplication::processEvents();
    check::equal(opened, 3, "window/file: Framing... opens it again");

    w.use_composition();
    check::is_true(!framing->isEnabled(), "window/file: back to the composition, nothing to frame");
}

}  // namespace

int main(int argc, char** argv) {
    check::report_crashes_instead_of_prompting();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    const std::filesystem::path dir(tmp.path().toStdString());
    test_the_sends_of_a_schedule();
    test_what_cannot_be_scheduled();
    test_clashes();
    test_upcoming_and_the_file();
    test_the_scheduler_starts_what_is_due(dir);
    test_missed_and_paused(dir);
    test_the_window(dir);
    test_a_file_is_framed_first(dir);
    return check::report("qrss schedule");
}
