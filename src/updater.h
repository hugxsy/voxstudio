// Vox Studio - mises à jour automatiques via le dépôt GitHub (manifeste JSON + exe)
#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

#define VOX_VERSION "2.1.0"
// Manifeste publié dans le dépôt GitHub : release/latest.json
#define VOX_MANIFEST_URL "https://raw.githubusercontent.com/hugxsy/voxstudio/main/release/latest.json"

class Updater {
public:
    enum State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };

    void checkAsync();                 // vérifie la dernière version publiée
    void installAsync();               // télécharge puis remplace l'exe
    State state() const { return state_.load(); }
    std::string latest() { std::lock_guard<std::mutex> l(mx_); return latest_; }
    std::string notes() { std::lock_guard<std::mutex> l(mx_); return notes_; }
    std::string error() { std::lock_guard<std::mutex> l(mx_); return error_; }
    float progress() const { return progress_.load(); }
    bool relaunch();                   // lance la nouvelle version (l'appelant doit quitter ensuite)
    static void cleanupOld();          // supprime VoxStudio.old.exe au démarrage
    static bool versionNewer(const std::string& a, const std::string& b); // a > b ?
    ~Updater() { if (th_.joinable()) th_.join(); }

private:
    void run(bool install);
    std::atomic<State> state_{Idle};
    std::atomic<float> progress_{0};
    std::mutex mx_;
    std::string latest_, notes_, error_, url_;
    std::thread th_;
};
