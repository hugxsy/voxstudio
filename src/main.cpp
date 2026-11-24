// Vox Studio V2 - interface (Dear ImGui + DirectX 11), design noir et orange
#include "win_audio.h"
#include "settings.h"
#include "updater.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <cstdio>
#include <complex>
#include <functional>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ============================================================ DirectX 11
static ID3D11Device* gDev = nullptr;
static ID3D11DeviceContext* gCtx = nullptr;
static IDXGISwapChain* gSwap = nullptr;
static ID3D11RenderTargetView* gRtv = nullptr;
static UINT gResizeW = 0, gResizeH = 0;

static void createRT() {
    ID3D11Texture2D* back = nullptr;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { gDev->CreateRenderTargetView(back, nullptr, &gRtv); back->Release(); }
}
static void cleanupRT() { if (gRtv) { gRtv->Release(); gRtv = nullptr; } }
static bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fls, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
    if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fls, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
    if (FAILED(hr)) return false;
    createRT();
    return true;
}
static void cleanupDevice() { cleanupRT(); if (gSwap) gSwap->Release(); if (gCtx) gCtx->Release(); if (gDev) gDev->Release(); }

static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg) {
    case WM_SIZE: if (w == SIZE_MINIMIZED) return 0; gResizeW = LOWORD(l); gResizeH = HIWORD(l); return 0;
    case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize = {1100, 700}; return 0; }
    case WM_SYSCOMMAND: if ((w & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ============================================================ Thème
namespace col {
static const ImVec4 bg      = ImVec4(0.000f, 0.000f, 0.000f, 1);
static const ImVec4 side    = ImVec4(0.043f, 0.043f, 0.047f, 1);
static const ImVec4 card    = ImVec4(0.082f, 0.082f, 0.090f, 1);
static const ImVec4 card2   = ImVec4(0.118f, 0.118f, 0.129f, 1);
static const ImVec4 line    = ImVec4(0.165f, 0.165f, 0.180f, 1);
static const ImVec4 text    = ImVec4(0.961f, 0.961f, 0.969f, 1);
static const ImVec4 text2   = ImVec4(0.557f, 0.557f, 0.576f, 1);
static const ImVec4 text3   = ImVec4(0.360f, 0.360f, 0.380f, 1);
static const ImVec4 orange  = ImVec4(1.000f, 0.584f, 0.000f, 1);   // #FF9500
static const ImVec4 orange2 = ImVec4(1.000f, 0.690f, 0.240f, 1);
static const ImVec4 red     = ImVec4(1.000f, 0.271f, 0.227f, 1);
static const ImVec4 green   = ImVec4(0.188f, 0.820f, 0.345f, 1);
static const ImVec4 track   = ImVec4(0.200f, 0.200f, 0.215f, 1);
}
static ImU32 U(const ImVec4& c, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

static ImFont *fBody, *fSmall, *fMed, *fTitle, *fHuge;
static float gDpi = 1.0f;
static float S(float v) { return v * gDpi; }

static void setupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.ChildRounding = 16; s.FrameRounding = 10; s.GrabRounding = 10; s.PopupRounding = 12;
    s.ScrollbarRounding = 10; s.WindowPadding = ImVec2(0, 0); s.FramePadding = ImVec2(12, 8); s.ItemSpacing = ImVec2(12, 10);
    s.ScrollbarSize = 8; s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.PopupBorderSize = 1; s.FrameBorderSize = 0;
    s.SelectableTextAlign = ImVec2(0, 0.5f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = col::bg; c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_PopupBg] = col::card2;
    c[ImGuiCol_Border] = col::line; c[ImGuiCol_Text] = col::text; c[ImGuiCol_TextDisabled] = col::text3;
    c[ImGuiCol_FrameBg] = col::card2; c[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.16f, 0.17f, 1); c[ImGuiCol_FrameBgActive] = ImVec4(0.19f, 0.19f, 0.2f, 1);
    c[ImGuiCol_Button] = col::card2; c[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.17f, 0.18f, 1); c[ImGuiCol_ButtonActive] = ImVec4(0.21f, 0.21f, 0.22f, 1);
    c[ImGuiCol_Header] = ImVec4(1, 0.584f, 0, 0.18f); c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.06f); c[ImGuiCol_HeaderActive] = ImVec4(1, 0.584f, 0, 0.28f);
    c[ImGuiCol_CheckMark] = col::orange; c[ImGuiCol_SliderGrab] = col::orange; c[ImGuiCol_SliderGrabActive] = col::orange2;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.2f); c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.28f);
    c[ImGuiCol_Separator] = col::line;
}

static void loadFonts(ImGuiIO& io) {
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0152, 0x0153, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2212, 0x2212, 0x25A0, 0x25FF, 0x2713, 0x2713, 0};
    auto pick = [](std::initializer_list<const char*> paths) -> const char* {
        for (auto p : paths) if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) return p;
        return nullptr;
    };
    const char* reg = pick({"C:\\Windows\\Fonts\\SegUIVar.ttf", "C:\\Windows\\Fonts\\segoeui.ttf"});
    const char* semi = pick({"C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\segoeui.ttf"});
    const char* bold = pick({"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeui.ttf"});
    ImFontConfig cfg; cfg.OversampleH = 2; cfg.OversampleV = 2;
    auto ld = [&](const char* p, float sz) -> ImFont* { return p ? io.Fonts->AddFontFromFileTTF(p, S(sz), &cfg, ranges) : nullptr; };
    fBody = ld(reg, 17); fSmall = ld(reg, 14); fMed = ld(semi, 17); fTitle = ld(bold, 30); fHuge = ld(bold, 22);
    if (!fBody) fBody = io.Fonts->AddFontDefault();
    for (ImFont** f : {&fSmall, &fMed, &fTitle, &fHuge}) if (!*f) *f = fBody;
    io.FontDefault = fBody;
}

// ============================================================ Widgets maison
static float anim(ImGuiID id, float target, float speed = 14.0f) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    float dt = ImGui::GetIO().DeltaTime;
    *v += (target - *v) * std::min(1.0f, dt * speed);
    return *v;
}

static void textCol(ImFont* f, const ImVec4& c, const char* t) {
    ImGui::PushFont(f); ImGui::PushStyleColor(ImGuiCol_Text, c); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont();
}

// Interrupteur façon iOS
static bool Switch(const char* id, bool* v) {
    ImVec2 sz(S(46), S(28));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, sz);
    bool changed = false;
    if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
    float t = anim(ImGui::GetID(id), *v ? 1.0f : 0.0f, 16);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bgc = ImLerp(col::track, col::orange, t);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), U(bgc), sz.y * 0.5f);
    float r = sz.y * 0.5f - S(2.5f);
    ImVec2 c(p.x + sz.y * 0.5f + t * (sz.x - sz.y), p.y + sz.y * 0.5f);
    dl->AddCircleFilled(ImVec2(c.x, c.y + S(1)), r, IM_COL32(0, 0, 0, 60), 32);
    dl->AddCircleFilled(c, r, IM_COL32(255, 255, 255, 255), 32);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return changed;
}
static bool SwitchA(const char* id, std::atomic<bool>& a) { bool b = a; bool c = Switch(id, &b); if (c) a = b; return c; }

