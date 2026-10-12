#include "stdafx.h"
#include <WinSock2.h>
#include <Windows.h>
#include "WorldMonitor.h"
#include "HarborWorld.h"
#include "Profiler.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>

namespace Monitor
{
    namespace
    {
        constexpr unsigned short Port = 18765;
        constexpr std::uint64_t SnapshotIntervalMs = 1000;
        constexpr std::uint64_t SubscriptionTimeoutMs = 5000;

        std::uint64_t NowMs()
        {
            return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
        }

        std::string ReadAsset(const std::wstring& path)
        {
            std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
            if (!file || file.tellg() <= 0 || file.tellg() > 512 * 1024)
            {
                return {};
            }
            std::string contents(size_t(file.tellg()), '\0');
            file.seekg(0);
            file.read(&contents[0], std::streamsize(contents.size()));
            return file ? contents : std::string();
        }

        // Reject remote origins and DNS-rebinding hosts; no permissive CORS or write endpoints.
        bool LocalRequest(const std::string& request)
        {
            std::string host;
            std::string origin;
            size_t start = request.find("\r\n") + 2;
            while (start < request.size())
            {
                size_t end = request.find("\r\n", start);
                if (end == std::string::npos || end == start)
                {
                    break;
                }
                std::string field = request.substr(start, end - start);
                for (char& c : field)
                {
                    if (c >= 'A' && c <= 'Z')
                    {
                        c += 'a' - 'A';
                    }
                }
                size_t colon = field.find(':');
                if (colon == std::string::npos)
                {
                    return false;
                }
                std::string name = field.substr(0, colon);
                std::string value = field.substr(colon + 1);
                size_t first = value.find_first_not_of(" \t");
                value = first == std::string::npos ? "" : value.substr(first);
                size_t last = value.find_last_not_of(" \t");
                value = last == std::string::npos ? "" : value.substr(0, last + 1);
                if (name == "host")
                {
                    if (!host.empty())
                    {
                        return false;
                    }
                    host = value;
                }
                if (name == "origin")
                {
                    if (!origin.empty())
                    {
                        return false;
                    }
                    origin = value;
                }
                start = end + 2;
            }
            return (host == "127.0.0.1:18765" || host == "localhost:18765") &&
                   (origin.empty() || origin == "http://127.0.0.1:18765" ||
                    origin == "http://localhost:18765");
        }
    } // namespace

    struct Server::Impl
    {
        SOCKET listener = INVALID_SOCKET;
        bool winsock = false;
        bool started = false;
        std::thread worker;
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> lastRequest{0};
        std::atomic<std::uint64_t> requests{0}, responseBytes{0}, errors{0}, ioMicroseconds{0};
        std::mutex mutex;
        std::shared_ptr<const std::string> snapshot =
            std::make_shared<const std::string>("{\"schema_version\":1,\"status\":\"waiting\"}");
        std::string html, javascript, stylesheet;
        std::uint64_t startedAt = 0, lastSnapshot = 0, sequence = 0;
        double frameSeconds = 0;
        std::uint64_t frames = 0, drawCalls = 0;

        bool Send(SOCKET client, const std::string& text)
        {
            size_t sent = 0;
            const auto deadline = NowMs() + 1000;
            while (sent < text.size() && !stop && NowMs() < deadline)
            {
                int bytes = send(client, text.data() + sent, int(text.size() - sent), 0);
                if (bytes <= 0)
                {
                    ++errors;
                    return false;
                }
                sent += size_t(bytes);
                responseBytes += std::uint64_t(bytes);
            }
            if (sent != text.size())
            {
                ++errors;
                return false;
            }
            return true;
        }

        void Reply(SOCKET client, const char* status, const char* contentType, const std::string& body)
        {
            std::string header = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + contentType +
                                 "\r\nContent-Length: " + std::to_string(body.size()) +
                                 "\r\nConnection: close\r\nCache-Control: no-store\r\n"
                                 "X-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n"
                                 "Referrer-Policy: no-referrer\r\n"
                                 "Content-Security-Policy: default-src 'none'; script-src 'self'; "
                                 "style-src 'self'; connect-src 'self'; frame-ancestors 'none'\r\n\r\n";
            if (Send(client, header))
            {
                Send(client, body);
            }
        }

