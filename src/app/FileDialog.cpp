// Fichier séparé : <windows.h> définit des macros (RGB, near, far...) qui
// entreraient en conflit avec le reste du code.
#include "app/FileDialog.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace drapeo::app {

#if defined(_WIN32)
namespace {
std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.pop_back();
    return w;
}

std::string narrow(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

/// "Desc|*.a;*.b" -> L"Desc\0*.a;*.b\0Tous les fichiers\0*.*\0\0"
std::wstring makeFilter(const std::string& filter)
{
    std::wstring f = widen(filter + "|Tous les fichiers (*.*)|*.*");
    for (auto& c : f)
        if (c == L'|') c = L'\0';
    f.push_back(L'\0');
    f.push_back(L'\0');
    return f;
}

std::string runDialog(const std::string& title, const std::string& filter, const std::string& ext, bool save)
{
    wchar_t file[4096] = L"";
    const std::wstring wtitle = widen(title), wfilter = makeFilter(filter), wext = widen(ext);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrDefExt = wext.empty() ? nullptr : wext.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(file) : std::string();
}
} // namespace

bool hasNativeFileDialog() { return true; }
std::string openFileDialog(const std::string& title, const std::string& filter) { return runDialog(title, filter, {}, false); }
std::string saveFileDialog(const std::string& title, const std::string& filter, const std::string& ext)
{
    return runDialog(title, filter, ext, true);
}
#else
bool hasNativeFileDialog() { return false; }
std::string openFileDialog(const std::string&, const std::string&) { return {}; }
std::string saveFileDialog(const std::string&, const std::string&, const std::string&) { return {}; }
#endif

} // namespace drapeo::app