// Curseur fin : libellé + valeur, piste fine orange, pouce blanc. Double-clic = valeur par défaut.
static bool Slim(const char* label, float* v, float mn, float mx, const char* fmt, float def, float width = -1, const char* tip = nullptr) {
    ImGui::PushID(label);
    float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    char val[48]; snprintf(val, sizeof val, fmt, *v);
    ImGui::PushFont(fSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, col::text2); ImGui::TextUnformatted(label); ImGui::PopStyleColor();
    float vw = ImGui::CalcTextSize(val).x;
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + w - vw, p0.y), U(col::text), val);
    ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = S(22);
    ImGui::InvisibleButton("##s", ImVec2(w, h));
    bool changed = false;
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    if (act) {
        float t = std::clamp((ImGui::GetIO().MousePos.x - p.x) / w, 0.0f, 1.0f);
        float nv = mn + t * (mx - mn);
        if (nv != *v) { *v = nv; changed = true; }
    }
    if (hov && ImGui::IsMouseDoubleClicked(0)) { *v = def; changed = true; }
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (tip && hov && !act) ImGui::SetTooltip("%s", tip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = p.y + h * 0.5f, th = S(4);
    dl->AddRectFilled(ImVec2(p.x, cy - th / 2), ImVec2(p.x + w, cy + th / 2), U(col::track), th);
    float t = (*v - mn) / (mx - mn);
    float zero = (mn < 0 && mx > 0) ? (-mn) / (mx - mn) : 0.0f;
    float a = std::min(t, zero), b = std::max(t, zero);
    bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    ImVec4 fc = disabled ? col::text3 : col::orange;
    dl->AddRectFilled(ImVec2(p.x + a * w, cy - th / 2), ImVec2(p.x + b * w, cy + th / 2), U(fc), th);
    float r = anim(ImGui::GetID("##r"), (hov || act) ? S(9) : S(7.5f), 20);
    ImVec2 c(p.x + t * w, cy);
    dl->AddCircleFilled(ImVec2(c.x, c.y + S(1)), r + 1, IM_COL32(0, 0, 0, 90), 24);
    dl->AddCircleFilled(c, r, disabled ? U(col::text2) : IM_COL32(255, 255, 255, 255), 24);
    ImGui::PopID();
    return changed;
}
static bool SlimA(const char* label, std::atomic<float>& a, float mn, float mx, const char* fmt, float def, float width = -1, const char* tip = nullptr) {
    float x = a; bool c = Slim(label, &x, mn, mx, fmt, def, width, tip); if (c) a = x; return c;
}

// Bouton pilule
static bool Pill(const char* label, bool active, ImVec2 size = ImVec2(0, 0), const ImVec4* activeCol = nullptr) {
    ImGui::PushFont(fMed);
    ImVec2 ts = ImGui::CalcTextSize(label);
    if (size.x <= 0) size.x = ts.x + S(28);
    if (size.y <= 0) size.y = S(36);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered();
    float t = anim(ImGui::GetID(label), active ? 1.0f : 0.0f, 18);
    ImVec4 ac = activeCol ? *activeCol : col::orange;
    ImVec4 bgc = ImLerp(hov ? ImVec4(0.17f, 0.17f, 0.18f, 1) : col::card2, ac, t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), U(bgc), size.y * 0.5f);
    ImVec4 tc = ImLerp(col::text, ImVec4(0, 0, 0, 1), t);
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), U(tc), label);
    ImGui::PopFont();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return r;
}

// Carte : fond arrondi, auto-hauteur
static bool beginCard(const char* id, float width = 0) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(18)));
    bool r = ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    return r;
}
static void endCard() { ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); }

// En-tête de carte : titre, sous-titre, interrupteur optionnel à droite
static void cardHeader(const char* title, const char* sub, std::atomic<bool>* on) {
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorPos();
    textCol(fMed, col::text, title);
    if (sub) textCol(fSmall, col::text2, sub);
    if (on) {
        ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(p.x + w - S(46), p.y + S(4)));
        SwitchA("##on", *on);
        ImGui::SetCursorPos(after);
    }
    ImGui::Dummy(ImVec2(0, S(2)));
}

static ImU32 levelColor(float db) {
    if (db > -3) return U(col::red);
    if (db > -12) return U(col::orange2);
    return U(col::orange);
}
static void hMeter(float lin, float* hold, ImVec2 size) {
    float db = dsp::linToDb(lin);
    float t = std::clamp((db + 60) / 60, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = size.y * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), U(col::track), r);
    if (t > 0.005f) dl->AddRectFilled(p, ImVec2(p.x + std::max(size.y, size.x * t), p.y + size.y), levelColor(db), r);
    if (hold) {
        *hold = std::max(lin, *hold * 0.985f);
        float ht = std::clamp((dsp::linToDb(*hold) + 60) / 60, 0.0f, 1.0f);
        if (ht > 0.01f) dl->AddRectFilled(ImVec2(p.x + size.x * ht - S(2), p.y), ImVec2(p.x + size.x * ht, p.y + size.y), IM_COL32(255, 255, 255, 200));
    }
    ImGui::Dummy(size);
}
static void grBar(float grDb, float w) {
    float t = std::clamp(-grDb / 20.0f, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos(); float h = S(4);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), U(col::track), h);
    if (t > 0.01f) dl->AddRectFilled(ImVec2(p.x + w * (1 - t), p.y), ImVec2(p.x + w, p.y + h), U(col::orange2), h);
    ImGui::Dummy(ImVec2(w, h));
}

// Fader vertical en forme de pilule, avec vu-mètre intégré
static bool Fader(const char* id, float* v, float meter, ImVec2 size, bool muted) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool changed = false, act = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    if (act) {
        float t = std::clamp(1.0f - (ImGui::GetIO().MousePos.y - p.y) / size.y, 0.0f, 1.0f);
        if (t != *v) { *v = t; changed = true; }
    }
    if (hov && ImGui::IsMouseDoubleClicked(0)) { *v = 1.0f; changed = true; }
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = size.x * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), U(col::card2), r);
    float t = anim(ImGui::GetID(id), *v, 22);
    float fh = std::max(size.x, size.y * t);
    ImVec4 fc = muted ? col::text3 : col::orange;
    if (t > 0.001f) dl->AddRectFilled(ImVec2(p.x, p.y + size.y - fh), ImVec2(p.x + size.x, p.y + size.y), U(fc), r);
    // niveau sonore : fine bande blanche translucide dans le remplissage
    float m = std::clamp((dsp::linToDb(meter) + 60) / 60, 0.0f, 1.0f) * t;
    if (m > 0.01f && !muted) {
        float mh = size.y * m;
        dl->AddRectFilled(ImVec2(p.x + size.x * 0.5f - S(2), p.y + size.y - mh), ImVec2(p.x + size.x * 0.5f + S(2), p.y + size.y - S(8)), IM_COL32(255, 255, 255, 110), S(2));
    }
    return changed;
}

