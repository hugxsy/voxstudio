// Tests natifs de la chaîne voix V2 (compilés sous Linux)
#include "../src/dsp.h"
#include <cstdio>
#include <vector>
#include <random>

using namespace dsp;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("ECHEC: %s\n", msg); ++fails; } else std::printf("ok   : %s\n", msg); } while (0)

static float rmsDb(const std::vector<float>& v, size_t from) {
    double s = 0; for (size_t i = from; i < v.size(); ++i) s += (double)v[i] * v[i];
    return linToDb((float)std::sqrt(s / (v.size() - from)));
}
using Tweak = void (*)(VoiceParams&);
static void noDen(VoiceParams& p) { p.denoiseOn = false; }

static std::vector<float> run(Preset pr, std::vector<float> sig, VoiceMeters& m, Tweak t1 = nullptr, Tweak t2 = nullptr, int block = 480) {
    VoiceParams p; applyPreset(p, pr); if (t1) t1(p); if (t2) t2(p);
    VoiceChain c; c.prepare();
    for (size_t i = 0; i < sig.size(); i += block) c.process(sig.data() + i, (int)std::min<size_t>(block, sig.size() - i), p, m);
    return sig;
}
static std::vector<float> sine(float f, float ampDb, float sec) {
    std::vector<float> v((size_t)(48000 * sec)); float a = dbToLin(ampDb);
    for (size_t i = 0; i < v.size(); ++i) v[i] = a * std::sin(2 * kPi * f * i / 48000.0f);
    return v;
}
static bool sane(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x) || std::fabs(x) > 1.0f) return false; return true; }

// Signal "voix" synthétique : fondamentale 140 Hz + harmoniques, modulée en syllabes
static std::vector<float> fakeVoice(float sec, float ampDb) {
    std::vector<float> v((size_t)(48000 * sec)); float a = dbToLin(ampDb);
    for (size_t i = 0; i < v.size(); ++i) {
        float t = i / 48000.0f, s = 0;
        for (int h = 1; h <= 25; ++h) s += std::sin(2 * kPi * 140 * h * t + h) / h;
        float env = 0.5f + 0.5f * std::sin(2 * kPi * 3.0f * t);
        v[i] = a * s * 0.5f * env;
    }
    return v;
}

