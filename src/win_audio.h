// Vox Studio - couche audio Windows (WASAPI, sessions, périphériques)
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

// MinGW ne fait que déclarer cette interface : on la définit (ordre de la vtable Windows)
#ifndef VOX_HAS_METER_INFO
struct IAudioMeterInformation : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 u32ChannelCount, float* afPeakValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* pdwHardwareSupportMask) = 0;
};
#endif
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include "dsp.h"

// Pointeur COM minimal
template <class T> class Com {
public:
    Com() = default;
    Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Com& operator=(const Com& o) { if (this != &o) { reset(); p_ = o.p_; if (p_) p_->AddRef(); } return *this; }
    ~Com() { reset(); }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T** put() { reset(); return &p_; }
    void** putVoid() { reset(); return reinterpret_cast<void**>(&p_); }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
private:
    T* p_ = nullptr;
};

std::string toUtf8(const wchar_t* w);
std::wstring toWide(const std::string& s);

struct DeviceInfo {
    std::wstring id;
    std::string  name;
};

std::vector<DeviceInfo> listDevices(bool capture);
std::wstring defaultDeviceId(bool capture);
bool setDefaultOutput(const std::wstring& id);

// File circulaire lock-free (1 producteur, 1 consommateur)
class Ring {
public:
    explicit Ring(size_t cap = 48000) : buf_(cap), cap_(cap) {}
    size_t size() const { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }
    void push(const float* d, size_t n) {
        size_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
        if (n > cap_ - (w - r)) n = cap_ - (w - r); // plein : on perd l'excédent
        for (size_t i = 0; i < n; ++i) buf_[(w + i) % cap_] = d[i];
        w_.store(w + n, std::memory_order_release);
    }
    size_t pop(float* d, size_t n) {
        size_t r = r_.load(std::memory_order_relaxed), w = w_.load(std::memory_order_acquire);
        if (n > w - r) n = w - r;
        for (size_t i = 0; i < n; ++i) d[i] = buf_[(r + i) % cap_];
        r_.store(r + n, std::memory_order_release);
        return n;
    }
    void skip(size_t n) {
        size_t r = r_.load(std::memory_order_relaxed), w = w_.load(std::memory_order_acquire);
        if (n > w - r) n = w - r;
        r_.store(r + n, std::memory_order_release);
    }
    void clear() { r_.store(w_.load()); }
private:
    std::vector<float> buf_;
    size_t cap_;
    std::atomic<size_t> w_{0}, r_{0};
};

// Moteur voix : micro -> effets -> micro virtuel (VB-Cable) + retour casque
class VoiceEngine {
public:
    enum Channel { Ch1 = 0, Ch2 = 1, Both = 2 };

    dsp::VoiceParams params;
    dsp::VoiceMeters meters;
    std::atomic<float> monitorVolume{0.8f};
    std::atomic<bool>  monitorOn{false};
    std::atomic<bool>  muted{false};

    ~VoiceEngine() { stop(); }

    bool start(const std::wstring& inputId, Channel ch, const std::wstring& cableId, const std::wstring& monitorId);
    void stop();
    bool running() const { return running_.load(); }
    bool failed() const { return failed_.load(); }
    std::string status();
    float latencyMs() const { return latencyMs_.load(); }
    size_t readAnalysis(float* dst, size_t n) { return ringAna_.pop(dst, n); }
    std::string monitorStatus() { std::lock_guard<std::mutex> l(statusMx_); return monStatus_; }

private:
    void captureThread(std::wstring id, Channel ch);
    void renderThread(std::wstring id, Ring* ring, bool isMonitor);
    void setStatus(const std::string& s, bool fail);

    std::atomic<bool> running_{false}, stopFlag_{false}, failed_{false};
    std::atomic<float> latencyMs_{0};
    std::thread tCap_, tOut_, tMon_;
    Ring ringOut_{48000}, ringMon_{48000}, ringAna_{16384};
    std::mutex statusMx_;
    std::string status_ = "Arrêté";
    std::string monStatus_;
    dsp::VoiceChain chain_;
};

// Diffusion multiple : copie le son de la sortie principale vers d'autres sorties
class MirrorEngine {
public:
    ~MirrorEngine() { stop(); }
    // targets : identifiants des sorties supplémentaires ; source = sortie principale Windows
    void start(const std::wstring& sourceId, const std::vector<std::wstring>& targets);
    void stop();
    bool running() const { return running_.load(); }
    std::string status() { std::lock_guard<std::mutex> l(mx_); return status_; }
    const std::wstring& source() const { return source_; }
    const std::vector<std::wstring>& targets() const { return targetIds_; }
private:
    struct Target { std::wstring id; std::unique_ptr<Ring> ring; std::thread th; };
    void captureThread();
    void renderThread(Target* t);
    void setStatus(const std::string& s) { std::lock_guard<std::mutex> l(mx_); status_ = s; }
    std::wstring source_;
    std::vector<std::wstring> targetIds_;
    std::vector<std::unique_ptr<Target>> targets_;
    std::thread cap_;
    std::atomic<bool> running_{false}, stop_{false};
    std::mutex mx_;
    std::string status_;
};

// Mixeur des applis (sessions audio Windows)
struct AppGroup {
    std::string key;        // nom d'exe en minuscules
    std::string label;      // nom affiché
    std::vector<Com<ISimpleAudioVolume>> volumes;
    std::vector<Com<IAudioMeterInformation>> meters;
    float volume = 1.0f;
    bool  mute = false;
    float peak() const;
    void  setVolume(float v);
    void  setMute(bool m);
};

class AppMixer {
public:
    void refresh();                 // à appeler ~1 fois par seconde depuis l'UI
    std::vector<AppGroup>& groups() { return groups_; }
private:
    std::vector<AppGroup> groups_;
};

// Périphériques de sortie (enceintes, casque...)
struct OutputDevice {
    std::wstring id;
    std::string  name;
    bool isDefault = false;
    bool isVirtual = false;
    Com<IAudioEndpointVolume> vol;
    Com<IAudioMeterInformation> meter;
    float volume = 1.0f;
    bool  mute = false;
    float peak() const;
};

class OutputManager {
public:
    void refresh();
    std::vector<OutputDevice>& devices() { return devs_; }
private:
    std::vector<OutputDevice> devs_;
};