// Bouton rond "power"
static bool PowerButton(const char* id, bool on, float d) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(d, d));
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float t = anim(ImGui::GetID(id), on ? 1.0f : 0.0f, 10);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + d / 2, p.y + d / 2);
    float r = d / 2;
    float pulse = on ? 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 2.2f) : 0;
    for (int i = 4; i >= 1; --i) dl->AddCircleFilled(c, r + i * S(4) * t, U(col::orange, 0.05f * t * (0.6f + 0.4f * pulse)), 48);
    dl->AddCircleFilled(c, r, U(ImLerp(col::card2, col::orange, t)), 48);
    if (hov && !on) dl->AddCircle(c, r, U(col::orange, 0.8f), 48, S(1.5f));
    ImU32 ic = U(ImLerp(col::orange, ImVec4(0, 0, 0, 1), t));
    float ir = r * 0.36f;
    dl->PathArcTo(c, ir, -IM_PI / 2 + 0.75f, 3 * IM_PI / 2 - 0.75f, 32);
    dl->PathStroke(ic, 0, S(2.6f));
    dl->AddLine(ImVec2(c.x, c.y - ir - S(3)), ImVec2(c.x, c.y - S(1)), ic, S(2.6f));
    return clicked;
}

// ============================================================ Analyseur de spectre
struct Spectrum {
    static constexpr int N = 2048, B = 72;
    std::vector<float> hist = std::vector<float>(N, 0.0f);
    float bands[B]{}, peak[B]{};
    void feed(const float* d, size_t n) {
        if (n >= (size_t)N) { std::copy(d + n - N, d + n, hist.begin()); return; }
        std::move(hist.begin() + n, hist.end(), hist.begin());
        std::copy(d, d + n, hist.end() - n);
    }
    void compute(float dt) {
        std::vector<std::complex<float>> x(N);
        for (int i = 0; i < N; ++i) x[i] = hist[i] * (0.5f - 0.5f * std::cos(2 * dsp::kPi * i / (N - 1)));
        for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(x[i], x[j]); }
        for (int len = 2; len <= N; len <<= 1) {
            float ang = -2 * dsp::kPi / len; std::complex<float> wl(std::cos(ang), std::sin(ang));
            for (int i = 0; i < N; i += len) { std::complex<float> w(1); for (int k = 0; k < len / 2; ++k) { auto u = x[i + k], v = x[i + k + len / 2] * w; x[i + k] = u + v; x[i + k + len / 2] = u - v; w *= wl; } }
        }
        const float fmin = 60, fmax = 16000;
        for (int b = 0; b < B; ++b) {
            float f0 = fmin * std::pow(fmax / fmin, (float)b / B), f1 = fmin * std::pow(fmax / fmin, (float)(b + 1) / B);
            int k0 = std::max(1, (int)(f0 * N / 48000)), k1 = std::max(k0 + 1, (int)(f1 * N / 48000));
            float mx = 0; for (int k = k0; k < k1 && k < N / 2; ++k) mx = std::max(mx, std::abs(x[k]));
            float db = dsp::linToDb(mx * 4.0f / N) + 6.0f * std::log2(std::max(f0, 1.0f) / 1000.0f) * 0.5f; // légère pente pour la lisibilité
            float t = std::clamp((db + 80) / 72, 0.0f, 1.0f);
            float a = t > bands[b] ? std::min(1.0f, dt * 30) : std::min(1.0f, dt * 6);
            bands[b] += (t - bands[b]) * a;
            peak[b] = std::max(bands[b], peak[b] - dt * 0.35f);
        }
    }
    void draw(ImVec2 size) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (float f : {100.0f, 1000.0f, 10000.0f}) {
            float x = p.x + size.x * std::log(f / 60) / std::log(16000.0f / 60);
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + size.y), U(col::line, 0.6f), 1);
            char lb[8]; snprintf(lb, sizeof lb, f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
            ImGui::PushFont(fSmall); dl->AddText(ImVec2(x + S(4), p.y + size.y - S(16)), U(col::text3), lb); ImGui::PopFont();
        }
        float bw = size.x / B, gap = std::max(1.0f, bw * 0.28f);
        for (int b = 0; b < B; ++b) {
            float h = std::max(S(2), bands[b] * size.y);
            float x0 = p.x + b * bw + gap * 0.5f, x1 = p.x + (b + 1) * bw - gap * 0.5f;
            ImU32 top = U(col::orange2), bot = U(col::orange, 0.25f);
            dl->AddRectFilledMultiColor(ImVec2(x0, p.y + size.y - h), ImVec2(x1, p.y + size.y), top, top, bot, bot);
            float ph = peak[b] * size.y;
            if (ph > S(3)) dl->AddRectFilled(ImVec2(x0, p.y + size.y - ph - S(2)), ImVec2(x1, p.y + size.y - ph), U(col::text, 0.5f));
        }
        ImGui::Dummy(size);
    }
};

// ============================================================ Application
static bool nameHas(const std::string& n, const char* s) {
    std::string a = n, b = s;
    for (auto& c : a) c = (char)toupper((unsigned char)c);
    for (auto& c : b) c = (char)toupper((unsigned char)c);
    return a.find(b) != std::string::npos;
}

struct App {
    VoiceEngine engine;
    MirrorEngine mirror;
    Updater updater;
    double lastUpdateCheck = -1e9;
    bool quitForUpdate = false;
    AppMixer mixer;
    OutputManager outputs;
    Settings st;
    Spectrum spec;
    std::vector<DeviceInfo> inputs, renders;
    float inHold = 0, outHold = 0;
    double lastRefresh = -10, lastDevRefresh = -10, lastRetry = 0, lastSave = 0;
    bool dirty = false, wantRunning = false;
    std::wstring lastDefault;

    void refreshDevices() { inputs = listDevices(true); renders = listDevices(false); }
    void autoPick() {
        auto has = [](const std::vector<DeviceInfo>& v, const std::wstring& id) {
            for (auto& d : v)
                if (d.id == id) return true;
            return false;
        };
        if (!has(inputs, st.inputId)) {
            st.inputId.clear();
            for (auto& d : inputs) if (nameHas(d.name, "MOTU")) { st.inputId = d.id; break; }
            if (st.inputId.empty()) {
                std::wstring def = defaultDeviceId(true);
                for (auto& d : inputs) if (d.id == def && !nameHas(d.name, "CABLE") && !nameHas(d.name, "VOICEMEETER")) st.inputId = d.id;
            }
        }
        if (!has(renders, st.cableId)) {
            st.cableId.clear();
            for (auto& d : renders) if (nameHas(d.name, "CABLE Input")) { st.cableId = d.id; break; }
        }
        if (!st.monitorId.empty() && !has(renders, st.monitorId)) st.monitorId.clear();
        if (st.monitorId.empty()) for (auto& d : renders) if (nameHas(d.name, "MOTU")) { st.monitorId = d.id; break; }
    }
    void startEngine() {
        engine.monitorOn = st.monitorOn; engine.monitorVolume = st.monitorVol;
        engine.start(st.inputId, (VoiceEngine::Channel)st.channel, st.cableId, st.monitorId);
        wantRunning = true;
    }
    void stopEngine() { engine.stop(); wantRunning = false; }
    void restartIfRunning() { if (wantRunning) startEngine(); }
    void applyMirror() {
        std::wstring def = defaultDeviceId(false);
        lastDefault = def;
        std::vector<std::wstring> t;
        for (auto& m : st.mirrors) if (m != def) t.push_back(m);
        if (t.empty()) mirror.stop(); else mirror.start(def, t);
    }
};

// ------------------------------------------------------------ Barre latérale
static const char* kPages[] = {"Voix", "Mixeur", "Sorties", "Réglages"};

