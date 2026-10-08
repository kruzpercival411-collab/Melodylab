// MELODY LAB — analysis + generation engine
#include "MelodyEngine.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <array>
#include <numeric>

namespace ml
{
static constexpr double kPi = 3.14159265358979323846;

static int mod12 (int x)            { return ((x % 12) + 12) % 12; }
static int floorDiv (int a, int b)  { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

std::string noteName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[mod12 (pc)];
}

std::string keyName (int tonic, bool minor) { return noteName (tonic) + (minor ? " minor" : " major"); }

//==============================================================================
// ANALYSIS
//==============================================================================
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double sr, double fc, double q)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0;  b1 = (1.0 - c) / a0;  b2 = b0;
        a1 = -2.0 * c / a0;          a2 = (1.0 - al) / a0;
    }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

float median (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}
}

AnalysisResult analyse (const float* x, int n, double sr)
{
    AnalysisResult res;
    if (x == nullptr || n <= 0 || sr <= 0) return res;
    res.durationSec = n / sr;

    // --- downsample to ~16 kHz ------------------------------------------------
    const int factor = std::max (1, (int) std::floor (sr / 16000.0));
    const double dsr = sr / factor;
    std::vector<float> y;
    y.reserve ((size_t) (n / factor + 1));
    {
        Biquad f1, f2;
        f1.lowpass (sr, std::min (5500.0, sr * 0.45), 0.707);
        f2.lowpass (sr, std::min (5500.0, sr * 0.45), 0.707);
        for (int i = 0; i < n; ++i)
        {
            const float v = f2.process (f1.process (x[i]));
            if (i % factor == 0) y.push_back (v);
        }
    }

    // --- YIN pitch tracking -------------------------------------------------
    const int W      = (int) (0.030 * dsr);
    const int tauMin = std::max (2, (int) (dsr / 1100.0));
    const int tauMax = (int) (dsr / 65.0);
    const int hop    = std::max (1, (int) (0.010 * dsr));
    const int ny     = (int) y.size();
    const double hopSec = hop / dsr;

    const int numFrames = std::max (0, (ny - W - tauMax) / hop);
    std::vector<float> pitch ((size_t) numFrames, 0.0f), rms ((size_t) numFrames, 0.0f), aper ((size_t) numFrames, 1.0f);
    std::vector<float> d ((size_t) tauMax + 2, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        const float* fr = y.data() + (size_t) f * (size_t) hop;
        double e = 0.0;
        for (int j = 0; j < W; ++j) e += (double) fr[j] * fr[j];
        rms[(size_t) f] = (float) std::sqrt (e / W);
        if (e < 1e-9) continue;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.0f;
            const float* g = fr + tau;
            for (int j = 0; j < W; ++j) { const float df = fr[j] - g[j]; s += df * df; }
            d[(size_t) tau] = s;
        }
        // cumulative mean normalised difference
        double run = 0.0;
        d[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            d[(size_t) tau] = run > 0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau; break;
            }
        }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau) if (d[(size_t) tau] < d[(size_t) best]) best = tau;
            if (d[(size_t) best] > 0.30f) continue;
        }
        aper[(size_t) f] = d[(size_t) best];
        double t = best;
        if (best > tauMin && best < tauMax - 1)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1e-12) t = best + 0.5 * (a - c) / den;
        }
        const double hz = dsr / t;
        pitch[(size_t) f] = (float) (69.0 + 12.0 * std::log2 (hz / 440.0));
    }

    // --- energy gate (relative to loud parts of the vocal) -------------------
    {
        std::vector<float> sorted = rms;
        std::sort (sorted.begin(), sorted.end());
        const float ref = sorted.empty() ? 0.0f : sorted[(size_t) ((sorted.size() - 1) * 0.95)];
        const float gate = ref * 0.08f;
        for (size_t i = 0; i < pitch.size(); ++i)
            if (rms[i] < gate) pitch[i] = 0.0f;
    }

    // --- median smoothing of voiced frames (kills octave blips) --------------
    {
        std::vector<float> sm = pitch;
        for (int i = 0; i < numFrames; ++i)
        {
            if (pitch[(size_t) i] <= 0) continue;
            std::vector<float> win;
            for (int k = -2; k <= 2; ++k)
            {
                const int j = i + k;
                if (j >= 0 && j < numFrames && pitch[(size_t) j] > 0) win.push_back (pitch[(size_t) j]);
            }
            sm[(size_t) i] = median (win);
        }
        pitch.swap (sm);
    }

    // --- segment into notes ---------------------------------------------------
    const double frameCentre = (W * 0.5) / dsr;
    std::vector<VocalNote> notes;
    std::vector<float> cur;
    int curStart = -1, unvoicedRun = 0;

    auto closeNote = [&] (int endFrame)
    {
        if (curStart >= 0 && ! cur.empty())
        {
            const double s = curStart * hopSec + frameCentre, e = endFrame * hopSec + frameCentre;
            if (e - s >= 0.07)
                notes.push_back ({ s, e, median (cur) });
        }
        cur.clear(); curStart = -1;
    };

    for (int i = 0; i < numFrames; ++i)
    {
        const float p = pitch[(size_t) i];
        if (p > 0)
        {
            unvoicedRun = 0;
            if (curStart >= 0)
            {
                std::vector<float> tail (cur.end() - std::min<long> ((long) cur.size(), 6), cur.end());
                if (std::abs (p - median (tail)) > 0.8f) closeNote (i);
            }
            if (curStart < 0) curStart = i;
            cur.push_back (p);
        }
        else if (curStart >= 0 && ++unvoicedRun > 3)
        {
            closeNote (i - unvoicedRun + 1);
        }
    }
    closeNote (numFrames);

    // merge tiny gaps between the same pitch
    std::vector<VocalNote> merged;
    for (auto& nn : notes)
    {
        if (! merged.empty() && std::abs (merged.back().pitch - nn.pitch) < 0.5f && nn.startSec - merged.back().endSec < 0.06)
            merged.back().endSec = nn.endSec;
        else
            merged.push_back (nn);
    }

    res.notes = std::move (merged);
    res.key = detectKey (res.notes);
    res.valid = true;
    return res;
}

