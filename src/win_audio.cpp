// Vox Studio - implémentation WASAPI
#include "win_audio.h"
#include <avrt.h>
#include <propsys.h>
#include <algorithm>
#include <cwctype>
#include <map>

// ---- GUID déclarés localement (indépendants des en-têtes MinGW) ----
static const CLSID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID kIID_IMMDeviceEnumerator   = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID kIID_IAudioClient          = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID kIID_IAudioCaptureClient   = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
static const IID kIID_IAudioRenderClient    = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const IID kIID_IAudioSessionManager2 = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
static const IID kIID_IAudioSessionControl2 = {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
static const IID kIID_ISimpleAudioVolume    = {0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};
static const IID kIID_IAudioMeterInformation= {0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};
static const IID kIID_IAudioEndpointVolume  = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};
static const GUID kSubtypeFloat             = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const PROPERTYKEY kPKEY_FriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

// Interface non documentée utilisée par les outils de bascule audio (SoundSwitch, EarTrumpet)
static const CLSID kCLSID_PolicyConfig = {0x870AF99C, 0x171D, 0x4F9E, {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}};
static const IID   kIID_IPolicyConfig  = {0xF8679F50, 0x850A, 0x41CF, {0x9C, 0x72, 0x43, 0x0F, 0x29, 0x02, 0x90, 0xC8}};
struct IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

// ---------------------------------------------------------------------------
std::string toUtf8(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring toWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static Com<IMMDeviceEnumerator> enumerator() {
    Com<IMMDeviceEnumerator> e;
    CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, e.putVoid());
    return e;
}

static std::string friendlyName(IMMDevice* d) {
    Com<IPropertyStore> ps;
    if (FAILED(d->OpenPropertyStore(STGM_READ, ps.put()))) return "?";
    PROPVARIANT v; PropVariantInit(&v);
    std::string name = "?";
    if (SUCCEEDED(ps->GetValue(kPKEY_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = toUtf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

std::vector<DeviceInfo> listDevices(bool capture) {
    std::vector<DeviceInfo> out;
    auto e = enumerator(); if (!e) return out;
    Com<IMMDeviceCollection> col;
    if (FAILED(e->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, col.put()))) return out;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> d; if (FAILED(col->Item(i, d.put()))) continue;
        LPWSTR id = nullptr; if (FAILED(d->GetId(&id))) continue;
        out.push_back({id, friendlyName(d.get())});
        CoTaskMemFree(id);
    }
    std::sort(out.begin(), out.end(), [](const DeviceInfo& a, const DeviceInfo& b) { return a.name < b.name; });
    return out;
}

std::wstring defaultDeviceId(bool capture) {
    auto e = enumerator(); if (!e) return {};
    Com<IMMDevice> d;
    if (FAILED(e->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eMultimedia, d.put()))) return {};
    LPWSTR id = nullptr; if (FAILED(d->GetId(&id))) return {};
    std::wstring s = id; CoTaskMemFree(id);
    return s;
}

bool setDefaultOutput(const std::wstring& id) {
    Com<IPolicyConfig> pc;
    if (FAILED(CoCreateInstance(kCLSID_PolicyConfig, nullptr, CLSCTX_ALL, kIID_IPolicyConfig, pc.putVoid()))) return false;
    bool ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eConsole));
    ok &= SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eMultimedia));
    ok &= SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eCommunications));
    return ok;
}

static WAVEFORMATEXTENSIBLE floatFormat(int channels) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = (WORD)channels;
    f.Format.nSamplesPerSec = 48000;
    f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = (WORD)(channels * 4);
    f.Format.nAvgBytesPerSec = 48000 * f.Format.nBlockAlign;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = 32;
    f.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER : channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
    f.SubFormat = kSubtypeFloat;
    return f;
}

static Com<IAudioClient> openClient(const std::wstring& id, int channels, REFERENCE_TIME bufHns, HANDLE ev, int* chOut, std::string* err, bool loopback = false) {
    Com<IAudioClient> client;
    auto e = enumerator();
    Com<IMMDevice> dev;
    if (!e || FAILED(e->GetDevice(id.c_str(), dev.put()))) { *err = "périphérique introuvable"; return {}; }
    if (FAILED(dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, client.putVoid()))) { *err = "activation impossible"; return {}; }
    if (channels <= 0) {
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(client->GetMixFormat(&mix))) { channels = std::clamp<int>(mix->nChannels, 1, 8); CoTaskMemFree(mix); }
        else channels = 2;
    }
    auto fmt = floatFormat(channels);
    DWORD flags = 0x80000000 /*AUTOCONVERTPCM*/ | 0x08000000 /*SRC_DEFAULT_QUALITY*/;
    if (ev) flags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufHns, 0, (WAVEFORMATEX*)&fmt, nullptr);
    if (FAILED(hr)) {
        char b[64]; snprintf(b, sizeof b, "initialisation refusée (0x%08lX)", (unsigned long)hr);
        *err = b; return {};
    }
    if (ev) client->SetEventHandle(ev);
    *chOut = channels;
    return client;
}

