#pragma once
#pragma execution_character_set("utf-8")

#include <string>

namespace LinguaAlpaca {

class WinMediaOcrHelper {
public:
    static bool TryExtract(int startX, int startY, int endX, int endY, std::string& outText, int& outAnchorX, int& outAnchorY);
};

} // namespace LinguaAlpaca
