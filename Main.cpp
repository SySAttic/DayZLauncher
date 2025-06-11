
#include "DayZLauncher.h"
#include <memory>
#include <objbase.h>


std::unique_ptr<DayZLauncher> g_launcher = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        MessageBox(nullptr, L"Failed to initialize COM", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }


    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBox(nullptr, L"Failed to initialize Winsock", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return -1;
    }

    g_launcher = std::make_unique<DayZLauncher>();


    if (!g_launcher->Initialize(hInstance)) {
        MessageBox(nullptr, L"Failed to initialize application", L"Error", MB_OK | MB_ICONERROR);
        g_launcher.reset();
        WSACleanup();
        CoUninitialize();
        return -1;
    }

    g_launcher->RefreshServers();
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }


    g_launcher.reset();
    WSACleanup();
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}