// ---------------------------------------------------------------------------
void VoiceEngine::setStatus(const std::string& s, bool fail) {
    std::lock_guard<std::mutex> l(statusMx_);
    status_ = s;
    if (fail) failed_ = true;
}
std::string VoiceEngine::status() { std::lock_guard<std::mutex> l(statusMx_); return status_; }

bool VoiceEngine::start(const std::wstring& inputId, Channel ch, const std::wstring& cableId, const std::wstring& monitorId) {
    stop();
    if (inputId.empty() || cableId.empty()) { setStatus("Choisis un micro et une sortie micro virtuel", true); return false; }
    stopFlag_ = false; failed_ = false;
    chain_.prepare();
    ringAna_.clear();
    ringOut_.clear(); ringMon_.clear();
    setStatus("Démarrage…", false);
    { std::lock_guard<std::mutex> l(statusMx_); monStatus_.clear(); }
    running_ = true;
    tCap_ = std::thread(&VoiceEngine::captureThread, this, inputId, ch);
    tOut_ = std::thread(&VoiceEngine::renderThread, this, cableId, &ringOut_, false);
    if (!monitorId.empty()) tMon_ = std::thread(&VoiceEngine::renderThread, this, monitorId, &ringMon_, true);
    return true;
}

void VoiceEngine::stop() {
    stopFlag_ = true;
    if (tCap_.joinable()) tCap_.join();
    if (tOut_.joinable()) tOut_.join();
    if (tMon_.joinable()) tMon_.join();
    if (running_) setStatus("Arrêté", false);
    running_ = false;
}

void VoiceEngine::captureThread(std::wstring id, Channel ch) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD ti = 0; HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &ti);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::string err; int chans = 0;
    auto client = openClient(id, 0, 100000, ev, &chans, &err);
    Com<IAudioCaptureClient> cap;
    if (!client || FAILED(client->GetService(kIID_IAudioCaptureClient, cap.putVoid()))) {
        if (err.find("0x8889000A") != std::string::npos) err = "bloqué par une autre appli (Voicemeeter ?)";
        setStatus("Micro : " + (err.empty() ? std::string("service indisponible") : err), true);
    } else if (FAILED(client->Start())) {
        setStatus("Micro : démarrage impossible", true);
    } else {
        setStatus("En marche", false);
        std::vector<float> mono;
        while (!stopFlag_) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
            UINT32 pkt = 0;
            HRESULT hr;
            while (SUCCEEDED(hr = cap->GetNextPacketSize(&pkt)) && pkt > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;
                mono.resize(frames);
                const float* f = reinterpret_cast<const float*>(data);
                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                for (UINT32 i = 0; i < frames; ++i) {
                    if (silent) { mono[i] = 0; continue; }
                    const float* fr = f + (size_t)i * chans;
                    if (chans == 1) mono[i] = fr[0];
                    else if (ch == Ch1) mono[i] = fr[0];
                    else if (ch == Ch2) mono[i] = fr[1];
                    else mono[i] = 0.5f * (fr[0] + fr[1]);
                }
                cap->ReleaseBuffer(frames);
                chain_.process(mono.data(), (int)frames, params, meters);
                if (muted) std::fill(mono.begin(), mono.end(), 0.0f);
                ringOut_.push(mono.data(), frames);
                ringMon_.push(mono.data(), frames);
                ringAna_.push(mono.data(), frames);
            }
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) { setStatus("Micro débranché ou changé", true); break; }
        }
        client->Stop();
    }
    CloseHandle(ev);
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