static void drawSidebar(App& a, float w) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::side);
    ImGui::BeginChild("side", ImVec2(w, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pad = S(22);
    ImGui::SetCursorPos(ImVec2(pad, S(26)));
    ImVec2 lp = ImGui::GetCursorScreenPos();
    { const float hs[5] = {0.35f, 0.65f, 1.0f, 0.65f, 0.35f}; float bw = S(3), gp = S(2.2f), H = S(18), cy = lp.y + S(15);
      for (int i = 0; i < 5; ++i) { float x = lp.x + i * (bw + gp), h = H * hs[i];
        dl->AddRectFilled(ImVec2(x, cy - h / 2), ImVec2(x + bw, cy + h / 2), U(col::orange), bw); } }
    ImGui::SetCursorPosX(pad + S(30));
    textCol(fHuge, col::text, "Vox Studio");
    ImGui::Dummy(ImVec2(0, S(18)));

    for (int i = 0; i < 4; ++i) {
        ImGui::SetCursorPosX(S(12));
        bool sel = a.st.page == i;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 sz(w - S(24), S(42));
        if (ImGui::InvisibleButton(kPages[i], sz)) { a.st.page = i; a.dirty = true; }
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float t = anim(ImGui::GetID(kPages[i]), sel ? 1.0f : (hov ? 0.4f : 0.0f), 18);
        dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), U(ImVec4(1, 1, 1, 0.07f * t)), S(10));
        if (sel) dl->AddRectFilled(ImVec2(p.x, p.y + S(11)), ImVec2(p.x + S(3), p.y + sz.y - S(11)), U(col::orange), S(2));
        ImGui::PushFont(sel ? fMed : fBody);
        dl->AddText(ImVec2(p.x + S(18), p.y + (sz.y - ImGui::GetFontSize()) * 0.5f), U(sel ? col::text : col::text2), kPages[i]);
        ImGui::PopFont();
    }

    // Bloc d'état en bas
    float bh = S(300);
    ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - bh));
    bool on = a.engine.running() && !a.engine.failed();
    float d = S(84);
    ImGui::SetCursorPosX((w - d) / 2);
    if (PowerButton("##power", on, d)) { if (on || a.wantRunning) a.stopEngine(); else a.startEngine(); a.dirty = true; }
    ImGui::Dummy(ImVec2(0, S(6)));
    const char* stt = on ? "Voix active" : (a.engine.failed() ? "Problème" : "Voix coupée");
    ImGui::PushFont(fMed);
    float tw = ImGui::CalcTextSize(stt).x;
    ImGui::SetCursorPosX((w - tw) / 2);
    ImGui::TextColored(on ? col::orange : (a.engine.failed() ? col::red : col::text2), "%s", stt);
    ImGui::PopFont();
    ImGui::PushFont(fSmall);
    std::string sub = on ? ("Latence ~" + std::to_string((int)a.engine.latencyMs()) + " ms") : a.engine.status();
    if (!on && !a.engine.failed()) sub = "Clique pour activer";
    ImGui::PushTextWrapPos(w - S(16));
    float sw = std::min(ImGui::CalcTextSize(sub.c_str()).x, w - S(32));
    ImGui::SetCursorPosX(std::max(S(16), (w - sw) / 2));
    ImGui::TextColored(a.engine.failed() ? col::red : col::text2, "%s", sub.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::SetCursorPosX(S(22));
    bool mon = a.st.monitorOn;
    if (Pill(mon ? "Je m'entends au casque" : "Retour casque coupé", mon, ImVec2(w - S(44), S(40)))) {
        a.st.monitorOn = !mon; a.engine.monitorOn = a.st.monitorOn; a.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Entendre ou non ta voix dans le casque. Les autres sons ne changent pas.");
    ImGui::Dummy(ImVec2(0, S(2)));
    bool m = a.engine.muted;
    ImGui::SetCursorPosX(S(22));
    if (Pill(m ? "Micro coupé" : "Couper le micro", m, ImVec2(w - S(44), S(40)), &col::red)) a.engine.muted = !m;
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void pageTitle(const char* t, const char* sub) {
    textCol(fTitle, col::text, t);
    if (sub) textCol(fBody, col::text2, sub);
    ImGui::Dummy(ImVec2(0, S(10)));
}

// ------------------------------------------------------------ Page Voix
static void drawVoice(App& a) {
    auto& p = a.engine.params;
    auto& m = a.engine.meters;
    std::string sub = std::string("Preset : ") + dsp::presetName(a.st.preset);
    pageTitle("Voix", sub.c_str());
    float W = ImGui::GetContentRegionAvail().x;

    // --- Carte héros : spectre + niveaux
    beginCard("hero");
    {
        float cw = ImGui::GetContentRegionAvail().x;
        float side = S(230);
        ImGui::BeginGroup();
        a.spec.draw(ImVec2(cw - side - S(24), S(150)));
        ImGui::EndGroup();
        ImGui::SameLine(0, S(24));
        ImGui::BeginGroup();
        bool speaking = m.vad.load() > 0.5f || m.gateGainDb.load() > -3.0f;
        float t = anim(ImGui::GetID("spk"), (speaking && a.engine.running()) ? 1.0f : 0.0f, 10);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cp.x + S(6), cp.y + S(11)), S(5), U(ImLerp(col::text3, col::orange, t)), 16);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(18));
        textCol(fMed, ImLerp(col::text2, col::text, t), t > 0.5f ? "Tu parles" : "Silence");
        ImGui::Dummy(ImVec2(0, S(6)));
        textCol(fSmall, col::text2, "Entrée");
        hMeter(m.inPeak, &a.inHold, ImVec2(side, S(6)));
        ImGui::Dummy(ImVec2(0, S(4)));
        textCol(fSmall, col::text2, "Sortie");
        hMeter(m.outPeak, &a.outHold, ImVec2(side, S(6)));
        ImGui::Dummy(ImVec2(0, S(4)));
        if (m.clipped) {
            textCol(fSmall, col::red, "Ça sature : baisse le gain sur la MOTU");
            if (ImGui::SmallButton("OK")) m.clipped = false;
        } else {
            char b[64]; snprintf(b, sizeof b, "Niveleur %+.1f dB", (double)m.levelerGainDb.load());
            textCol(fSmall, col::text3, b);
        }
        ImGui::EndGroup();
    }
    endCard();
    ImGui::Dummy(ImVec2(0, S(6)));

    // --- Presets
    for (int i = 0; i < (int)dsp::Preset::Count; ++i) {
        if (i) ImGui::SameLine(0, S(8));
        if (ImGui::GetCursorPosX() + S(100) > W) ImGui::NewLine();
        if (Pill(dsp::presetName(i), a.st.preset == i)) { a.st.preset = i; dsp::applyPreset(p, (dsp::Preset)i); a.dirty = true; }
    }
    ImGui::Dummy(ImVec2(0, S(10)));

    // --- Cartes d'effets sur 2 ou 3 colonnes
    bool changed = false;
    int cols = W > S(1150) ? 3 : 2;
    float gap = S(14), cw = (W - gap * (cols - 1)) / cols;
    std::vector<std::function<void()>> cards;
    auto add = [&](const char* id, auto body) { cards.push_back([=, &changed]() mutable { beginCard(id, cw); body(); endCard(); }); };

    add("den", [&] {
        cardHeader("Anti-bruit IA", "Clavier, ventilo, souffle", &p.denoiseOn);
        ImGui::BeginDisabled(!p.denoiseOn);
        float v = p.denoiseAmount * 100;
        if (Slim("Intensité", &v, 0, 100, "%.0f %%", 90)) { p.denoiseAmount = v / 100; changed = true; }
        ImGui::EndDisabled();
    });
    add("gate", [&] {
        cardHeader("Noise gate", "Ferme le micro quand tu ne parles pas", &p.gateOn);
        ImGui::BeginDisabled(!p.gateOn);
        changed |= SlimA("Seuil", p.gateThreshDb, -80, -20, "%.0f dB", -50, -1, "Monte-le si le clavier passe, baisse-le si le début des mots est coupé");
        bool open = m.gateGainDb.load() > -6;
        textCol(fSmall, open ? col::orange : col::text3, open ? "Ouvert" : "Fermé");
        ImGui::EndDisabled();
    });
    add("eq", [&] {
        cardHeader("Timbre", "La couleur de ta voix", &p.eqOn);
        ImGui::BeginDisabled(!p.eqOn);
        changed |= SlimA("Chaleur", p.eqWarmthDb, -6, 9, "%+.1f dB", 0);
        changed |= SlimA("Désépaissir", p.eqMudDb, -9, 3, "%+.1f dB", 0);
        changed |= SlimA("Présence", p.eqPresenceDb, -6, 9, "%+.1f dB", 0);
        changed |= SlimA("Air", p.eqAirDb, -6, 9, "%+.1f dB", 0);
        ImGui::EndDisabled();
    });
    add("comp", [&] {
        cardHeader("Compresseur", "Voix posée et dense, effet radio", &p.compOn);
        ImGui::BeginDisabled(!p.compOn);
        changed |= SlimA("Seuil", p.compThreshDb, -50, 0, "%.0f dB", -24);
        changed |= SlimA("Intensité", p.compRatio, 1, 12, "%.1f : 1", 4);
        changed |= SlimA("Volume ajouté", p.compMakeupDb, 0, 20, "%+.0f dB", 8);
        textCol(fSmall, col::text3, "Réduction en cours");
        grBar(m.compGrDb, ImGui::GetContentRegionAvail().x);
        ImGui::EndDisabled();
    });
    add("lev", [&] {
        cardHeader("Niveleur", "Garde un volume constant", &p.levelerOn);
        ImGui::BeginDisabled(!p.levelerOn);
        changed |= SlimA("Niveau visé", p.levelerTargetDb, -30, -12, "%.0f dB", -20);
        ImGui::EndDisabled();
    });
    add("ds", [&] {
        cardHeader("Anti-sifflantes", "Adoucit les « s » et « ch »", &p.deessOn);
        ImGui::BeginDisabled(!p.deessOn);
        changed |= SlimA("Intensité max", p.deessMaxDb, 0, 15, "%.0f dB", 8);
        changed |= SlimA("Sensibilité", p.deessThreshDb, -50, -10, "%.0f dB", -32);
        grBar(m.deessGrDb, ImGui::GetContentRegionAvail().x);
        ImGui::EndDisabled();
    });
    add("ex", [&] {
        cardHeader("Brillance", "Harmoniques aiguës façon FM", &p.exciterOn);
        ImGui::BeginDisabled(!p.exciterOn);
        float v = p.exciterAmount * 100;
        if (Slim("Quantité", &v, 0, 100, "%.0f %%", 30)) { p.exciterAmount = v / 100; changed = true; }
        ImGui::EndDisabled();
    });
    add("sat", [&] {
        cardHeader("Chaleur analogique", "Grain de console radio", &p.satOn);
        ImGui::BeginDisabled(!p.satOn);
        float v = p.satAmount * 100;
        if (Slim("Quantité", &v, 0, 100, "%.0f %%", 25)) { p.satAmount = v / 100; changed = true; }
        ImGui::EndDisabled();
    });
    add("pitch", [&] {
        cardHeader("Hauteur", "Voix plus grave ou plus aiguë", &p.pitchOn);
        ImGui::BeginDisabled(!p.pitchOn);
        changed |= SlimA("Demi-tons", p.pitchSemis, -12, 12, "%+.1f", 0, -1, "Au-delà de 2 à 3 demi-tons, la voix devient vite artificielle");
        ImGui::EndDisabled();
    });
    add("fx", [&] {
        cardHeader("Effet spécial", "Pour le fun", nullptr);
        const char* names[] = {"Aucun", "Talkie", "Téléphone", "Robot"};
        float bw = (ImGui::GetContentRegionAvail().x - S(6) * 3) / 4;
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine(0, S(6));
            ImGui::PushID(i);
            if (Pill(names[i], p.fxMode == i, ImVec2(bw, S(32)))) { p.fxMode = i; changed = true; }
            ImGui::PopID();
        }
        ImGui::BeginDisabled(p.fxMode == dsp::FxNone);
        float v = p.fxMix * 100;
        if (Slim("Dosage", &v, 0, 100, "%.0f %%", 100)) { p.fxMix = v / 100; changed = true; }
        ImGui::EndDisabled();
    });
    add("echo", [&] {
        cardHeader("Écho", "Répétitions de la voix", &p.echoOn);
        ImGui::BeginDisabled(!p.echoOn);
        changed |= SlimA("Temps", p.echoTimeMs, 60, 900, "%.0f ms", 280);
        float fb = p.echoFeedback * 100, mx = p.echoMix * 100;
        if (Slim("Répétitions", &fb, 0, 85, "%.0f %%", 35)) { p.echoFeedback = fb / 100; changed = true; }
        if (Slim("Dosage", &mx, 0, 80, "%.0f %%", 20)) { p.echoMix = mx / 100; changed = true; }
        ImGui::EndDisabled();
    });
    add("rev", [&] {
        cardHeader("Réverb", "Ambiance de pièce ou de studio", &p.reverbOn);
        ImGui::BeginDisabled(!p.reverbOn);
        float sz = p.reverbSize * 100, dp = p.reverbDamp * 100, mx = p.reverbMix * 100;
        if (Slim("Taille", &sz, 0, 100, "%.0f %%", 45)) { p.reverbSize = sz / 100; changed = true; }
        if (Slim("Douceur", &dp, 0, 100, "%.0f %%", 50)) { p.reverbDamp = dp / 100; changed = true; }
        if (Slim("Dosage", &mx, 0, 60, "%.0f %%", 15)) { p.reverbMix = mx / 100; changed = true; }
        ImGui::EndDisabled();
    });
    add("hpf", [&] {
        cardHeader("Coupe-bas", "Retire les grondements", &p.hpfOn);
        ImGui::BeginDisabled(!p.hpfOn);
        changed |= SlimA("Fréquence", p.hpfHz, 40, 200, "%.0f Hz", 90);
        ImGui::EndDisabled();
    });
    add("out", [&] {
        cardHeader("Sortie", "Limiteur anti-saturation", &p.limiterOn);
        changed |= SlimA("Volume final", p.outputGainDb, -20, 12, "%+.1f dB", 0);
        ImGui::BeginDisabled(!p.limiterOn);
        changed |= SlimA("Plafond", p.limiterCeilDb, -12, 0, "%.1f dB", -1);
        ImGui::EndDisabled();
    });

    // répartition en colonnes (remplissage par colonne la plus courte, hauteurs mémorisées)
    static std::vector<float> heights;
    if (heights.size() != cards.size()) heights.assign(cards.size(), S(150));
    std::vector<std::vector<int>> colIdx(cols);
    std::vector<float> colH(cols, 0);
    for (size_t i = 0; i < cards.size(); ++i) {
        int c = (int)(std::min_element(colH.begin(), colH.end()) - colH.begin());
        colIdx[c].push_back((int)i); colH[c] += heights[i] + gap;
    }
    ImVec2 start = ImGui::GetCursorPos();
    float maxY = start.y;
    for (int c = 0; c < cols; ++c) {
        float y = start.y;
        for (int i : colIdx[c]) {
            ImGui::SetCursorPos(ImVec2(start.x + c * (cw + gap), y));
            cards[i]();
            heights[i] = ImGui::GetItemRectSize().y;
            y += heights[i] + gap;
        }
        maxY = std::max(maxY, y);
    }
    ImGui::SetCursorPos(ImVec2(start.x, maxY));
    ImGui::Dummy(ImVec2(0, S(20)));

    if (changed) { a.dirty = true; if (a.st.preset == (int)dsp::Preset::Off) p.bypass = false; a.st.preset = -1; }
}

