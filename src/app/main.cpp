// Drapeo — point d'entrée.
// Usage : drapeo [avatar.fbx|avatar.obj|patron] [--frames N] [--screenshot fichier.ppm]
//                [--size LxH] [--yaw degrés] [--zoom x] [--modele debardeur|jupe|robe]
#include <cctype>
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
        } else if (arg == "--accept-import") {
            options.acceptImport = true;
        } else if (arg == "--paused") {
            options.startPaused = true;
        } else if (arg == "--about") {
            options.showAbout = true;
        } else if (arg == "--yaw" && i + 1 < argc) {
            options.yawDeg = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--zoom" && i + 1 < argc) {
            options.zoom = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--modele" && i + 1 < argc) {
            options.model = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &options.width, &options.height);
        } else if (!arg.empty() && arg[0] != '-') {
            // drapeo avatar.fbx  ou  drapeo patron.pdf / patron.png
            const auto dot = arg.find_last_of('.');
            std::string ext = dot == std::string::npos ? "" : arg.substr(dot + 1);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == "fbx" || ext == "obj") options.avatarPath = arg;
            else options.patternPath = arg;
        } else {
            std::printf("Usage : %s [avatar.fbx|patron] [--frames N] [--screenshot fichier.ppm] [--size 1600x900]\n"
                        "         [--yaw degrés] [--zoom x] [--modele debardeur|jupe|robe]\n", argv[0]);
            return arg == "--help" ? 0 : 1;
        }
    }
    drapeo::app::App app;
    return app.run(options);
}