void VoiceEngine::renderThread(std::wstring id, Ring* ring, bool isMonitor) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD ti = 0; HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &ti);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::string err; int chans = 0;
    auto client = openClient(id, 2, 200000, ev, &chans, &err);
    Com<IAudioRenderClient> rc;
    UINT32 bufFrames = 0;
    const char* what = isMonitor ? "Retour casque : " : "Micro virtuel : ";
    if (!client || FAILED(client->GetService(kIID_IAudioRenderClient, rc.putVoid())) || FAILED(client->GetBufferSize(&bufFrames))) {
        if (err.find("0x8889000A") != std::string::npos) err = "périphérique bloqué par une autre appli (Voicemeeter ?)";
        if (isMonitor) { std::lock_guard<std::mutex> l(statusMx_); monStatus_ = err.empty() ? "service indisponible" : err; }
        else setStatus(std::string(what) + (err.empty() ? "service indisponible" : err), true);
    } else {
        BYTE* d = nullptr;
        if (SUCCEEDED(rc->GetBuffer(bufFrames, &d))) rc->ReleaseBuffer(bufFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        client->Start();
        std::vector<float> tmp;
        float gCur = 0.0f;
        const size_t target = 480;          // 10 ms de marge
        const size_t maxFill = target + 1920; // au-delà, on rattrape le retard
        while (!stopFlag_) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
            UINT32 pad = 0;
            HRESULT hr = client->GetCurrentPadding(&pad);
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                if (isMonitor) { std::lock_guard<std::mutex> l(statusMx_); monStatus_ = "casque débranché ou changé"; }
                else setStatus(std::string(what) + "périphérique débranché", true);
                break;
            }
            if (FAILED(hr)) continue;
            UINT32 avail = bufFrames - pad;
            if (!avail) continue;
            if (ring->size() > maxFill) ring->skip(ring->size() - target);
            tmp.assign(avail, 0.0f);
            size_t got = ring->pop(tmp.data(), avail);
            (void)got;
            float gTarget = isMonitor ? (monitorOn ? monitorVolume.load() : 0.0f) : 1.0f;
            if (!isMonitor) gCur = 1.0f;
            if (FAILED(rc->GetBuffer(avail, &d))) continue;
            float* out = reinterpret_cast<float*>(d);
            for (UINT32 i = 0; i < avail; ++i) {
                gCur += (gTarget - gCur) * 0.002f;   // fondu ~10 ms : pas de clic quand on coupe le retour
                for (int c = 0; c < chans; ++c) out[(size_t)i * chans + c] = (c < 2) ? tmp[i] * gCur : 0.0f;
            }
            rc->ReleaseBuffer(avail, 0);
            if (!isMonitor) latencyMs_ = (float)(ring->size() + pad) / 48.0f + 10.0f;
        }
        client->Stop();
    }
    CloseHandle(ev);
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

// ---------------------------------------------------------------------------
float AppGroup::peak() const {
    float p = 0;
    for (auto& m : meters) { float v = 0; if (m && SUCCEEDED(m->GetPeakValue(&v))) p = std::max(p, v); }
    return p;
}
void AppGroup::setVolume(float v) {
    volume = v;
    for (auto& s : volumes) if (s) s->SetMasterVolume(v, nullptr);
}
void AppGroup::setMute(bool m) {
    mute = m;
    for (auto& s : volumes) if (s) s->SetMute(m ? TRUE : FALSE, nullptr);
}

static std::string exeName(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH]; DWORD n = MAX_PATH;
    std::string out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
        std::wstring p(buf, n);
        size_t s = p.find_last_of(L"\\/");
        std::wstring f = s == std::wstring::npos ? p : p.substr(s + 1);
        if (f.size() > 4 && _wcsicmp(f.c_str() + f.size() - 4, L".exe") == 0) f.resize(f.size() - 4);
        for (auto& c : f) c = (wchar_t)std::towlower(c);
        out = toUtf8(f.c_str());
    }
    CloseHandle(h);
    return out;
}

