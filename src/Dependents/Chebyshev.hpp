// Chords made by distorting one sine wave.
//
// After Astrobear Music (Aspen Instruments), "This distortion plays chords
// using Chebyshev harmonic exciters" -- https://youtu.be/O0QLnR406pQ -- and the
// per-harmonic shaping in their Black Diamond Distortion. The idea demonstrated
// there is theirs; the mathematics under it is public and old, and this is that
// mathematics built as a Rack voice rather than a copy of their plugin.
//
// Three facts, in the order they matter:
//
//  1. Chebyshev polynomials of the first kind satisfy T_n(cos x) = cos(n x).
//     So a *unit-amplitude* sine through T_n comes out as exactly the nth
//     harmonic and nothing else -- not mostly, exactly.
//  2. A waveshaper is therefore a harmonic recipe: sum a_n * T_n and the output
//     is the harmonic series with those amplitudes, chosen outright.
//  3. Harmonic numbers in small whole ratios are chords. 4:5:6 is a just major
//     triad, 10:12:15 a minor, 4:5:6:7 a dominant seventh. Drive a sine an
//     octave or two below hearing through T_4 + T_5 + T_6 and what comes out is
//     a major triad whose root you cannot hear.
//
// The catch, and it is not a small one: (1) holds only at unit amplitude. At
// half amplitude T_4+T_5+T_6 measures as harmonics 1,2,3,4,5 at wrong weights --
// the chord folds down into a muddle. Anything built on this has to hold the
// drive at unity or accept that the chord dissolves as it gets quieter. Both
// are useful; the module makes it a switch.
#pragma once
#include <cmath>
#include <cstddef>

namespace dependents {

inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

//: How many harmonics the panel gives a trim of its own. Twelve reaches the
//: ninth of a 4:5:6:9 and covers every preset below except the minor sevenths,
//: which is as far as a row of trims can go before it stops being readable.
static const int kCustomHarmonics = 12;

//: The most harmonics any chord here asks for. 18 is the top of the minor
//: seventh's 10:12:15:18; going much past it is asking for aliasing rather than
//: music, and the chord table is bounded so the recurrence below can be too.
static const int kMaxHarmonic = 24;

/** T_0..T_n at x, by the recurrence T_n = 2x T_{n-1} - T_{n-2}.

    Written out rather than called per-harmonic: every member of a chord needs a
    different T_n of the *same* x, and computing them together is one pass
    instead of one pass per note. */
inline void chebyshev(float x, int upTo, float* t) {
    t[0] = 1.f;
    if (upTo >= 1) t[1] = x;
    for (int n = 2; n <= upTo; n++)
        t[n] = 2.f * x * t[n - 1] - t[n - 2];
}

/** A chord, as the harmonic numbers that sound it.

    These are just ratios, not equal temperament, because that is what falls out
    of a harmonic series -- the major third here is 5:4, fourteen cents flatter
    than a piano's, and the seventh is the flat 7:4 that no keyboard has. That
    is the sound of the thing and not a compromise in it. */
struct Chord {
    const char* name;
    int n[5];
    int count;
};

static const Chord kChords[] = {
    { "OCTAVE",  { 2,  4,  0,  0,  0 }, 2 },
    { "FIFTH",   { 2,  3,  0,  0,  0 }, 2 },
    { "SUS4",    { 6,  8,  9,  0,  0 }, 3 },
    { "MAJOR",   { 4,  5,  6,  0,  0 }, 3 },
    { "MINOR",   {10, 12, 15,  0,  0 }, 3 },
    { "DIM",     { 5,  6,  7,  0,  0 }, 3 },
    { "MAJ 6",   {12, 15, 18, 20,  0 }, 4 },
    { "DOM 7",   { 4,  5,  6,  7,  0 }, 4 },
    { "MIN 7",   {10, 12, 15, 18,  0 }, 4 },
    { "MAJ 7",   { 8, 10, 12, 15,  0 }, 4 },
    { "ADD 9",   { 4,  5,  6,  9,  0 }, 4 },
    { "DOM 9",   { 4,  5,  6,  7,  9 }, 5 },
    { "STACK",   { 1,  2,  3,  4,  5 }, 5 },
    // Count 0 means "not a preset": the weights come from the panel's own
    // per-harmonic trims instead. It is the last entry so the presets keep
    // their indices, and it morphs against them like any other chord -- a named
    // triad on one side of MORPH and a spectrum you drew on the other.
    { "CUSTOM",  { 0,  0,  0,  0,  0 }, 0 },
};
static const int kChordCount = (int)(sizeof kChords / sizeof kChords[0]);

/** Holds a signal at unit amplitude, so the recipe above stays true.

    A peak *hold* -- instant attack, slow release -- rather than a follower with
    an attack time. The instinct is to smooth the attack so the gain does not
    jump, and measuring it says the opposite: with the members of a major triad
    at 0.33, the stray energy between them goes 0.11 at a 2 ms attack, 0.23 at
    10 ms, 0.41 at 120 ms. A follower that lags never reaches the true peak, so
    the shaper is driven under unity, and under unity the chord folds into a
    muddle -- the same collapse the test below measures deliberately. Accuracy
    of the peak matters far more here than smoothness of the gain.

    Caught instantly and released over a second, the same signal at 12 %% of
    full scale gives 0.006 of stray against 0.334 of member: a clean triad.

    The cost is that a loud transient holds the gain down for about a second
    afterwards, which for an instrument whose whole business is a sustained
    chord is the right way round. */
struct Unity {
    float peak = 0.f;
    float atk = 1.f, rel = 0.f;

