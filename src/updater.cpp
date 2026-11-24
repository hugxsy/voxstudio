// Vox Studio - mises à jour (WinHTTP)
#include "updater.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <cstdio>

static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

// GET HTTPS, suit les redirections. progress : 0..1 si la taille est connue.
static bool httpGet(const std::string& url, std::vector<char>& out, std::string& err, std::atomic<float>* progress = nullptr) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    wchar_t extra[1024]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { err = "adresse invalide"; return false; }
    std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);

    HINTERNET s = WinHttpOpen(L"VoxStudio/" VOX_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"VoxStudio/" VOX_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { err = "réseau indisponible"; return false; }
    HINTERNET c = WinHttpConnect(s, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET r = nullptr;
    if (c) r = WinHttpOpenRequest(c, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    bool ok = false;
    if (r) {
        const wchar_t* hdr = L"Cache-Control: no-cache\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof code;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            DWORD total = 0; sz = sizeof total;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &total, &sz, WINHTTP_NO_HEADER_INDEX);
            if (code == 200) {
                out.clear();
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(r, &avail) && avail) {
                    size_t old = out.size(); out.resize(old + avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(r, out.data() + old, avail, &got)) break;
                    out.resize(old + got);
                    if (progress && total) *progress = (float)out.size() / total;
                }
                ok = true;
            } else if (code == 404) err = "aucune version publiée";
            else { char b[48]; snprintf(b, sizeof b, "erreur serveur %lu", (unsigned long)code); err = b; }
        } else err = "pas de connexion Internet";
        WinHttpCloseHandle(r);
    } else if (err.empty()) err = "connexion impossible";
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

// Extraction JSON minimale : valeur texte qui suit "key": à partir de pos
static std::string jsonString(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    std::string k = "\"" + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return {};
    p = j.find(':', p + k.size()); if (p == std::string::npos) return {};
    p = j.find('"', p); if (p == std::string::npos) return {};
    std::string v;
    for (size_t i = p + 1; i < j.size(); ++i) {
        char ch = j[i];
        if (ch == '\\' && i + 1 < j.size()) {
            char n = j[++i];
            if (n == 'n') v += '\n'; else if (n == 'r') {} else if (n == 't') v += ' ';
            else if (n == 'u' && i + 4 < j.size()) { // \uXXXX -> UTF-8
                unsigned cp = (unsigned)strtoul(j.substr(i + 1, 4).c_str(), nullptr, 16); i += 4;
                if (cp < 0x80) v += (char)cp;
                else if (cp < 0x800) { v += (char)(0xC0 | (cp >> 6)); v += (char)(0x80 | (cp & 0x3F)); }
                else { v += (char)(0xE0 | (cp >> 12)); v += (char)(0x80 | ((cp >> 6) & 0x3F)); v += (char)(0x80 | (cp & 0x3F)); }
            } else v += n;
        } else if (ch == '"') { if (at) *at = i; return v; }
        else v += ch;
    }
    return v;
}

bool Updater::versionNewer(const std::string& a, const std::string& b) {
    int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
    sscanf(a.c_str() + (a.size() && (a[0] == 'v' || a[0] == 'V')), "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf(b.c_str() + (b.size() && (b[0] == 'v' || b[0] == 'V')), "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; ++i) if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

static std::wstring exePath() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH); return b;
}
static std::wstring sibling(const wchar_t* name) {
    std::wstring p = exePath(); size_t s = p.find_last_of(L"\\/");
    return p.substr(0, s + 1) + name;
}

void Updater::cleanupOld() {
    std::wstring old = sibling(L"VoxStudio.old.exe");
    for (int i = 0; i < 20 && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) {
        if (DeleteFileW(old.c_str())) break;
        Sleep(250);
    }
    DeleteFileW(sibling(L"VoxStudio.new.exe").c_str());
}

void Updater::checkAsync() {
    State s = state_;
    if (s == Checking || s == Downloading || s == Ready) return;
    if (th_.joinable()) th_.join();
    state_ = Checking;
    th_ = std::thread(&Updater::run, this, false);
}
void Updater::installAsync() {
    if (state_ != Available) return;
    if (th_.joinable()) th_.join();
    state_ = Downloading; progress_ = 0;
    th_ = std::thread(&Updater::run, this, true);
}

void Updater::run(bool install) {
    std::string err;
    if (!install) {
        std::vector<char> body;
        if (!httpGet(VOX_MANIFEST_URL, body, err)) { std::lock_guard<std::mutex> l(mx_); error_ = err; state_ = Failed; return; }
        std::string j(body.begin(), body.end());
        std::string tag = jsonString(j, "version"), notes = jsonString(j, "notes"), url = jsonString(j, "url");
        std::lock_guard<std::mutex> l(mx_);
        latest_ = tag; notes_ = notes; url_ = url;
        if (tag.empty() || url.empty()) { error_ = "manifeste de version incomplet"; state_ = Failed; }
        else state_ = versionNewer(tag, VOX_VERSION) ? Available : UpToDate;
        return;
    }
    std::string url; { std::lock_guard<std::mutex> l(mx_); url = url_; }
    std::vector<char> bin;
    if (!httpGet(url, bin, err, &progress_)) { std::lock_guard<std::mutex> l(mx_); error_ = err; state_ = Failed; return; }
    if (bin.size() < 200000 || bin[0] != 'M' || bin[1] != 'Z') { std::lock_guard<std::mutex> l(mx_); error_ = "fichier téléchargé invalide"; state_ = Failed; return; }
    std::wstring cur = exePath(), nw = sibling(L"VoxStudio.new.exe"), old = sibling(L"VoxStudio.old.exe");
    FILE* f = _wfopen(nw.c_str(), L"wb");
    if (!f || fwrite(bin.data(), 1, bin.size(), f) != bin.size()) { if (f) fclose(f); std::lock_guard<std::mutex> l(mx_); error_ = "écriture impossible dans le dossier de Vox Studio"; state_ = Failed; return; }
    fclose(f);
    // Windows autorise à renommer un exe en cours d'exécution : on met l'ancien de côté
    DeleteFileW(old.c_str());
    if (!MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) || !MoveFileExW(nw.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), cur.c_str(), 0); // retour arrière
        std::lock_guard<std::mutex> l(mx_); error_ = "remplacement impossible"; state_ = Failed; return;
    }
    progress_ = 1; state_ = Ready;
}

bool Updater::relaunch() {
    std::wstring cmd = L"\"" + exePath() + L"\" --updated";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}