static std::string labelFor(const std::string& key) {
    static const std::map<std::string, std::string> known = {
        {"discord", "Discord"}, {"spotify", "Spotify"}, {"chrome", "Chrome"}, {"msedge", "Edge"},
        {"arc", "Arc"}, {"firefox", "Firefox"}, {"brave", "Brave"}, {"opera", "Opera"},
        {"obs64", "OBS"}, {"vlc", "VLC"}, {"steam", "Steam"}, {"steamwebhelper", "Steam"},
        {"whatsapp", "WhatsApp"}, {"slack", "Slack"}, {"teams", "Teams"}, {"ms-teams", "Teams"},
        {"zoom", "Zoom"}, {"dazn", "DAZN"}, {"premiere pro", "Premiere Pro"}, {"notion", "Notion"},
        {"__system", "Sons système"},
    };
    auto it = known.find(key);
    if (it != known.end()) return it->second;
    std::string s = key; if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

void AppMixer::refresh() {
    std::vector<AppGroup> next;
    auto e = enumerator(); if (!e) return;
    Com<IMMDeviceCollection> col;
    if (FAILED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, col.put()))) return;
    UINT n = 0; col->GetCount(&n);
    DWORD self = GetCurrentProcessId();
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> d; if (FAILED(col->Item(i, d.put()))) continue;
        Com<IAudioSessionManager2> mgr;
        if (FAILED(d->Activate(kIID_IAudioSessionManager2, CLSCTX_ALL, nullptr, mgr.putVoid()))) continue;
        Com<IAudioSessionEnumerator> se;
        if (FAILED(mgr->GetSessionEnumerator(se.put()))) continue;
        int cnt = 0; se->GetCount(&cnt);
        for (int k = 0; k < cnt; ++k) {
            Com<IAudioSessionControl> sc; if (FAILED(se->GetSession(k, sc.put()))) continue;
            AudioSessionState st; if (SUCCEEDED(sc->GetState(&st)) && st == AudioSessionStateExpired) continue;
            Com<IAudioSessionControl2> sc2;
            if (FAILED(sc->QueryInterface(kIID_IAudioSessionControl2, sc2.putVoid()))) continue;
            DWORD pid = 0; sc2->GetProcessId(&pid);
            if (pid == self) continue;
            std::string key = sc2->IsSystemSoundsSession() == S_OK ? "__system" : exeName(pid);
            if (key.empty()) continue;
            Com<ISimpleAudioVolume> vol; sc->QueryInterface(kIID_ISimpleAudioVolume, vol.putVoid());
            Com<IAudioMeterInformation> met; sc->QueryInterface(kIID_IAudioMeterInformation, met.putVoid());
            auto it = std::find_if(next.begin(), next.end(), [&](const AppGroup& g) { return g.key == key; });
            if (it == next.end()) {
                AppGroup g; g.key = key; g.label = labelFor(key);
                if (vol) { float v = 1; BOOL m = FALSE; vol->GetMasterVolume(&v); vol->GetMute(&m); g.volume = v; g.mute = m != FALSE; }
                next.push_back(g); it = next.end() - 1;
            }
            if (vol) it->volumes.push_back(vol);
            if (met) it->meters.push_back(met);
        }
    }
    // Ordre stable : les applis connues d'abord, puis alphabétique, sons système en dernier
    auto rank = [](const std::string& k) {
        static const char* order[] = {"discord", "spotify", "chrome", "msedge", "arc", "firefox"};
        for (int i = 0; i < 6; ++i) if (k == order[i]) return i;
        return k == "__system" ? 100 : 50;
    };
    std::sort(next.begin(), next.end(), [&](const AppGroup& a, const AppGroup& b) {
        int ra = rank(a.key), rb = rank(b.key);
        return ra != rb ? ra < rb : a.label < b.label;
    });
    groups_ = std::move(next);
}

// ---------------------------------------------------------------------------
float OutputDevice::peak() const { float v = 0; if (meter) meter->GetPeakValue(&v); return v; }

void OutputManager::refresh() {
    std::vector<OutputDevice> next;
    std::wstring def = defaultDeviceId(false);
    auto e = enumerator(); if (!e) return;
    Com<IMMDeviceCollection> col;
    if (FAILED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, col.put()))) return;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> d; if (FAILED(col->Item(i, d.put()))) continue;
        LPWSTR id = nullptr; if (FAILED(d->GetId(&id))) continue;
        OutputDevice o; o.id = id; CoTaskMemFree(id);
        o.name = friendlyName(d.get());
        std::string up = o.name; for (auto& c : up) c = (char)std::toupper((unsigned char)c);
        o.isVirtual = up.find("CABLE") != std::string::npos || up.find("VOICEMEETER") != std::string::npos ||
                      up.find("VB-AUDIO") != std::string::npos || up.find("VIRTUAL") != std::string::npos;
        o.isDefault = (o.id == def);
        d->Activate(kIID_IAudioEndpointVolume, CLSCTX_ALL, nullptr, o.vol.putVoid());
        d->Activate(kIID_IAudioMeterInformation, CLSCTX_ALL, nullptr, o.meter.putVoid());
        if (o.vol) { BOOL m = FALSE; o.vol->GetMasterVolumeLevelScalar(&o.volume); o.vol->GetMute(&m); o.mute = m != FALSE; }
        next.push_back(o);
    }
    std::sort(next.begin(), next.end(), [](const OutputDevice& a, const OutputDevice& b) {
        if (a.isVirtual != b.isVirtual) return !a.isVirtual;
        return a.name < b.name;
    });
    devs_ = std::move(next);
}

