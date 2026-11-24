// Vox Studio V2 - chaîne de traitement de la voix (mono, 48 kHz, float, temps réel)
#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

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

// ---------------------------------------------------------------- Paramètres
enum FxMode { FxNone = 0, FxTalkie, FxPhone, FxRobot, FxCount };

struct VoiceParams {
    std::atomic<bool>  bypass{false};
    std::atomic<float> inputGainDb{0.0f};
    std::atomic<bool>  hpfOn{true};          std::atomic<float> hpfHz{90.0f};
    std::atomic<bool>  denoiseOn{true};      std::atomic<float> denoiseAmount{0.9f};
    std::atomic<bool>  gateOn{true};         std::atomic<float> gateThreshDb{-50.0f};
    std::atomic<float> gateRangeDb{-40.0f};  std::atomic<float> gateReleaseMs{150.0f};
    std::atomic<bool>  pitchOn{false};       std::atomic<float> pitchSemis{0.0f};
    std::atomic<bool>  eqOn{true};
    std::atomic<float> eqWarmthDb{3.0f}, eqMudDb{-3.0f}, eqPresenceDb{3.5f}, eqAirDb{2.5f};
    std::atomic<bool>  levelerOn{true};      std::atomic<float> levelerTargetDb{-20.0f};
    std::atomic<bool>  compOn{true};
    std::atomic<float> compThreshDb{-24.0f}, compRatio{4.0f}, compAttackMs{5.0f}, compReleaseMs{90.0f}, compMakeupDb{8.0f};
    std::atomic<bool>  deessOn{true};
    std::atomic<float> deessFreqHz{6000.0f}, deessThreshDb{-32.0f}, deessMaxDb{8.0f};
    std::atomic<bool>  exciterOn{true};      std::atomic<float> exciterAmount{0.3f};
    std::atomic<bool>  satOn{true};          std::atomic<float> satAmount{0.25f};
    std::atomic<int>   fxMode{FxNone};       std::atomic<float> fxMix{1.0f};
    std::atomic<bool>  echoOn{false};        std::atomic<float> echoTimeMs{280.0f}, echoFeedback{0.35f}, echoMix{0.2f};
    std::atomic<bool>  reverbOn{false};      std::atomic<float> reverbSize{0.45f}, reverbDamp{0.5f}, reverbMix{0.15f};
    std::atomic<float> outputGainDb{0.0f};
    std::atomic<bool>  limiterOn{true};      std::atomic<float> limiterCeilDb{-1.0f};
};

struct VoiceMeters {
    std::atomic<float> inPeak{0}, outPeak{0};
    std::atomic<float> gateGainDb{0}, compGrDb{0}, deessGrDb{0}, limGrDb{0}, levelerGainDb{0};
    std::atomic<float> vad{0};
    std::atomic<bool>  clipped{false};
};

// ---------------------------------------------------------------- Chaîne complète
class VoiceChain {
public:
    void prepare() {
        lastSig_ = -1;
        for (Biquad* b : {&hpf1_, &hpf2_, &warm_, &mud_, &pres_, &air_, &dsBp_, &exHp_, &fxHp_, &fxLp_, &fxHp2_, &fxLp2_}) b->reset();
        den_.reset(); pitch_.reset(); rev_.reset(); echo_.reset(); echo_.prepare();
        gateEnv_ = 0; gateGain_ = 1; gateHold_ = 0;
        compDet_ = 0; dsEnv_ = 0; limGain_ = 1;
        levRms_ = 0; levGainDb_ = 0; robotPh_ = 0;
        std::fill(robotBuf_, robotBuf_ + 1024, 0.0f); robotW_ = 0;
    }

