// Mods: see mods.h
#include "mods.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace mods {
namespace {

struct State {
    std::string app;                 // the game's folder (with its slash)
    std::string root;                // where the packs are: <app>mods/ (or BS_MODDIR)
    std::vector<std::string> installed;
    std::string active, dir;         // the pack in use, and its folder (with a slash)
    bool japanese = false;
    uint32_t gen = 1;
    std::unordered_map<std::string, std::string> text;
    std::unordered_map<std::string, int> sex;
    std::unordered_map<std::string, Babble> babble;
    std::unordered_map<std::string, int> hasFace, hasVoices, hasVoicesJa;  // (cached: 0 unknown... see Has)
    int jaAvailable = -1;
} S;

bool Exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::u8path(p), ec);
}

// a cached "does the pack have this": a face's portrait, a face's voices folder
bool Has(std::unordered_map<std::string, int>& cache, const std::string& id, const std::string& path) {
    auto it = cache.find(id);
    if (it == cache.end()) it = cache.emplace(id, Exists(path) ? 1 : 0).first;
    return it->second == 1;
}

std::string Unescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            o += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else o += s[i];
    }
    return o;
}

std::vector<std::string> Lines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::string l;
    bool first = true;
    while (std::getline(f, l)) {
        if (first && l.size() >= 3 && (unsigned char)l[0] == 0xEF && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF) l.erase(0, 3);
        first = false;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        out.push_back(l);
    }
    return out;
}

void Load() {  // the active pack's words and people
    S.text.clear();
    S.sex.clear();
    S.babble.clear();
    S.hasFace.clear();
    S.hasVoices.clear();
    S.hasVoicesJa.clear();
    S.jaAvailable = -1;
    ++S.gen;
    if (S.dir.empty()) return;
    for (const std::string& l : Lines(S.dir + "text.tsv")) {
        const size_t t = l.find('\t');
        if (t == std::string::npos) continue;
        S.text[Unescape(l.substr(0, t))] = Unescape(l.substr(t + 1));
    }
    for (const std::string& l : Lines(S.dir + "people.tsv")) {
        std::istringstream in(l);
        std::string id, sx;
        if (!std::getline(in, id, '\t') || !std::getline(in, sx, '\t')) {
            const size_t t = l.find('\t');
            if (t == std::string::npos) continue;
            id = l.substr(0, t);
            sx = l.substr(t + 1);
        }
        if (!sx.empty() && (sx[0] == 'f' || sx[0] == 'F')) S.sex[id] = 1;
        else if (!sx.empty() && (sx[0] == 'm' || sx[0] == 'M')) S.sex[id] = 2;
        Babble b{};
        if (in >> b.pitch >> b.formant >> b.breath >> b.rasp >> b.manner) S.babble[id] = b;
    }
}

void Save() {
    if (S.app.empty()) return;
    std::ofstream f(fs::u8path(S.app + "mods.cfg"), std::ios::binary);
    f << "mod " << (S.active.empty() ? "-" : S.active) << "\nvoices " << (S.japanese ? "ja" : "en") << "\n";
}

void Scan() {
    S.installed.clear();
    std::error_code ec;
    if (S.root.empty() || !fs::is_directory(fs::u8path(S.root), ec)) return;
    for (const auto& e : fs::directory_iterator(fs::u8path(S.root), ec)) {
        if (!e.is_directory(ec)) continue;
        const std::string name = e.path().filename().u8string();
        if (name.empty() || name[0] == '.' || name[0] == '_') continue;  // (hidden, and the tests' own)
        S.installed.push_back(name);
    }
    std::sort(S.installed.begin(), S.installed.end());
}

}  // namespace

void Init(const std::string& appDir) {
    S = State{};
    S.app = appDir;
    if (!S.app.empty() && S.app.back() != '/' && S.app.back() != '\\') S.app += '/';
    S.root = getenv("BS_MODDIR") ? std::string(getenv("BS_MODDIR")) : S.app + "mods";
    if (!S.root.empty() && S.root.back() != '/' && S.root.back() != '\\') S.root += '/';
    Scan();
    std::string want;
    for (const std::string& l : Lines(S.app + "mods.cfg")) {
        if (l.rfind("mod ", 0) == 0) want = l.substr(4);
        else if (l.rfind("voices ", 0) == 0) S.japanese = l.substr(7) == "ja";
    }
    if (getenv("BS_MOD")) want = getenv("BS_MOD");
    if (!want.empty() && want != "-" && std::find(S.installed.begin(), S.installed.end(), want) != S.installed.end()) {
        S.active = want;
        S.dir = S.root + want + "/";
    }
    Load();
}

