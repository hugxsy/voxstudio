// Vox Studio - sauvegarde des réglages (%APPDATA%\VoxStudio\settings.ini)
#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "win_audio.h"

struct Settings {
    std::wstring inputId, cableId, monitorId;
    int  channel = 0;
    bool monitorOn = false;
    float monitorVol = 0.8f;
    int  preset = 1;
    int  page = 0;
    int  version = 4;
    bool autoStart = true;
    bool showVirtual = false;
    std::vector<std::wstring> mirrors;   // sorties en diffusion supplémentaire
};

inline std::wstring settingsPath() {
    wchar_t* appdata = _wgetenv(L"APPDATA");
    std::wstring dir = appdata ? std::wstring(appdata) + L"\\VoxStudio" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

#define VOX_PARAMS(X) \
    X(bypass) X(inputGainDb) X(hpfOn) X(hpfHz) X(denoiseOn) X(denoiseAmount) X(gateOn) X(gateThreshDb) X(gateRangeDb) \
    X(gateReleaseMs) X(pitchOn) X(pitchSemis) X(eqOn) \
    X(levelerOn) X(levelerTargetDb) X(compOn) X(compStyle) X(compMix) X(mbOn) X(mbAmount) X(compThreshDb) X(compRatio) X(compAttackMs) X(compReleaseMs) \
    X(compMakeupDb) X(deessOn) X(deessFreqHz) X(deessThreshDb) X(deessMaxDb) X(exciterOn) X(exciterAmount) \
    X(satOn) X(satAmount) X(fxMode) X(fxMix) X(echoOn) X(echoTimeMs) X(echoFeedback) X(echoMix) \
    X(reverbOn) X(reverbType) X(reverbSize) X(reverbDamp) X(reverbMix) X(outputGainDb) X(limiterOn) X(limiterCeilDb)

inline void saveSettings(const Settings& s, const dsp::VoiceParams& p) {
    FILE* f = _wfopen(settingsPath().c_str(), L"wb");
    if (!f) return;
    fprintf(f, "inputId=%s\n", toUtf8(s.inputId.c_str()).c_str());
    fprintf(f, "cableId=%s\n", toUtf8(s.cableId.c_str()).c_str());
    fprintf(f, "monitorId=%s\n", toUtf8(s.monitorId.c_str()).c_str());
    fprintf(f, "channel=%d\nmonitorOn=%d\nmonitorVol=%g\npreset=%d\npage=%d\nversion=%d\nautoStart=%d\nshowVirtual=%d\n",
            s.channel, (int)s.monitorOn, s.monitorVol, s.preset, s.page, s.version, (int)s.autoStart, (int)s.showVirtual);
    { std::string mj; for (auto& m : s.mirrors) { if (!mj.empty()) mj += "|"; mj += toUtf8(m.c_str()); }
      fprintf(f, "mirrors=%s\n", mj.c_str()); }
#define X(n) fprintf(f, "p." #n "=%g\n", (double)p.n.load());
    VOX_PARAMS(X)
#undef X
    for (int i = 0; i < dsp::kEqBands; ++i)
        fprintf(f, "p.eq%d=%g,%g,%g\n", i, (double)p.eqF[i].load(), (double)p.eqG[i].load(), (double)p.eqQ[i].load());
    fclose(f);
}

inline bool loadSettings(Settings& s, dsp::VoiceParams& p) {
    FILE* f = _wfopen(settingsPath().c_str(), L"rb");
    if (!f) return false;
    std::map<std::string, std::string> kv;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t eq = l.find('=');
        if (eq != std::string::npos) kv[l.substr(0, eq)] = l.substr(eq + 1);
    }
    fclose(f);
    auto num = [&](const char* k, double d) { auto it = kv.find(k); return it == kv.end() ? d : atof(it->second.c_str()); };
    if (kv.count("inputId")) s.inputId = toWide(kv["inputId"]);
    if (kv.count("cableId")) s.cableId = toWide(kv["cableId"]);
    if (kv.count("monitorId")) s.monitorId = toWide(kv["monitorId"]);
    s.channel = (int)num("channel", s.channel);
    s.monitorOn = num("monitorOn", s.monitorOn) != 0;
    s.monitorVol = (float)num("monitorVol", s.monitorVol);
    s.preset = (int)num("preset", s.preset);
    s.page = (int)num("page", s.page);
    s.version = (int)num("version", 1);
    s.autoStart = num("autoStart", s.autoStart) != 0;
    s.showVirtual = num("showVirtual", s.showVirtual) != 0;
    s.mirrors.clear();
    if (kv.count("mirrors")) {
        std::string m = kv["mirrors"]; size_t a = 0;
        while (a < m.size()) { size_t b = m.find('|', a); if (b == std::string::npos) b = m.size(); if (b > a) s.mirrors.push_back(toWide(m.substr(a, b - a))); a = b + 1; }
    }
#define X(n) { auto it = kv.find("p." #n); if (it != kv.end()) p.n = (decltype(p.n.load()))atof(it->second.c_str()); }
    VOX_PARAMS(X)
#undef X
    for (int i = 0; i < dsp::kEqBands; ++i) {
        auto it = kv.find("p.eq" + std::to_string(i));
        float fr, g, q;
        if (it != kv.end() && sscanf(it->second.c_str(), "%f,%f,%f", &fr, &g, &q) == 3) { p.eqF[i] = fr; p.eqG[i] = g; p.eqQ[i] = q; }
    }
    return true;
}
