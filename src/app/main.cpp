// Drapeo — point d'entrée.
// Usage : drapeo [avatar.fbx|avatar.obj] [--frames N] [--screenshot fichier.ppm] [--size LxH]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.h"

int main(int argc, char** argv)
{
    drapeo::app::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            options.screenshotPath = argv[++i];
        } else if (arg == "--about") {
            options.showAbout = true;
        } else if (arg == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &options.width, &options.height);
        } else if (!arg.empty() && arg[0] != '-') {
            options.avatarPath = arg;   // drapeo avatar.fbx
        } else {
            std::printf("Usage : %s [avatar.fbx] [--frames N] [--screenshot fichier.ppm] [--size 1600x900]\n", argv[0]);
            return arg == "--help" ? 0 : 1;
        }
    }
    drapeo::app::App app;
    return app.run(options);
}
