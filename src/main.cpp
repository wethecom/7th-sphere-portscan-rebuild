#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"SeventhSphereRebuildWindow";
constexpr UINT WM_SCAN_RESULT = WM_APP + 1;
constexpr UINT WM_SCAN_STATUS = WM_APP + 2;
constexpr UINT WM_SCAN_DONE = WM_APP + 3;
constexpr UINT WM_SCAN_CONNECTED = WM_APP + 4;
constexpr int kMaxOpenSockets = 220;
constexpr int kConnectTimeoutMs = 4000;
constexpr int kBannerTimeoutMs = 1800;
constexpr int kBannerBytes = 50;

enum ControlId {
    ID_TARGET = 1000, ID_START_PORT = 1001, ID_HALT = 1004, ID_START = 1005,
    ID_RECEIVE_PORT = 1009, ID_RESULTS = 1010, ID_DELAY = 1011, ID_SAVE = 1012,
    ID_FILENAME = 1013, ID_CLEAR = 1014, ID_QUICK_SCAN = 1015, ID_SAVE_ON_HALT = 1016,
    ID_OPEN_SOCKETS = 1017, ID_STOP_PORT = 1018, ID_ABOUT = 1020, ID_RESET = 1021,
    ID_OVERWRITE = 1022, ID_RESET_ON_HALT = 1023, ID_SCROLL = 1026, ID_DEBUG = 1027
};

struct ScanConfig {
    std::wstring host;
    unsigned firstPort = 1, lastPort = 65535, delayMs = 20;
    bool quickScan = false, debug = false;
};

struct App {
    HWND window = nullptr;
    HWND targetLabel = nullptr, target = nullptr;
    HWND startPortLabel = nullptr, startPort = nullptr;
    HWND stopPortLabel = nullptr, stopPort = nullptr;
    HWND delayLabel = nullptr, delay = nullptr;
    HWND receivePortLabel = nullptr, receivePort = nullptr;
    HWND openSocketsLabel = nullptr, openSockets = nullptr;
    HWND quickScan = nullptr, resetOnHalt = nullptr, saveOnHalt = nullptr;
    HWND scroll = nullptr, debug = nullptr, overwrite = nullptr;
    HWND clearButton = nullptr, resetButton = nullptr, aboutButton = nullptr;
    HWND resultsLabel = nullptr, results = nullptr;
    HWND filenameLabel = nullptr, filename = nullptr, saveButton = nullptr;
    HWND startButton = nullptr, haltButton = nullptr;
    HFONT font = nullptr;
    std::thread worker;
    std::atomic<bool> stopRequested{false};
    bool running = false, resetWhenDone = false, commandLineMode = false;
};

std::atomic<bool>* g_cliStopRequested = nullptr;

struct ActiveSocket {
    SOCKET handle = INVALID_SOCKET;
    unsigned port = 0;
    bool connected = false;
    std::chrono::steady_clock::time_point deadline;
};

std::wstring readText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

std::wstring portPrefix(unsigned port) {
    std::wstring value = std::to_wstring(port);
    if (value.size() < 5) value.append(5 - value.size(), L' ');
    return value + L":";
}

std::wstring row(unsigned port, const std::wstring& message) {
    return portPrefix(port) + message;
}

std::wstring mainRow(const std::wstring& message) {
    return row(0, message);
}

