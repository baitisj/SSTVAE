#include "dsp/slow_spectrogram.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

#include "dsp/fft.hpp"

namespace sstvae::dsp {

namespace {

constexpr double FS = 8000.0;

const std::vector<double>& window() {
    static const std::vector<double> w = [] {
        std::vector<double> v(SlowSpectrogram::NFFT);
        for (int i = 0; i < SlowSpectrogram::NFFT; ++i) {
            v[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (SlowSpectrogram::NFFT - 1));
        }
        return v;
    }();
    return w;
}

// Power of a unit-amplitude sine through the window, so a column reads
// in "amplitude squared" whatever NFFT is.
double power_scale() {
    static const double s = [] {
        double sum = 0.0;
        for (double v : window()) sum += v;
        return 4.0 / (sum * sum);
    }();
    return s;
}

// The columns of `d` whose [t, t + len) overlaps [t0, t1).
template <class Fn>
int each_overlapping(const std::deque<SlowSpectrogram::Column>& d, double len, double t0,
                     double t1, Fn fn) {
    auto it = std::lower_bound(d.begin(), d.end(), t0 - len,
                               [](const SlowSpectrogram::Column& c, double t) { return c.t < t; });
    int n = 0;
    for (; it != d.end() && it->t < t1; ++it) {
        if (it->t + len <= t0) continue;
        fn(*it);
        ++n;
    }
    return n;
}

}  // namespace

void SlowSpectrogram::restart() { pending_.clear(); }

void SlowSpectrogram::clear() {
    pending_.clear();
    fine_.clear();
    coarse_.clear();
    block_sum_.clear();
    block_t_ = -1.0;
    block_n_ = 0;
}

void SlowSpectrogram::push(const std::vector<double>& samples, double end_time) {
    if (samples.empty()) return;
    const double start = end_time - static_cast<double>(samples.size()) / FS;
    // Audio that does not follow on from what is pending: a gap.
    if (!pending_.empty() && std::abs(start - pending_end_) > 0.5) pending_.clear();
    pending_.insert(pending_.end(), samples.begin(), samples.end());
    pending_end_ = end_time;

    const std::vector<double>& w = window();
    const double scale = power_scale();
    while (static_cast<int>(pending_.size()) >= NFFT) {
        const double t = pending_end_ - static_cast<double>(pending_.size()) / FS;
        std::vector<cdouble> buf(NFFT);
        for (int i = 0; i < NFFT; ++i) buf[i] = pending_[static_cast<std::size_t>(i)] * w[i];
        const std::vector<cdouble> spec = fft(buf, true);
        Column c;
        c.t = std::round(t * 1000.0) / 1000.0;
        c.power.resize(BINS);
        for (int k = 0; k < BINS; ++k) c.power[k] = static_cast<float>(std::norm(spec[k]) * scale);
        add_fine(std::move(c));
        pending_.erase(pending_.begin(), pending_.begin() + HOP);
    }
}

void SlowSpectrogram::add_fine(Column column) {
    const double block = std::floor(column.t / COARSE_S) * COARSE_S;
    if (block != block_t_) {
        flush_coarse();
        block_t_ = block;
        block_sum_.assign(BINS, 0.0);
        block_n_ = 0;
    }
    for (int k = 0; k < BINS; ++k) block_sum_[k] += column.power[k];
    ++block_n_;
    if (!fine_.empty() && column.t <= fine_.back().t) fine_.clear();   // the clock went back
    fine_.push_back(std::move(column));
    const double newest = fine_.back().t;
    while (!fine_.empty() && fine_.front().t < newest - FINE_KEEP_S) fine_.pop_front();
    while (!coarse_.empty() && coarse_.front().t < newest - COARSE_KEEP_S) coarse_.pop_front();
}

void SlowSpectrogram::flush_coarse() {
    if (block_n_ == 0) return;
    Column c;
    c.t = block_t_;
    c.power.resize(BINS);
    for (int k = 0; k < BINS; ++k) c.power[k] = static_cast<float>(block_sum_[k] / block_n_);
    if (!coarse_.empty() && c.t <= coarse_.back().t) coarse_.clear();
    coarse_.push_back(std::move(c));
    block_n_ = 0;
}

std::vector<float> SlowSpectrogram::mean_between(double t0, double t1, int* count) const {
    std::vector<double> sum;
    const auto add = [&sum](const Column& c) {
        if (sum.empty()) sum.assign(c.power.size(), 0.0);
        for (std::size_t k = 0; k < c.power.size(); ++k) sum[k] += c.power[k];
    };
    int columns = 0;   // how many were summed
    int seconds = 0;   // how much audio they hold
    const bool fine_reaches = !fine_.empty() && t0 >= fine_.front().t - COLUMN_S;
    if (!fine_reaches) {
        columns = each_overlapping(coarse_, COARSE_S, t0, t1, add);
        // Counted in seconds of audio, as a fine column is: what the
        // mean's noise depends on.
        seconds = columns * static_cast<int>(COARSE_S / COLUMN_S);
    }
    // Recent enough for the fine store, or the block still filling,
    // which is not in the coarse one yet.
    if (columns == 0 && !fine_.empty()) {
        columns = each_overlapping(fine_, COLUMN_S, t0, t1, add);
        seconds = columns;
    }
    if (count != nullptr) *count = seconds;
    std::vector<float> out(sum.size());
    for (std::size_t k = 0; k < sum.size(); ++k) out[k] = static_cast<float>(sum[k] / columns);
    return out;
}

// --- time scale -------------------------------------------------------------

TimeScale::TimeScale(double lens_spp, double history_spp, int lens_px, int ramp_px)
    : lens_spp_(std::clamp(lens_spp, MIN_SPP, MAX_SPP)),
      history_spp_(std::clamp(history_spp, MIN_SPP, MAX_SPP)),
      lens_px_(std::max(0, lens_px)),
      ramp_px_(std::max(1, ramp_px)) {}

void TimeScale::set_lens_spp(double spp) { lens_spp_ = std::clamp(spp, MIN_SPP, MAX_SPP); }
void TimeScale::set_history_spp(double spp) { history_spp_ = std::clamp(spp, MIN_SPP, MAX_SPP); }

double TimeScale::spp_at(double x) const {
    if (x < lens_px_) return lens_spp_;
    if (x < lens_px_ + ramp_px_) {
        return lens_spp_ * std::pow(history_spp_ / lens_spp_, (x - lens_px_) / ramp_px_);
    }
    return history_spp_;
}

namespace {

// The integral of s0 * r^(u/R) for u in [0, d].
double ramp_age(double s0, double s1, double R, double d) {
    const double ln_r = std::log(s1 / s0);
    if (std::abs(ln_r) < 1e-12) return s0 * d;
    return s0 * R * (std::exp(ln_r * d / R) - 1.0) / ln_r;
}

}  // namespace

double TimeScale::age_at(double x) const {
    if (x <= 0) return 0.0;
    const double L = lens_px_;
    const double R = ramp_px_;
    if (x <= L) return lens_spp_ * x;
    const double lens_age = lens_spp_ * L;
    if (x <= L + R) return lens_age + ramp_age(lens_spp_, history_spp_, R, x - L);
    return lens_age + ramp_age(lens_spp_, history_spp_, R, R) + history_spp_ * (x - L - R);
}

double TimeScale::x_of(double age) const {
    if (age <= 0) return 0.0;
    const double L = lens_px_;
    const double R = ramp_px_;
    const double lens_age = lens_spp_ * L;
    if (age <= lens_age) return age / lens_spp_;
    const double ramp_total = ramp_age(lens_spp_, history_spp_, R, R);
    if (age <= lens_age + ramp_total) {
        const double a = age - lens_age;
        const double ln_r = std::log(history_spp_ / lens_spp_);
        if (std::abs(ln_r) < 1e-12) return L + a / lens_spp_;
        return L + R * std::log(1.0 + a * ln_r / (lens_spp_ * R)) / ln_r;
    }
    return L + R + (age - lens_age - ramp_total) / history_spp_;
}

float median_of(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

}  // namespace sstvae::dsp
