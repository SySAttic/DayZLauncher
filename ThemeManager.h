#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

class ThemeManager {
public:
    static COLORREF GetColor(const std::string& key);
    static void ApplyTheme(HWND hWnd);
    static void SetColor(const std::string& key, COLORREF color);

};
#pragma once