// ------------------------------------------------------------ Page Mixeur
static void drawMixer(App& a) {
    pageTitle("Mixeur", "Le volume de chaque appli, indépendamment");
    auto& gs = a.mixer.groups();
    float H = std::max(S(300), ImGui::GetContentRegionAvail().y - S(30));
    float cardW = S(124), faderH = H - S(150);

    // Sortie principale en premier
    OutputDevice* def = nullptr;
    for (auto& d : a.outputs.devices()) if (d.isDefault) def = &d;

    auto strip = [&](const char* id, const std::string& name, const char* sub, float* vol, bool* mute, float meter, auto onVol, auto onMute) {
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col::card);
        ImGui::BeginChild("strip", ImVec2(cardW, H), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImGui::PushFont(fMed);
        std::string nm = name; while (nm.size() > 3 && ImGui::CalcTextSize(nm.c_str()).x > cardW - S(16)) nm = nm.substr(0, nm.size() - 2) + ".";
        float tw = ImGui::CalcTextSize(nm.c_str()).x;
        dl->AddText(ImVec2(wp.x + (cardW - tw) / 2, wp.y + S(16)), U(col::text), nm.c_str());
        ImGui::PopFont();
        ImGui::PushFont(fSmall);
        float sw = ImGui::CalcTextSize(sub).x;
        dl->AddText(ImVec2(wp.x + (cardW - sw) / 2, wp.y + S(38)), U(col::text3), sub);
        ImGui::PopFont();
        float fw = S(52);
        ImGui::SetCursorPos(ImVec2((cardW - fw) / 2, S(66)));
        float v = *vol;
        if (Fader("##f", &v, *mute ? 0 : meter, ImVec2(fw, faderH), *mute)) onVol(v);
        char pct[16]; snprintf(pct, sizeof pct, "%.0f", *vol * 100);
        ImGui::PushFont(fHuge);
        float pw = ImGui::CalcTextSize(pct).x;
        ImGui::SetCursorPos(ImVec2((cardW - pw) / 2, S(76) + faderH));
        ImGui::TextUnformatted(pct);
        ImGui::PopFont();
        ImGui::SetCursorPos(ImVec2(S(14), H - S(48)));
        if (Pill(*mute ? "Muet" : "Son", *mute, ImVec2(cardW - S(28), S(34)), &col::red)) onMute(!*mute);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    };

    ImGui::BeginChild("mixscroll", ImVec2(0, H + S(14)), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    bool first = true;
    if (def) {
        strip("__master", "Général", "sortie principale", &def->volume, &def->mute, def->peak(),
              [&](float v) { def->volume = v; if (def->vol) def->vol->SetMasterVolumeLevelScalar(v, nullptr); },
              [&](bool m) { def->mute = m; if (def->vol) def->vol->SetMute(m, nullptr); });
        first = false;
    }
    for (auto& g : gs) {
        if (!first) ImGui::SameLine(0, S(12));
        first = false;
        strip(g.key.c_str(), g.label, "appli", &g.volume, &g.mute, g.peak(),
              [&](float v) { g.setVolume(v); }, [&](bool m) { g.setMute(m); });
    }
    if (gs.empty()) { ImGui::SameLine(0, S(20)); textCol(fBody, col::text2, "Aucune appli ne joue de son pour l'instant."); }
    ImGui::EndChild();
    textCol(fSmall, col::text3, "Double-clic sur un fader pour revenir à 100.");
}

// ------------------------------------------------------------ Page Sorties
static void drawOutputs(App& a) {
    pageTitle("Sorties", "Où part le son de ton PC");
    float W = ImGui::GetContentRegionAvail().x;
    beginCard("outs");
    {
        textCol(fMed, col::text, "Sortie principale et diffusion");
        textCol(fSmall, col::text2, "Clique sur un périphérique pour en faire la sortie principale. Active « Diffuser aussi » pour y copier le son en même temps.");
        ImGui::Dummy(ImVec2(0, S(8)));
        float cw = ImGui::GetContentRegionAvail().x;
        bool mirrorChanged = false;
        int row = 0;
        for (auto& d : a.outputs.devices()) {
            if (d.isVirtual && !a.st.showVirtual) continue;
            ImGui::PushID(toUtf8(d.id.c_str()).c_str());
            if (row++) { ImVec2 lp = ImGui::GetCursorScreenPos(); ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + cw, lp.y), U(col::line), 1); ImGui::Dummy(ImVec2(0, S(6))); }
            ImVec2 p = ImGui::GetCursorScreenPos();
            float nameW = cw * 0.38f;
            // coche + nom (cliquable)
            ImGui::InvisibleButton("##def", ImVec2(nameW, S(44)));
            bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked() && !d.isDefault) { setDefaultOutput(d.id); a.lastRefresh = -10; }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 cc(p.x + S(12), p.y + S(22));
            if (d.isDefault) {
                dl->AddCircleFilled(cc, S(11), U(col::orange), 24);
                dl->AddLine(ImVec2(cc.x - S(5), cc.y), ImVec2(cc.x - S(1.5f), cc.y + S(4)), IM_COL32(0, 0, 0, 255), S(2.2f));
                dl->AddLine(ImVec2(cc.x - S(1.5f), cc.y + S(4)), ImVec2(cc.x + S(5.5f), cc.y - S(4)), IM_COL32(0, 0, 0, 255), S(2.2f));
            } else dl->AddCircle(cc, S(10.5f), U(hov ? col::orange : col::text3), 24, S(1.5f));
            ImGui::PushFont(fMed);
            std::string nm = d.name; while (nm.size() > 4 && ImGui::CalcTextSize(nm.c_str()).x > nameW - S(40)) nm = nm.substr(0, nm.size() - 2);
            if (nm != d.name) nm += "…";
            dl->AddText(ImVec2(p.x + S(34), p.y + S(4)), U(col::text), nm.c_str());
            ImGui::PopFont();
            ImGui::PushFont(fSmall);
            bool mirrored = std::find(a.st.mirrors.begin(), a.st.mirrors.end(), d.id) != a.st.mirrors.end();
            const char* role = d.isDefault ? "Sortie principale" : (mirrored ? "Diffusion en cours" : "Inactive");
            dl->AddText(ImVec2(p.x + S(34), p.y + S(25)), U(d.isDefault || mirrored ? col::orange : col::text3), role);
            ImGui::PopFont();

            // volume + niveau
            ImGui::SameLine(0, S(16));
            float sw = cw - nameW - S(16) - S(250);
            ImGui::BeginGroup();
            float v = d.volume * 100;
            if (Slim("Volume", &v, 0, 100, "%.0f %%", 50, sw) && d.vol) { d.volume = v / 100; d.vol->SetMasterVolumeLevelScalar(d.volume, nullptr); }
            hMeter(d.mute ? 0 : d.peak(), nullptr, ImVec2(sw, S(3)));
            ImGui::EndGroup();

            // diffusion + muet
            ImGui::SameLine(0, S(20));
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, S(2)));
            if (!d.isDefault && !d.isVirtual) {
                bool b = mirrored;
                ImGui::PushFont(fSmall); ImGui::TextColored(col::text2, "Diffuser aussi"); ImGui::PopFont();
                ImGui::SameLine(0, S(8));
                if (Switch("##mir", &b)) {
                    if (b) a.st.mirrors.push_back(d.id);
                    else a.st.mirrors.erase(std::remove(a.st.mirrors.begin(), a.st.mirrors.end(), d.id), a.st.mirrors.end());
                    mirrorChanged = true;
                }
            } else ImGui::Dummy(ImVec2(S(150), S(28)));
            ImGui::EndGroup();
            ImGui::SameLine(0, S(12));
            if (Pill(d.mute ? "Muet" : "Son", d.mute, ImVec2(S(70), S(32)), &col::red) && d.vol) { d.mute = !d.mute; d.vol->SetMute(d.mute, nullptr); }
            ImGui::Dummy(ImVec2(0, S(4)));
            ImGui::PopID();
        }
        if (mirrorChanged) { a.applyMirror(); a.dirty = true; }
        ImGui::Dummy(ImVec2(0, S(6)));
        bool sv = a.st.showVirtual;
        if (Switch("##virt", &sv)) { a.st.showVirtual = sv; a.dirty = true; }
        ImGui::SameLine(0, S(10)); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
        textCol(fSmall, col::text2, "Afficher les périphériques virtuels (VB-Cable, Voicemeeter)");
        std::string ms = a.mirror.status();
        if (!ms.empty() && a.mirror.running()) textCol(fSmall, ms.find("impossible") != std::string::npos || ms.find("indisponible") != std::string::npos ? col::red : col::text3, ms.c_str());
    }
    endCard();
    (void)W;
    ImGui::Dummy(ImVec2(0, S(12)));
    beginCard("tip");
    textCol(fMed, col::text, "Bon à savoir");
    textCol(fSmall, col::text2, "La diffusion copie le son de la sortie principale vers les autres sorties. Chaque carte son a sa propre horloge : un léger décalage (20 à 40 ms) est normal.");
    endCard();
}

