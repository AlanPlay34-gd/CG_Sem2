#include "DirectXApp.h"
#include "GameTimer.h"

#include <Windows.h>
#include <string>
#include <stdexcept>

namespace {
DirectXApp* gApp = nullptr;

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    if (gApp) {
        return gApp->MsgProc(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int nCmdShow) {
    WNDCLASS wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"Lab3DX12Window";

    if (!RegisterClass(&wc)) {
        return 0;
    }

    RECT rect = {0, 0, 1280, 720};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        L"Lab 3 - Tessellation, Displacement, Normal Mapping (DX12)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    DirectXApp app;
    gApp = &app;

    try {
        if (!app.Initialize(hwnd, 1280, 720)) {
            MessageBox(hwnd, L"Initialization failed.", L"Error", MB_OK | MB_ICONERROR);
            return 0;
        }
    } catch (const std::exception& ex) {
        std::string msg = "Initialization exception: ";
        msg += ex.what();
        MessageBoxA(hwnd, msg.c_str(), "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    GameTimer timer;
    timer.Reset();

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            timer.Tick();
            app.Update(timer);
            app.Draw(timer);
        }
    }

    gApp = nullptr;
    return static_cast<int>(msg.wParam);
}
