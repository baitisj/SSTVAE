// The arithmetic behind the QRSSTVAE tab's spectrogram, Qt-free.
//
// QRSS signals are hours long and a few hertz to 50 Hz wide, so this is
// the opposite of the SSTVAE waterfall (spectrum.hpp): fine in frequency
// (about 1 Hz per bin) and slow in time (one column per second), and kept
// for hours. Two stores:
//
//   * **Fine**: one power spectrum per second of audio (8192-point FFT,
//     Hann, a hop of one second), kept for FINE_KEEP_S.
//   * **Coarse**: the mean of each whole COARSE_S block of fine columns,
//     kept for COARSE_KEEP_S. Averaging is what lets a weak carrier show
//     at a slow scale: the noise in a 15 s mean is a quarter as rough.
//
// Columns carry the wall-clock second they began at, so a gap in the
// audio (the app transmitting, receiving stopped) is a gap on screen
// rather than a join, and `mean_between` averages whatever is there.
//
// `TimeScale` is the horizontal axis: newest at x = 0, a "lens" of fine
// seconds per pixel at the left, easing into a coarser scale for the
// long history to the right.

#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace sstvae::dsp {

class SlowSpectrogram {
public:
    static constexpr int NFFT = 8192;
    static constexpr int HOP = 8000;                 // one second at 8 kHz
    static constexpr double BIN_HZ = 8000.0 / NFFT;  // ~0.98 Hz
    static constexpr double MAX_HZ = 3000.0;
    static constexpr int BINS = static_cast<int>(MAX_HZ / BIN_HZ) + 1;
    static constexpr double COLUMN_S = 1.0;
    static constexpr double COARSE_S = 15.0;
    static constexpr double FINE_KEEP_S = 20 * 60.0;
    static constexpr double COARSE_KEEP_S = 6 * 3600.0;

    struct Column {
        double t = 0.0;            // unix seconds its audio began
        std::vector<float> power;  // BINS linear power values
    };

    // Audio at 8 kHz whose last sample was captured at `end_time`.
    void push(const std::vector<double>& samples, double end_time);
    // Forget the audio not yet in a column (a gap in capture).
    void restart();
    void clear();

    // The mean power of every column beginning in [t0, t1), from the
    // fine store where it reaches back that far, else the coarse one.
    // Empty when no column falls in the range. `count` is how many
    // seconds of audio went into it (a coarse column counts COARSE_S).
    std::vector<float> mean_between(double t0, double t1, int* count = nullptr) const;

    const std::deque<Column>& fine() const { return fine_; }
    const std::deque<Column>& coarse() const { return coarse_; }
    // The newest column's start, or 0 with none.
    double newest() const { return fine_.empty() ? 0.0 : fine_.back().t; }

private:
    void add_fine(Column column);
    void flush_coarse();

    std::vector<double> pending_;
    double pending_end_ = 0.0;   // time of the last pending sample
    std::deque<Column> fine_;
    std::deque<Column> coarse_;
    std::vector<double> block_sum_;
    double block_t_ = -1.0;
    int block_n_ = 0;
};

// The time axis: x = 0 is now, x grows into the past.
class TimeScale {
public:
    // Seconds per pixel for the first `lens_px` pixels, then easing over
    // `ramp_px` pixels to `history_spp` for the rest.
    TimeScale(double lens_spp = 1.0, double history_spp = 15.0, int lens_px = 240,
              int ramp_px = 60);

    double lens_spp() const { return lens_spp_; }
    double history_spp() const { return history_spp_; }
    int lens_px() const { return lens_px_; }
    int ramp_px() const { return ramp_px_; }
    void set_lens_spp(double spp);
    void set_history_spp(double spp);

    // Seconds per pixel at pixel x.
    double spp_at(double x) const;
    // How long ago pixel x's left edge is, in seconds (0 at x = 0).
    double age_at(double x) const;
    // The pixel showing audio `age` seconds old.
    double x_of(double age) const;

    static constexpr double MIN_SPP = 0.25;
    static constexpr double MAX_SPP = 120.0;

private:
    double lens_spp_;
    double history_spp_;
    int lens_px_;
    int ramp_px_;
};

// The median of `values` (a copy is partially sorted). 0 when empty.
float median_of(std::vector<float> values);

}  // namespace sstvae::dsp
