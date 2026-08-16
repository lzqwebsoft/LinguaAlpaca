#pragma once
#pragma execution_character_set("utf-8")

#include <string>

namespace LinguaAlpaca {

class WinUIAutomationHelper {
public:
    static bool TryExtract(int x, int y, std::string& outText, int& outAnchorX, int& outAnchorY);
};

} // namespace LinguaAlpaca
