void postResult(HWND window, const std::wstring& line) {
    auto* copy = new (std::nothrow) std::wstring(line);
    if (copy && !PostMessageW(window, WM_SCAN_RESULT, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

void postStatus(HWND window, size_t count, unsigned currentPort) {
    PostMessageW(window, WM_SCAN_STATUS, static_cast<WPARAM>(count),
                 static_cast<LPARAM>(currentPort));
}

std::wstring socketErrorText(int code) {
    wchar_t buffer[512] = {};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code), 0,
        buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
    std::wstring text;
    if (length) {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
               text.back() == L' ' || text.back() == L'.')) text.pop_back();
    }
    if (text.empty()) text = L"socket error " + std::to_wstring(code);
    return text;
}

std::wstring printableBanner(const char* data, int length) {
    std::wstring text;
    text.reserve(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        if (ch == '\r' || ch == '\n' || ch == '\t') text.push_back(L' ');
        else if (ch >= 32 && ch < 127) text.push_back(static_cast<wchar_t>(ch));
        else if (ch >= 160) text.push_back(static_cast<wchar_t>(ch));
        else text.push_back(L'.');
    }
    while (!text.empty() && text.back() == L' ') text.pop_back();
    return text;
}

bool resolveTarget(const std::wstring& host, sockaddr_in& address, std::wstring& error) {
    addrinfoW hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfoW* list = nullptr;
    const int result = GetAddrInfoW(host.c_str(), nullptr, &hints, &list);
    if (result || !list) {
        error = L"Could not resolve host \"" + host + L"\" (" +
                std::to_wstring(result) + L").";
        if (list) FreeAddrInfoW(list);
        return false;
    }
    address = *reinterpret_cast<sockaddr_in*>(list->ai_addr);
    FreeAddrInfoW(list);
    return true;
}

void reportOpen(HWND window, ActiveSocket& connection,
                std::vector<ActiveSocket>& active, bool quickScan) {
    postResult(window, row(connection.port, L"CONNECT"));
    PostMessageW(window, WM_SCAN_CONNECTED, connection.port, 0);
    postStatus(window, active.size(), connection.port);
    if (quickScan) {
        closesocket(connection.handle);
        connection.handle = INVALID_SOCKET;
    } else {
        connection.connected = true;
        connection.deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kBannerTimeoutMs);
    }
}

