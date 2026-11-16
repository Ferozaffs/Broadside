#include "livery.h"
#include <algorithm>
#include <cstring>
#include <map>

bool Livery::AnyPaint() const {
    for (const Color& c : px)
        if (c.a) return true;
    return false;
}

static uint32_t Key(Color c) { return c.a ? (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | 0xFF000000u : 0u; }

std::vector<uint8_t> Livery::Encode() const {
    std::vector<uint8_t> out;
    out.push_back(1);  // version
    for (const Color& c : {hull, trim, sail}) {
        out.push_back(c.r); out.push_back(c.g); out.push_back(c.b); out.push_back(c.a ? 1 : 0);
    }
    out.push_back(logoWhere);
    if (!AnyPaint()) {  // nothing painted: no picture at all
        out.push_back(0);
        return out;
    }
    // palette: index 0 is clear
    std::map<uint32_t, int> idx;
    std::vector<uint32_t> pal = {0u};
    idx[0u] = 0;
    for (const Color& c : px) {
        uint32_t k = Key(c);
        if (idx.count(k)) continue;
        if (pal.size() >= 256) return {};  // (Simplify first)
        idx[k] = (int)pal.size();
        pal.push_back(k);
    }
    out.push_back((uint8_t)(pal.size() - 1));
    for (size_t i = 1; i < pal.size(); ++i) {
        out.push_back(pal[i] & 255); out.push_back((pal[i] >> 8) & 255); out.push_back((pal[i] >> 16) & 255);
    }
    // runs of one colour: (length 1..255, palette index)
    size_t i = 0, n = px.size();
    while (i < n) {
        int k = idx[Key(px[i])];
        size_t j = i + 1;
        while (j < n && j - i < 255 && idx[Key(px[j])] == k) ++j;
        out.push_back((uint8_t)(j - i));
        out.push_back((uint8_t)k);
        i = j;
    }
    return out;
}

bool Livery::Decode(const uint8_t* d, size_t n) {
    size_t p = 0;
    auto get = [&](uint8_t& v) { if (p >= n) return false; v = d[p++]; return true; };
    uint8_t ver = 0;
    if (!get(ver) || ver != 1) return false;
    Livery l;
    for (Color* c : {&l.hull, &l.trim, &l.sail}) {
        uint8_t on = 0;
        if (!get(c->r) || !get(c->g) || !get(c->b) || !get(on)) return false;
        c->a = on ? 255 : 0;
    }
    if (!get(l.logoWhere) || l.logoWhere >= LOGO_COUNT) return false;
    uint8_t np = 0;
    if (!get(np)) return false;
    if (np > 0) {
        std::vector<Color> pal(np + 1, Color{0, 0, 0, 0});
        for (int i = 1; i <= np; ++i) {
            Color& c = pal[i];
            if (!get(c.r) || !get(c.g) || !get(c.b)) return false;
            c.a = 255;
        }
        size_t at = 0, total = l.px.size();
        while (at < total) {
            uint8_t len = 0, k = 0;
            if (!get(len) || !get(k) || len == 0 || k > np || at + len > total) return false;
            for (int i = 0; i < len; ++i) l.px[at++] = pal[k];
        }
    }
    *this = std::move(l);
    return true;
}

void Livery::Simplify() {
    // fewer colours first; a busy picture (a photo) gets coarser pixels too: blocks of 2 x 2, then 4 x 4
    const std::vector<Color> orig = px;
    for (int block : {1, 2, 4})
    for (int bits = 8; bits >= 2; --bits) {
        if (block > 1 && bits > 5) continue;
        px = orig;
        if (block > 1)
            for (int by = 0; by < kSailH; by += block)
                for (int bx = 0; bx < kSailW; bx += block) {
                    int r = 0, g = 0, b = 0, n = 0, a = 0, cells = 0;
                    for (int y = by; y < std::min(by + block, kSailH); ++y)
                        for (int x = bx; x < std::min(bx + block, kSailW); ++x, ++cells) {
                            const Color& c = orig[y * kSailW + x];
                            if (!c.a) continue;
                            r += c.r; g += c.g; b += c.b; ++n;
                        }
                    a = n * 2 >= cells;
                    Color c = a ? Color{(unsigned char)(r / n), (unsigned char)(g / n), (unsigned char)(b / n), 255} : Color{0, 0, 0, 0};
                    for (int y = by; y < std::min(by + block, kSailH); ++y)
                        for (int x = bx; x < std::min(bx + block, kSailW); ++x) px[y * kSailW + x] = c;
                }
        // count the colours (at `bits` per channel) and keep the commonest 255
        int shift = 8 - bits;
        auto q = [&](Color c) {
            if (!c.a) return Color{0, 0, 0, 0};
            auto f = [&](unsigned char v) { int b = v >> shift; return (unsigned char)(bits == 8 ? v : (b << shift) + (1 << shift) / 2); };
            return Color{f(c.r), f(c.g), f(c.b), 255};
        };
        std::map<uint32_t, int> count;
        for (const Color& c : px) ++count[Key(q(c))];
        count.erase(0u);
        std::vector<std::pair<int, uint32_t>> order;
        for (auto& [k, c] : count) order.push_back({c, k});
        std::sort(order.rbegin(), order.rend());
        if (order.size() > 255) order.resize(255);
        std::vector<Color> pal;
        for (auto& o : order) pal.push_back({(unsigned char)(o.second & 255), (unsigned char)((o.second >> 8) & 255), (unsigned char)((o.second >> 16) & 255), 255});
        std::map<uint32_t, Color> nearest;
        std::vector<Color> out(px.size());
        for (size_t i = 0; i < px.size(); ++i) {
            Color c = q(px[i]);
            if (!c.a) { out[i] = c; continue; }
            uint32_t k = Key(c);
            auto it = nearest.find(k);
            if (it == nearest.end()) {
                int best = 0;
                long bd = 1L << 30;
                for (int j = 0; j < (int)pal.size(); ++j) {
                    long dr = c.r - pal[j].r, dg = c.g - pal[j].g, db = c.b - pal[j].b;
                    long dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                    if (dd < bd) { bd = dd; best = j; }
                }
                it = nearest.emplace(k, pal[best]).first;
            }
            out[i] = it->second;
        }
        Livery t = *this;
        t.px = out;
        std::vector<uint8_t> e = t.Encode();
        if (!e.empty() && (int)e.size() <= kLiveryMaxBytes) {
            px = std::move(out);
            return;
        }
    }
    px = orig;
    // (a picture that's noise all over: give up on it)
    std::fill(px.begin(), px.end(), Color{0, 0, 0, 0});
}

bool Livery::Save(const std::string& path) const {
    std::vector<uint8_t> e = Encode();
    if (e.empty()) return false;
    return SaveFileData(path.c_str(), e.data(), (int)e.size());
}

bool Livery::Load(const std::string& path) {
    if (!FileExists(path.c_str())) return false;
    int n = 0;
    unsigned char* d = LoadFileData(path.c_str(), &n);
    bool ok = d && Decode(d, (size_t)n);
    UnloadFileData(d);
    return ok;
}

void LiveryGpu::Set(const Livery& l) {
    on = true;
    hull = l.hull;
    trim = l.trim;
    sail = l.sail;
    logoWhere = l.HasLogo() ? l.logoWhere : LOGO_OFF;
    if (logoWhere == LOGO_OFF) return;
    if (logo.id == 0) {
        Image img = GenImageColor(kSailW, kSailH, BLANK);
        logo = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(logo, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(logo, TEXTURE_WRAP_CLAMP);
    }
    UpdateTexture(logo, l.px.data());
}

void LiveryGpu::Clear() {
    if (logo.id) UnloadTexture(logo);
    *this = LiveryGpu{};
}

std::string LiveryPath() { return std::string(GetApplicationDirectory()) + "livery.dat"; }
