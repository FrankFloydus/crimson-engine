#include "App.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <fstream>
#include <exception>

int main(int argc, char** argv) {
    try {
        return crimson::run(crimson::parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Renderer ERROR: %s\n", error.what());
        std::ofstream log("renderer.log", std::ios::app);
        log << "Renderer ERROR: " << error.what() << '\n';
        log.close();
        if (argc == 1) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "crimson-renderer error", error.what(), nullptr);
        return 1;
    }
}