void scanThread(HWND window, ScanConfig config, std::atomic<bool>& stopRequested) {
    sockaddr_in target{};
    target.sin_family = AF_INET;
    std::wstring resolveError;
    if (!resolveTarget(config.host, target, resolveError)) {
        postResult(window, mainRow(L"UNABLE TO RESOLVE HOSTNAME."));
        postResult(window, mainRow(resolveError));
        PostMessageW(window, WM_SCAN_DONE, 0, config.firstPort);
        return;
    }

    wchar_t addressText[INET_ADDRSTRLEN] = {};
    InetNtopW(AF_INET, &target.sin_addr, addressText,
              static_cast<DWORD>(std::size(addressText)));
    postResult(window, mainRow(L"IP: " + std::wstring(addressText)));
    postResult(window, mainRow(L"SCAN BEGUN ON PORT: " +
                               std::to_wstring(config.firstPort)));
    postResult(window, mainRow(L"SCANNING HOST: " + config.host));
    if (config.debug) postResult(window, mainRow(L"DEBUG MODE"));

    std::vector<ActiveSocket> active;
    active.reserve(kMaxOpenSockets);
    unsigned nextPort = config.firstPort;
    unsigned lastAttempted = config.firstPort;
    auto nextLaunch = std::chrono::steady_clock::now();

    while (!stopRequested.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        if (nextPort <= config.lastPort && active.size() < kMaxOpenSockets &&
            now >= nextLaunch) {
            lastAttempted = nextPort;
            postStatus(window, active.size(), nextPort);
            SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (handle == INVALID_SOCKET) {
                const int code = WSAGetLastError();
                if (config.debug)
                    postResult(window, row(nextPort, L"CREATE: " + socketErrorText(code)));
            } else {
                u_long nonBlocking = 1;
                if (ioctlsocket(handle, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
                    const int code = WSAGetLastError();
                    if (config.debug)
                        postResult(window, row(nextPort, L"CREATE: " + socketErrorText(code)));
                    closesocket(handle);
                } else {
                    target.sin_port = htons(static_cast<u_short>(nextPort));
                    const int result = connect(handle,
                        reinterpret_cast<const sockaddr*>(&target), sizeof(target));
                    if (result == 0) {
                        active.push_back({handle, nextPort, false,
                            std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(kConnectTimeoutMs)});
                        reportOpen(window, active.back(), active, config.quickScan);
                        if (config.quickScan) active.pop_back();
                    } else {
                        const int code = WSAGetLastError();
                        if (code == WSAEWOULDBLOCK || code == WSAEINPROGRESS ||
                            code == WSAEALREADY) {
                            active.push_back({handle, nextPort, false,
                                std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(kConnectTimeoutMs)});
                            if (config.debug)
                                postResult(window, row(nextPort, L"CON_TRY"));
                        } else {
                            if (config.debug)
                                postResult(window, row(nextPort,
                                    L"CONFAIL: " + socketErrorText(code)));
                            closesocket(handle);
                        }
                    }
                }
            }
            ++nextPort;
            nextLaunch = std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(config.delayMs);
        }

        if (nextPort > config.lastPort && active.empty()) break;

        std::vector<WSAPOLLFD> pollSet;
        pollSet.reserve(active.size());
        for (const auto& connection : active) {
            WSAPOLLFD item{};
            item.fd = connection.handle;
            item.events = POLLRDNORM | POLLWRNORM;
            pollSet.push_back(item);
        }

        int waitMs = 50;
        now = std::chrono::steady_clock::now();
        if (nextPort <= config.lastPort && active.size() < kMaxOpenSockets) {
            const auto untilLaunch = std::chrono::duration_cast<std::chrono::milliseconds>(
                nextLaunch - now).count();
            waitMs = untilLaunch > 0 ? std::min(waitMs, static_cast<int>(untilLaunch)) : 0;
        }
        for (const auto& connection : active) {
            const auto untilClose = std::chrono::duration_cast<std::chrono::milliseconds>(
                connection.deadline - now).count();
            waitMs = untilClose <= 0 ? 0 :
                     std::min(waitMs, static_cast<int>(untilClose));
        }

        if (!pollSet.empty()) {
            const int result = WSAPoll(pollSet.data(),
                static_cast<ULONG>(pollSet.size()), waitMs);
            if (result == SOCKET_ERROR && config.debug)
                postResult(window, mainRow(L"WSAPoll: " +
                                           socketErrorText(WSAGetLastError())));
        } else if (waitMs > 0) {
            Sleep(static_cast<DWORD>(waitMs));
        }

        now = std::chrono::steady_clock::now();
        for (size_t i = active.size(); i-- > 0;) {
            ActiveSocket& connection = active[i];
            const short events = pollSet.empty() ? 0 : pollSet[i].revents;
            bool remove = false;
            if (!connection.connected &&
                (events & (POLLWRNORM | POLLERR | POLLHUP | POLLNVAL)) != 0) {
                int socketError = 0;
                int optionLength = sizeof(socketError);
                if (getsockopt(connection.handle, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char*>(&socketError), &optionLength) == 0 &&
                    socketError == 0) {                    reportOpen(window, connection, active, config.quickScan);
                    if (config.quickScan) remove = true;
                } else {
                    if (socketError == 0) socketError = WSAGetLastError();
                    if (config.debug)
                        postResult(window, row(connection.port,
                            L"CONFAIL: " + socketErrorText(socketError)));
                    remove = true;
                }
            } else if (connection.connected &&
                (events & (POLLRDNORM | POLLERR | POLLHUP | POLLNVAL)) != 0) {
                char banner[kBannerBytes + 1] = {};
                const int received = recv(connection.handle, banner, kBannerBytes, 0);
                if (received > 0) {
                    const std::wstring text = printableBanner(banner, received);
                    if (!text.empty())
                        postResult(window, row(connection.port, L"RECEIVE: " + text));
                    remove = true;
                } else if (received == 0) {
                    postResult(window, row(connection.port, L"CLOSED"));
                    remove = true;
                } else {
                    const int code = WSAGetLastError();
                    if (code != WSAEWOULDBLOCK) {
                        if (config.debug)
                            postResult(window, row(connection.port,
                                L"RECEIVE: " + socketErrorText(code)));
                        remove = true;
                    }
                }
            }
            if (!remove && now >= connection.deadline) {
                if (config.debug && !connection.connected)
                    postResult(window, row(connection.port, L"CONFAIL: timed out"));
                remove = true;
            }
            if (remove) {
                if (connection.handle != INVALID_SOCKET) closesocket(connection.handle);
                active.erase(active.begin() + static_cast<std::ptrdiff_t>(i));
                postStatus(window, active.size(), lastAttempted);
            }
        }
        postStatus(window, active.size(), lastAttempted);
    }

    const bool halted = stopRequested.load(std::memory_order_relaxed);
    for (auto& connection : active)
        if (connection.handle != INVALID_SOCKET) closesocket(connection.handle);
    postStatus(window, 0, lastAttempted);
    PostMessageW(window, WM_SCAN_DONE, halted ? 1 : 0,
                 static_cast<LPARAM>(lastAttempted));
}