const std::vector<std::string>& Installed() { return S.installed; }
const std::string& Active() { return S.active; }

bool SetActive(const std::string& name) {
    if (!name.empty() && std::find(S.installed.begin(), S.installed.end(), name) == S.installed.end()) return false;
    if (name == S.active) return true;
    S.active = name;
    S.dir = name.empty() ? "" : S.root + name + "/";
    Load();
    Save();
    return true;
}

bool LoadFrom(const std::string& dir) {
    if (!fs::is_directory(fs::u8path(dir))) return false;
    S.active = fs::u8path(dir).filename().u8string();
    S.dir = dir;
    if (S.dir.back() != '/' && S.dir.back() != '\\') S.dir += '/';
    Load();
    return true;
}

void Reset() {
    S.active.clear();
    S.dir.clear();
    S.japanese = false;
    Load();
}

bool Japanese() { return S.japanese; }
void SetJapanese(bool on) {
    if (on == S.japanese) return;
    S.japanese = on;
    Save();
}

bool JapaneseAvailable() {
    if (S.jaAvailable < 0) {
        std::error_code ec;
        auto any = [&](const std::string& d) {
            if (!fs::is_directory(fs::u8path(d), ec)) return false;
            for (const auto& e : fs::directory_iterator(fs::u8path(d), ec))
                if (e.is_directory(ec)) return true;
            return false;
        };
        S.jaAvailable = (any(S.app + "data/voices_ja") || (!S.dir.empty() && any(S.dir + "voices_ja"))) ? 1 : 0;
    }
    return S.jaAvailable == 1;
}

uint32_t Generation() { return S.gen; }

static bool PackFace(const std::string& id) {
    return !S.dir.empty() && Has(S.hasFace, id, S.dir + "portraits/" + id + ".png");
}

std::string PortraitPath(const std::string& id) {
    if (PackFace(id)) return S.dir + "portraits/" + id + ".png";
    return S.app + "data/portraits/" + id + ".png";
}

std::string IdlePath(const std::string& id) {
    if (PackFace(id)) {  // (the game's idle is of the face the pack replaced: its own, or none)
        const std::string p = S.dir + "idles/" + id + ".png";
        return Exists(p) ? p : std::string();
    }
    const std::string p = S.app + "data/idles/" + id + ".png";
    return Exists(p) ? p : std::string();
}

std::string VoicePath(const std::string& id, uint32_t hash, int gameSex) {
    char file[16];
    snprintf(file, sizeof file, "%08x.ogg", hash);
    const bool packVoices = !S.dir.empty() && Has(S.hasVoices, id, S.dir + "voices/" + id);
    const bool packVoicesJa = !S.dir.empty() && Has(S.hasVoicesJa, id, S.dir + "voices_ja/" + id);
    // a face the pack voices (in either tongue), or turns from a man into a woman (or back), isn't the game's any
    // more: not its recordings (a new face of the same sex keeps the game's voice)
    const int sx = Sex(id);
    const bool own = packVoices || packVoicesJa || (PackFace(id) && sx && gameSex && sx != gameSex);
    std::string p;
    if (S.japanese) {
        if (packVoicesJa && Exists(p = S.dir + "voices_ja/" + id + "/" + file)) return p;
        if (!own && Exists(p = S.app + "data/voices_ja/" + id + "/" + file)) return p;
    }
    if (packVoices && Exists(p = S.dir + "voices/" + id + "/" + file)) return p;
    if (!own && Exists(p = S.app + "data/voices/" + id + "/" + file)) return p;
    return "";
}

const char* T(const char* s) {
    if (!s || S.text.empty()) return s;
    auto it = S.text.find(s);
    return it == S.text.end() ? s : it->second.c_str();
}

std::string T(const std::string& s) {
    if (S.text.empty()) return s;
    auto it = S.text.find(s);
    return it == S.text.end() ? s : it->second;
}

int Sex(const std::string& id) {
    auto it = S.sex.find(id);
    return it == S.sex.end() ? 0 : it->second;
}

bool BabbleOf(const std::string& id, Babble& out) {
    auto it = S.babble.find(id);
    if (it == S.babble.end()) return false;
    out = it->second;
    return true;
}

}  // namespace mods