static void drawUpdateCard(App& a, float cw);

// ------------------------------------------------------------ Page Réglages
static bool deviceCombo(const char* id, const std::vector<DeviceInfo>& devs, std::wstring& sel, const char* none = nullptr) {
    std::string cur = none ? none : "(aucun)";
    for (auto& d : devs) if (d.id == sel) cur = d.name;
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14), S(10)));
    if (ImGui::BeginCombo(id, cur.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (none && ImGui::Selectable(none, sel.empty())) { sel.clear(); changed = true; }
        for (auto& d : devs) if (ImGui::Selectable(d.name.c_str(), d.id == sel)) { sel = d.id; changed = true; }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    return changed;
}

static void drawSettings(App& a) {
    pageTitle("Réglages", "Micro, micro virtuel et retour casque");
    float W = ImGui::GetContentRegionAvail().x;
    float cw = (W - S(14)) / 2;
    auto& p = a.engine.params;

    ImGui::BeginGroup();
    beginCard("mic", cw);
    textCol(fMed, col::text, "Micro");
    textCol(fSmall, col::text2, "Ta carte son et l'entrée où est branché le micro XLR");
    ImGui::Dummy(ImVec2(0, S(6)));
    if (deviceCombo("##in", a.inputs, a.st.inputId)) { a.dirty = true; a.restartIfRunning(); }
    const char* chs[] = {"Entrée 1", "Entrée 2", "Les deux"};
    float bw = (ImGui::GetContentRegionAvail().x - S(12)) / 3;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, S(6));
        ImGui::PushID(i);
        if (Pill(chs[i], a.st.channel == i, ImVec2(bw, S(34)))) { a.st.channel = i; a.dirty = true; a.restartIfRunning(); }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    if (SlimA("Gain logiciel", p.inputGainDb, -12, 30, "%+.1f dB", 0, -1, "Règle d'abord le gain sur la MOTU : vise -18 dB en parlant normalement")) a.dirty = true;
    hMeter(a.engine.meters.inPeak, &a.inHold, ImVec2(ImGui::GetContentRegionAvail().x, S(6)));
    endCard();
    ImGui::Dummy(ImVec2(0, S(6)));
    beginCard("gen", cw);
    textCol(fMed, col::text, "Démarrage");
    ImGui::Dummy(ImVec2(0, S(4)));
    bool as = a.st.autoStart;
    if (Switch("##as", &as)) { a.st.autoStart = as; a.dirty = true; }
    ImGui::SameLine(0, S(10)); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
    textCol(fBody, col::text, "Activer ma voix au lancement");
    endCard();
    ImGui::Dummy(ImVec2(0, S(6)));
    drawUpdateCard(a, cw);
    ImGui::EndGroup();

    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    beginCard("cable", cw);
    textCol(fMed, col::text, "Micro virtuel");
    textCol(fSmall, col::text2, "Où part ta voix traitée. Dans Discord / OBS, choisis « CABLE Output » comme micro.");
    ImGui::Dummy(ImVec2(0, S(6)));
    if (deviceCombo("##cable", a.renders, a.st.cableId)) { a.dirty = true; a.restartIfRunning(); }
    endCard();
    ImGui::Dummy(ImVec2(0, S(6)));
    beginCard("mon", cw);
    float w2 = ImGui::GetContentRegionAvail().x;
    ImVec2 hp = ImGui::GetCursorPos();
    textCol(fMed, col::text, "M'écouter");
    textCol(fSmall, col::text2, "Entends ta voix avec les effets (environ 30 ms de décalage)");
    ImVec2 after = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(hp.x + w2 - S(46), hp.y + S(4)));
    bool mo = a.st.monitorOn;
    if (Switch("##mon", &mo)) { a.st.monitorOn = mo; a.engine.monitorOn = mo; a.dirty = true; }
    ImGui::SetCursorPos(after);
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::BeginDisabled(!a.st.monitorOn);
    if (deviceCombo("##mondev", a.renders, a.st.monitorId, "(choisir un casque)")) { a.dirty = true; a.restartIfRunning(); }
    float mv = a.st.monitorVol * 100;
    if (Slim("Volume du retour", &mv, 0, 100, "%.0f %%", 80)) { a.st.monitorVol = mv / 100; a.engine.monitorVolume = a.st.monitorVol; a.dirty = true; }
    ImGui::EndDisabled();
    std::string ms = a.engine.monitorStatus();
    if (!ms.empty() && a.st.monitorOn) {
        textCol(fSmall, col::red, ("Indisponible : " + ms).c_str());
        if (Pill("Réessayer", false, ImVec2(S(120), S(32)))) a.restartIfRunning();
    }
    endCard();
    ImGui::EndGroup();
}