    void process(float* buf, int n, const VoiceParams& p, VoiceMeters& m) {
        updateFilters(p);
        const float inG = dbToLin(p.inputGainDb.load());
        float inPk = 0, outPk = 0;

        if (p.bypass.load()) {
            for (int i = 0; i < n; ++i) {
                buf[i] = std::clamp(buf[i] * inG, -1.0f, 1.0f);
                inPk = std::max(inPk, std::fabs(buf[i]));
            }
            m.inPeak = inPk; m.outPeak = inPk;
            m.gateGainDb = 0; m.compGrDb = 0; m.deessGrDb = 0; m.limGrDb = 0; m.levelerGainDb = 0; m.vad = 0;
            return;
        }

        const bool hpfOn = p.hpfOn, denOn = p.denoiseOn, gateOn = p.gateOn, pitchOn = p.pitchOn, eqOn = p.eqOn;
        const bool levOn = p.levelerOn, compOn = p.compOn, dsOn = p.deessOn, exOn = p.exciterOn, satOn = p.satOn;
        const bool echoOn = p.echoOn, revOn = p.reverbOn, limOn = p.limiterOn;
        const int fx = p.fxMode;
        const float denAmt = std::clamp(p.denoiseAmount.load(), 0.0f, 1.0f);
        const float semis = std::clamp(p.pitchSemis.load(), -12.0f, 12.0f);

        const float gThr = dbToLin(p.gateThreshDb), gFloor = dbToLin(p.gateRangeDb);
        const float gAtt = coefFromMs(1.0f), gRel = coefFromMs(p.gateReleaseMs), envRel = coefFromMs(40.0f);
        const int holdN = (int)(0.08f * kFs);

        const float levTarget = p.levelerTargetDb, levRmsC = coefFromMs(300.0f), levSlow = coefFromMs(1500.0f);

        const float cThr = p.compThreshDb, cRatio = std::max(1.0f, p.compRatio.load());
        const float cAtt = coefFromMs(p.compAttackMs), cRel = coefFromMs(p.compReleaseMs);
        const float cMake = dbToLin(p.compMakeupDb), knee = 6.0f;

        const float dThr = p.deessThreshDb, dMax = p.deessMaxDb, dAtt = coefFromMs(0.5f), dRel = coefFromMs(60.0f);
        const float exAmt = std::clamp(p.exciterAmount.load(), 0.0f, 1.0f);
        const float satAmt = std::clamp(p.satAmount.load(), 0.0f, 1.0f), drive = 1 + satAmt * 5, satNorm = 1 / std::tanh(drive);
        const float fxMix = std::clamp(p.fxMix.load(), 0.0f, 1.0f);
        const float eT = p.echoTimeMs, eFb = std::clamp(p.echoFeedback.load(), 0.0f, 0.9f), eMix = p.echoMix;
        const float rS = p.reverbSize, rD = p.reverbDamp, rMix = p.reverbMix;
        const float outG = dbToLin(p.outputGainDb), ceil = dbToLin(p.limiterCeilDb), lRel = coefFromMs(80.0f);

        float minGate = 1, maxCompGr = 0, maxDsGr = 0, minLim = 1;
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
            if (eqOn) x = air_.process(pres_.process(mud_.process(warm_.process(x))));

            if (levOn) { // niveleur lent : ne remonte que quand tu parles
                levRms_ = levRmsC * levRms_ + (1 - levRmsC) * x * x;
                float lvl = 10 * std::log10(std::max(levRms_, 1e-12f));
                bool speaking = lvl > -50 && (!denOn || den_.vad() > 0.5f);
                if (speaking) {
                    float want = std::clamp(levTarget - lvl, -12.0f, 12.0f);
                    levGainDb_ = levSlow * levGainDb_ + (1 - levSlow) * want;
                }
                x *= dbToLin(levGainDb_);
            }

            if (compOn) {
                float a = std::fabs(x);
                float ce = a > compDet_ ? cAtt : cRel;
                compDet_ = ce * compDet_ + (1 - ce) * a;
                float over = linToDb(compDet_) - cThr, gr;
                if (2 * over < -knee) gr = 0;
                else if (2 * std::fabs(over) <= knee) { float t = over + knee / 2; gr = (1 / cRatio - 1) * t * t / (2 * knee); }
                else gr = (1 / cRatio - 1) * over;
                x *= dbToLin(gr) * cMake;
                maxCompGr = std::min(maxCompGr, gr);
            }

            if (dsOn) {
                float hi = dsBp_.process(x), a = std::fabs(hi);
                float c = a > dsEnv_ ? dAtt : dRel;
                dsEnv_ = c * dsEnv_ + (1 - c) * a;
                float red = std::clamp(linToDb(dsEnv_) - dThr, 0.0f, dMax);
                x -= (1 - dbToLin(-red)) * hi;
                maxDsGr = std::max(maxDsGr, red);
            }

            if (exOn && exAmt > 0.001f) { // harmoniques ajoutées dans les aigus
                float h = exHp_.process(x);
                x += exAmt * 0.6f * (std::tanh(h * 4.0f) * 0.5f - h * 0.25f);
            }

            if (satOn && satAmt > 0.001f) x += satAmt * (std::tanh(drive * x) * satNorm - x);

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
            if (revOn) x += rMix * rev_.process(x, rS, rD, 960);

            x *= outG;
            if (limOn) {
                float a = std::fabs(x), need = a > ceil ? ceil / a : 1.0f;
                if (need < limGain_) limGain_ = need; else limGain_ = lRel * limGain_ + (1 - lRel);
                x = std::clamp(x * limGain_, -ceil, ceil);
                minLim = std::min(minLim, limGain_);
            } else x = std::clamp(x, -1.0f, 1.0f);

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
        m.vad = denOn ? den_.vad() : 0.0f;
        if (clip) m.clipped = true;
    }

private:
    void updateFilters(const VoiceParams& p) {
        float hz = p.hpfHz, w = p.eqWarmthDb, mu = p.eqMudDb, pr = p.eqPresenceDb, ai = p.eqAirDb, dsf = p.deessFreqHz;
        int fx = p.fxMode;
        float sig = hz * 1.1f + w * 3.3f + mu * 5.7f + pr * 7.9f + ai * 11.3f + dsf * 0.013f + fx * 101.0f;
        if (sig == lastSig_) return;
        lastSig_ = sig;
        hpf1_.highpass(hz, 0.5412f); hpf2_.highpass(hz, 1.3066f);
        warm_.lowShelf(120.0f, w, 0.8f);
        mud_.peak(350.0f, 1.0f, mu);
        pres_.peak(3500.0f, 0.9f, pr);
        air_.highShelf(10000.0f, ai, 0.8f);
        dsBp_.bandpass(dsf, 0.8f);
        exHp_.highpass(3000.0f);
        if (fx == FxTalkie) { fxHp_.highpass(500); fxHp2_.highpass(500); fxLp_.lowpass(2600); fxLp2_.lowpass(2600); }
        else { fxHp_.highpass(320); fxHp2_.highpass(320); fxLp_.lowpass(3400); fxLp2_.lowpass(3400); }
    }

