#define REBUILD_SMOKE_TEST
#include "../src/main.cpp"
#include <iostream>

namespace {
std::vector<std::wstring> captured;
bool finished = false;

LRESULT CALLBACK testProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SCAN_RESULT) {
        std::unique_ptr<std::wstring> value(reinterpret_cast<std::wstring*>(lParam));
        if (value) captured.push_back(*value);
        return 0;
    }
    if (message == WM_SCAN_DONE) {
        finished = true;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void drainMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool contains(const wchar_t* fragment) {
    for (const auto& line : captured)
        if (line.find(fragment) != std::wstring::npos) return true;
    return false;
}
}

int main() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2), &data) != 0) return 2;
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = testProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"ScannerSmokeWindow";
    RegisterClassExW(&klass);
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"", WS_OVERLAPPED,
        0, 0, 20, 20, nullptr, nullptr, klass.hInstance, nullptr);
    if (!window) return 3;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return 4;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR) return 5;
    int localLength = sizeof(local);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&local), &localLength) == SOCKET_ERROR)
        return 6;
    const unsigned testPort = ntohs(local.sin_port);

    std::thread server([listener]() {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);
        timeval timeout{5, 0};
        if (select(0, &readSet, nullptr, nullptr, &timeout) > 0) {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                const char banner[] = "7THSPHERE-SMOKE-BANNER\r\n";
                send(client, banner, static_cast<int>(sizeof(banner) - 1), 0);
                Sleep(100);
                closesocket(client);
            }
        }
        closesocket(listener);
    });

    ScanConfig config;
    config.host = L"127.0.0.1";
    config.firstPort = testPort;
    config.lastPort = testPort;
    config.delayMs = 1;
    config.quickScan = false;
    config.debug = true;
    std::atomic<bool> stop{false};
    scanThread(window, config, stop);
    server.join();
    drainMessages();
    const bool openPass = contains(L"CONNECT") && contains(L"RECEIVE: 7THSPHERE-SMOKE-BANNER") && finished;
    std::wcout << L"open-banner-smoke=" << (openPass ? L"PASS" : L"FAIL") << L"\n";
    if (!openPass) {
        for (const auto& line : captured) std::wcout << line << L"\n";
        return 10;
    }

    captured.clear();
    finished = false;
    config.quickScan = true;
    scanThread(window, config, stop);
    drainMessages();
    const bool closedPass = !contains(L"CONNECT") && contains(L"CONFAIL") && finished;
    std::wcout << L"closed-port-smoke=" << (closedPass ? L"PASS" : L"FAIL") << L"\n";
    if (!closedPass)
        for (const auto& line : captured) std::wcout << line << L"\n";

    DestroyWindow(window);
    WSACleanup();
    return closedPass ? 0 : 11;
}