void appendResult(App& app, const std::wstring& line) {
    if (!app.results) return;
    const LRESULT result = SendMessageW(app.results, LB_ADDSTRING, 0,
                                        reinterpret_cast<LPARAM>(line.c_str()));
    if (result != LB_ERR && result != LB_ERRSPACE &&
        SendMessageW(app.scroll, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        const LRESULT count = SendMessageW(app.results, LB_GETCOUNT, 0, 0);
        if (count > 0) SendMessageW(app.results, LB_SETTOPINDEX, count - 1, 0);
    }
}

#include "scanner_impl.inl"

HWND createControl(App& app, const wchar_t* klass, const wchar_t* text, DWORD style,
                   int id, DWORD exStyle = 0) {
    HWND control = CreateWindowExW(exStyle, klass, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 10, 10, app.window,
        id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control && app.font)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return control;
}

HWND createLabel(App& app, const wchar_t* text) {
    return createControl(app, L"STATIC", text, SS_LEFT, 0);
}

HWND createEdit(App& app, int id, const wchar_t* value, bool readOnly = false) {
    DWORD style = WS_TABSTOP | ES_AUTOHSCROLL;
    if (readOnly) style |= ES_READONLY;
    return createControl(app, L"EDIT", value, style, id, WS_EX_CLIENTEDGE);
}

HWND createButton(App& app, int id, const wchar_t* text, bool checkBox = false) {
    DWORD style = WS_TABSTOP | (checkBox ? BS_AUTOCHECKBOX : BS_PUSHBUTTON);
    if (checkBox && (id == ID_QUICK_SCAN || id == ID_RESET_ON_HALT ||
                     id == ID_SAVE_ON_HALT)) style |= BS_LEFTTEXT;
    return createControl(app, L"BUTTON", text, style, id);
}

void createUi(App& app) {
    // The original used a compact 11-pixel font. Use 14 pixels here so the
    // rebuild remains readable with a 125% accessibility text increase.
    app.font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_ROMAN, L"Times New Roman");

    app.targetLabel = createLabel(app, L"Scan:");
    app.target = createEdit(app, ID_TARGET, L"");
    app.startButton = createButton(app, ID_START, L"START");

    app.startPortLabel = createLabel(app, L"Send Port:");
    app.startPort = createEdit(app, ID_START_PORT, L"1");
    app.receivePortLabel = createLabel(app, L"Recv Port:");
    app.receivePort = createEdit(app, ID_RECEIVE_PORT, L"0", true);
    app.stopPortLabel = createLabel(app, L"Stop Port:");
    app.stopPort = createEdit(app, ID_STOP_PORT, L"65536");
    app.delayLabel = createLabel(app, L"Delay (MS):");
    app.delay = createEdit(app, ID_DELAY, L"20");
    app.openSocketsLabel = createLabel(app, L"Open Sockets:");
    app.openSockets = createEdit(app, ID_OPEN_SOCKETS, L"0", true);

    app.haltButton = createButton(app, ID_HALT, L"HALT");
    app.saveButton = createButton(app, ID_SAVE, L"SAVE");
    app.resetButton = createButton(app, ID_RESET, L"RESET");
    app.aboutButton = createControl(app, L"BUTTON", L"A\nB\nO\nU\nT",
        WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE, ID_ABOUT);
    app.clearButton = createControl(app, L"BUTTON", L"C\nL\nE\nA\nR",
        WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE, ID_CLEAR);

    app.quickScan = createButton(app, ID_QUICK_SCAN, L"&1 Quick Scan", true);
    app.resetOnHalt = createButton(app, ID_RESET_ON_HALT, L"&2 Reset on halt", true);
    app.saveOnHalt = createButton(app, ID_SAVE_ON_HALT, L"&3 Save on halt", true);
    app.scroll = createButton(app, ID_SCROLL, L"&4 Scroll", true);
    app.debug = createButton(app, ID_DEBUG, L"&Debug", true);
    app.overwrite = createButton(app, ID_OVERWRITE, L"&5 Overwrite", true);

    app.resultsLabel = createLabel(app, L"Scan Results:");
    app.results = createControl(app, L"LISTBOX", L"",
        WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_SORT | LBS_NOINTEGRALHEIGHT,
        ID_RESULTS, WS_EX_CLIENTEDGE);
    app.filenameLabel = createLabel(app, L"Filename:");
    app.filename = createEdit(app, ID_FILENAME, L"");
    wchar_t profile[MAX_PATH] = {};
    const DWORD profileLength = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    std::wstring path = profileLength && profileLength < MAX_PATH ?
        profile : L".";
    path += L"\\Documents\\SCANLOG.TXT";
    SetWindowTextW(app.filename, path.c_str());

    wchar_t host[256] = {};
    DWORD hostLength = static_cast<DWORD>(std::size(host));
    if (GetComputerNameW(host, &hostLength)) SetWindowTextW(app.target, host);
    else SetWindowTextW(app.target, L"localhost");

    SendMessageW(app.scroll, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(app.haltButton, FALSE);
    appendResult(app, mainRow(L"PORTSCAN READY."));
    appendResult(app, mainRow(L"V:PS1.1 WB1 011997"));
}
void layout(App& app, int width, int height) {
    constexpr int fieldHeight = 29;
    constexpr int left = 26;
    constexpr int buttonWidth = 94;
    constexpr int rightMargin = 18;
    const int rightColumn = width - rightMargin - buttonWidth;

    MoveWindow(app.targetLabel, 20, 13, 44, 30, TRUE);
    MoveWindow(app.target, 70, 10, std::max(220, rightColumn - 84), fieldHeight, TRUE);
    MoveWindow(app.startButton, rightColumn, 10, buttonWidth, 33, TRUE);
    MoveWindow(app.haltButton, rightColumn, 48, buttonWidth, 33, TRUE);
    MoveWindow(app.saveButton, rightColumn, 86, buttonWidth, 33, TRUE);
    MoveWindow(app.resetButton, rightColumn, 124, buttonWidth, 33, TRUE);

    MoveWindow(app.aboutButton, 0, 34, 22, 142, TRUE);
    MoveWindow(app.startPortLabel, 32, 54, 80, 30, TRUE);
    MoveWindow(app.startPort, 116, 51, 100, fieldHeight, TRUE);
    MoveWindow(app.receivePortLabel, 260, 54, 84, 30, TRUE);
    MoveWindow(app.receivePort, 350, 51, 100, fieldHeight, TRUE);
    MoveWindow(app.stopPortLabel, 32, 94, 80, 30, TRUE);
    MoveWindow(app.stopPort, 116, 91, 100, fieldHeight, TRUE);
    MoveWindow(app.delayLabel, 260, 94, 84, 30, TRUE);
    MoveWindow(app.delay, 350, 91, 100, fieldHeight, TRUE);

    MoveWindow(app.quickScan, 28, 132, 205, 25, TRUE);
    MoveWindow(app.resetOnHalt, 28, 160, 205, 25, TRUE);
    MoveWindow(app.saveOnHalt, 28, 188, 205, 25, TRUE);
    MoveWindow(app.openSocketsLabel, 270, 149, 112, 30, TRUE);
    MoveWindow(app.openSockets, 390, 146, 92, fieldHeight, TRUE);

    MoveWindow(app.debug, width - 225, 218, 76, 24, TRUE);
    MoveWindow(app.scroll, width - 140, 218, 118, 24, TRUE);
    MoveWindow(app.resultsLabel, left, 218, 150, 25, TRUE);

    const int filenameY = height - 45;
    const int listTop = 248;
    const int listBottom = filenameY - 16;
    MoveWindow(app.results, left, listTop, width - left - 18,
               std::max(100, listBottom - listTop), TRUE);
    MoveWindow(app.clearButton, 0, listTop + 36, 22,
               std::max(100, filenameY - listTop - 36), TRUE);

    const int overwriteX = width - 136;
    MoveWindow(app.filenameLabel, 24, filenameY + 5, 74, 25, TRUE);
    MoveWindow(app.filename, 104, filenameY, std::max(180, overwriteX - 118),
               fieldHeight, TRUE);
    MoveWindow(app.overwrite, overwriteX, filenameY + 2, 118, 24, TRUE);
}
bool parseUnsigned(HWND control, unsigned& value, unsigned maxValue = 65535) {
    const std::wstring text = readText(control);
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || parsed > maxValue) return false;
    value = static_cast<unsigned>(parsed);
    return true;
}