// ------------------------------------------------------------ Mises à jour
static void drawUpdateBanner(App& a) {
    Updater::State s = a.updater.state();
    if (s != Updater::Available && s != Updater::Downloading && s != Updater::Ready) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 0.584f, 0.0f, 0.14f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(14)));
    ImGui::BeginChild("upd", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    float w = ImGui::GetContentRegionAvail().x;
    std::string t = "Vox Studio " + a.updater.latest() + " est disponible";
    textCol(fMed, col::orange, t.c_str());
    std::string n = a.updater.notes();
    if (!n.empty()) { if (n.size() > 160) n = n.substr(0, 157) + "..."; for (auto& ch : n) if (ch == '\n') ch = ' '; textCol(fSmall, col::text2, n.c_str()); }
    ImGui::SetCursorPos(ImVec2(w - S(190), S(16)));
    if (s == Updater::Available) {
        if (Pill("Installer et redémarrer", true, ImVec2(S(210), S(36)))) a.updater.installAsync();
    } else {
        char b[48]; snprintf(b, sizeof b, s == Updater::Ready ? "Redémarrage…" : "Téléchargement %.0f %%", a.updater.progress() * 100);
        textCol(fMed, col::text, b);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(); ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(10)));
}

static void drawUpdateCard(App& a, float cw) {
    beginCard("updcard", cw);
    textCol(fMed, col::text, "Mises à jour");
    std::string v = std::string("Version installée : ") + VOX_VERSION;
    textCol(fSmall, col::text2, v.c_str());
    ImGui::Dummy(ImVec2(0, S(4)));
    Updater::State s = a.updater.state();
    const char* msg = "";
    std::string err;
    switch (s) {
    case Updater::Checking: msg = "Recherche en cours…"; break;
    case Updater::UpToDate: msg = "Tu as la dernière version"; break;
    case Updater::Available: msg = "Une nouvelle version est disponible (bandeau en haut)"; break;
    case Updater::Downloading: msg = "Téléchargement…"; break;
    case Updater::Ready: msg = "Redémarrage…"; break;
    case Updater::Failed: err = "Vérification impossible : " + a.updater.error(); msg = err.c_str(); break;
    default: break;
    }
    if (*msg) textCol(fSmall, s == Updater::Failed ? col::text3 : (s == Updater::Available ? col::orange : col::text2), msg);
    ImGui::Dummy(ImVec2(0, S(4)));
    if (Pill("Vérifier maintenant", false, ImVec2(S(190), S(34)))) a.updater.checkAsync();
    endCard();
}