int main() {
    VoiceMeters m;
    std::mt19937 rng(1);

    auto out = run(Preset::Radio, sine(1000, 0, 1.0f), m, noDen);
    float pk = 0; for (float x : out) pk = std::max(pk, std::fabs(x));
    CHECK(pk <= dbToLin(-1.0f) + 1e-4f, "limiteur : crête <= -1 dBFS");
    CHECK(sane(out), "sortie finie et bornée");

    std::normal_distribution<float> nd(0, dbToLin(-70));
    std::vector<float> noise(48000); for (auto& x : noise) x = nd(rng);
    auto nOut = run(Preset::Radio, noise, m, noDen, [](VoiceParams& p) { p.compOn = false; p.satOn = false; p.levelerOn = false; });
    std::printf("     bruit après gate : %.1f dB\n", rmsDb(nOut, 24000));
    CHECK(rmsDb(nOut, 24000) < -95.0f, "noise gate : bruit -70 dB ramené sous -95 dB");

    auto lo = run(Preset::Radio, sine(40, -20, 1.0f), m, [](VoiceParams& p) {
        p.compOn = false; p.gateOn = false; p.eqOn = false; p.satOn = false; p.deessOn = false; p.denoiseOn = false; p.levelerOn = false; p.exciterOn = false; });
    CHECK(rmsDb(lo, 24000) < -41, "coupe-bas : 40 Hz atténué de plus de 18 dB");

    auto loud = run(Preset::Radio, sine(1000, -6, 2.0f), m, noDen);
    auto soft = run(Preset::Radio, sine(1000, -30, 2.0f), m, noDen);
    float diff = rmsDb(loud, 48000) - rmsDb(soft, 48000);
    std::printf("     écart entrée 24 dB -> sortie %.1f dB\n", diff);
    CHECK(diff < 12, "compression + niveleur : écart réduit de moitié");

    Tweak dsOnly = [](VoiceParams& p) { p.compOn = false; p.satOn = false; p.eqOn = false; p.limiterOn = false; p.levelerOn = false; p.exciterOn = false; p.denoiseOn = false; };
    auto s1 = run(Preset::Radio, sine(7000, -12, 1.0f), m, dsOnly);
    auto s0 = run(Preset::Radio, sine(7000, -12, 1.0f), m, dsOnly, [](VoiceParams& p) { p.deessOn = false; });
    CHECK(rmsDb(s1, 24000) < rmsDb(s0, 24000) - 3, "anti-sifflantes : 7 kHz atténué de 3 dB ou plus");

    auto bp = run(Preset::Off, sine(1000, -18, 0.5f), m);
    auto ref = sine(1000, -18, 0.5f);
    float err = 0; for (size_t i = 0; i < bp.size(); ++i) err = std::max(err, std::fabs(bp[i] - ref[i]));
    CHECK(err < 1e-6f, "sans effet : transparent");

    // IA anti-bruit : bruit blanc -40 dB fortement réduit, voix conservée
    std::normal_distribution<float> nd40(0, dbToLin(-60));
    std::vector<float> wn(96000); for (auto& x : wn) x = nd40(rng);
    Tweak denOnly = [](VoiceParams& p) { p.gateOn = false; p.compOn = false; p.levelerOn = false; p.satOn = false; p.exciterOn = false; p.eqOn = false; p.deessOn = false; p.limiterOn = false; p.hpfOn = false; p.denoiseAmount = 1.0f; };
    auto wnOut = run(Preset::Radio, wn, m, denOnly);
    std::printf("     bruit de fond -60 dB -> %.1f dB après IA\n", rmsDb(wnOut, 48000));
    CHECK(rmsDb(wnOut, 48000) < rmsDb(wn, 48000) - 15, "IA anti-bruit : bruit réduit de 15 dB ou plus");

    auto voice = fakeVoice(3.0f, -12);
    auto vOut = run(Preset::Radio, voice, m, denOnly);
    std::printf("     voix synthétique : %.1f dB -> %.1f dB\n", rmsDb(voice, 48000), rmsDb(vOut, 48000));
    CHECK(sane(vOut), "IA anti-bruit : sortie saine sur voix");

    // Pitch : -12 demi-tons divise la fréquence par 2 (comptage de passages par zéro)
    auto pin = sine(400, -12, 2.0f);
    auto pOut = run(Preset::Radio, pin, m, [](VoiceParams& p) {
        p.denoiseOn = false; p.gateOn = false; p.compOn = false; p.levelerOn = false; p.satOn = false; p.exciterOn = false;
        p.eqOn = false; p.deessOn = false; p.hpfOn = false; p.limiterOn = false; p.pitchOn = true; p.pitchSemis = -12; });
    int zc = 0; for (size_t i = 48001; i < pOut.size(); ++i) if ((pOut[i - 1] < 0) != (pOut[i] < 0)) ++zc;
    float f = zc / 2.0f;
    std::printf("     pitch -12 : 400 Hz -> %.0f Hz\n", f);
    CHECK(f > 170 && f < 230, "pitch : une octave plus bas");

    // Réverb et écho stables avec queue
    auto rv = run(Preset::Studio, fakeVoice(2.0f, -6), m, [](VoiceParams& p) { p.reverbMix = 0.6f; p.reverbSize = 1.0f; p.echoOn = true; p.echoFeedback = 0.9f; p.echoMix = 0.8f; });
    CHECK(sane(rv), "réverb + écho au maximum : stable");

    // Tous les presets, blocs de tailles irrégulières, bruit fort
    std::normal_distribution<float> big(0, 0.5f);
    for (int pr = 0; pr < (int)Preset::Count; ++pr) {
        std::vector<float> nz(48000); for (auto& x : nz) x = std::clamp(big(rng), -1.0f, 1.0f);
        auto o = run((Preset)pr, nz, m, nullptr, nullptr, 441);
        char msg[64]; snprintf(msg, sizeof msg, "preset %s stable", presetName(pr));
        CHECK(sane(o), msg);
    }
    std::printf(fails ? "\n%d test(s) en échec\n" : "\nTous les tests passent\n", fails);
    return fails ? 1 : 0;
}