bool prepareConfig(App& app, ScanConfig& config) {
    config.host = readText(app.target);
    if (config.host.empty()) {
        MessageBoxW(app.window, L"Enter a host name or IPv4 address.",
                    L"Scan target", MB_OK | MB_ICONWARNING);
        SetFocus(app.target);
        return false;
    }
    if (!parseUnsigned(app.startPort, config.firstPort) || config.firstPort == 0) {
        MessageBoxW(app.window, L"Send Port must be between 1 and 65535.",
                    L"Port range", MB_OK | MB_ICONWARNING);
        SetFocus(app.startPort);
        return false;
    }
    if (!parseUnsigned(app.stopPort, config.lastPort, 65536) || config.lastPort == 0 ||
        config.lastPort < config.firstPort) {
        MessageBoxW(app.window, L"Stop Port must be at least Send Port and no greater than 65536.",
                    L"Port range", MB_OK | MB_ICONWARNING);
        SetFocus(app.stopPort);
        return false;
    }
    if (config.lastPort == 65536) config.lastPort = 65535;
    if (!parseUnsigned(app.delay, config.delayMs) || config.delayMs == 0 ||
        config.delayMs > 60000) {
        MessageBoxW(app.window, L"Delay must be between 1 and 60000 milliseconds.",
                    L"Scan delay", MB_OK | MB_ICONWARNING);
        SetFocus(app.delay);
        return false;
    }
    config.quickScan = SendMessageW(app.quickScan, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.debug = SendMessageW(app.debug, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return true;
}

void startScan(App& app) {
    if (app.running) return;
    if (app.worker.joinable()) app.worker.join();

    ScanConfig config;
    if (!prepareConfig(app, config)) return;

    app.stopRequested.store(false, std::memory_order_relaxed);
    app.resetWhenDone = false;
    app.running = true;
    EnableWindow(app.startButton, FALSE);
    EnableWindow(app.haltButton, TRUE);
    SetWindowTextW(app.receivePort, L"0");
    SetWindowTextW(app.openSockets, L"0");

    try {
        app.worker = std::thread(scanThread, app.window, std::move(config),
                                 std::ref(app.stopRequested));
    } catch (...) {
        app.running = false;
        EnableWindow(app.startButton, TRUE);
        EnableWindow(app.haltButton, FALSE);
        MessageBoxW(app.window, L"Could not start the scanner worker.",
                    L"Start failed", MB_OK | MB_ICONERROR);
    }
}

void haltScan(App& app, bool resetAfter) {
    if (!app.running) return;
    app.resetWhenDone = resetAfter;
    app.stopRequested.store(true, std::memory_order_relaxed);
    EnableWindow(app.haltButton, FALSE);
    SetWindowTextW(app.haltButton, L"HALTING...");
}

bool saveResults(App& app, bool showDialog) {
    const std::wstring path = readText(app.filename);    if (path.empty()) {
        if (showDialog) MessageBoxW(app.window, L"Enter a filename first.",
                                    L"Save results", MB_OK | MB_ICONWARNING);
        return false;
    }

    const LRESULT count = SendMessageW(app.results, LB_GETCOUNT, 0, 0);
    std::vector<std::wstring> lines;
    lines.reserve(count > 0 ? static_cast<size_t>(count) : 0);
    for (LRESULT i = 0; i < count; ++i) {
        const LRESULT length = SendMessageW(app.results, LB_GETTEXTLEN, i, 0);
        if (length == LB_ERR) continue;
        std::wstring line(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(app.results, LB_GETTEXT, i, reinterpret_cast<LPARAM>(line.data()));
        line.resize(static_cast<size_t>(length));
        lines.push_back(std::move(line));
    }
    std::stable_sort(lines.begin(), lines.end(), [](const auto& left, const auto& right) {
        return wcstoul(left.c_str(), nullptr, 10) < wcstoul(right.c_str(), nullptr, 10);
    });

    const bool overwriteFile =
        SendMessageW(app.overwrite, BM_GETCHECK, 0, 0) == BST_CHECKED;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
        overwriteFile ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (showDialog) MessageBoxW(app.window, L"Could not open the selected output file.",
                                    L"Save results", MB_OK | MB_ICONERROR);
        return false;
    }

    std::string output = "-----RESULTS OF SCAN FOLLOW-----\r\n";
    for (const auto& line : lines) {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
            static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        if (needed > 0) {
            const size_t offset = output.size();
            output.resize(offset + static_cast<size_t>(needed));
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                output.data() + offset, needed, nullptr, nullptr);
        }
        output += "\r\n";
    }
    output += "END PORT INFO\r\n";
    DWORD written = 0;
    const BOOL ok = WriteFile(file, output.data(), static_cast<DWORD>(output.size()),
                              &written, nullptr);
    CloseHandle(file);
    if (!ok || written != output.size()) {
        if (showDialog) MessageBoxW(app.window, L"Could not write the full results file.",
                                    L"Save results", MB_OK | MB_ICONERROR);
        return false;
    }
    if (showDialog) MessageBoxW(app.window, L"Scan results saved.", L"Save results",
                                MB_OK | MB_ICONINFORMATION);
    return true;
}

void markReset(App& app) {
    SetWindowTextW(app.receivePort, L"0");
    SetWindowTextW(app.openSockets, L"0");
    appendResult(app, mainRow(L"RESET - ALL SOCKETS CLOSED"));
}

void finishScan(App& app, bool halted, unsigned lastPort) {
    if (app.worker.joinable()) app.worker.join();
    app.running = false;
    EnableWindow(app.startButton, TRUE);
    EnableWindow(app.haltButton, FALSE);
    SetWindowTextW(app.haltButton, L"HALT");

    if (halted) {
        appendResult(app, mainRow(L"SCAN HALTED ON PORT: " + std::to_wstring(lastPort)));
        if (app.resetWhenDone ||
            SendMessageW(app.resetOnHalt, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            markReset(app);
        }
        if (SendMessageW(app.saveOnHalt, BM_GETCHECK, 0, 0) == BST_CHECKED &&
            !app.resetWhenDone) {
            saveResults(app, false);
        }
    } else {
        appendResult(app, mainRow(L"SCAN COMPLETE"));
    }
    app.resetWhenDone = false;
}

void showAbout(HWND window) {
    MessageBoxW(window,
        L"7th Sphere PortScan 1.1 - independent 64-bit rebuild\n\n"
        L"This is new source code based on static analysis of the original "
        L"Windows application. It does not include or replace the original binary.",
        L"About the rebuild", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }    if (!app) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        createUi(*app);
        return 0;
    case WM_SIZE:
        layout(*app, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 800;
        info->ptMinTrackSize.y = 560;
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_START: startScan(*app); return 0;
        case ID_HALT: haltScan(*app, false); return 0;
        case ID_SAVE: saveResults(*app, true); return 0;
        case ID_CLEAR:
            SendMessageW(app->results, LB_RESETCONTENT, 0, 0);
            return 0;
        case ID_RESET:
            if (app->running) haltScan(*app, true);
            else markReset(*app);
            return 0;
        case ID_ABOUT: showAbout(window); return 0;
        }
        break;
    case WM_SCAN_RESULT: {
        std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lParam));
        if (line) {
            appendResult(*app, *line);
            if (app->commandLineMode) {
                std::fwprintf(stdout, L"%ls\n", line->c_str());
                std::fflush(stdout);
            }
        }
        return 0;
    }
    case WM_SCAN_STATUS:
        SetWindowTextW(app->openSockets, std::to_wstring(wParam).c_str());
        if (lParam > 0 && lParam <= 65535)
            SetWindowTextW(app->startPort, std::to_wstring(lParam).c_str());
        return 0;
    case WM_SCAN_CONNECTED:
        SetWindowTextW(app->receivePort, std::to_wstring(wParam).c_str());
        return 0;
    case WM_SCAN_DONE:
        SetWindowTextW(app->openSockets, L"0");
        finishScan(*app, wParam != 0, static_cast<unsigned>(lParam));
        return 0;
    case WM_CLOSE:
        if (app->running) app->stopRequested.store(true, std::memory_order_relaxed);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (app->running) app->stopRequested.store(true, std::memory_order_relaxed);
        if (app->worker.joinable()) app->worker.join();
        if (app->font) DeleteObject(app->font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

#ifndef REBUILD_SMOKE_TEST
namespace {
struct CliOptions {
    std::wstring target;
    std::wstring output;
    unsigned firstPort = 1;
    unsigned lastPort = 65536;
    unsigned delayMs = 20;
    bool quickScan = false;
    bool debug = false;
    bool overwrite = false;
    bool help = false;
};

void attachCliConsoleIfNeeded() {
    auto validHandle = [](DWORD id) {
        const HANDLE handle = GetStdHandle(id);
        return handle && handle != INVALID_HANDLE_VALUE &&
               GetFileType(handle) != FILE_TYPE_UNKNOWN;
    };
    if (validHandle(STD_OUTPUT_HANDLE) && validHandle(STD_ERROR_HANDLE)) return;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    if (!validHandle(STD_OUTPUT_HANDLE)) std::freopen("CONOUT$", "w", stdout);
    if (!validHandle(STD_ERROR_HANDLE)) std::freopen("CONOUT$", "w", stderr);
}

void printCliUsage() {
    std::fwprintf(stdout,
        L"7th Sphere PortScan 1.1 (Rebuild)\n"
        L"Usage: 7th-sphere-portscan-rebuild.exe --target HOST [options]\n\n"
        L"Options:\n"
        L"  -t, --target HOST       Host name or IPv4 address (required)\n"
        L"  -s, --start-port PORT   First port (default: 1)\n"
        L"  -e, --stop-port PORT    Last port (default: 65536; scans through 65535)\n"
        L"  -d, --delay MS          Delay between attempts (default: 20)\n"
        L"  -o, --output FILE       Save results (default: Documents\\SCANLOG.TXT)\n"
        L"  -q, --quick             Close a connection as soon as it succeeds\n"
        L"      --debug             Include connection errors and debug rows\n"
        L"      --overwrite         Replace the output file instead of appending\n"
        L"  -h, --help              Show this help\n\n"
        L"Example:\n"
        L"  7th-sphere-portscan-rebuild.exe --target 192.168.1.1 "
        L"--start-port 1 --stop-port 1024 --quick --output \"scan results.txt\"\n");
    std::fflush(stdout);
}

bool parseCliNumber(const std::wstring& text, unsigned minimum, unsigned maximum,
                    unsigned& value) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || parsed < minimum || parsed > maximum)
        return false;
    value = static_cast<unsigned>(parsed);
    return true;
}

bool parseCliOptions(int argc, LPWSTR* argv, CliOptions& options,
                     std::wstring& error) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") {
            options.help = true;
            continue;
        }
        if (arg == L"--quick" || arg == L"-q") {
            options.quickScan = true;
            continue;
        }
        if (arg == L"--debug") {
            options.debug = true;
            continue;
        }
        if (arg == L"--overwrite") {
            options.overwrite = true;
            continue;
        }

        if (arg == L"--target" || arg == L"-t" ||
            arg == L"--start-port" || arg == L"-s" ||
            arg == L"--stop-port" || arg == L"-e" ||
            arg == L"--delay" || arg == L"-d" ||
            arg == L"--output" || arg == L"-o") {
            if (i + 1 >= argc) {
                error = L"Missing value after " + arg + L".";
                return false;
            }
            const std::wstring value = argv[++i];
            if (arg == L"--target" || arg == L"-t") {
                options.target = value;
            } else if (arg == L"--output" || arg == L"-o") {
                options.output = value;
            } else {
                unsigned parsed = 0;
                const unsigned minimum = (arg == L"--delay" || arg == L"-d") ? 1 : 1;
                const unsigned maximum = (arg == L"--stop-port" || arg == L"-e") ?
                    65536 : ((arg == L"--delay" || arg == L"-d") ? 60000 : 65535);
                if (!parseCliNumber(value, minimum, maximum, parsed)) {
                    error = L"Invalid value for " + arg + L": " + value;
                    return false;
                }
                if (arg == L"--start-port" || arg == L"-s")
                    options.firstPort = parsed;
                else if (arg == L"--stop-port" || arg == L"-e")
                    options.lastPort = parsed;
                else
                    options.delayMs = parsed;
            }
            continue;
        }

        error = L"Unknown option: " + arg;
        return false;
    }

    if (!options.help && options.target.empty()) {
        error = L"--target is required.";
        return false;
    }
    if (options.lastPort < options.firstPort) {
        error = L"--stop-port must be greater than or equal to --start-port.";
        return false;
    }
    if (options.output.empty()) {
        wchar_t profile[MAX_PATH] = {};
        const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        options.output = length && length < MAX_PATH ? profile : L".";
        options.output += L"\\Documents\\SCANLOG.TXT";
    }
    return true;
}