KeyResult detectKey (const std::vector<VocalNote>& notes)
{
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double hist[12] = {};
    for (auto& nn : notes)
        hist[mod12 ((int) std::lround (nn.pitch))] += std::min (2.0, nn.endSec - nn.startSec);

    auto corr = [&] (const double* prof, int tonic)
    {
        double mx = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mx += hist[i]; mp += prof[i]; }
        mx /= 12; mp /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[mod12 (i + tonic)] - mx, b = prof[i] - mp;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return (sxx > 0 && syy > 0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    struct Cand { double score; int tonic; bool minor; };
    std::vector<Cand> c;
    for (int t = 0; t < 12; ++t) { c.push_back ({ corr (maj, t), t, false }); c.push_back ({ corr (mnr, t), t, true }); }
    std::sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.score > b.score; });

    KeyResult k;
    k.tonic = c[0].tonic; k.minor = c[0].minor;
    k.altTonic = c[1].tonic; k.altMinor = c[1].minor;
    k.confidence = (float) std::clamp (c[0].score, 0.0, 1.0);
    return k;
}

//==============================================================================
// GENERATION
//==============================================================================
namespace
{
static const int kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int kMinor[7] = { 0, 2, 3, 5, 7, 8, 10 };

struct Rng
{
    std::mt19937 g;
    explicit Rng (uint32_t seed) : g (seed * 2654435761u + 12345u) {}
    float uni() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (g); }
    int range (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (g); }
};

struct Chord
{
    int degree = 0;
    int rootPc = 0;
    std::vector<int> pcs;        // absolute pitch classes, root first
    std::string name;
};

struct Ctx
{
    const GenSettings& s;
    const int* scale;
    int tonic;
    bool minor;
    explicit Ctx (const GenSettings& gs) : s (gs), scale (gs.keyMinor ? kMinor : kMajor), tonic (mod12 (gs.keyTonic)), minor (gs.keyMinor) {}

    int degPc (int deg) const { return mod12 (tonic + scale[((deg % 7) + 7) % 7]); }

    // degree (any integer) -> midi, degree 0 == tonic at 'base'
    int degToMidi (int deg, int base) const
    {
        const int oct = floorDiv (deg, 7);
        const int m = deg - oct * 7;
        return base + 12 * oct + scale[m];
    }

    bool inScale (int pc) const
    {
        for (int i = 0; i < 7; ++i) if (degPc (i) == pc) return true;
        return false;
    }