    void setRate(float sr) {
        atk = 1.f;
        rel = 1.f - std::exp(-1.f / (1.0f * sr));
    }

    /** Returns the gain to apply, not the signal: the caller may want to know
        how hard it is working. */
    float gain(float x) {
        float a = std::fabs(x);
        peak += (a > peak ? atk : rel) * (a - peak);
        // Below this there is nothing to normalise and everything to amplify.
        return peak > 0.02f ? clampf(1.f / peak, 0.f, 12.f) : 0.f;
    }

    void reset() { peak = 0.f; }
};

/** DC blocker: even-numbered T_n carry a constant term, so a chord with an even
    member sits off zero without this. */
struct DcBlock {
    float x1 = 0.f, y1 = 0.f;
    float process(float x, float r) {
        float y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0.f; }
};

/** A chord as a weight per harmonic, which is the form everything downstream
    wants.

    Held as a vector rather than as a list of members because two chords can
    then be crossfaded: the weights interpolate, so a major triad becomes a
    minor one by 5 fading down while 12 and 15 come up, and every position in
    between is a real spectrum rather than two chords playing at once. That is
    the difference between switching chords and morphing between them, and it is
    only available if the recipe is a vector.

    `tilt` leans the voicing toward its lowest member: 0 is even, 1 hard. */
inline void voicing(const Chord& c, float tilt, float* w) {
    for (int n = 0; n <= kMaxHarmonic; n++) w[n] = 0.f;
    for (int i = 0; i < c.count; i++) {
        int n = c.n[i];
        if (n < 1 || n > kMaxHarmonic) continue;
        // Weight by position in the chord, not by harmonic number: a voicing
        // should thin toward its top whichever ratios it happens to use.
        w[n] += 1.f - tilt * ((float)i / (float)(c.count > 1 ? c.count - 1 : 1));
    }
}

/** Crossfade two weight vectors into a third. */
inline void morph(const float* a, const float* b, float t, float* out) {
    for (int n = 0; n <= kMaxHarmonic; n++) out[n] = a[n] + (b[n] - a[n]) * t;
}

/** Sum w[n] * T_n(x), normalised so the total weight is unity.

    Normalising by the summed weight rather than by the count is what keeps the
    level steady through a morph: halfway between a three-note and a four-note
    chord there are seven partials sounding, and dividing by either 3 or 4 would
    step the output as the knob crossed. */
inline float excite(float x, const float* w) {
    float t[kMaxHarmonic + 1];
    chebyshev(clampf(x, -1.f, 1.f), kMaxHarmonic, t);
    float sum = 0.f, norm = 0.f;
    for (int n = 1; n <= kMaxHarmonic; n++) {
        if (w[n] == 0.f) continue;
        sum += w[n] * t[n];
        norm += w[n] < 0.f ? -w[n] : w[n];
    }
    return norm > 1e-6f ? sum / norm : 0.f;
}

/** The same recipe summed straight from a phase instead of shaped from a
    sample. T_n(cos p) *is* cos(n p), so when the input is a sine whose phase we
    already know there is no need to shape anything: the chord can be added up
    directly, exactly, with any member above Nyquist simply left out rather than
    folded back as an alias. No amount of oversampling beats not making the
    alias in the first place. */
inline float exciteFromPhase(double phase, float freq, float nyquist, const float* w) {
    float sum = 0.f, norm = 0.f;
    for (int n = 1; n <= kMaxHarmonic; n++) {
        if (w[n] == 0.f) continue;
        norm += w[n] < 0.f ? -w[n] : w[n];
        if (freq * (float)n >= nyquist) continue;
        sum += w[n] * (float)std::cos(2.0 * M_PI * (double)n * phase);
    }
    return norm > 1e-6f ? sum / norm : 0.f;
}

}  // namespace dependents
