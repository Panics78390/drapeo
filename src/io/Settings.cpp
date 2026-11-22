#include "io/Settings.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace drapeo::io {
namespace fs = std::filesystem;

namespace {

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string toUtf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return {u.begin(), u.end()};
}

fs::path settingsFile()
{
#ifdef _WIN32
    const wchar_t* appData = _wgetenv(L"APPDATA");
    if (appData == nullptr || *appData == 0) return {};
    return fs::path(appData) / L"Drapeo" / L"reglages.txt";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != 0)
        return fs::path(xdg) / "drapeo" / "reglages.txt";
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != 0)
        return fs::path(home) / ".config" / "drapeo" / "reglages.txt";
    return {};
#endif
}

bool isFile(const fs::path& p)
{
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}

} // namespace

std::string executableDirectory()
{
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return toUtf8(fs::path(buf).parent_path());
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : toUtf8(exe.parent_path());
#endif
}

struct Store {
    bool present = false;
    std::string avatar;
    bool tutorial = false;
    float heightM = -1.0f, chest = -1.0f, waist = -1.0f, hips = -1.0f;
};

Store readStore()
{
    Store s;
    const fs::path file = settingsFile();
    if (!isFile(file)) return s;
    s.present = true;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto val = [&](const char* key) -> const char* {
            const std::size_t n = std::char_traits<char>::length(key);
            return line.rfind(key, 0) == 0 ? line.c_str() + n : nullptr;
        };
        if (const char* v = val("avatar ")) s.avatar = v;
        else if (const char* v = val("tutoriel ")) s.tutorial = v[0] == '1';
        else if (const char* v = val("stature ")) s.heightM = std::strtof(v, nullptr);
        else if (const char* v = val("poitrine ")) s.chest = std::strtof(v, nullptr);
        else if (const char* v = val("taille ")) s.waist = std::strtof(v, nullptr);
        else if (const char* v = val("hanches ")) s.hips = std::strtof(v, nullptr);
    }
    return s;
}

void writeStore(const Store& s)
{
    const fs::path file = settingsFile();
    if (file.empty()) return;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "drapeo-reglages 2\n";
    if (!s.avatar.empty()) out << "avatar " << s.avatar << '\n';
    out << "tutoriel " << (s.tutorial ? 1 : 0) << '\n';
    if (s.heightM > 0.0f) out << "stature " << s.heightM << '\n';
    if (s.chest > 0.0f) out << "poitrine " << s.chest << '\n';
    if (s.waist > 0.0f) out << "taille " << s.waist << '\n';
    if (s.hips > 0.0f) out << "hanches " << s.hips << '\n';
}

bool rememberedAvatar(std::string& path)
{
    path.clear();
    const Store s = readStore();
    if (!s.present) return false;
    path = s.avatar;
    if (!path.empty() && !isFile(fromUtf8(path))) path.clear();
    return true;
}

void rememberAvatar(const std::string& utf8Path)
{
    Store s = readStore();
    s.avatar = utf8Path;
    writeStore(s);
}

bool tutorialDone()
{
    return readStore().tutorial;
}

void setTutorialDone(bool done)
{
    Store s = readStore();
    s.tutorial = done;
    writeStore(s);
}

BodyPrefs loadBodyPrefs()
{
    const Store s = readStore();
    return {s.heightM, s.chest, s.waist, s.hips};
}

void saveBodyPrefs(const BodyPrefs& prefs)
{
    Store s = readStore();
    s.heightM = prefs.heightM;
    s.chest = prefs.chestCm;
    s.waist = prefs.waistCm;
    s.hips = prefs.hipsCm;
    writeStore(s);
}

fs::path recoveryPath()
{
    const fs::path file = settingsFile();
    if (file.empty()) return {};
    return file.parent_path() / "reprise.drapeo";
}

std::string recoveryFilePath()
{
    const fs::path p = recoveryPath();
    return p.empty() ? std::string() : toUtf8(p);
}

bool hasRecovery()
{
    return isFile(recoveryPath());
}

bool writeRecovery(const std::string& projectText)
{
    const fs::path p = recoveryPath();
    if (p.empty()) return false;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << projectText;
    return static_cast<bool>(out);
}

void clearRecovery()
{
    std::error_code ec;
    fs::remove(recoveryPath(), ec);
}

std::string avatarNextToExecutable()
{
    const std::string dir = executableDirectory();
    if (dir.empty()) return {};
    std::error_code ec;
    std::vector<fs::path> fbx, obj;
    for (const auto& entry : fs::directory_iterator(fromUtf8(dir), ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = toUtf8(entry.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".fbx") fbx.push_back(entry.path());
        else if (ext == ".obj") obj.push_back(entry.path());
    }
    std::sort(fbx.begin(), fbx.end());
    std::sort(obj.begin(), obj.end());
    if (!fbx.empty()) return toUtf8(fbx.front());
    if (!obj.empty()) return toUtf8(obj.front());
    return {};
}

} // namespace drapeo::io