    Chord makeChord (int degree, bool sevenths, bool majorV) const
    {
        Chord c;
        c.degree = degree;
        const int r = scale[degree], t3 = scale[(degree + 2) % 7], t5 = scale[(degree + 4) % 7], t7 = scale[(degree + 6) % 7];
        int i3 = mod12 (t3 - r), i5 = mod12 (t5 - r), i7 = mod12 (t7 - r);
        if (majorV && minor && degree == 4) { i3 = 4; i7 = 10; }
        c.rootPc = mod12 (tonic + r);
        c.pcs = { c.rootPc, mod12 (c.rootPc + i3), mod12 (c.rootPc + i5) };
        if (sevenths) c.pcs.push_back (mod12 (c.rootPc + i7));

        std::string q;
        const bool isMaj = (i3 == 4 && i5 == 7), isMin = (i3 == 3 && i5 == 7), isDim = (i3 == 3 && i5 == 6);
        if (sevenths)
        {
            if (isMaj) q = (i7 == 11) ? "maj7" : "7";
            else if (isMin) q = "m7";
            else if (isDim) q = "m7b5";
        }
        else
        {
            if (isMin) q = "m";
            else if (isDim) q = "dim";
        }
        c.name = noteName (c.rootPc) + q;
        return c;
    }

    // scale pcs adjusted so raised chord tones (e.g. leading tone in V) replace the note below
    std::array<bool, 12> allowedPcs (const Chord& c) const
    {
        std::array<bool, 12> a {};
        for (int i = 0; i < 7; ++i) a[(size_t) degPc (i)] = true;
        for (int pc : c.pcs)
            if (! a[(size_t) pc]) { a[(size_t) pc] = true; a[(size_t) mod12 (pc - 1)] = false; }
        return a;
    }
};

bool contains (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }

int nearestPitchWithPc (int target, const std::vector<int>& pcs)
{
    int best = target, bestD = 99;
    for (int p = target - 6; p <= target + 6; ++p)
        if (contains (pcs, mod12 (p)) && std::abs (p - target) < bestD) { bestD = std::abs (p - target); best = p; }
    return best;
}

int nearestAllowed (int target, const std::array<bool, 12>& a)
{
    for (int dd = 0; dd < 6; ++dd)
    {
        if (a[(size_t) mod12 (target + dd)]) return target + dd;
        if (a[(size_t) mod12 (target - dd)]) return target - dd;
    }
    return target;
}

// -------------------------------------------------------------------------
// chord fitting
using PcWeights = std::array<double, 12>;

double fitScore (const Ctx& cx, const Chord& c, const PcWeights& w)
{
    double total = 0, sc = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (w[(size_t) pc] <= 0) continue;
        total += w[(size_t) pc];
        if (contains (c.pcs, pc))   sc += w[(size_t) pc];
        else if (cx.inScale (pc))   sc -= 0.15 * w[(size_t) pc];
        else                        sc -= 0.6 * w[(size_t) pc];
    }
    return total > 1e-6 ? sc / total : 0.0;
}

