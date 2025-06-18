#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows /entry:WinMainCRTStartup")
#endif

#include "VideoPlayer.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>
#include <iostream>

// 添加链接器指令解决入口点问题
#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows /entry:WinMainCRTStartup")
#endif

#define ID_PLAY 1001
#define ID_STOP 1002
#define ID_URL_INPUT 1003
#define ID_PAUSE 1004
#define ID_SEEK 1005

VideoPlayer g_player;
HWND g_hwndVideo = nullptr;
HWND g_hwndPlayBtn = nullptr;
HWND g_hwndStopBtn = nullptr;
HWND g_hwndUrlInput = nullptr;

std::string WideToUTF8(const wchar_t* wideStr) {
    if (!wideStr) return "";
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length == 0) return "";
    std::string utf8Str(utf8Length, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &utf8Str[0], utf8Length, nullptr, nullptr);
    if (!utf8Str.empty() && utf8Str.back() == '\0') {
        utf8Str.pop_back();
    }
    return utf8Str;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwndUrlInput = CreateWindow(
            L"EDIT", L"D://FC//129_rtsp_xmux//v1080.mp4",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 500, 30,
            hwnd, (HMENU)ID_URL_INPUT, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

        g_hwndPlayBtn = CreateWindow(
            L"BUTTON", L"播放",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            520, 10, 80, 30,
            hwnd, (HMENU)ID_PLAY, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

        g_hwndStopBtn = CreateWindow(
            L"BUTTON", L"停止",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            610, 10, 80, 30,
            hwnd, (HMENU)ID_STOP, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

        g_hwndVideo = CreateWindow(
            L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
            10, 50, 760, 500,
            hwnd, nullptr, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_PLAY) {
            wchar_t url[1024];
            GetWindowText(g_hwndUrlInput, url, 1024);
            std::string utf8Url = WideToUTF8(url);

            if (utf8Url.empty()) {
                MessageBox(hwnd, L"无效的URL", L"错误", MB_ICONERROR);
                break;
            }

            g_player.SetRenderWindow(g_hwndVideo);

            if (g_player.Open(utf8Url)) {
                g_player.Play();
                SetWindowText(g_hwndPlayBtn, L"播放中...");
                EnableWindow(g_hwndPlayBtn, FALSE);
            }
            else {
                MessageBox(hwnd, L"无法打开视频文件", L"错误", MB_ICONERROR);
            }
        }
        else if (LOWORD(wParam) == ID_STOP) {
            g_player.Stop();
            SetWindowText(g_hwndPlayBtn, L"播放");
            EnableWindow(g_hwndPlayBtn, TRUE);
        }

       
        break;
    }
    case WM_DESTROY:
        g_player.Stop();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0,
        hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW),
        (HBRUSH)(COLOR_WINDOW + 1), nullptr, L"VideoPlayerWindow", nullptr };

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"窗口注册失败!", L"错误", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindow(L"VideoPlayerWindow", L"远程MP4播放器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBox(nullptr, L"窗口创建失败!", L"错误", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}