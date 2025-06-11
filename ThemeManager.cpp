#include "ThemeManager.h"
#include <unordered_map>
#include <windows.h>

static std::unordered_map<std::string, COLORREF> colorMap = {
    { "windowBg", RGB(255, 255, 255) },      
    { "text", RGB(0, 0, 0) },               
    { "buttonBg", RGB(245, 245, 245) },      
    { "buttonText", RGB(0, 0, 0) },           
    { "highlight", RGB(200, 200, 200) },      
    { "listHeader", RGB(240, 240, 240) },     
    { "listRowEven", RGB(255, 255, 255) },   
    { "listRowOdd", RGB(250, 250, 250) },     
};


COLORREF ThemeManager::GetColor(const std::string& key) {
    auto it = colorMap.find(key);
    if (it != colorMap.end()) {
        return it->second;
    }
    return RGB(255, 255, 255); 
}

void ThemeManager::SetColor(const std::string& key, COLORREF color) {
    colorMap[key] = color;
}


void ThemeManager::ApplyTheme(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;


    WNDCLASS wc = {};
    TCHAR className[256];
    GetClassName(hWnd, className, 256);
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);


    if (GetClassInfo(hInstance, className, &wc)) {
     
        HBRUSH newBrush = CreateSolidBrush(GetColor("windowBg"));
        wc.hbrBackground = newBrush;

    
        SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)newBrush);


        InvalidateRect(hWnd, nullptr, TRUE);
    }
}