std::vector<int> voiceChord (const Chord& c, const std::vector<int>& prev, int lo, int hi)
{
    std::vector<int> best;
    double bestCost = 1e9;
    const int nt = (int) c.pcs.size();
    for (int inv = 0; inv < nt; ++inv)
    {
        for (int b = lo; b <= lo + 11; ++b)
        {
            if (mod12 (b) != c.pcs[(size_t) inv]) continue;
            std::vector<int> v { b };
            for (int k = 1; k < nt; ++k)
            {
                int p = v.back() + 1;
                while (mod12 (p) != c.pcs[(size_t) ((inv + k) % nt)]) ++p;
                v.push_back (p);
            }
            if (v.back() > hi) continue;
            double cost = 0;
            if (prev.size() == v.size())
                for (size_t k = 0; k < v.size(); ++k) cost += std::abs (v[k] - prev[k]);
            else
                cost = std::abs ((v.front() + v.back()) * 0.5 - (lo + hi) * 0.5) * 2.0;
            if (inv != 0) cost += 0.5;          // mild preference for root position
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
    }
    if (best.empty()) best = { lo + mod12 (c.rootPc - lo) };
    return best;
}

struct Pat { double pos, len; int interval; float vel; };

// -------------------------------------------------------------------------
// melody helpers
struct MotifNote { int step; int lenSteps; int off; };   // off = scale degrees above chord root
using Motif = std::vector<MotifNote>;

std::vector<int> makeRhythm (Rng& rng, Style st, float density, float complexity, bool sparse)
{
    float p[16];
    for (int i = 0; i < 16; ++i)
    {
        if (i == 0)            p[i] = 0.9f;
        else if (i == 8)       p[i] = 0.65f;
        else if (i % 4 == 0)   p[i] = 0.5f;
        else if (i % 2 == 0)   p[i] = 0.2f + 0.5f * density;
        else                   p[i] = (0.03f + 0.35f * density) * (0.4f + complexity);
    }
    switch (st)
    {
        case Style::House:     for (int i : { 2, 6, 10, 14 }) p[i] += 0.25f; p[0] -= 0.25f; break;
        case Style::RnB:       for (int i : { 3, 7, 11 }) p[i] += 0.2f; break;
        case Style::Trap:      for (int i : { 3, 5, 11, 13 }) p[i] += 0.15f; break;
        case Style::Emotional: for (int i = 0; i < 16; ++i) if (i % 4 != 0) p[i] *= 0.5f; break;
        case Style::Afrobeats: for (int i : { 3, 6, 10 }) p[i] += 0.3f; p[4] -= 0.2f; break;
        case Style::Pop: default: break;
    }
    const float scaleF = sparse ? 0.55f : (0.55f + density * 0.8f);
    std::vector<int> on;
    for (int i = 0; i < 16; ++i) if (rng.uni() < std::clamp (p[i] * scaleF, 0.0f, 0.97f)) on.push_back (i);
    if (on.size() < 2) on = { 0, 8 };
    return on;
}

Motif makeMotif (Rng& rng, const GenSettings& s, bool cadence)
{
    auto steps = makeRhythm (rng, s.style, s.density, s.complexity, cadence);
    if (cadence)
    {
        // cadence bars breathe: drop the last quarter so the final note rings
        steps.erase (std::remove_if (steps.begin(), steps.end(), [] (int st) { return st > 10; }), steps.end());
        if (steps.empty()) steps = { 0 };
    }
    const int maxLen = (s.style == Style::Emotional || s.density < 0.35f) ? 8 : 4;

    Motif m;
    int cur = std::array<int, 4> { 0, 2, 4, 7 }[(size_t) rng.range (0, 3)];
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const int st = steps[i];
        const int nextSt = (i + 1 < steps.size()) ? steps[i + 1] : 16;
        int len = std::min (nextSt - st, maxLen);
        if (cadence && i + 1 == steps.size()) len = 16 - st;

        if (i > 0)
        {
            const float r = rng.uni();
            const int dir = (cur > 5) ? -1 : (cur < 1 ? 1 : (rng.uni() < 0.5f ? -1 : 1));
            if (r < 0.55f)                     cur += dir;
            else if (r < 0.55f + 0.25f * (0.5f + s.complexity)) cur += dir * rng.range (2, 4);
            // else: repeat the note
        }
        cur = std::clamp (cur, -3, 9);
        if (st % 4 == 0)                       // strong beats land on chord tones
        {
            const int m7 = ((cur % 7) + 7) % 7;
            if (m7 != 0 && m7 != 2 && m7 != 4) cur += (m7 == 1 || m7 == 3 || m7 == 5) ? -1 : 1;
        }
        if (cadence && i + 1 == steps.size())  // resolve to the chord root
            cur = (cur > 3) ? 7 : 0;
        m.push_back ({ st, std::max (1, len), cur });
    }
    return m;
}

Motif vary (Rng& rng, Motif m)
{
    if (m.empty()) return m;
    const size_t changes = std::min<size_t> (m.size(), (size_t) rng.range (1, 2));
    for (size_t k = 0; k < changes; ++k)
    {
        auto& n = m[m.size() - 1 - k];
        n.off += rng.uni() < 0.5f ? -1 : (rng.uni() < 0.5f ? 1 : 2);
        n.off = std::clamp (n.off, -3, 9);
    }
    return m;
}

// busy vocal intervals (beats), merged
std::vector<std::pair<double, double>> busyRegions (const std::vector<Note>& vocal, double mergeGap)
{
    std::vector<std::pair<double, double>> r;
    for (auto& v : vocal)
    {
        const double a = v.start, b = v.start + v.length;
        if (! r.empty() && a - r.back().second < mergeGap) r.back().second = std::max (r.back().second, b);
        else r.push_back ({ a, b });
    }
    return r;
}

void fixClashes (std::vector<Note>& mel, const std::vector<Note>& vocal, const Ctx& cx,
                 const std::vector<Chord>& barChords)
{
    for (auto& m : mel)
    {
        auto clashesWith = [&] (int pitch)
        {
            for (auto& v : vocal)
            {
                if (v.start >= m.start + m.length) break;
                if (v.start + v.length <= m.start) continue;
                const int iv = mod12 (pitch - v.pitch);
                if (iv == 1 || iv == 11 || iv == 6) return true;
            }
            return false;
        };
        if (! clashesWith (m.pitch)) continue;
        const int bar = std::clamp ((int) (m.start / 4.0), 0, (int) barChords.size() - 1);
        const auto allowed = cx.allowedPcs (barChords[(size_t) bar]);
        for (int dd : { 1, -1, 2, -2, 3, -3 })
        {
            const int p = m.pitch + dd;
            if (allowed[(size_t) mod12 (p)] && ! clashesWith (p)) { m.pitch = p; break; }
        }
    }
}
} // namespace

