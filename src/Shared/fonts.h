#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <string>

inline bool LoadFont(sf::Font& font) {
    std::vector<std::string> fontPaths;

#ifdef _WIN32
    fontPaths = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };
#elif __APPLE__
    fontPaths = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Helvetica.ttf",
        "/Library/Fonts/Arial.ttf"
    };
#else // Linux
    fontPaths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"
    };
#endif

    for (const auto& path : fontPaths) {
        if (font.openFromFile(path))
            return true;
    }

    std::vector<std::string> fallbackNames = {"arial.ttf", "sans.ttf", "DejaVuSans.ttf"};
    for (const auto& name : fallbackNames) {
        if (font.openFromFile(name))
            return true;
    }

    return false;
}
