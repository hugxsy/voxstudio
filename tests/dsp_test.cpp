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
    auto nOut = run(Preset::Radio, noise, m, noDen, [](VoiceParams& p) { p.compOn = false; p.satOn = false; p.levelerOn = false; p.mbOn = false; p.exciterOn = false; });
    std::printf("     bruit après gate : %.1f dB\n", rmsDb(nOut, 24000));
    CHECK(rmsDb(nOut, 24000) < -95.0f, "noise gate : bruit -70 dB ramené sous -95 dB");

    auto lo = run(Preset::Radio, sine(40, -20, 1.0f), m, [](VoiceParams& p) {
        p.compOn = false; p.gateOn = false; p.eqOn = false; p.satOn = false; p.deessOn = false; p.denoiseOn = false; p.levelerOn = false; p.exciterOn = false; p.mbOn = false; });
    CHECK(rmsDb(lo, 24000) < -41, "coupe-bas : 40 Hz atténué de plus de 18 dB");

    auto loud = run(Preset::Radio, sine(1000, -6, 2.0f), m, noDen);
    auto soft = run(Preset::Radio, sine(1000, -30, 2.0f), m, noDen);
    float diff = rmsDb(loud, 48000) - rmsDb(soft, 48000);
    std::printf("     écart entrée 24 dB -> sortie %.1f dB\n", diff);
    CHECK(diff < 12, "compression + niveleur : écart réduit de moitié");

    Tweak dsOnly = [](VoiceParams& p) { p.mbOn = false; p.compOn = false; p.satOn = false; p.eqOn = false; p.limiterOn = false; p.levelerOn = false; p.exciterOn = false; p.denoiseOn = false; };
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
    Tweak denOnly = [](VoiceParams& p) { p.mbOn = false; p.gateOn = false; p.compOn = false; p.levelerOn = false; p.satOn = false; p.exciterOn = false; p.eqOn = false; p.deessOn = false; p.limiterOn = false; p.hpfOn = false; p.denoiseAmount = 1.0f; };
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
        p.eqOn = false; p.deessOn = false; p.hpfOn = false; p.limiterOn = false; p.mbOn = false; p.pitchOn = true; p.pitchSemis = -12; });
    int zc = 0; for (size_t i = 48001; i < pOut.size(); ++i) if ((pOut[i - 1] < 0) != (pOut[i] < 0)) ++zc;
    float f = zc / 2.0f;
    std::printf("     pitch -12 : 400 Hz -> %.0f Hz\n", f);
    CHECK(f > 170 && f < 230, "pitch : une octave plus bas");

    // Réverb et écho stables avec queue
    auto rv = run(Preset::Studio, fakeVoice(2.0f, -6), m, [](VoiceParams& p) { p.reverbMix = 0.6f; p.reverbSize = 1.0f; p.echoOn = true; p.echoFeedback = 0.9f; p.echoMix = 0.8f; });
    CHECK(sane(rv), "réverb + écho au maximum : stable");


    // --- 2.3 : égaliseur paramétrique : la réponse des filtres correspond aux réglages
    {
        Biquad b; buildEqBand(b, 2, 1000, 6, 1.0f);
        CHECK(std::fabs(b.responseDb(1000) - 6) < 0.1f && std::fabs(b.responseDb(60)) < 0.3f, "EQ : cloche +6 dB à 1 kHz, neutre ailleurs");
        Biquad ls; buildEqBand(ls, 0, 120, -4, 0.8f);
        CHECK(std::fabs(ls.responseDb(30) + 4) < 0.5f && std::fabs(ls.responseDb(5000)) < 0.3f, "EQ : plateau grave -4 dB");
    }
    // --- densité 3 bandes neutre à 0 (reconstruction plate)
    {
        Multiband mb; mb.prepare(); float g[3];
        float worst = 0;
        for (float f : {100.0f, 220.0f, 1000.0f, 3200.0f, 8000.0f}) {
            std::vector<float> in = sine(f, -12, 0.5f), out(in.size());
            Multiband mm; mm.prepare();
            for (size_t i = 0; i < in.size(); ++i) out[i] = mm.process(in[i], 0.0f, g);
            worst = std::max(worst, std::fabs(rmsDb(out, 12000) - rmsDb(in, 12000)));
        }
        std::printf("     densité à 0 : écart max %.2f dB\n", worst);
        CHECK(worst < 0.5f, "densité 3 bandes : neutre quand désactivée");
        std::vector<float> lo = sine(1000, -30, 2.0f), hi = sine(1000, -6, 2.0f), o1(lo.size()), o2(hi.size());
        Multiband a, b2; a.prepare(); b2.prepare();
        for (size_t i = 0; i < lo.size(); ++i) { o1[i] = a.process(lo[i], 0.8f, g); o2[i] = b2.process(hi[i], 0.8f, g); }
        float d = rmsDb(o2, 48000) - rmsDb(o1, 48000);
        std::printf("     densité 80 %% : écart 24 dB -> %.1f dB\n", d);
        CHECK(d < 16, "densité 3 bandes : resserre la dynamique");
    }
    // --- limiteur à anticipation : aucune crête au-dessus du plafond, même sur un transitoire brutal
    {
        LookaheadLimiter L; L.reset(); float g, pk = 0; float ceil = dbToLin(-1);
        for (int i = 0; i < 48000; ++i) { float x = (i % 4800 < 5) ? 1.0f : 0.05f * std::sin(i * 0.05f); pk = std::max(pk, std::fabs(L.process(x * 1.8f, ceil, coefFromMs(80), g))); }
        CHECK(pk <= ceil + 1e-5f, "limiteur à anticipation : plafond respecté sur transitoires");
    }
    // --- saturation suréchantillonnée : sinus 15 kHz saturé, peu de repliement sous 10 kHz
    {
        Oversampler2x os; os.reset();
        std::vector<float> in = sine(15000, -3, 1.0f), out(in.size());
        for (size_t i = 0; i < in.size(); ++i) out[i] = os.process(in[i], [](float v) { return std::tanh(3 * v) / std::tanh(3.0f); });
        // énergie repliée : 3e harmonique (45 kHz) replie à 3 kHz -> mesurer 3 kHz par corrélation
        double re = 0, im = 0; for (size_t i = 24000; i < out.size(); ++i) { re += out[i] * std::cos(2 * kPi * 3000 * i / 48000.0); im += out[i] * std::sin(2 * kPi * 3000 * i / 48000.0); }
        float alias = linToDb((float)(2 * std::sqrt(re * re + im * im) / 24000));
        std::printf("     repliement à 3 kHz : %.1f dB\n", alias);
        CHECK(alias < -40, "saturation x2 : repliement sous -40 dB");
    }
    // --- plate : queue qui décroît, stable
    {
        Plate pl; std::vector<float> o(96000); 
        for (size_t i = 0; i < o.size(); ++i) o[i] = pl.process(i < 480 ? 0.5f : 0.0f, 0.6f, 0.4f);
        float early = 0, late = 0; for (size_t i = 2400; i < 12000; ++i) early = std::max(early, std::fabs(o[i]));
        for (size_t i = 84000; i < 96000; ++i) late = std::max(late, std::fabs(o[i]));
        std::printf("     plate : début %.4f, après 1,8 s %.6f\n", early, late);
        CHECK(sane(o) && early > 0.0003f && late < early * 0.05f, "réverb plate : produit une queue qui s'éteint");
    }
    // --- compresseur : les 3 caractères réduisent la dynamique et restent stables
    for (int st = 0; st < 3; ++st) {
        auto lo2 = run(Preset::Radio, sine(1000, -30, 2.0f), m, [](VoiceParams& p) { p.denoiseOn = false; p.levelerOn = false; p.mbOn = false; });
        (void)lo2;
        VoiceParams pp; applyPreset(pp, Preset::Radio); pp.denoiseOn = false; pp.levelerOn = false; pp.mbOn = false; pp.compStyle = st;
        VoiceChain c1, c2; c1.prepare(); c2.prepare();
        auto a = sine(1000, -30, 2.0f), b = sine(1000, -6, 2.0f);
        for (size_t i = 0; i < a.size(); i += 480) { c1.process(a.data() + i, 480, pp, m); c2.process(b.data() + i, 480, pp, m); }
        float d = rmsDb(b, 48000) - rmsDb(a, 48000);
        char msg[80]; snprintf(msg, sizeof msg, "compresseur style %d : écart 24 dB -> %.1f dB", st, d);
        CHECK(d < 16 && sane(a) && sane(b), msg);
    }

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