bool registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClass;
    return RegisterClassExW(&windowClass) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

BOOL WINAPI cliControlHandler(DWORD controlType) {
    if ((controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT) &&
        g_cliStopRequested) {
        g_cliStopRequested->store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

int runCli(HINSTANCE instance, int argc, LPWSTR* argv) {
    attachCliConsoleIfNeeded();
    CliOptions options;
    std::wstring error;
    if (!parseCliOptions(argc, argv, options, error)) {
        std::fwprintf(stderr, L"Error: %ls\n\n", error.c_str());
        printCliUsage();
        return 2;
    }
    if (options.help) {
        printCliUsage();
        return 0;
    }

    WSADATA winsock{};
    const int startup = WSAStartup(MAKEWORD(2, 2), &winsock);
    if (startup != 0) {
        std::fwprintf(stderr, L"Could not initialize Windows Sockets (%d).\n", startup);
        return 1;
    }
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);
    if (!registerWindowClass(instance)) {
        std::fwprintf(stderr, L"Could not register the scanner window class.\n");
        WSACleanup();
        return 1;
    }

    App app;
    app.commandLineMode = true;
    HWND window = CreateWindowExW(0, kWindowClass, L"7th Sphere PortScan CLI",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 600, nullptr, nullptr, instance, &app);
    if (!window) {
        std::fwprintf(stderr, L"Could not create the scanner window.\n");
        WSACleanup();
        return 1;
    }

    SetWindowTextW(app.target, options.target.c_str());
    SetWindowTextW(app.startPort, std::to_wstring(options.firstPort).c_str());
    SetWindowTextW(app.stopPort, std::to_wstring(options.lastPort).c_str());
    SetWindowTextW(app.delay, std::to_wstring(options.delayMs).c_str());
    SetWindowTextW(app.filename, options.output.c_str());
    SendMessageW(app.quickScan, BM_SETCHECK,
                 options.quickScan ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(app.debug, BM_SETCHECK,
                 options.debug ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(app.overwrite, BM_SETCHECK,
                 options.overwrite ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(app.results, LB_RESETCONTENT, 0, 0);

    std::fwprintf(stdout, L"Scanning %ls, ports %u-%u, delay %u ms.\n",
                  options.target.c_str(), options.firstPort, options.lastPort,
                  options.delayMs);
    std::fwprintf(stdout, L"Results will be saved to: %ls\n", options.output.c_str());
    std::fflush(stdout);

    g_cliStopRequested = &app.stopRequested;
    SetConsoleCtrlHandler(cliControlHandler, TRUE);
    startScan(app);
    if (!app.running) {
        SetConsoleCtrlHandler(cliControlHandler, FALSE);
        g_cliStopRequested = nullptr;
        DestroyWindow(window);
        WSACleanup();
        return 1;
    }
    MSG message{};
    while (app.running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    const bool interrupted = app.stopRequested.load(std::memory_order_relaxed);
    if (app.running) app.stopRequested.store(true, std::memory_order_relaxed);
    if (app.worker.joinable()) app.worker.join();

    bool unresolved = false;
    const LRESULT resultCount = SendMessageW(app.results, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < resultCount; ++i) {
        const LRESULT length = SendMessageW(app.results, LB_GETTEXTLEN, i, 0);
        if (length == LB_ERR) continue;
        std::wstring line(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(app.results, LB_GETTEXT, i,
                     reinterpret_cast<LPARAM>(line.data()));
        line.resize(static_cast<size_t>(length));
        if (line.find(L"UNABLE TO RESOLVE HOSTNAME") != std::wstring::npos)
            unresolved = true;
    }

    const bool saved = saveResults(app, false);
    if (saved)
        std::fwprintf(stdout, L"Results saved to: %ls\n", options.output.c_str());
    else
        std::fwprintf(stderr, L"Could not save results to: %ls\n", options.output.c_str());
    if (interrupted) std::fwprintf(stdout, L"Scan halted by user.\n");
    std::fflush(stdout);
    std::fflush(stderr);

    SetConsoleCtrlHandler(cliControlHandler, FALSE);
    g_cliStopRequested = nullptr;
    DestroyWindow(window);
    WSACleanup();
    if (!saved) return 4;
    if (unresolved) return 3;
    return interrupted ? 130 : 0;
}

int runGui(HINSTANCE instance, int showCommand) {
    WSADATA winsock{};
    const int startup = WSAStartup(MAKEWORD(2, 2), &winsock);
    if (startup != 0) {
        MessageBoxW(nullptr, L"Windows Sockets could not be initialized.",
                    L"Startup failed", MB_OK | MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);
    if (!registerWindowClass(instance)) {
        WSACleanup();
        return 1;
    }

    App app;
    HWND window = CreateWindowExW(0, kWindowClass,
        L"7th Sphere PortScan 1.1 (Rebuild)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 600, nullptr, nullptr, instance, &app);
    if (!window) {
        WSACleanup();
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    app.stopRequested.store(true, std::memory_order_relaxed);
    if (app.worker.joinable()) app.worker.join();
    WSACleanup();
    return static_cast<int>(message.wParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        MessageBoxW(nullptr, L"Could not read the command line.",
                    L"Startup failed", MB_OK | MB_ICONERROR);
        return 1;
    }

    const bool commandLineMode = argc > 1;
    const int result = commandLineMode ? runCli(instance, argc, argv) :
                                         runGui(instance, showCommand);
    LocalFree(argv);
    return result;
}










#endif
