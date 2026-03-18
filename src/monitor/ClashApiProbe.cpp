#include "ClashApiProbe.h"

#include <cctype>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace NetGuard {

namespace {

static std::string trimCopy(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static void stripQuotePair(std::string& s) {
    if (s.size() >= 2) {
        if ((s.front() == '\'' && s.back() == '\'') ||
            (s.front() == '"' && s.back() == '"')) {
            s = s.substr(1, s.size() - 2);
        }
    }
}

} // namespace

bool ClashApiProbe::parseClashConfig(const std::string& configPath,
                                     std::string& outHost,
                                     uint16_t& outPort,
                                     std::string& outSecret) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    std::string parsedHost = outHost;
    uint16_t parsedPort = outPort;
    std::string parsedSecret = outSecret;

    std::string line;
    while (std::getline(file, line)) {
        line = trimCopy(line);

        if (line.rfind("external-controller:", 0) == 0) {
            std::string val = trimCopy(line.substr(20));
            stripQuotePair(val);

            const size_t colon = val.rfind(':');
            if (colon != std::string::npos && colon + 1 < val.size()) {
                try {
                    const int parsed = std::stoi(val.substr(colon + 1));
                    if (parsed > 0 && parsed <= 65535) {
                        parsedHost = val.substr(0, colon);
                        parsedPort = static_cast<uint16_t>(parsed);
                    }
                } catch (...) {
                    // ignore malformed port
                }
            }
        }

        if (line.rfind("secret:", 0) == 0) {
            std::string val = trimCopy(line.substr(7));
            stripQuotePair(val);
            parsedSecret = val;
        }
    }

    outHost = parsedHost;
    outPort = parsedPort;
    outSecret = parsedSecret;
    return (outPort != 0);
}

bool ClashApiProbe::parseSseLine(const std::string& line,
                                 uint64_t& up,
                                 uint64_t& down) {
    auto findVal = [&](const std::string& key) -> int64_t {
        size_t pos = line.find(key);
        if (pos == std::string::npos) {
            return -1;
        }
        pos += key.size();
        while (pos < line.size() && line[pos] == ' ') {
            ++pos;
        }

        size_t end = pos;
        while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        if (end == pos) {
            return -1;
        }

        try {
            return std::stoll(line.substr(pos, end - pos));
        } catch (...) {
            return -1;
        }
    };

    const int64_t u = findVal("\"up\":");
    const int64_t d = findVal("\"down\":");
    if (u < 0 || d < 0) {
        return false;
    }

    up = static_cast<uint64_t>(u);
    down = static_cast<uint64_t>(d);
    return true;
}

void ClashApiProbe::sseLoop() {
    HINTERNET hSession = WinHttpOpen(
        L"NetGuard/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        m_online = false;
        return;
    }

    std::wstring wHost(m_host.begin(), m_host.end());

    HINTERNET hConnect = WinHttpConnect(
        hSession, wHost.c_str(), m_port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        m_online = false;
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        L"/traffic",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        m_online = false;
        return;
    }

    WinHttpSetTimeouts(hRequest, 5000, 5000, 5000, 5000);

    if (!m_secret.empty()) {
        std::wstring auth = L"Authorization: Bearer " +
            std::wstring(m_secret.begin(), m_secret.end());
        WinHttpAddRequestHeaders(hRequest, auth.c_str(), static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(hRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        m_online = false;
        return;
    }

    m_online = true;
    std::string buffer;

    while (!m_stopFlag.load()) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) {
            break;
        }

        if (available == 0) {
            Sleep(50);
            continue;
        }

        std::vector<char> chunk(available + 1, 0);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) {
            break;
        }

        buffer.append(chunk.data(), read);

        size_t pos = 0;
        while (true) {
            const size_t nl = buffer.find('\n', pos);
            if (nl == std::string::npos) {
                break;
            }

            std::string line = buffer.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            uint64_t up = 0;
            uint64_t down = 0;
            if (parseSseLine(line, up, down)) {
                m_lastUp.store(up, std::memory_order_relaxed);
                m_lastDown.store(down, std::memory_order_relaxed);
            }

            pos = nl + 1;
        }

        buffer = buffer.substr(pos);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    m_online = false;
}

bool ClashApiProbe::start(const std::string& configPath) {
    m_configPath = configPath;
    if (!parseClashConfig(configPath, m_host, m_port, m_secret)) {
        m_online = false;
        return false;
    }
    return start(m_host, m_port, m_secret);
}

bool ClashApiProbe::start(const std::string& host,
                          uint16_t port,
                          const std::string& secret) {
    if (m_thread.joinable()) {
        m_stopFlag = true;
        m_thread.join();
    }

    if (host.empty() || port == 0) {
        m_online = false;
        return false;
    }

    m_host = host;
    m_port = port;
    m_secret = secret;
    m_stopFlag = false;
    m_online = false;
    m_lastUp = 0;
    m_lastDown = 0;

    m_thread = std::thread([this]() { sseLoop(); });

    for (int i = 0; i < 10 && !m_online.load(); ++i) {
        Sleep(100);
    }

    return m_online.load();
}

void ClashApiProbe::stop() {
    if (!m_thread.joinable()) {
        m_online = false;
        return;
    }

    m_stopFlag = true;
    m_thread.join();
    m_online = false;
}

bool ClashApiProbe::tryReconnect() {
    if (m_online.load()) {
        return true;
    }

    if (!m_configPath.empty()) {
        parseClashConfig(m_configPath, m_host, m_port, m_secret);
    }

    return start(m_host, m_port, m_secret);
}

} // namespace NetGuard