Generated generate (const AnalysisResult& an, const GenSettings& s)
{
    Generated out;
    Ctx cx (s);
    const double spb = 60.0 / std::max (20.0, s.bpm);
    auto toBeat = [&] (double sec) { return (sec - s.offsetSec) / spb; };

    // vocal in beats
    for (auto& v : an.notes)
    {
        const double a = toBeat (v.startSec), b = toBeat (v.endSec);
        if (b <= 0) continue;
        out.vocal.push_back ({ std::max (0.0, a), b - std::max (0.0, a), (int) std::lround (v.pitch), 0.8f });
    }
    std::sort (out.vocal.begin(), out.vocal.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    const int bars = std::max (1, (int) std::ceil (toBeat (an.durationSec) / 4.0 - 1e-6));
    out.totalBeats = bars * 4.0;

    const int bpc = std::clamp (s.barsPerChord, 1, 4);
    const int nSeg = (bars + bpc - 1) / bpc;
    const double segLen = 4.0 * bpc;

    // ---- vocal pitch-class weight per chord segment ---------------------------
    std::vector<PcWeights> segW ((size_t) nSeg, PcWeights {});
    for (auto& v : out.vocal)
    {
        const double a = v.start, b = v.start + v.length;
        const bool strong = std::fmod (a + 1e-3, 1.0) < 0.12;
        for (int sg = std::max (0, (int) (a / segLen)); sg < nSeg && sg * segLen < b; ++sg)
        {
            const double ov = std::min (b, (sg + 1) * segLen) - std::max (a, sg * segLen);
            if (ov > 0) segW[(size_t) sg][(size_t) mod12 (v.pitch)] += ov * (strong ? 1.5 : 1.0);
        }
    }

    const bool sevenths = (s.style == Style::RnB) || s.complexity > 0.7f;
    const bool majorV   = cx.minor && s.complexity >= 0.5f;
    const std::vector<int> cands = cx.minor ? std::vector<int> { 0, 2, 3, 4, 5, 6 } : std::vector<int> { 0, 1, 2, 3, 4, 5 };
    std::vector<Chord> chordFor ((size_t) 7);
    for (int d = 0; d < 7; ++d) chordFor[(size_t) d] = cx.makeChord (d, sevenths, majorV);

    std::vector<int> segDeg ((size_t) nSeg, 0);
    Rng hr (s.harmonySeed);

    if (s.harmonyMode == HarmonyMode::Loop)
    {
        static const std::vector<std::vector<int>> majLoops = {
            { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 3, 0, 4, 5 },
            { 1, 4, 0, 5 }, { 0, 3, 0, 4 }, { 3, 4, 5, 5 }, { 0, 2, 3, 4 }, { 5, 4, 3, 4 } };
        static const std::vector<std::vector<int>> minLoops = {
            { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 5, 3, 4 }, { 5, 6, 0, 0 },
            { 0, 3, 6, 2 }, { 0, 2, 6, 3 }, { 3, 5, 0, 4 }, { 5, 2, 6, 0 }, { 0, 0, 5, 6 } };
        const auto& loops = cx.minor ? minLoops : majLoops;

        struct Opt { double score; std::vector<int> seq; };
        std::vector<Opt> opts;
        for (auto& L : loops)
            for (int rot = 0; rot < 4; ++rot)
            {
                std::vector<int> seq { L[(size_t) rot], L[(size_t) ((rot + 1) % 4)], L[(size_t) ((rot + 2) % 4)], L[(size_t) ((rot + 3) % 4)] };
                double sc = 0;
                for (int sg = 0; sg < nSeg; ++sg) sc += fitScore (cx, chordFor[(size_t) seq[(size_t) (sg % 4)]], segW[(size_t) sg]);
                sc /= nSeg;
                if (rot == 0) sc += 0.03;                 // templates start where they were written
                opts.push_back ({ sc, seq });
            }
        std::sort (opts.begin(), opts.end(), [] (const Opt& a, const Opt& b) { return a.score > b.score; });
        size_t pick = 0;
        if (s.harmonySeed > 1)
        {
            const size_t top = std::min<size_t> (opts.size(), 8);
            double tot = 0; std::vector<double> w;
            for (size_t i = 0; i < top; ++i) { w.push_back (std::exp ((opts[i].score - opts[0].score) * 6.0)); tot += w.back(); }
            double r = hr.uni() * tot;
            for (size_t i = 0; i < top; ++i) { r -= w[i]; if (r <= 0) { pick = i; break; } }
        }
        for (int sg = 0; sg < nSeg; ++sg) segDeg[(size_t) sg] = opts[pick].seq[(size_t) (sg % 4)];
    }
    else
    {
        // follow the vocal: Viterbi over segments with transition priors
        const size_t C = cands.size();
        const double jitter = 0.08 + 0.25 * s.complexity;
        std::vector<std::vector<double>> sc ((size_t) nSeg, std::vector<double> (C, -1e9));
        std::vector<std::vector<int>> bp ((size_t) nSeg, std::vector<int> (C, 0));
        auto trans = [&] (int from, int to)
        {
            if (from == to) return -0.25;
            const int iv = mod12 (cx.scale[to] - cx.scale[from]);
            double t = 0.0;
            if (iv == 5) t += 0.25;               // up a 4th / down a 5th
            if (to == 0) t += 0.12;
            return t;
        };
        for (size_t c = 0; c < C; ++c)
            sc[0][c] = fitScore (cx, chordFor[(size_t) cands[c]], segW[0]) + (cands[c] == 0 ? 0.3 : 0.0) + hr.uni() * jitter;
        for (int sg = 1; sg < nSeg; ++sg)
            for (size_t c = 0; c < C; ++c)
            {
                const double local = fitScore (cx, chordFor[(size_t) cands[c]], segW[(size_t) sg]) + hr.uni() * jitter;
                for (size_t p = 0; p < C; ++p)
                {
                    const double v = sc[(size_t) sg - 1][p] + local + trans (cands[p], cands[c]);
                    if (v > sc[(size_t) sg][c]) { sc[(size_t) sg][c] = v; bp[(size_t) sg][c] = (int) p; }
                }
            }
        size_t best = 0;
        for (size_t c = 1; c < C; ++c) if (sc[(size_t) nSeg - 1][c] > sc[(size_t) nSeg - 1][best]) best = c;
        for (int sg = nSeg - 1; sg >= 0; --sg)
        {
            segDeg[(size_t) sg] = cands[best];
            best = (size_t) bp[(size_t) sg][best];
        }
    }

    // chord per bar
    std::vector<Chord> barChord ((size_t) bars);
    for (int b = 0; b < bars; ++b) barChord[(size_t) b] = chordFor[(size_t) segDeg[(size_t) (b / bpc)]];
    for (int sg = 0; sg < nSeg; ++sg)
        out.chordNames.push_back ({ sg * segLen, std::min (segLen, out.totalBeats - sg * segLen), chordFor[(size_t) segDeg[(size_t) sg]].name });

    // ---- chords -------------------------------------------------------------------
    {
        std::vector<Pat> pat;
        const float d = s.density;
        switch (s.style)
        {
            case Style::House:     pat = { { 0.5, 0.3, 0, .8f }, { 1.5, 0.3, 0, .75f }, { 2.5, 0.3, 0, .8f }, { 3.5, 0.3, 0, .75f } }; break;
            case Style::Afrobeats: pat = { { 0, 0.5, 0, .8f }, { 0.75, 0.5, 0, .7f }, { 1.5, 0.5, 0, .75f }, { 2.5, 0.5, 0, .7f }, { 3.25, 0.5, 0, .7f } }; break;
            case Style::RnB:       pat = d > 0.5f ? std::vector<Pat> { { 0, 2.5, 0, .7f }, { 2.5, 1.5, 0, .65f } } : std::vector<Pat> { { 0, 4, 0, .7f } }; break;
            case Style::Pop:       pat = d > 0.6f ? std::vector<Pat> { { 0, 1.5, 0, .75f }, { 1.5, 1, 0, .65f }, { 2.5, 1.5, 0, .7f } }
                                                  : (d > 0.3f ? std::vector<Pat> { { 0, 2, 0, .75f }, { 2, 2, 0, .7f } } : std::vector<Pat> { { 0, 4, 0, .75f } }); break;
            case Style::Trap:
            case Style::Emotional:
            default:               pat = { { 0, 4, 0, .7f } }; break;
        }
        std::vector<int> prev;
        for (int b = 0; b < bars; ++b)
        {
            const auto v = voiceChord (barChord[(size_t) b], prev, 52, 72);
            prev = v;
            for (auto& p : pat)
                for (int pitch : v)
                    out.chords.push_back ({ b * 4.0 + p.pos, p.len * 0.97, pitch, p.vel });
        }
    }

    // ---- bass ---------------------------------------------------------------------
    {
        std::vector<Pat> pat;
        const float d = s.density, c = s.complexity;
        switch (s.style)
        {
            case Style::House:     pat = { { 0.5, 0.35, 0, .9f }, { 1.5, 0.35, 0, .85f }, { 2.5, 0.35, 0, .9f }, { 3.5, 0.35, 0, .85f } };
                                   if (d > 0.6f) pat.push_back ({ 1.75, 0.2, 12, .7f }); break;
            case Style::Trap:      pat = { { 0, 1.4, 0, .95f }, { 1.5, 0.45, 0, .8f }, { 2.75, 1.2, c > 0.5f ? 12 : 0, .9f } };
                                   if (d > 0.6f) pat.push_back ({ 2.25, 0.25, 0, .7f }); break;
            case Style::RnB:       pat = { { 0, 1, 0, .9f }, { 1.5, 0.5, 0, .75f }, { 2, 1, 7, .8f }, { 3, 0.45, 0, .75f }, { 3.5, 0.45, 99, .7f } }; break;
            case Style::Emotional: pat = d > 0.5f ? std::vector<Pat> { { 0, 2, 0, .85f }, { 2, 2, 0, .8f } } : std::vector<Pat> { { 0, 4, 0, .85f } }; break;
            case Style::Afrobeats: pat = { { 0, 0.7, 0, .9f }, { 0.75, 0.7, 0, .75f }, { 1.5, 0.45, 7, .8f }, { 2.5, 0.7, 0, .85f }, { 3.5, 0.45, 12, .75f } }; break;
            case Style::Pop: default:
                                   pat = { { 0, 1.45, 0, .9f }, { 1.5, 0.45, 0, .75f }, { 2, 1.9, 0, .85f } };
                                   if (d > 0.6f) { pat.back() = { 2, 1.45, 0, .85f }; pat.push_back ({ 3.5, 0.45, 7, .75f }); }
                                   break;
        }
        for (int b = 0; b < bars; ++b)
        {
            const int root = 33 + mod12 (barChord[(size_t) b].rootPc - 9);
            const int nextRoot = 33 + mod12 (barChord[(size_t) std::min (b + 1, bars - 1)].rootPc - 9);
            const bool lastOfSeg = ((b + 1) % bpc == 0);
            for (auto& p : pat)
            {
                int pitch = root + p.interval;
                if (p.interval == 99) { if (! lastOfSeg || nextRoot == root) continue; pitch = nextRoot - 1; }
                out.bass.push_back ({ b * 4.0 + p.pos, p.len, pitch, p.vel });
            }
        }
    }

    // ---- melody -------------------------------------------------------------------
    {
        Rng mr (s.melodySeed);
        int base = 60 + cx.tonic;
        if (base < 64) base += 12;
        base += 12 * std::clamp (s.octave, -2, 2);

        auto rootDegree = [&] (const Chord& c) { const int dg = c.degree; return dg <= 3 ? dg : dg - 7; };

        if (s.melodyMode == MelodyMode::Harmony)
        {
            // diatonic third (or sixth) below each sung note
            for (auto& v : out.vocal)
            {
                const double st = std::round (v.start * 4.0) / 4.0;
                const double ln = std::max (0.25, std::round (v.length * 4.0) / 4.0);
                if (ln < 0.25 || st >= out.totalBeats) continue;
                const int bar = std::clamp ((int) (st / 4.0), 0, bars - 1);
                const auto allowed = cx.allowedPcs (barChord[(size_t) bar]);
                const int vp = nearestAllowed (v.pitch, allowed);
                int steps = (s.complexity > 0.6f && ln >= 1.0) ? 5 : 2;
                int p = vp - 1, count = 0;
                while (p > vp - 12) { if (allowed[(size_t) mod12 (p)] && ++count == steps) break; --p; }
                p += 12 * s.octave;
                if (! out.melody.empty() && out.melody.back().start + out.melody.back().length > st)
                    out.melody.back().length = std::max (0.1, st - out.melody.back().start);
                out.melody.push_back ({ st, ln * 0.95, p, 0.75f });
            }
        }
        else if (s.melodyMode == MelodyMode::Arp)
        {
            const double rate = s.density > 0.55f ? 0.25 : 0.5;
            const int shape = mr.range (0, 2);   // up, up-down, skip
            for (int b = 0; b < bars; ++b)
            {
                const auto& ch = barChord[(size_t) b];
                std::vector<int> tones;
                for (int p = base - 3; p <= base + 15; ++p) if (contains (ch.pcs, mod12 (p))) tones.push_back (p);
                if (tones.empty()) continue;
                std::vector<int> seq = tones;
                if (shape == 1) for (int i = (int) tones.size() - 2; i > 0; --i) seq.push_back (tones[(size_t) i]);
                if (shape == 2) { seq.clear(); for (size_t i = 0; i < tones.size(); ++i) { seq.push_back (tones[i]); seq.push_back (tones[(i + 2) % tones.size()]); } }
                int idx = 0;
                for (double t = 0; t < 4.0 - 1e-6; t += rate)
                {
                    const int p = seq[(size_t) (idx++ % (int) seq.size())];
                    if (mr.uni() < (1.0f - s.density) * 0.2f) continue;
                    const bool strong = std::fmod (t, 1.0) < 1e-6;
                    out.melody.push_back ({ b * 4.0 + t, rate * 0.85, p, strong ? 0.8f : 0.62f + mr.uni() * 0.08f });
                }
            }
        }
        else
        {
            // Hook / Counter: motif-based phrases A A' A B, re-fitted to each chord
            Motif A = makeMotif (mr, s, false), C = makeMotif (mr, s, false), Bc = makeMotif (mr, s, true);
            Motif A2 = vary (mr, A), C2 = vary (mr, C);
            for (int b = 0; b < bars; ++b)
            {
                const int inPhrase = b % 4, phrase = b / 4;
                const bool alt = (phrase % 4 == 2) && s.complexity > 0.25f;
                const Motif& m = inPhrase == 3 ? Bc : (inPhrase == 1 ? (alt ? C2 : A2) : (alt && inPhrase == 0 ? C : A));
                const auto& ch = barChord[(size_t) b];
                const auto allowed = cx.allowedPcs (ch);
                const int rd = rootDegree (ch);

                std::vector<Note> barNotes;
                for (auto& mn : m)
                {
                    int p = cx.degToMidi (rd + mn.off, base);
                    p = nearestAllowed (p, allowed);
                    if (mn.step % 4 == 0) p = nearestPitchWithPc (p, ch.pcs);
                    if (b == bars - 1 && &mn == &m.back()) p = nearestPitchWithPc (p, { cx.tonic });
                    const float vel = mn.step % 8 == 0 ? 0.85f : (mn.step % 4 == 0 ? 0.75f : 0.62f + mr.uni() * 0.08f);
                    barNotes.push_back ({ b * 4.0 + mn.step * 0.25, mn.lenSteps * 0.25 * 0.92, p, vel });
                }
                // keep each bar inside the register
                double mean = 0; for (auto& n : barNotes) mean += n.pitch;
                if (! barNotes.empty())
                {
                    mean /= (double) barNotes.size();
                    const int shift = mean > base + 13 ? -12 : (mean < base - 6 ? 12 : 0);
                    for (auto& n : barNotes) { n.pitch += shift; out.melody.push_back (n); }
                }
            }

            if (s.melodyMode == MelodyMode::Counter && ! out.vocal.empty())
            {
                // keep only what falls in the vocal's gaps (call and response)
                const auto busy = busyRegions (out.vocal, 0.25 + (1.0 - s.density) * 0.75);
                std::vector<Note> kept;
                for (auto n : out.melody)
                {
                    bool ok = true;
                    for (auto& r : busy)
                    {
                        if (r.first >= n.start + n.length + 0.05) break;
                        if (r.second + 0.05 <= n.start) continue;
                        if (n.start < r.first - 0.05)
                        {
                            const double newLen = r.first - 0.05 - n.start;
                            if (newLen >= 0.25) { n.length = newLen; continue; }
                        }
                        ok = false; break;
                    }
                    if (ok) kept.push_back (n);
                    else if (s.density > 0.8f) { n.velocity *= 0.55f; kept.push_back (n); }
                }
                out.melody.swap (kept);
            }
        }

        if (s.melodyMode != MelodyMode::Harmony)
            fixClashes (out.melody, out.vocal, cx, barChord);
    }

    auto clip = [&] (std::vector<Note>& v)
    {
        v.erase (std::remove_if (v.begin(), v.end(), [&] (const Note& n) { return n.start >= out.totalBeats || n.start < 0; }), v.end());
        for (auto& n : v) n.length = std::min (n.length, out.totalBeats - n.start);
        std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    };
    clip (out.melody); clip (out.chords); clip (out.bass);
    return out;
}
} // namespace ml
