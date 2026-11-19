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

bool rememberedAvatar(std::string& path)
{
    path.clear();
    const fs::path file = settingsFile();
    if (!isFile(file)) return false;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("avatar ", 0) == 0) {
            path = line.substr(7);
            if (!isFile(fromUtf8(path))) path.clear();
        }
    }
    return true;
}

void rememberAvatar(const std::string& utf8Path)
{
    const fs::path file = settingsFile();
    if (file.empty()) return;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "drapeo-reglages 1\n";
    if (!utf8Path.empty()) out << "avatar " << utf8Path << "\n";
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
