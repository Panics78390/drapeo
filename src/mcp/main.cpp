// Drapeo — serveur MCP (outils pour un assistant comme Claude Desktop).
// Usage : drapeo-mcp [--projet chemin.drapeo]
// Messages JSON-RPC sur l'entrée / la sortie standard, un par ligne. Les messages de suivi vont sur stderr.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "mcp/McpServer.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {

std::string exeDirectory(const char* argv0)
{
#ifdef _WIN32
    (void)argv0;
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    std::filesystem::path p(std::wstring(buf, n));
#else
    std::error_code ec;
    std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) p = std::filesystem::absolute(argv0);
#endif
    const std::u8string s = p.parent_path().u8string();
    return std::string(s.begin(), s.end());
}

#ifdef _WIN32
std::string utf8(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<std::size_t>(n) : 1u, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(s.size() - 1);
    return s;
}
#endif

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    // Arguments en UTF-8 (chemins avec accents).
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> args;
    for (int i = 0; i < wargc; ++i) args.push_back(utf8(wargv[i]));
    LocalFree(wargv);
#else
    std::vector<std::string> args(argv, argv + argc);
#endif
    std::string project;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if ((args[i] == "--projet" || args[i] == "--project") && i + 1 < args.size()) {
            project = args[++i];
        } else if (args[i] == "--help" || args[i] == "-h") {
            std::fprintf(stderr, "Usage : drapeo-mcp [--projet chemin.drapeo]\n"
                                 "Serveur MCP de Drapeo (à déclarer dans Claude Desktop, pas à lancer à la main).\n");
            return 0;
        }
    }
    if (project.empty()) project = drapeo::mcp::defaultProjectPath();

    const std::string dir = exeDirectory(argv[0]);
#ifdef _WIN32
    const std::string exe = dir + "\\drapeo.exe";
#else
    const std::string exe = dir + "/drapeo";
#endif
    drapeo::mcp::Server server(project, exe);
    std::fprintf(stderr, "drapeo-mcp : projet %s\n", project.c_str());

    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        const std::string reply = server.handleLine(line);
        if (reply.empty()) continue;
        std::fwrite(reply.data(), 1, reply.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    return 0;
}