    float lastSig_ = -1;
    Biquad hpf1_, hpf2_, warm_, mud_, pres_, air_, dsBp_, exHp_, fxHp_, fxLp_, fxHp2_, fxLp2_;
    Denoiser den_;
    PitchShifter pitch_;
    Reverb rev_;
    Echo echo_;
    float gateEnv_ = 0, gateGain_ = 1; int gateHold_ = 0;
    float compDet_ = 0, dsEnv_ = 0, limGain_ = 1;
    float levRms_ = 0, levGainDb_ = 0;
    float robotPh_ = 0, robotBuf_[1024]{}; int robotW_ = 0;
};

// ---------------------------------------------------------------- Presets
enum class Preset { Naturel, Radio, Podcast, Grave, Studio, Talkie, Robot, Off, Count };
inline const char* presetName(int i) {
    static const char* n[] = {"Naturel", "Radio", "Podcast", "Grave", "Studio", "Talkie", "Robot", "Sans effet"};
    return (i >= 0 && i < (int)Preset::Count) ? n[i] : "Perso";
}

inline void applyPreset(VoiceParams& p, Preset pr) {
    p.bypass = false;
    p.hpfOn = true; p.denoiseOn = true; p.denoiseAmount = 0.9f; p.gateOn = true; p.eqOn = true;
    p.levelerOn = true; p.levelerTargetDb = -20; p.compOn = true; p.deessOn = true; p.exciterOn = true; p.satOn = true;
    p.pitchOn = false; p.pitchSemis = 0; p.fxMode = FxNone; p.fxMix = 1; p.echoOn = false; p.reverbOn = false;
    p.limiterOn = true; p.limiterCeilDb = -1; p.outputGainDb = 0;
    p.gateRangeDb = -40; p.gateReleaseMs = 150; p.deessFreqHz = 6000;
    p.reverbSize = 0.45f; p.reverbDamp = 0.5f; p.reverbMix = 0.15f;
    p.echoTimeMs = 280; p.echoFeedback = 0.35f; p.echoMix = 0.2f;
    auto eq = [&](float w, float m, float pr_, float a) { p.eqWarmthDb = w; p.eqMudDb = m; p.eqPresenceDb = pr_; p.eqAirDb = a; };
    auto comp = [&](float t, float r, float at, float re, float mk) { p.compThreshDb = t; p.compRatio = r; p.compAttackMs = at; p.compReleaseMs = re; p.compMakeupDb = mk; };
    switch (pr) {
    case Preset::Naturel:
        p.hpfHz = 80; p.gateThreshDb = -55; eq(1, -1.5f, 1.5f, 1); comp(-20, 2.5f, 10, 120, 4);
        p.deessThreshDb = -30; p.deessMaxDb = 5; p.exciterAmount = 0.1f; p.satOn = false; p.satAmount = 0; break;
    case Preset::Radio:
        p.hpfHz = 90; p.gateThreshDb = -50; eq(3.5f, -3.5f, 4, 3); comp(-26, 5, 4, 80, 9);
        p.deessThreshDb = -32; p.deessMaxDb = 8; p.exciterAmount = 0.35f; p.satAmount = 0.3f; break;
    case Preset::Podcast:
        p.hpfHz = 85; p.gateThreshDb = -52; eq(2, -2.5f, 3, 2); comp(-22, 3.5f, 6, 100, 6);
        p.deessThreshDb = -30; p.deessMaxDb = 6; p.exciterAmount = 0.2f; p.satAmount = 0.15f; break;
    case Preset::Grave:
        p.hpfHz = 70; p.gateThreshDb = -50; eq(6, -4, 2.5f, 1.5f); comp(-28, 6, 3, 70, 10);
        p.deessThreshDb = -32; p.deessMaxDb = 8; p.exciterAmount = 0.2f; p.satAmount = 0.4f;
        p.pitchOn = true; p.pitchSemis = -1.5f; break;
    case Preset::Studio:
        p.hpfHz = 85; p.gateThreshDb = -52; eq(2.5f, -3, 3, 3); comp(-24, 4, 5, 90, 7);
        p.deessThreshDb = -31; p.deessMaxDb = 7; p.exciterAmount = 0.3f; p.satAmount = 0.2f;
        p.reverbOn = true; p.reverbSize = 0.35f; p.reverbDamp = 0.6f; p.reverbMix = 0.12f; break;
    case Preset::Talkie:
        p.hpfHz = 120; p.gateThreshDb = -48; eq(0, -2, 3, 0); comp(-24, 6, 2, 60, 8);
        p.deessThreshDb = -30; p.deessMaxDb = 4; p.exciterAmount = 0; p.exciterOn = false; p.satAmount = 0.2f;
        p.fxMode = FxTalkie; break;
    case Preset::Robot:
        p.hpfHz = 90; p.gateThreshDb = -50; eq(2, -2, 3, 1); comp(-24, 4, 5, 90, 6);
        p.deessThreshDb = -30; p.deessMaxDb = 5; p.exciterAmount = 0.1f; p.satAmount = 0.1f;
        p.fxMode = FxRobot; p.fxMix = 0.85f; break;
    case Preset::Off: p.bypass = true; break;
    default: break;
    }
}

} // namespace dsp
