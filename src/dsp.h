// Vox Studio V2.3 - chaîne de traitement de la voix (mono, 48 kHz, float, temps réel)
#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <complex>
#include <deque>

extern "C" {
#include "rnnoise.h"
}

namespace dsp {

constexpr float kPi = 3.14159265358979f;
constexpr float kFs = 48000.0f;

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1e-9f)); }
inline float coefFromMs(float ms, float fs = kFs) { return std::exp(-1.0f / (std::max(ms, 0.01f) * 0.001f * fs)); }

// ---------------------------------------------------------------- Biquad (RBJ)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    // Réponse en amplitude (dB) à la fréquence f
    float responseDb(float f) const {
        float w = 2 * kPi * f / kFs;
        std::complex<float> z1 = std::polar(1.0f, -w), z2 = z1 * z1;
        std::complex<float> num = b0 + b1 * z1 + b2 * z2, den = 1.0f + a1 * z1 + a2 * z2;
        return 20.0f * std::log10(std::max(std::abs(num / den), 1e-9f));
    }
    void highpass(float f, float q = 0.7071f) {
        float w = 2 * kPi * f / kFs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowpass(float f, float q = 0.7071f) {
        float w = 2 * kPi * f / kFs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass(float f, float q) {
        float w = 2 * kPi * f / kFs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak(float f, float q, float g) {
        float A = std::pow(10.0f, g / 40.0f), w = 2 * kPi * f / kFs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf(float f, float g, float S = 1.0f) {
        float A = std::pow(10.0f, g / 40.0f), w = 2 * kPi * f / kFs, c = std::cos(w), s = std::sin(w);
        float al = s / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
            (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf(float f, float g, float S = 1.0f) {
        float A = std::pow(10.0f, g / 40.0f), w = 2 * kPi * f / kFs, c = std::cos(w), s = std::sin(w);
        float al = s / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
            (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

// ---------------------------------------------------------------- Suppression de bruit IA (RNNoise)
// Travaille par trames de 480 échantillons : 10 ms de latence fixe.
class Denoiser {
public:
    Denoiser() { st_ = rnnoise_create(nullptr); reset(); }
    ~Denoiser() { if (st_) rnnoise_destroy(st_); }
    Denoiser(const Denoiser&) = delete;
    Denoiser& operator=(const Denoiser&) = delete;
    void reset() { inN_ = 0; outR_ = 0; std::fill(out_, out_ + kN, 0.0f); vad_ = 0; }
    float vad() const { return vad_; }
    // amount : 0 = signal d'origine, 1 = débruitage complet (toujours retardé de 10 ms pour rester aligné)
    inline float process(float x, float amount) {
        in_[inN_] = x;
        float y = out_[outR_];
        ++inN_; ++outR_;
        if (inN_ == kN) {
            float tmp[kN], den[kN];
            for (int i = 0; i < kN; ++i) tmp[i] = in_[i] * 32768.0f;
            vad_ = rnnoise_process_frame(st_, den, tmp);
            for (int i = 0; i < kN; ++i) out_[i] = amount * (den[i] / 32768.0f) + (1 - amount) * in_[i];
            inN_ = 0; outR_ = 0;
        }
        return y;
    }
private:
    static constexpr int kN = 480;
    DenoiseState* st_ = nullptr;
    float in_[kN]{}, out_[kN]{};
    int inN_ = 0, outR_ = 0;
    float vad_ = 0;
};

// ---------------------------------------------------------------- Pitch (deux têtes de lecture croisées)
class PitchShifter {
public:
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; phase_ = 0; }
    inline float process(float x, float semis) {
        buf_[w_] = x;
        float ratio = std::pow(2.0f, semis / 12.0f);
        phase_ += (1.0f - ratio) / kWin;
        phase_ -= std::floor(phase_);
        float p2 = phase_ + 0.5f; p2 -= std::floor(p2);
        float y = read(phase_ * kWin) * win(phase_) + read(p2 * kWin) * win(p2);
        w_ = (w_ + 1) % kSize;
        return y;
    }
private:
    static constexpr int kWin = 1920;   // 40 ms
    static constexpr int kSize = 4096;
    static float win(float p) { return 0.5f - 0.5f * std::cos(2 * kPi * p); }
    float read(float delay) const {
        float pos = (float)w_ - delay - 1.0f;
        while (pos < 0) pos += kSize;
        int i0 = (int)pos; float fr = pos - i0;
        return buf_[i0 % kSize] * (1 - fr) + buf_[(i0 + 1) % kSize] * fr;
    }
    std::vector<float> buf_ = std::vector<float>(kSize, 0.0f);
    int w_ = 0; float phase_ = 0;
};

// ---------------------------------------------------------------- Réverb (Freeverb mono)
class Reverb {
public:
    Reverb() {
        const int combT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        const int apT[4] = {556, 441, 341, 225};
        for (int i = 0; i < 8; ++i) comb_[i].assign((size_t)(combT[i] * kFs / 44100.0f), 0.0f);
        for (int i = 0; i < 4; ++i) ap_[i].assign((size_t)(apT[i] * kFs / 44100.0f), 0.0f);
        pre_.assign(4800, 0.0f);
    }
    void reset() {
        for (auto& c : comb_) std::fill(c.begin(), c.end(), 0.0f);
        for (auto& a : ap_) std::fill(a.begin(), a.end(), 0.0f);
        std::fill(pre_.begin(), pre_.end(), 0.0f);
        for (int i = 0; i < 8; ++i) { ci_[i] = 0; lp_[i] = 0; }
        for (int i = 0; i < 4; ++i) ai_[i] = 0;
        pi_ = 0;
    }
    // size 0..1, damp 0..1 ; renvoie le signal mouillé uniquement
    inline float process(float x, float size, float damp, int predelay) {
        pre_[pi_] = x;
        int rd = pi_ - predelay; if (rd < 0) rd += (int)pre_.size();
        float in = pre_[rd] * 0.015f;
        pi_ = (pi_ + 1) % (int)pre_.size();
        float fb = 0.70f + 0.28f * size, d = 0.05f + 0.6f * damp, out = 0;
        for (int i = 0; i < 8; ++i) {
            auto& b = comb_[i]; float y = b[ci_[i]];
            lp_[i] = y * (1 - d) + lp_[i] * d;
            b[ci_[i]] = in + lp_[i] * fb;
            ci_[i] = (ci_[i] + 1) % (int)b.size();
            out += y;
        }
        for (int i = 0; i < 4; ++i) {
            auto& b = ap_[i]; float bo = b[ai_[i]];
            b[ai_[i]] = out + bo * 0.5f;
            out = bo - out;
            ai_[i] = (ai_[i] + 1) % (int)b.size();
        }
        return out;
    }
private:
    std::vector<float> comb_[8], ap_[4], pre_;
    int ci_[8]{}, ai_[4]{}, pi_ = 0;
    float lp_[8]{};
};

// ---------------------------------------------------------------- Écho
class Echo {
public:
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; lp_.reset(); }
    void prepare() { lp_.lowpass(3500.0f); }
    inline float process(float x, float timeMs, float fb) {
        int d = std::clamp((int)(timeMs * 48.0f), 1, (int)buf_.size() - 1);
        int r = w_ - d; if (r < 0) r += (int)buf_.size();
        float y = buf_[r];
        buf_[w_] = x + lp_.process(y) * fb;
        w_ = (w_ + 1) % (int)buf_.size();
        return y;
    }
private:
    std::vector<float> buf_ = std::vector<float>(48000, 0.0f);
    int w_ = 0; Biquad lp_;
};

// ---------------------------------------------------------------- Suréchantillonnage x2 (saturation sans aliasing)
class Oversampler2x {
public:
    Oversampler2x() {
        // FIR demi-bande 31 coefficients (sinc fenêtré Blackman), coupure fs/4 au taux suréchantillonné
        for (int i = 0; i < kTaps; ++i) {
            int n = i - kTaps / 2;
            float sinc = n == 0 ? 0.5f : std::sin(kPi * n * 0.5f) / (kPi * n);
            float w = 0.42f - 0.5f * std::cos(2 * kPi * i / (kTaps - 1)) + 0.08f * std::cos(4 * kPi * i / (kTaps - 1));
            h_[i] = sinc * w;
        }
        float sum = 0; for (float v : h_) sum += v;
        for (float& v : h_) v /= sum;
        reset();
    }
    void reset() { std::fill(up_, up_ + kTaps, 0.0f); std::fill(dn_, dn_ + kTaps, 0.0f); ui_ = di_ = 0; }
    template <class F> inline float process(float x, F&& f) {
        float o[2];
        for (int k = 0; k < 2; ++k) {       // insertion de zéros + filtrage (gain x2)
            up_[ui_] = k == 0 ? 2.0f * x : 0.0f;
            o[k] = f(conv(up_, ui_));
            ui_ = (ui_ + 1) % kTaps;
        }
        dn_[di_] = o[0]; di_ = (di_ + 1) % kTaps;
        dn_[di_] = o[1];
        float y = conv(dn_, di_);
        di_ = (di_ + 1) % kTaps;
        return y;
    }
private:
    static constexpr int kTaps = 31;
    float conv(const float* b, int head) const {
        float acc = 0; int idx = head;
        for (int i = 0; i < kTaps; ++i) { acc += h_[i] * b[idx]; idx = idx == 0 ? kTaps - 1 : idx - 1; }
        return acc;
    }
    float h_[kTaps], up_[kTaps], dn_[kTaps];
    int ui_ = 0, di_ = 0;
};

// ---------------------------------------------------------------- Compresseur à caractère (VCA / Opto / FET) avec dosage parallèle
enum CompStyle { CompVCA = 0, CompOpto, CompFET };
class Compressor {
public:
    void reset() { det_ = 0; rms_ = 0; slow_ = 0; gr_ = 0; }
    // renvoie le signal compressé (avec gain de compensation), gr = réduction instantanée (dB, <= 0)
    inline float process(float x, int style, float thr, float ratio, float attMs, float relMs, float makeup, float& gr) {
        float a = std::fabs(x), lvl;
        if (style == CompOpto) {          // détection RMS lente, relâchement dépendant du programme
            float c = coefFromMs(30.0f); rms_ = c * rms_ + (1 - c) * a * a;
            lvl = 10 * std::log10(std::max(rms_ * 2.0f, 1e-12f));
        } else if (style == CompFET) {    // crête rapide
            float c = a > det_ ? coefFromMs(std::max(0.05f, attMs * 0.2f)) : coefFromMs(relMs * 0.6f);
            det_ = c * det_ + (1 - c) * a; lvl = linToDb(det_);
        } else {                          // VCA : mélange crête / RMS
            float c = a > det_ ? coefFromMs(attMs) : coefFromMs(relMs);
            det_ = c * det_ + (1 - c) * a;
            float cr = coefFromMs(10.0f); rms_ = cr * rms_ + (1 - cr) * a * a;
            lvl = 0.5f * linToDb(det_) + 0.5f * 10 * std::log10(std::max(rms_ * 2.0f, 1e-12f));
        }
        float knee = style == CompOpto ? 12.0f : (style == CompFET ? 2.0f : 6.0f);
        float r = style == CompOpto ? std::min(ratio, 6.0f) : ratio;
        float over = lvl - thr, target;
        if (2 * over < -knee) target = 0;
        else if (2 * std::fabs(over) <= knee) { float t = over + knee / 2; target = (1 / r - 1) * t * t / (2 * knee); }
        else target = (1 / r - 1) * over;
        if (style == CompOpto) {
            // attaque 10 ms ; relâchement en deux temps : rapide 60 ms puis lent 1,2 s
            float c = target < gr_ ? coefFromMs(10.0f) : coefFromMs(60.0f);
            gr_ = c * gr_ + (1 - c) * target;
            float cs = target < slow_ ? coefFromMs(200.0f) : coefFromMs(1200.0f);
            slow_ = cs * slow_ + (1 - cs) * target;
            gr = std::min(gr_ * 0.7f + slow_ * 0.3f, 0.0f);
        } else gr = target;
        float y = x * dbToLin(gr) * makeup;
        if (style == CompFET) y = std::tanh(y * 1.15f) / 1.15f;   // couleur des transistors
        return y;
    }
private:
    float det_ = 0, rms_ = 0, slow_ = 0, gr_ = 0;
};

// ---------------------------------------------------------------- Densité radio : compresseur 3 bandes (crossovers Linkwitz-Riley 24 dB/oct)
class Multiband {
public:
    void prepare() {
        for (auto* b : {&lp1a_, &lp1b_, &hp1a_, &hp1b_}) b->reset();
        for (auto* b : {&lp2a_, &lp2b_, &hp2a_, &hp2b_, &aplA_, &aplB_, &aphA_, &aphB_}) b->reset();
        lp1a_.lowpass(220); lp1b_.lowpass(220); hp1a_.highpass(220); hp1b_.highpass(220);
        lp2a_.lowpass(3200); lp2b_.lowpass(3200); hp2a_.highpass(3200); hp2b_.highpass(3200);
        aplA_.lowpass(3200); aplB_.lowpass(3200); aphA_.highpass(3200); aphB_.highpass(3200);
        for (auto& c : comp_) c.reset();
    }
    // amount 0..1 ; gr[3] reçoit la réduction par bande
    inline float process(float x, float amount, float* gr) {
        float lo = lp1b_.process(lp1a_.process(x));
        float rest = hp1b_.process(hp1a_.process(x));
        lo = aplB_.process(aplA_.process(lo)) + aphB_.process(aphA_.process(lo));   // passe-tout : phase alignée
        float mid = lp2b_.process(lp2a_.process(rest));
        float hi = hp2b_.process(hp2a_.process(rest));
        if (amount <= 0.001f) { gr[0] = gr[1] = gr[2] = 0; return lo + mid + hi; }
        float thr = -6.0f - 22.0f * amount, ratio = 2.0f + 3.0f * amount;
        float mk = dbToLin(std::min(14.0f, -thr * (1 - 1 / ratio) * 0.55f));
        float g0, g1, g2;
        lo  = comp_[0].process(lo,  CompVCA, thr + 2, ratio, 12, 160, mk, g0);
        mid = comp_[1].process(mid, CompVCA, thr,     ratio, 6,  110, mk, g1);
        hi  = comp_[2].process(hi,  CompVCA, thr - 3, ratio, 2,  70,  mk, g2);
        gr[0] = g0; gr[1] = g1; gr[2] = g2;
        return lo + mid + hi;
    }
private:
    Biquad lp1a_, lp1b_, hp1a_, hp1b_, lp2a_, lp2b_, hp2a_, hp2b_, aplA_, aplB_, aphA_, aphB_;
    Compressor comp_[3];
};

// ---------------------------------------------------------------- Limiteur à anticipation (2 ms)
class LookaheadLimiter {
public:
    void reset() { std::fill(buf_, buf_ + kLA, 0.0f); w_ = 0; dq_.clear(); idx_ = 0; g_ = 1; }
    inline float process(float x, float ceil, float relCoef, float& gainOut) {
        float need = std::fabs(x) > ceil ? ceil / std::fabs(x) : 1.0f;
        // minimum glissant du gain requis sur la fenêtre d'anticipation
        while (!dq_.empty() && dq_.back().second >= need) dq_.pop_back();
        dq_.push_back({idx_, need});
        while (dq_.front().first <= idx_ - kLA) dq_.pop_front();
        float target = dq_.front().second;
        float y = buf_[w_]; buf_[w_] = x; w_ = (w_ + 1) % kLA;
        ++idx_;
        if (target < g_) g_ += (target - g_) * 0.25f;   // descend en ~quelques échantillons, en avance
        else g_ = relCoef * g_ + (1 - relCoef) * target;
        float out = std::clamp(y * g_, -ceil, ceil);
        gainOut = g_;
        return out;
    }
private:
    static constexpr int kLA = 96;
    float buf_[kLA]{};
    int w_ = 0; long long idx_ = 0;
    std::deque<std::pair<long long, float>> dq_;
    float g_ = 1;
};

// ---------------------------------------------------------------- Réverb Plate (topologie de Dattorro)
class Plate {
public:
    Plate() {
        auto sz = [](int n) { return (size_t)(n * kFs / 29761.0f) + 64; };
        const int in[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) { inAp_[i].assign(sz(in[i]), 0.0f); inLen_[i] = (int)(in[i] * kFs / 29761.0f); }
        const int tank[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
        for (int i = 0; i < 8; ++i) { tk_[i].assign(sz(tank[i]) + 40, 0.0f); tkLen_[i] = (int)(tank[i] * kFs / 29761.0f); }
        pre_.assign(4800, 0.0f);
        reset();
    }
    void reset() {
        for (auto& v : inAp_) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : tk_) std::fill(v.begin(), v.end(), 0.0f);
        std::fill(pre_.begin(), pre_.end(), 0.0f);
        for (int& i : inW_) i = 0;
        for (int& i : tkW_) i = 0;
        preW_ = 0; bw_ = 0; dampA_ = dampB_ = 0; lfo_ = 0; fbA_ = fbB_ = 0;
    }
    // decay 0..1, damp 0..1 ; sortie mouillée
    inline float process(float x, float decay, float damp) {
        pre_[preW_] = x; int pr = preW_ - 720; if (pr < 0) pr += (int)pre_.size(); preW_ = (preW_ + 1) % (int)pre_.size();
        float s = pre_[pr];
        bw_ += 0.9995f * (s - bw_);
        float v = bw_;
        const float inC[4] = {0.75f, 0.75f, 0.625f, 0.625f};
        for (int i = 0; i < 4; ++i) v = allpass(inAp_[i], inW_[i], inLen_[i], v, inC[i]);
        float dec = 0.25f + 0.70f * decay, d = 0.0005f + 0.7f * damp;
        lfo_ += 2 * kPi * 1.0f / kFs; if (lfo_ > 2 * kPi) lfo_ -= 2 * kPi;
        float exc = 16.0f * kFs / 29761.0f;
        // branche A
        float a = v + fbB_ * dec;
        a = modAllpass(tk_[0], tkW_[0], tkLen_[0], a, -0.70f, exc * std::sin(lfo_));
        write(tk_[1], tkW_[1], a); float a2 = tap(tk_[1], tkW_[1], tkLen_[1]);
        dampA_ += (1 - d) * (a2 - dampA_);
        float a3 = tankAllpass(tk_[2], tkW_[2], tkLen_[2], dampA_ * dec, 0.5f);
        write(tk_[3], tkW_[3], a3); fbA_ = tap(tk_[3], tkW_[3], tkLen_[3]);
        // branche B
        float b = v + fbA_ * dec;
        b = modAllpass(tk_[4], tkW_[4], tkLen_[4], b, -0.70f, exc * std::cos(lfo_));
        write(tk_[5], tkW_[5], b); float b2 = tap(tk_[5], tkW_[5], tkLen_[5]);
        dampB_ += (1 - d) * (b2 - dampB_);
        float b3 = tankAllpass(tk_[6], tkW_[6], tkLen_[6], dampB_ * dec, 0.5f);
        write(tk_[7], tkW_[7], b3); fbB_ = tap(tk_[7], tkW_[7], tkLen_[7]);
        for (int i = 0; i < 8; ++i) tkW_[i] = (tkW_[i] + 1) % (int)tk_[i].size();
        // sortie (prises de Dattorro, canal gauche)
        auto T = [&](int k, int pos) { return tap(tk_[k], tkW_[k], (int)(pos * kFs / 29761.0f)); };
        float y = T(5, 266) + T(5, 2974) - T(6, 1913) + T(7, 1996) - T(1, 1990) - T(2, 187) - T(3, 1066);
        return y * 0.6f;
    }
private:
    static float tap(const std::vector<float>& b, int w, int delay) {
        int r = w - delay; while (r < 0) r += (int)b.size(); return b[r];
    }
    static void write(std::vector<float>& b, int w, float v) { b[w] = v; }
    static float allpass(std::vector<float>& b, int& w, int len, float x, float g) {
        int r = w - len; while (r < 0) r += (int)b.size();
        float d = b[r], v = x - g * d;   // forme de Dattorro
        b[w] = v; w = (w + 1) % (int)b.size();
        return d + g * v;
    }
    static float tankAllpass(std::vector<float>& b, int w, int len, float x, float g) {
        int r = w - len; while (r < 0) r += (int)b.size();
        float d = b[r], v = x - g * d; b[w] = v;
        return d + g * v;
    }
    static float modAllpass(std::vector<float>& b, int w, int len, float x, float g, float mod) {
        float pos = (float)w - (len + mod); while (pos < 0) pos += (float)b.size();
        int i0 = (int)pos; float fr = pos - i0;
        float d = b[i0 % b.size()] * (1 - fr) + b[(i0 + 1) % b.size()] * fr;
        float v = x - g * d; b[w] = v;
        return d + g * v;
    }
    std::vector<float> inAp_[4], tk_[8], pre_;
    int inLen_[4]{}, tkLen_[8]{}, inW_[4]{}, tkW_[8]{}, preW_ = 0;
    float bw_ = 0, dampA_ = 0, dampB_ = 0, lfo_ = 0, fbA_ = 0, fbB_ = 0;
};

// ---------------------------------------------------------------- Paramètres
enum FxMode { FxNone = 0, FxTalkie, FxPhone, FxRobot, FxCount };
enum EqType { EqLowShelf = 0, EqBell, EqHighShelf };
constexpr int kEqBands = 5;
inline EqType eqBandType(int i) { return i == 0 ? EqLowShelf : (i == kEqBands - 1 ? EqHighShelf : EqBell); }
inline void eqDefaults(float* f, float* q) {
    const float F[kEqBands] = {120, 320, 1200, 3500, 10000}, Q[kEqBands] = {0.8f, 1.0f, 1.0f, 0.9f, 0.8f};
    for (int i = 0; i < kEqBands; ++i) { f[i] = F[i]; q[i] = Q[i]; }
}

struct VoiceParams {
    std::atomic<bool>  bypass{false};
    std::atomic<float> inputGainDb{0.0f};
    std::atomic<bool>  hpfOn{true};          std::atomic<float> hpfHz{90.0f};
    std::atomic<bool>  denoiseOn{true};      std::atomic<float> denoiseAmount{0.9f};
    std::atomic<bool>  gateOn{true};         std::atomic<float> gateThreshDb{-50.0f};
    std::atomic<float> gateRangeDb{-40.0f};  std::atomic<float> gateReleaseMs{150.0f};
    std::atomic<bool>  pitchOn{false};       std::atomic<float> pitchSemis{0.0f};
    std::atomic<bool>  eqOn{true};
    std::atomic<float> eqF[kEqBands], eqG[kEqBands], eqQ[kEqBands];
    std::atomic<bool>  levelerOn{true};      std::atomic<float> levelerTargetDb{-20.0f};
    std::atomic<bool>  compOn{true};         std::atomic<int> compStyle{CompOpto};
    std::atomic<float> compThreshDb{-24.0f}, compRatio{4.0f}, compAttackMs{5.0f}, compReleaseMs{90.0f}, compMakeupDb{8.0f}, compMix{1.0f};
    std::atomic<bool>  mbOn{false};          std::atomic<float> mbAmount{0.4f};
    std::atomic<bool>  deessOn{true};
    std::atomic<float> deessFreqHz{6000.0f}, deessThreshDb{-32.0f}, deessMaxDb{8.0f};
    std::atomic<bool>  exciterOn{true};      std::atomic<float> exciterAmount{0.3f};
    std::atomic<bool>  satOn{true};          std::atomic<float> satAmount{0.25f};
    std::atomic<int>   fxMode{FxNone};       std::atomic<float> fxMix{1.0f};
    std::atomic<bool>  echoOn{false};        std::atomic<float> echoTimeMs{280.0f}, echoFeedback{0.35f}, echoMix{0.2f};
    std::atomic<bool>  reverbOn{false};      std::atomic<int> reverbType{1};   // 0 = pièce, 1 = plate
    std::atomic<float> reverbSize{0.45f}, reverbDamp{0.5f}, reverbMix{0.15f};
    std::atomic<float> outputGainDb{0.0f};
    std::atomic<bool>  limiterOn{true};      std::atomic<float> limiterCeilDb{-1.0f};
    VoiceParams() {
        float f[kEqBands], q[kEqBands]; eqDefaults(f, q);
        for (int i = 0; i < kEqBands; ++i) { eqF[i] = f[i]; eqG[i] = 0.0f; eqQ[i] = q[i]; }
    }
};

struct VoiceMeters {
    std::atomic<float> inPeak{0}, outPeak{0};
    std::atomic<float> gateGainDb{0}, compGrDb{0}, deessGrDb{0}, limGrDb{0}, levelerGainDb{0};
    std::atomic<float> mbGr[3];
    std::atomic<float> vad{0};
    std::atomic<bool>  clipped{false};
    VoiceMeters() { for (auto& g : mbGr) g = 0; }
};

// Construit les filtres de l'égaliseur (aussi utilisé par l'interface pour tracer la courbe)
inline void buildEqBand(Biquad& b, int i, float f, float g, float q) {
    switch (eqBandType(i)) {
    case EqLowShelf: b.lowShelf(f, g, std::clamp(q, 0.3f, 1.5f)); break;
    case EqHighShelf: b.highShelf(f, g, std::clamp(q, 0.3f, 1.5f)); break;
    default: b.peak(f, std::max(q, 0.1f), g); break;
    }
}

// ---------------------------------------------------------------- Chaîne complète
class VoiceChain {
public:
    void prepare() {
        lastSig_ = -1;
        for (Biquad* b : {&hpf1_, &hpf2_, &dsBp_, &exHp_, &fxHp_, &fxLp_, &fxHp2_, &fxLp2_}) b->reset();
        for (auto& b : eq_) b.reset();
        den_.reset(); pitch_.reset(); rev_.reset(); plate_.reset(); echo_.reset(); echo_.prepare();
        comp_.reset(); mb_.prepare(); os_.reset(); lim_.reset();
        gateEnv_ = 0; gateGain_ = 1; gateHold_ = 0;
        dsEnv_ = 0; levRms_ = 0; levGainDb_ = 0; robotPh_ = 0;
        std::fill(robotBuf_, robotBuf_ + 1024, 0.0f); robotW_ = 0;
    }

    void process(float* buf, int n, const VoiceParams& p, VoiceMeters& m) {
        updateFilters(p);
        const float inG = dbToLin(p.inputGainDb.load());
        float inPk = 0, outPk = 0;

        if (p.bypass.load()) {
            for (int i = 0; i < n; ++i) { buf[i] = std::clamp(buf[i] * inG, -1.0f, 1.0f); inPk = std::max(inPk, std::fabs(buf[i])); }
            m.inPeak = inPk; m.outPeak = inPk;
            m.gateGainDb = 0; m.compGrDb = 0; m.deessGrDb = 0; m.limGrDb = 0; m.levelerGainDb = 0; m.vad = 0;
            for (auto& g : m.mbGr) g = 0;
            return;
        }

        const bool hpfOn = p.hpfOn, denOn = p.denoiseOn, gateOn = p.gateOn, pitchOn = p.pitchOn, eqOn = p.eqOn;
        const bool levOn = p.levelerOn, compOn = p.compOn, mbOn = p.mbOn, dsOn = p.deessOn, exOn = p.exciterOn, satOn = p.satOn;
        const bool echoOn = p.echoOn, revOn = p.reverbOn, limOn = p.limiterOn;
        const int fx = p.fxMode, cStyle = p.compStyle, rType = p.reverbType;
        const float denAmt = std::clamp(p.denoiseAmount.load(), 0.0f, 1.0f);
        const float semis = std::clamp(p.pitchSemis.load(), -12.0f, 12.0f);
        const float gThr = dbToLin(p.gateThreshDb), gFloor = dbToLin(p.gateRangeDb);
        const float gAtt = coefFromMs(1.0f), gRel = coefFromMs(p.gateReleaseMs), envRel = coefFromMs(40.0f);
        const int holdN = (int)(0.08f * kFs);
        const float levTarget = p.levelerTargetDb, levRmsC = coefFromMs(300.0f), levSlow = coefFromMs(1500.0f);
        const float cThr = p.compThreshDb, cRatio = std::max(1.0f, p.compRatio.load());
        const float cAtt = p.compAttackMs, cRel = p.compReleaseMs, cMake = dbToLin(p.compMakeupDb);
        const float cMix = std::clamp(p.compMix.load(), 0.0f, 1.0f);
        const float mbAmt = std::clamp(p.mbAmount.load(), 0.0f, 1.0f);
        const float dThr = p.deessThreshDb, dMax = p.deessMaxDb, dAtt = coefFromMs(0.5f), dRel = coefFromMs(60.0f);
        const float exAmt = std::clamp(p.exciterAmount.load(), 0.0f, 1.0f);
        const float satAmt = std::clamp(p.satAmount.load(), 0.0f, 1.0f), drive = 1 + satAmt * 5, satNorm = 1 / std::tanh(drive);
        const float fxMix = std::clamp(p.fxMix.load(), 0.0f, 1.0f);
        const float eT = p.echoTimeMs, eFb = std::clamp(p.echoFeedback.load(), 0.0f, 0.9f), eMix = p.echoMix;
        const float rS = p.reverbSize, rD = p.reverbDamp, rMix = p.reverbMix;
        const float outG = dbToLin(p.outputGainDb), ceil = dbToLin(p.limiterCeilDb), lRel = coefFromMs(80.0f);

        float minGate = 1, maxCompGr = 0, maxDsGr = 0, minLim = 1, mbGr[3] = {0, 0, 0};
        bool clip = false;

        for (int i = 0; i < n; ++i) {
            float x = buf[i] * inG;
            inPk = std::max(inPk, std::fabs(x));
            if (std::fabs(x) >= 0.999f) clip = true;

            if (hpfOn) x = hpf2_.process(hpf1_.process(x));
            if (denOn) x = den_.process(x, denAmt);

            if (gateOn) {
                float a = std::fabs(x);
                gateEnv_ = a > gateEnv_ ? a : envRel * gateEnv_ + (1 - envRel) * a;
                bool voice = gateEnv_ >= gThr && (!denOn || den_.vad() > 0.15f || gateEnv_ >= gThr * 4);
                float target;
                if (voice) { target = 1; gateHold_ = holdN; }
                else if (gateHold_ > 0) { target = 1; --gateHold_; }
                else target = gFloor;
                float c = target > gateGain_ ? gAtt : gRel;
                gateGain_ = c * gateGain_ + (1 - c) * target;
                x *= gateGain_;
                minGate = std::min(minGate, gateGain_);
            }

            if (pitchOn && std::fabs(semis) > 0.01f) x = pitch_.process(x, semis);
            if (eqOn) for (auto& b : eq_) x = b.process(x);

            if (levOn) {
                levRms_ = levRmsC * levRms_ + (1 - levRmsC) * x * x;
                float lvl = 10 * std::log10(std::max(levRms_, 1e-12f));
                bool speaking = lvl > -50 && (!denOn || den_.vad() > 0.5f);
                if (speaking) levGainDb_ = levSlow * levGainDb_ + (1 - levSlow) * std::clamp(levTarget - lvl, -12.0f, 12.0f);
                x *= dbToLin(levGainDb_);
            }

            if (compOn) {
                float gr;
                float wet = comp_.process(x, cStyle, cThr, cRatio, cAtt, cRel, cMake, gr);
                x = x * (1 - cMix) + wet * cMix;   // compression parallèle
                maxCompGr = std::min(maxCompGr, gr);
            }

            if (mbOn) {
                float g[3];
                x = mb_.process(x, mbAmt, g);
                for (int k = 0; k < 3; ++k) mbGr[k] = std::min(mbGr[k], g[k]);
            }

            if (dsOn) {
                float hi = dsBp_.process(x), a = std::fabs(hi);
                float c = a > dsEnv_ ? dAtt : dRel;
                dsEnv_ = c * dsEnv_ + (1 - c) * a;
                float red = std::clamp(linToDb(dsEnv_) - dThr, 0.0f, dMax);
                x -= (1 - dbToLin(-red)) * hi;
                maxDsGr = std::max(maxDsGr, red);
            }

            if (exOn && exAmt > 0.001f) { float h = exHp_.process(x); x += exAmt * 0.6f * (std::tanh(h * 4.0f) * 0.5f - h * 0.25f); }

            if (satOn && satAmt > 0.001f)
                x = os_.process(x, [&](float v) { return v + satAmt * (std::tanh(drive * v) * satNorm - v); });

            if (fx != FxNone) {
                float dry = x, w = x;
                if (fx == FxTalkie) { w = fxLp_.process(fxHp_.process(w)); w = fxLp2_.process(fxHp2_.process(w)); w = std::tanh(w * 6.0f) * 0.35f; }
                else if (fx == FxPhone) { w = fxLp_.process(fxHp_.process(w)); w = fxLp2_.process(fxHp2_.process(w)); w = std::tanh(w * 2.5f) * 0.6f; }
                else if (fx == FxRobot) {
                    robotPh_ += 2 * kPi * 70.0f / kFs; if (robotPh_ > 2 * kPi) robotPh_ -= 2 * kPi;
                    float rm = w * std::sin(robotPh_);
                    int r = robotW_ - 480; if (r < 0) r += 1024;
                    float y = rm + robotBuf_[r] * 0.55f;
                    robotBuf_[robotW_] = y; robotW_ = (robotW_ + 1) % 1024;
                    w = y * 1.4f;
                }
                x = dry + fxMix * (w - dry);
            }

            if (echoOn) x += eMix * echo_.process(x, eT, eFb);
            if (revOn) x += rMix * (rType == 1 ? plate_.process(x, rS, rD) : rev_.process(x, rS, rD, 960));

            x *= outG;
            if (limOn) { float g; x = lim_.process(x, ceil, lRel, g); minLim = std::min(minLim, g); }
            else x = std::clamp(x, -1.0f, 1.0f);

            if (!std::isfinite(x)) { x = 0; prepare(); }
            buf[i] = x;
            outPk = std::max(outPk, std::fabs(x));
        }

        m.inPeak = inPk; m.outPeak = outPk;
        m.gateGainDb = gateOn ? linToDb(minGate) : 0.0f;
        m.compGrDb = compOn ? maxCompGr : 0.0f;
        m.deessGrDb = dsOn ? -maxDsGr : 0.0f;
        m.limGrDb = limOn ? linToDb(minLim) : 0.0f;
        m.levelerGainDb = levOn ? levGainDb_ : 0.0f;
        for (int k = 0; k < 3; ++k) m.mbGr[k] = mbOn ? mbGr[k] : 0.0f;
        m.vad = denOn ? den_.vad() : 0.0f;
        if (clip) m.clipped = true;
    }

private:
    void updateFilters(const VoiceParams& p) {
        float hz = p.hpfHz, dsf = p.deessFreqHz; int fx = p.fxMode;
        float sig = hz * 1.1f + dsf * 0.013f + fx * 101.0f;
        float ef[kEqBands], eg[kEqBands], eq[kEqBands];
        for (int i = 0; i < kEqBands; ++i) {
            ef[i] = p.eqF[i]; eg[i] = p.eqG[i]; eq[i] = p.eqQ[i];
            sig += ef[i] * (0.37f + i) + eg[i] * (13.1f + 7 * i) + eq[i] * (91.3f + 11 * i);
        }
        if (sig == lastSig_) return;
        lastSig_ = sig;
        hpf1_.highpass(hz, 0.5412f); hpf2_.highpass(hz, 1.3066f);
        for (int i = 0; i < kEqBands; ++i) buildEqBand(eq_[i], i, ef[i], eg[i], eq[i]);
        dsBp_.bandpass(dsf, 0.8f);
        exHp_.highpass(3000.0f);
        if (fx == FxTalkie) { fxHp_.highpass(500); fxHp2_.highpass(500); fxLp_.lowpass(2600); fxLp2_.lowpass(2600); }
        else { fxHp_.highpass(320); fxHp2_.highpass(320); fxLp_.lowpass(3400); fxLp2_.lowpass(3400); }
    }

    float lastSig_ = -1;
    Biquad hpf1_, hpf2_, eq_[kEqBands], dsBp_, exHp_, fxHp_, fxLp_, fxHp2_, fxLp2_;
    Denoiser den_;
    PitchShifter pitch_;
    Reverb rev_;
    Plate plate_;
    Echo echo_;
    Compressor comp_;
    Multiband mb_;
    Oversampler2x os_;
    LookaheadLimiter lim_;
    float gateEnv_ = 0, gateGain_ = 1; int gateHold_ = 0;
    float dsEnv_ = 0, levRms_ = 0, levGainDb_ = 0;
    float robotPh_ = 0, robotBuf_[1024]{}; int robotW_ = 0;
};

// ---------------------------------------------------------------- Presets
enum class Preset { C214Radio, Naturel, Radio, Podcast, Grave, Studio, Talkie, Robot, Off, Count };
inline const char* presetName(int i) {
    static const char* n[] = {"C214 Radio", "Naturel", "Radio", "Podcast", "Grave", "Studio", "Talkie", "Robot", "Sans effet"};
    return (i >= 0 && i < (int)Preset::Count) ? n[i] : "Perso";
}

inline void applyPreset(VoiceParams& p, Preset pr) {
    p.bypass = false;
    p.hpfOn = true; p.denoiseOn = true; p.denoiseAmount = 0.9f; p.gateOn = true; p.eqOn = true;
    p.levelerOn = true; p.levelerTargetDb = -20; p.compOn = true; p.compStyle = CompOpto; p.compMix = 1.0f;
    p.mbOn = false; p.mbAmount = 0.4f;
    p.deessOn = true; p.exciterOn = true; p.satOn = true;
    p.pitchOn = false; p.pitchSemis = 0; p.fxMode = FxNone; p.fxMix = 1; p.echoOn = false; p.reverbOn = false; p.reverbType = 1;
    p.limiterOn = true; p.limiterCeilDb = -1; p.outputGainDb = 0;
    p.gateRangeDb = -40; p.gateReleaseMs = 150; p.deessFreqHz = 6000;
    p.reverbSize = 0.45f; p.reverbDamp = 0.5f; p.reverbMix = 0.15f;
    p.echoTimeMs = 280; p.echoFeedback = 0.35f; p.echoMix = 0.2f;
    float df[kEqBands], dq[kEqBands]; eqDefaults(df, dq);
    for (int i = 0; i < kEqBands; ++i) { p.eqF[i] = df[i]; p.eqQ[i] = dq[i]; p.eqG[i] = 0; }
    auto eq = [&](float lo, float mud, float mid, float pres, float air) { p.eqG[0] = lo; p.eqG[1] = mud; p.eqG[2] = mid; p.eqG[3] = pres; p.eqG[4] = air; };
    auto comp = [&](int st, float t, float r, float at, float re, float mk, float mix) {
        p.compStyle = st; p.compThreshDb = t; p.compRatio = r; p.compAttackMs = at; p.compReleaseMs = re; p.compMakeupDb = mk; p.compMix = mix; };
    switch (pr) {
    case Preset::C214Radio:
        // C214 : statique brillant avec bosse de présence vers 7-12 kHz, capte la pièce
        p.hpfHz = 85; p.gateThreshDb = -52;
        eq(3.0f, -3.5f, 0.0f, 2.0f, -0.5f);
        p.eqF[1] = 280; p.eqQ[1] = 1.1f; p.eqF[3] = 3000; p.eqQ[3] = 1.0f; p.eqF[4] = 9000;
        comp(CompOpto, -26, 4, 8, 120, 9, 0.85f);
        p.mbOn = true; p.mbAmount = 0.45f;
        p.deessFreqHz = 7000; p.deessThreshDb = -34; p.deessMaxDb = 9;
        p.exciterAmount = 0.15f; p.satAmount = 0.25f;
        p.reverbOn = true; p.reverbType = 1; p.reverbSize = 0.25f; p.reverbDamp = 0.7f; p.reverbMix = 0.05f;
        break;
    case Preset::Naturel:
        p.hpfHz = 80; p.gateThreshDb = -55; eq(1, -1.5f, 0, 1.5f, 1); comp(CompOpto, -20, 2.5f, 10, 120, 4, 1);
        p.deessThreshDb = -30; p.deessMaxDb = 5; p.exciterAmount = 0.1f; p.satOn = false; p.satAmount = 0; break;
    case Preset::Radio:
        p.hpfHz = 90; p.gateThreshDb = -50; eq(3.5f, -3.5f, 0, 4, 3); comp(CompFET, -26, 5, 4, 80, 9, 0.8f);
        p.mbOn = true; p.mbAmount = 0.5f;
        p.deessThreshDb = -32; p.deessMaxDb = 8; p.exciterAmount = 0.35f; p.satAmount = 0.3f; break;
    case Preset::Podcast:
        p.hpfHz = 85; p.gateThreshDb = -52; eq(2, -2.5f, 0, 3, 2); comp(CompVCA, -22, 3.5f, 6, 100, 6, 1);
        p.deessThreshDb = -30; p.deessMaxDb = 6; p.exciterAmount = 0.2f; p.satAmount = 0.15f; break;
    case Preset::Grave:
        p.hpfHz = 70; p.gateThreshDb = -50; eq(6, -4, 0, 2.5f, 1.5f); comp(CompOpto, -28, 5, 3, 70, 10, 1);
        p.deessThreshDb = -32; p.deessMaxDb = 8; p.exciterAmount = 0.2f; p.satAmount = 0.4f;
        p.pitchOn = true; p.pitchSemis = -1.5f; break;
    case Preset::Studio:
        p.hpfHz = 85; p.gateThreshDb = -52; eq(2.5f, -3, 0, 3, 3); comp(CompVCA, -24, 4, 5, 90, 7, 1);
        p.deessThreshDb = -31; p.deessMaxDb = 7; p.exciterAmount = 0.3f; p.satAmount = 0.2f;
        p.reverbOn = true; p.reverbType = 1; p.reverbSize = 0.4f; p.reverbDamp = 0.6f; p.reverbMix = 0.12f; break;
    case Preset::Talkie:
        p.hpfHz = 120; p.gateThreshDb = -48; eq(0, -2, 0, 3, 0); comp(CompFET, -24, 6, 2, 60, 8, 1);
        p.deessThreshDb = -30; p.deessMaxDb = 4; p.exciterOn = false; p.exciterAmount = 0; p.satAmount = 0.2f;
        p.fxMode = FxTalkie; break;
    case Preset::Robot:
        p.hpfHz = 90; p.gateThreshDb = -50; eq(2, -2, 0, 3, 1); comp(CompVCA, -24, 4, 5, 90, 6, 1);
        p.deessThreshDb = -30; p.deessMaxDb = 5; p.exciterAmount = 0.1f; p.satAmount = 0.1f;
        p.fxMode = FxRobot; p.fxMix = 0.85f; break;
    case Preset::Off: p.bypass = true; break;
    default: break;
    }
}

} // namespace dsp
