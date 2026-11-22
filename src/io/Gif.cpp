#include "io/Gif.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drapeo::io {

namespace {
void put16(std::vector<std::uint8_t>& o, int v)
{
    o.push_back(static_cast<std::uint8_t>(v & 255));
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 255));
}

int quantize(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return (r * 5 / 255) * 36 + (g * 5 / 255) * 6 + (b * 5 / 255);
}

void lzw(const std::vector<std::uint8_t>& idx, std::vector<std::uint8_t>& block)
{
    constexpr int kMin = 8;
    const int clear = 1 << kMin;
    const int eoi = clear + 1;
    int bits = kMin + 1;
    int next = eoi + 1;
    std::uint32_t acc = 0;
    int nbits = 0;
    auto emit = [&](int code) {
        acc |= static_cast<std::uint32_t>(code) << nbits;
        nbits += bits;
        while (nbits >= 8) {
            block.push_back(static_cast<std::uint8_t>(acc & 255));
            acc >>= 8;
            nbits -= 8;
        }
    };
    std::unordered_map<std::uint32_t, int> dict;
    dict.reserve(4096);
    emit(clear);
    if (idx.empty()) {
        emit(eoi);
        if (nbits > 0) block.push_back(static_cast<std::uint8_t>(acc & 255));
        return;
    }
    int prefix = idx[0];
    for (std::size_t i = 1; i < idx.size(); ++i) {
        const int k = idx[i];
        const std::uint32_t key = (static_cast<std::uint32_t>(prefix) << 8) | static_cast<std::uint32_t>(k);
        const auto it = dict.find(key);
        if (it != dict.end()) {
            prefix = it->second;
            continue;
        }
        emit(prefix);
        if (next < 4096) {
            dict.emplace(key, next++);
            if (next >= (1 << bits) && bits < 12) ++bits;
        } else {
            emit(clear);
            dict.clear();
            bits = kMin + 1;
            next = eoi + 1;
        }
        prefix = k;
    }
    emit(prefix);
    emit(eoi);
    if (nbits > 0) block.push_back(static_cast<std::uint8_t>(acc & 255));
}
} // namespace

bool encodeGif(const std::vector<std::vector<std::uint8_t>>& frames, int width, int height, int delayCs, std::vector<std::uint8_t>& gif)
{
    if (frames.empty() || width <= 0 || height <= 0) return false;
    const std::size_t need = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3;
    for (const auto& f : frames)
        if (f.size() < need) return false;

    gif.clear();
    gif.insert(gif.end(), {'G', 'I', 'F', '8', '9', 'a'});
    put16(gif, width);
    put16(gif, height);
    gif.push_back(0xF7);   // palette globale, 256 couleurs
    gif.push_back(0);
    gif.push_back(0);
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b) {
                gif.push_back(static_cast<std::uint8_t>(r * 255 / 5));
                gif.push_back(static_cast<std::uint8_t>(g * 255 / 5));
                gif.push_back(static_cast<std::uint8_t>(b * 255 / 5));
            }
    for (int i = 216; i < 256; ++i) {
        gif.push_back(0);
        gif.push_back(0);
        gif.push_back(0);
    }
    // Boucle infinie (Netscape).
    gif.insert(gif.end(), {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00});

    std::vector<std::uint8_t> index(static_cast<std::size_t>(width) * height);
    for (const auto& frame : frames) {
        for (int i = 0; i < width * height; ++i)
            index[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(quantize(frame[static_cast<std::size_t>(i) * 3], frame[static_cast<std::size_t>(i) * 3 + 1], frame[static_cast<std::size_t>(i) * 3 + 2]));
        gif.insert(gif.end(), {0x21, 0xF9, 0x04, 0x08, static_cast<std::uint8_t>(delayCs & 255), static_cast<std::uint8_t>((delayCs >> 8) & 255), 0x00, 0x00});
        gif.push_back(0x2C);
        put16(gif, 0);
        put16(gif, 0);
        put16(gif, width);
        put16(gif, height);
        gif.push_back(0);
        gif.push_back(8);   // taille de code minimale
        std::vector<std::uint8_t> packed;
        lzw(index, packed);
        std::size_t at = 0;
        while (at < packed.size()) {
            const int n = static_cast<int>(std::min<std::size_t>(255, packed.size() - at));
            gif.push_back(static_cast<std::uint8_t>(n));
            gif.insert(gif.end(), packed.begin() + static_cast<std::ptrdiff_t>(at), packed.begin() + static_cast<std::ptrdiff_t>(at + n));
            at += static_cast<std::size_t>(n);
        }
        gif.push_back(0);
    }
    gif.push_back(0x3B);
    return true;
}

} // namespace drapeo::io