// ============================================================ main
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int) {
    bool afterUpdate = cmdLine && wcsstr(cmdLine, L"--updated");
    HANDLE single = CreateMutexW(nullptr, TRUE, L"VoxStudio.SingleInstance");
    for (int i = 0; afterUpdate && GetLastError() == ERROR_ALREADY_EXISTS && i < 60; ++i) { // l'ancienne version se ferme
        CloseHandle(single); Sleep(100);
        single = CreateMutexW(nullptr, TRUE, L"VoxStudio.SingleInstance");
    }
    Updater::cleanupOld();
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"VoxStudioWnd", nullptr);
        if (w) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, LoadIconW(hInst, MAKEINTRESOURCEW(1)), LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"VoxStudioWnd", nullptr};
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);
    gDpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Vox Studio", WS_OVERLAPPEDWINDOW, 80, 60, (int)S(1400), (int)S(900), nullptr, nullptr, hInst, nullptr);
    // Barre de titre sombre (Windows 10 20H1+ / 11)
    { BOOL dark = TRUE; typedef HRESULT(WINAPI * DwmSet)(HWND, DWORD, LPCVOID, DWORD);
      if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) { if (auto f = (DwmSet)GetProcAddress(dwm, "DwmSetWindowAttribute")) { f(hwnd, 20, &dark, sizeof dark); COLORREF c = RGB(11, 11, 12); f(hwnd, 35, &c, sizeof c); } } }
    if (!createDevice(hwnd)) { cleanupDevice(); UnregisterClassW(wc.lpszClassName, hInst); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    setupStyle();
    ImGui::GetStyle().ScaleAllSizes(gDpi);
    loadFonts(io);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDev, gCtx);

    App app;
    bool had = loadSettings(app.st, app.engine.params);
    if (!had || app.st.version < 2) { dsp::applyPreset(app.engine.params, dsp::Preset::Radio); app.st.preset = 1; app.st.version = 2; app.dirty = true; }
    app.refreshDevices();
    app.autoPick();
    app.mixer.refresh();
    app.outputs.refresh();
    if (app.st.autoStart && !app.st.inputId.empty() && !app.st.cableId.empty()) app.startEngine();
    app.applyMirror();

    std::vector<float> ana(16384);
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (IsIconic(hwnd)) { Sleep(50); continue; }
        if (gResizeW) { cleanupRT(); gSwap->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0); gResizeW = gResizeH = 0; createRT(); }

        double now = ImGui::GetTime();
        if (now - app.lastRefresh > 1.0 && !ImGui::IsAnyItemActive()) {
            app.mixer.refresh(); app.outputs.refresh(); app.lastRefresh = now;
            std::wstring def = defaultDeviceId(false);
            if (def != app.lastDefault) app.applyMirror();   // la sortie principale a changé
        }
        if (now - app.lastDevRefresh > 5.0 && !ImGui::IsAnyItemActive()) { app.refreshDevices(); app.lastDevRefresh = now; }
        if (app.wantRunning && app.engine.failed() && now - app.lastRetry > 3.0) { app.lastRetry = now; app.refreshDevices(); app.autoPick(); app.startEngine(); }
        if (app.dirty && now - app.lastSave > 1.0) { saveSettings(app.st, app.engine.params); app.dirty = false; app.lastSave = now; }

        if (now - app.lastUpdateCheck > 6 * 3600.0) { app.lastUpdateCheck = now; app.updater.checkAsync(); }
        if (app.updater.state() == Updater::Ready && !app.quitForUpdate) {
            app.quitForUpdate = true;
            saveSettings(app.st, app.engine.params);
            app.mirror.stop(); app.engine.stop();
            if (single) { CloseHandle(single); single = nullptr; }
            app.updater.relaunch();
            PostQuitMessage(0);
        }

        size_t n = app.engine.readAnalysis(ana.data(), ana.size());
        if (n) app.spec.feed(ana.data(), n);
        if (app.st.page == 0) app.spec.compute(io.DeltaTime);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        float sideW = S(250);
        drawSidebar(app, sideW);
        ImGui::SameLine(0, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(40), S(34)));
        ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        drawUpdateBanner(app);
        switch (app.st.page) {
        case 0: drawVoice(app); break;
        case 1: drawMixer(app); break;
        case 2: drawOutputs(app); break;
        default: drawSettings(app); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();

        ImGui::Render();
        const float clear[4] = {0, 0, 0, 1};
        gCtx->OMSetRenderTargets(1, &gRtv, nullptr);
        gCtx->ClearRenderTargetView(gRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gSwap->Present(1, 0);
    }

    saveSettings(app.st, app.engine.params);
    app.mirror.stop();
    app.engine.stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    if (single) CloseHandle(single);
    return 0;
}