        void Handle(SOCKET client)
        {
            DWORD timeout = 200;
            setsockopt(
                client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(
                client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            std::string request;
            char buffer[2048];
            const auto deadline = NowMs() + 1000;
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192 &&
                   NowMs() < deadline && !stop)
            {
                int length = recv(client, buffer, sizeof(buffer), 0);
                if (length <= 0)
                {
                    ++errors;
                    return;
                }
                request.append(buffer, size_t(length));
            }
            ++requests;
            if (request.size() >= 8192 || request.find("\r\n\r\n") == std::string::npos)
            {
                ++errors;
                Reply(client, "400 Bad Request", "text/plain", "Invalid request");
                return;
            }
            if (!LocalRequest(request))
            {
                ++errors;
                Reply(client, "403 Forbidden", "text/plain", "Loopback origin required");
                return;
            }
            std::istringstream line(request.substr(0, request.find("\r\n")));
            std::string method, path, protocol;
            line >> method >> path >> protocol;
            if (method != "GET")
            {
                Reply(client, "405 Method Not Allowed", "text/plain", "Read-only monitor");
                return;
            }
            if (path == "/api/snapshot")
            {
                lastRequest = NowMs();
                std::shared_ptr<const std::string> current;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    current = snapshot;
                }
                Reply(client, "200 OK", "application/json; charset=utf-8", *current);
            }
            else if (path == "/" || path == "/index.html")
            {
                Reply(client, "200 OK", "text/html; charset=utf-8", html);
            }
            else if (path == "/monitor.js")
            {
                Reply(client, "200 OK", "text/javascript; charset=utf-8", javascript);
            }
            else if (path == "/monitor.css")
            {
                Reply(client, "200 OK", "text/css; charset=utf-8", stylesheet);
            }
            else if (path == "/favicon.ico")
            {
                Reply(client, "204 No Content", "text/plain", "");
            }
            else
            {
                Reply(client, "404 Not Found", "text/plain", "Not found");
            }
        }