// ---------------------------------------------------------------------------
void MirrorEngine::start(const std::wstring& sourceId, const std::vector<std::wstring>& targets) {
    stop();
    source_ = sourceId; targetIds_.clear();
    for (auto& t : targets) if (!t.empty() && t != sourceId) targetIds_.push_back(t);
    if (source_.empty() || targetIds_.empty()) { setStatus(""); return; }
    stop_ = false; running_ = true;
    setStatus("Diffusion en cours");
    for (auto& id : targetIds_) {
        auto t = std::make_unique<Target>();
        t->id = id; t->ring = std::make_unique<Ring>(48000 * 2);
        targets_.push_back(std::move(t));
    }
    cap_ = std::thread(&MirrorEngine::captureThread, this);
    for (auto& t : targets_) t->th = std::thread(&MirrorEngine::renderThread, this, t.get());
}

void MirrorEngine::stop() {
    stop_ = true;
    if (cap_.joinable()) cap_.join();
    for (auto& t : targets_) if (t->th.joinable()) t->th.join();
    targets_.clear();
    running_ = false;
}

void MirrorEngine::captureThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD ti = 0; HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &ti);
    std::string err; int chans = 0;
    // Capture "ce que tu entends" sur la sortie principale (mode loopback, interrogation toutes les 3 ms)
    auto client = openClient(source_, 2, 200000, nullptr, &chans, &err, true);
    Com<IAudioCaptureClient> cap;
    if (!client || FAILED(client->GetService(kIID_IAudioCaptureClient, cap.putVoid())) || FAILED(client->Start())) {
        setStatus("Diffusion impossible : " + (err.empty() ? std::string("sortie principale indisponible") : err));
    } else {
        std::vector<float> st;
        while (!stop_) {
            Sleep(3);
            UINT32 pkt = 0; HRESULT hr;
            while (SUCCEEDED(hr = cap->GetNextPacketSize(&pkt)) && pkt > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                st.resize((size_t)frames * 2);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) std::fill(st.begin(), st.end(), 0.0f);
                else memcpy(st.data(), data, st.size() * sizeof(float));
                cap->ReleaseBuffer(frames);
                for (auto& t : targets_) t->ring->push(st.data(), st.size());
            }
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) { setStatus("Sortie principale changée"); break; }
        }
        client->Stop();
    }
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

void MirrorEngine::renderThread(Target* t) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD ti = 0; HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &ti);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::string err; int chans = 0;
    auto client = openClient(t->id, 2, 300000, ev, &chans, &err);
    Com<IAudioRenderClient> rc; UINT32 bufFrames = 0;
    if (!client || FAILED(client->GetService(kIID_IAudioRenderClient, rc.putVoid())) || FAILED(client->GetBufferSize(&bufFrames))) {
        if (err.find("0x8889000A") != std::string::npos) err = "bloquée par une autre appli";
        setStatus("Une sortie de diffusion est indisponible : " + err);
    } else {
        BYTE* d = nullptr;
        if (SUCCEEDED(rc->GetBuffer(bufFrames, &d))) rc->ReleaseBuffer(bufFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        client->Start();
        std::vector<float> tmp;
        const size_t target = 960 * 2, maxFill = target + 4800 * 2; // marge 20 ms, rattrapage au-delà de 120 ms
        while (!stop_) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
            UINT32 pad = 0;
            HRESULT hr = client->GetCurrentPadding(&pad);
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) { setStatus("Une sortie de diffusion a été débranchée"); break; }
            if (FAILED(hr)) continue;
            UINT32 avail = bufFrames - pad; if (!avail) continue;
            size_t have = t->ring->size();
            if (have > maxFill) t->ring->skip((have - target) & ~size_t(1));
            tmp.assign((size_t)avail * 2, 0.0f);
            size_t got = t->ring->pop(tmp.data(), tmp.size() & ~size_t(1));
            (void)got;
            if (FAILED(rc->GetBuffer(avail, &d))) continue;
            float* out = reinterpret_cast<float*>(d);
            for (UINT32 i = 0; i < avail; ++i) {
                out[(size_t)i * chans] = tmp[(size_t)i * 2];
                if (chans > 1) out[(size_t)i * chans + 1] = tmp[(size_t)i * 2 + 1];
                for (int c = 2; c < chans; ++c) out[(size_t)i * chans + c] = 0;
            }
            rc->ReleaseBuffer(avail, 0);
        }
        client->Stop();
    }
    CloseHandle(ev);
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}