        void Run()
        {
            while (!stop)
            {
                fd_set readable;
                FD_ZERO(&readable);
                FD_SET(listener, &readable);
                timeval timeout{0, 200000};
                int ready = select(0, &readable, nullptr, nullptr, &timeout);
                if (ready == SOCKET_ERROR)
                {
                    ++errors;
                    break;
                }
                if (ready == 0 || stop)
                {
                    continue;
                }
                SOCKET client = accept(listener, nullptr, nullptr);
                if (client == INVALID_SOCKET)
                {
                    ++errors;
                    continue;
                }
                auto begin = std::chrono::steady_clock::now();
                Handle(client);
                closesocket(client);
                ioMicroseconds += std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                    std::chrono::steady_clock::now() - begin)
                                                    .count());
            }
        }
    };

    Server::Server() : m_Impl(new Impl())
    {
    }

    Server::~Server()
    {
        Stop();
    }

    bool Server::Start()
    {
        auto& state = *m_Impl;
        if (state.started)
        {
            return true;
        }
        char setting[8]{};
        GetEnvironmentVariableA("SIMPLEGAME_MONITOR", setting, sizeof(setting));
        if (setting[0] == '0')
        {
            return false;
        }
        wchar_t path[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, path, 32768);
        if (!length || length >= 32768)
        {
            std::cerr << "[monitor] executable_path_failed\n";
            return false;
        }
        std::wstring directory(path, length);
        directory = directory.substr(0, directory.find_last_of(L"\\/") + 1) + L"Monitor\\";
        state.html = ReadAsset(directory + L"index.html");
        state.javascript = ReadAsset(directory + L"monitor.js");
        state.stylesheet = ReadAsset(directory + L"monitor.css");
        if (state.html.empty() || state.javascript.empty() || state.stylesheet.empty())
        {
            std::cerr << "[monitor] assets_missing; rebuild to copy Monitor assets next to executable\n";
            return false;
        }
        WSADATA data{};
        int error = WSAStartup(MAKEWORD(2, 2), &data);
        if (error)
        {
            std::cerr << "[monitor] winsock_start_failed code=" << error << '\n';
            return false;
        }
        state.winsock = true;
        state.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        BOOL exclusive = TRUE;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(Port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (state.listener == INVALID_SOCKET ||
            setsockopt(state.listener,
                       SOL_SOCKET,
                       SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char*>(&exclusive),
                       sizeof(exclusive)) == SOCKET_ERROR ||
            bind(state.listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
                SOCKET_ERROR ||
            listen(state.listener, 8) == SOCKET_ERROR)
        {
            std::cerr << "[monitor] listen_failed port=" << Port << " code=" << WSAGetLastError()
                      << "; game continues without monitor\n";
            Stop();
            return false;
        }
        state.stop = false;
        state.startedAt = NowMs();
        state.lastSnapshot = state.sequence = 0;
        state.lastRequest = 0;
        try
        {
            state.worker = std::thread(&Impl::Run, &state);
        }
        catch (const std::exception&)
        {
            std::cerr << "[monitor] worker_start_failed\n";
            Stop();
            return false;
        }
        state.started = true;
        std::cout << "[monitor] http://127.0.0.1:18765/ (read-only JSON, 1 second snapshots)\n";
        return true;
    }

    void Server::Stop()
    {
        auto& state = *m_Impl;
        state.stop = true;
        if (state.worker.joinable())
        {
            state.worker.join();
        }
        if (state.listener != INVALID_SOCKET)
        {
            closesocket(state.listener);
            state.listener = INVALID_SOCKET;
        }
        if (state.winsock)
        {
            WSACleanup();
            state.winsock = false;
        }
        state.started = false;
    }

    void Server::Tick(const Harbor::World& world, bool paused, double frameIntervalSeconds)
    {
        auto& state = *m_Impl;
        if (!state.started)
        {
            return;
        }
        // Perf owns frame-local arrays: collect worker atomics only on the game thread.
        Perf::Count(Perf::Counter::MonitorRequests, state.requests.exchange(0));
        Perf::Count(Perf::Counter::MonitorResponseBytes, state.responseBytes.exchange(0));
        Perf::Count(Perf::Counter::MonitorErrors, state.errors.exchange(0));
        Perf::Count(Perf::Counter::MonitorIoMicroseconds, state.ioMicroseconds.exchange(0));
        if (std::isfinite(frameIntervalSeconds) && frameIntervalSeconds > 0)
        {
            state.frameSeconds += frameIntervalSeconds;
            ++state.frames;
            state.drawCalls += Perf::Profiler::Get().Value(Perf::Counter::DrawCalls);
        }
        auto now = NowMs();
        if (state.lastSnapshot && now - state.lastSnapshot < SnapshotIntervalMs)
        {
            return;
        }
        auto requestAt = state.lastRequest.load();
        if (!requestAt || now - requestAt > SubscriptionTimeoutMs)
        {
            // No viewer: avoid encoding the world and keep rolling frame stats bounded.
            state.frameSeconds = 0;
            state.frames = state.drawCalls = 0;
            return;
        }
        Perf::Scope scope(Perf::Cpu::MonitorSnapshot);
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(2)
            << "{\"schema_version\":1,\"event\":\"monitor_snapshot\",\"status\":\"ready\",\"sequence\":"
            << ++state.sequence << ",\"session_id\":\"" << GetCurrentProcessId() << '_' << state.startedAt
            << "\",\"uptime_ms\":" << now - state.startedAt
            << ",\"snapshot_interval_ms\":" << SnapshotIntervalMs
            << ",\"paused\":" << (paused ? "true" : "false") << ",\"performance\":{\"fps\":";
        if (state.frameSeconds > 0 && state.frames > 1)
        {
            out << double(state.frames) / state.frameSeconds;
        }
        else
        {
            out << "null";
        }
        out << ",\"draw_calls_mean\":";
        if (state.frames && Perf::Profiler::Get().Enabled())
        {
            out << double(state.drawCalls) / double(state.frames);
        }
        else
        {
            out << "null";
        }
        out << "},\"world\":" << world.SnapshotJson() << '}';
        auto json = std::make_shared<const std::string>(out.str());
        Perf::Count(Perf::Counter::MonitorSnapshots);
        Perf::Count(Perf::Counter::MonitorSnapshotBytes, json->size());
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.snapshot = std::move(json);
        }
        state.lastSnapshot = now;
        state.frameSeconds = 0;
        state.frames = state.drawCalls = 0;
    }
} // namespace Monitor
