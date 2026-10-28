#include "stdafx.h"
#include "Profiler.h"
#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>

namespace Perf
{
    namespace
    {
        const char* CpuNames[] = {"frame",
                                  "update",
                                  "movement",
                                  "ai",
                                  "navigation",
                                  "collision_rebuild",
                                  "interaction",
                                  "combat",
                                  "loot",
                                  "animation",
                                  "scene_prepare",
                                  "scene_sort",
                                  "render_ground",
                                  "render_shadow",
                                  "render_world",
                                  "render_effects",
                                  "render_ui",
                                  "minimap",
                                  "minimap_rebuild",
                                  "text",
                                  "glyph_rasterize",
                                  "batch_submit",
                                  "postprocess",
                                  "swap_wait",
                                  "world_generate",
                                  "model_load",
                                  "model_build",
                                  "model_save",
                                  "renderer_init",
                                  "font_init",
                                  "world_update",
                                  "npc_update",
                                  "npc_think",
                                  "npc_navigation",
                                  "npc_combat",
                                  "npc_trade",
                                  "story_update",
                                  "world_snapshot",
                                  "chunk_visibility",
                                  "quest_update",
                                  "monitor_snapshot",
                                  "terrain_appearance",
                                  "scenery_build",
                                  "lighting",
                                  "character_build",
                                  "rumor_update",
                                  "village_statistics",
                                  "enemy_respawn",
                                  "resource_load",
                                  "enemy_combat"};
        const char* CounterNames[] = {"draw_calls",
                                      "quad_instances",
                                      "glyph_instances",
                                      "upload_bytes",
                                      "upload_calls",
                                      "gpu_buffer_reuse",
                                      "batch_capacity_flush",
                                      "texture_change_flush",
                                      "actors_total",
                                      "actors_visible",
                                      "actors_culled",
                                      "scene_nodes_visited",
                                      "sort_items",
                                      "kind_queries",
                                      "minimap_cache_hit",
                                      "minimap_cache_miss",
                                      "glyph_cache_miss",
                                      "glyph_failure",
                                      "navigation_cells",
                                      "collision_tests",
                                      "enemies_updated",
                                      "loot_updated",
                                      "model_cache_hit",
                                      "model_cache_miss",
                                      "gpu_sample_skipped",
                                      "gpu_samples_pending",
                                      "npcs_updated",
                                      "npc_decisions",
                                      "npc_path_cells",
                                      "npc_path_failures",
                                      "npc_combat_hits",
                                      "npc_trades",
                                      "npc_raid_coins",
                                      "npc_deaths",
                                      "story_events",
                                      "chunks_visible",
                                      "world_props_added",
                                      "npc_path_reuse",
                                      "raid_attempts_blocked",
                                      "quest_transitions",
                                      "terrain_tiles_changed",
                                      "ui_labels_culled",
                                      "world_snapshot_npcs",
                                      "monitor_snapshots",
                                      "monitor_snapshot_bytes",
                                      "monitor_requests",
                                      "monitor_response_bytes",
                                      "monitor_errors",
                                      "monitor_io_microseconds",
                                      "terrain_appearance_tiles",
                                      "shoreline_tiles",
                                      "scenery_props",
                                      "lights_considered",
                                      "lights_selected",
                                      "shadow_casters",
                                      "character_models_built",
                                      "character_instances",
                                      "mirrored_character_instances",
                                      "rumor_candidates",
                                      "rumor_transfers",
                                      "courier_departures",
                                      "village_mood_samples",
                                      "respawn_candidates",
                                      "enemy_respawns",
                                      "resource_bytes_read",
                                      "resource_atlas_bytes",
                                      "resource_sprites_loaded",
                                      "resource_lookup_hit",
                                      "resource_lookup_miss",
                                      "enemy_attacks_started",
                                      "enemy_attack_updates",
                                      "enemy_attack_impacts",
                                      "enemy_attack_hits",
                                      "enemy_attack_misses",
                                      "enemy_attacks_canceled"};
        static_assert(sizeof(CpuNames) / sizeof(*CpuNames) == size_t(Cpu::Count), "CPU schema mismatch");
        static_assert(sizeof(CounterNames) / sizeof(*CounterNames) == size_t(Counter::Count),
                      "Counter schema mismatch");

        std::string JsonString(const char* text)
        {
            std::string result = "\"";
            for (const unsigned char* c = reinterpret_cast<const unsigned char*>(text ? text : "unavailable");
                 *c;
                 ++c)
            {
                if (*c == '"' || *c == '\\')
                {
                    result += '\\';
                }
                if (*c >= 32)
                {
                    result += char(*c);
                }
            }
            return result + "\"";
        }

        double Ms(std::chrono::steady_clock::duration d)
        {
            return std::chrono::duration<double, std::milli>(d).count();
        }
    } // namespace

    Profiler& Profiler::Get()
    {
        static Profiler instance;
        return instance;
    }

    void Profiler::Initialize()
    {
        if (m_Initialized)
        {
            return;
        }
        m_Initialized = true;
        m_SessionId = std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64());
        char setting[8]{};
        GetEnvironmentVariableA("SIMPLEGAME_PROFILE", setting, sizeof(setting));
        m_Enabled = setting[0] != '0';
        if (!m_Enabled)
        {
            return;
        }
        wchar_t path[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, path, 32768);
        std::wstring directory(path, length < 32768 ? length : 0);
        directory = directory.substr(0, directory.find_last_of(L"\\/") + 1) + L"Profiles";
        CreateDirectoryW(directory.c_str(), nullptr);
        SYSTEMTIME now{};
        GetSystemTime(&now);
        std::wostringstream name;
        name << directory << L"\\profile_" << now.wYear << L'_' << now.wMonth << L'_' << now.wDay << L'_'
             << now.wHour << L'_' << now.wMinute << L'_' << now.wSecond << L'_' << GetCurrentProcessId()
             << L"_";
        m_FileStem = name.str();
        m_File.open((m_FileStem + L"0.jsonl").c_str(), std::ios::binary);
        m_FileFailed = !m_File;
        std::wcout << L"[profile] log_path=" << m_FileStem << L"0.jsonl (4 rotating segments)\n";
        if (m_FileFailed)
        {
            std::cerr << "[profile] file_open_failed; console summaries remain enabled\n";
        }
        GLint timerBits = 0;
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &timerBits);
        m_GpuEnabled = timerBits > 0;
        for (auto& slot : m_Gpu)
        {
            if (!m_GpuEnabled)
            {
                break;
            }
            glGenQueries(GLsizei(slot.queries.size()), slot.queries.data());
        }
        m_SummaryStart = std::chrono::steady_clock::now();
        std::ostringstream metadata;
        metadata
            << "{\"schema_version\":10,\"event\":\"session\",\"build\":"
#ifdef _DEBUG
            << "\"debug\""
#else
            << "\"release\""
#endif
            << ",\"gpu_vendor\":" << JsonString(reinterpret_cast<const char*>(glGetString(GL_VENDOR)))
            << ",\"gpu_renderer\":" << JsonString(reinterpret_cast<const char*>(glGetString(GL_RENDERER)))
            << ",\"gl_version\":" << JsonString(reinterpret_cast<const char*>(glGetString(GL_VERSION)))
            << ",\"cpu_scope_semantics\":\"inclusive_nested_do_not_sum\",\"retention_bytes_per_session\":67108864,\"segment_bytes\":16777216,\"segment_count\":4}";
        m_SessionHeader = metadata.str();
        m_SessionHeader.insert(1, "\"session_id\":" + JsonString(m_SessionId.c_str()) + ",");
        m_Writer = std::thread(&Profiler::Writer, this);
        Queue(m_SessionHeader);
    }

    Profiler::~Profiler()
    {
        // Drain CPU logging without making GL calls if the context was lost.
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stop = true;
        }
        m_Wake.notify_one();
        if (m_Writer.joinable())
        {
            m_Writer.join();
        }
    }

    void Profiler::SetSceneState(
        std::uint32_t seed, float x, float y, double dt, bool paused, int width, int height)
    {
        if (!m_Enabled)
        {
            return;
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "{\"world_seed\":" << seed << ",\"player_x\":" << x << ",\"player_y\":" << y
            << ",\"simulation_dt_ms\":" << dt * 1000 << ",\"paused\":" << (paused ? "true" : "false")
            << ",\"output_width\":" << width << ",\"output_height\":" << height << '}';
        m_State = out.str();
    }

    void Profiler::Add(Cpu key, double ms)
    {
        if (!m_Enabled)
        {
            return;
        }
        m_Cpu[size_t(key)] += ms;
        ++m_Calls[size_t(key)];
    }

    void Profiler::Count(Counter key, std::uint64_t amount)
    {
        if (m_Enabled)
        {
            m_Counts[size_t(key)] += amount;
        }
    }

    void Profiler::BeginFrame()
    {
        if (!m_Enabled || !m_Initialized)
        {
            return;
        }
        m_Cpu.fill(0);
        m_Calls.fill(0);
        m_ScopeDraws.fill(0);
        m_ScopeBytes.fill(0);
        m_Counts.fill(0);
        m_Start = std::chrono::steady_clock::now();
        m_Interval = m_Frame ? Ms(m_Start - m_Previous) : 0;
        m_Previous = m_Start;
        ++m_Frame;
        PollGpu();
        auto& slot = m_Gpu[m_Frame % m_Gpu.size()];
        m_CurrentGpu = !m_GpuEnabled || slot.pending ? nullptr : &slot;
        if (m_CurrentGpu)
        {
            slot.count = 0;
            slot.frame = m_Frame;
        }
        else if (m_GpuEnabled)
        {
            Count(Counter::GpuSkipped);
        }
    }

    void Profiler::GpuMark(const char* name)
    {
        if (!m_Enabled || !m_CurrentGpu || m_CurrentGpu->count == m_CurrentGpu->queries.size())
        {
            return;
        }
        auto i = m_CurrentGpu->count++;
        m_CurrentGpu->labels[i] = name;
        glQueryCounter(m_CurrentGpu->queries[i], GL_TIMESTAMP);
    }

    void Profiler::PollGpu()
    {
        for (auto& slot : m_Gpu)
        {
            if (!slot.pending)
            {
                continue;
            }
            GLint ready = GL_FALSE;
            glGetQueryObjectiv(slot.queries[slot.count - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready)
            {
                Count(Counter::GpuPending);
                continue;
            }
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::fixed << std::setprecision(4)
                << "{\"schema_version\":10,\"event\":\"gpu_frame\",\"frame_id\":" << slot.frame
                << ",\"readback_delay_frames\":" << m_Frame - slot.frame << ",\"gpu_interval_ms\":{";
            GLuint64 previous = 0, first = 0, stamp = 0;
            for (unsigned i = 0; i < slot.count; ++i)
            {
                glGetQueryObjectui64v(slot.queries[i], GL_QUERY_RESULT, &stamp);
                if (i)
                {
                    out << (i > 1 ? "," : "") << JsonString(slot.labels[i]) << ':'
                        << double(stamp - previous) / 1e6;
                }
                else
                {
                    first = stamp;
                }
                previous = stamp;
            }
            out << "},\"gpu_timeline_ms\":" << double(stamp - first) / 1e6 << '}';
            Queue(out.str());
            slot.pending = false;
        }
    }

    void Profiler::EndFrame(const char* event)
    {
        if (!m_Enabled || !m_Initialized)
        {
            return;
        }
        if (m_CurrentGpu && m_CurrentGpu->count)
        {
            // Last timestamp is submitted before SwapBuffers; presentation wait is a CPU scope.
            m_CurrentGpu->pending = true;
            m_CurrentGpu = nullptr;
        }
        const bool isFrame = std::string(event) == "frame";
        if (!m_HaveRenderedFrame)
        {
            m_Interval = 0;
        }
        if (isFrame)
        {
            m_HaveRenderedFrame = true;
        }
        Add(Cpu::Frame, Ms(std::chrono::steady_clock::now() - m_Start));
        auto encodeStart = std::chrono::steady_clock::now();
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(4) << "{\"schema_version\":10,\"event\":" << JsonString(event)
            << ",\"frame_id\":" << m_Frame << ",\"frame_interval_ms\":" << m_Interval
            << ",\"over_budget_60fps\":" << (m_Interval > 16.6667 ? "true" : "false") << ",\"cpu_ms\":{";
        for (size_t i = 0; i < size_t(Cpu::Count); ++i)
        {
            out << (i ? "," : "") << JsonString(CpuNames[i]) << ':' << m_Cpu[i];
        }
        out << "},\"scope_calls\":{";
        for (size_t i = 0; i < m_Calls.size(); ++i)
        {
            out << (i ? "," : "") << JsonString(CpuNames[i]) << ':' << m_Calls[i];
        }
        out << "},\"scope_draw_calls\":{";
        for (size_t i = 0; i < m_ScopeDraws.size(); ++i)
        {
            out << (i ? "," : "") << JsonString(CpuNames[i]) << ':' << m_ScopeDraws[i];
        }
        out << "},\"scope_upload_bytes\":{";
        for (size_t i = 0; i < m_ScopeBytes.size(); ++i)
        {
            out << (i ? "," : "") << JsonString(CpuNames[i]) << ':' << m_ScopeBytes[i];
        }
        out << "},\"counters\":{";
        for (size_t i = 0; i < m_Counts.size(); ++i)
        {
            out << (i ? "," : "") << JsonString(CounterNames[i]) << ':' << m_Counts[i];
        }
        out << "},\"scene_state\":" << m_State
            << ",\"gpu_profiling_available\":" << (m_GpuEnabled ? "true" : "false")
            << ",\"log_writer_ms_total\":" << double(m_WriterMicroseconds.load()) / 1000
            << ",\"log_dropped_total\":" << m_Dropped.load()
            << ",\"log_file_failed\":" << (m_FileFailed ? "true" : "false")
            << ",\"log_encode_ms\":" << Ms(std::chrono::steady_clock::now() - encodeStart) << '}';
        Queue(out.str());

        if (isFrame && m_Interval > 0)
        {
            m_FrameTimes.push_back(m_Interval);
        }
        auto now = std::chrono::steady_clock::now();
        if (Ms(now - m_SummaryStart) >= 1000 && !m_FrameTimes.empty())
        {
            double sum = 0;
            for (double value : m_FrameTimes)
            {
                sum += value;
            }
            std::sort(m_FrameTimes.begin(), m_FrameTimes.end());
            PROCESS_MEMORY_COUNTERS memory{};
            bool memoryValid = GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)) != FALSE;
            std::ostringstream summary;
            summary.imbue(std::locale::classic());
            summary << std::fixed << std::setprecision(3)
                    << "{\"schema_version\":10,\"event\":\"summary\",\"frame_id\":" << m_Frame
                    << ",\"fps\":" << 1000 * m_FrameTimes.size() / sum
                    << ",\"frame_ms_mean\":" << sum / m_FrameTimes.size()
                    << ",\"frame_ms_p95\":" << m_FrameTimes[(m_FrameTimes.size() * 95 + 99) / 100 - 1]
                    << ",\"frame_ms_max\":" << m_FrameTimes.back()
                    << ",\"draw_calls_last\":" << m_Counts[size_t(Counter::DrawCalls)]
                    << ",\"working_set_bytes\":"
                    << (memoryValid ? std::to_string(memory.WorkingSetSize) : "null")
                    << ",\"log_dropped_total\":" << m_Dropped.load()
                    << ",\"log_file_failed\":" << (m_FileFailed ? "true" : "false") << '}';
            Queue(summary.str());
            std::cout << summary.str() << '\n';
            m_FrameTimes.clear();
            m_SummaryStart = now;
        }
    }

    void Profiler::WorldSnapshot(const std::string& state)
    {
        if (m_Enabled && m_Initialized)
        {
            Queue("{\"schema_version\":10,\"event\":\"world_state\",\"frame_id\":" + std::to_string(m_Frame) +
                  ",\"world\":" + state + "}");
        }
    }

    void Profiler::Queue(std::string line)
    {
        if (line.find("\"session_id\"") == std::string::npos)
        {
            line.insert(1, "\"session_id\":" + JsonString(m_SessionId.c_str()) + ",");
        }
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Lines.size() >= 512 || m_FileFailed)
        {
            ++m_Dropped;
            return;
        }
        m_Lines.push_back(std::move(line));
        if (m_Lines.size() >= 64)
        {
            m_Wake.notify_one();
        }
    }

    void Profiler::Writer()
    {
        for (;;)
        {
            std::deque<std::string> lines;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_Wake.wait_for(lock,
                                std::chrono::milliseconds(250),
                                [&]
                                {
                                    return m_Stop || m_Lines.size() >= 64;
                                });
                lines.swap(m_Lines);
                if (m_Stop && lines.empty())
                {
                    break;
                }
            }
            auto writeStart = std::chrono::steady_clock::now();
            for (const auto& line : lines)
            {
                if (!m_FileFailed && m_FileBytes + line.size() + 1 > 16ull * 1024 * 1024)
                {
                    m_File.close();
                    m_FileSegment = (m_FileSegment + 1) % 4;
                    ++m_Rotation;
                    m_File.open((m_FileStem + std::to_wstring(m_FileSegment) + L".jsonl").c_str(),
                                std::ios::binary | std::ios::trunc);
                    m_FileFailed = !m_File;
                    const std::string marker =
                        "{\"schema_version\":10,\"session_id\":" + JsonString(m_SessionId.c_str()) +
                        ",\"event\":\"log_segment\",\"rotation\":" + std::to_string(m_Rotation) + "}";
                    m_File << m_SessionHeader << '\n' << marker << '\n';
                    m_FileBytes = m_SessionHeader.size() + marker.size() + 2;
                }
                if (m_FileFailed)
                {
                    m_FileFailed = true;
                    ++m_Dropped;
                    continue;
                }
                m_File << line << '\n';
                m_FileBytes += line.size() + 1;
                if (!m_File)
                {
                    m_FileFailed = true;
                }
            }
            // Buffered writes on the worker only; never flush from the render thread.
            m_File.flush();
            if (!m_File)
            {
                m_FileFailed = true;
            }
            m_WriterMicroseconds += std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                      std::chrono::steady_clock::now() - writeStart)
                                                      .count());
        }
    }

    void Profiler::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }
        if (m_Enabled)
        {
            PollGpu();
            std::uint64_t pending = 0;
            for (auto& slot : m_Gpu)
            {
                if (slot.pending)
                {
                    ++pending;
                }
                if (m_GpuEnabled)
                {
                    glDeleteQueries(GLsizei(slot.queries.size()), slot.queries.data());
                }
            }
            Queue("{\"schema_version\":10,\"event\":\"session_end\",\"gpu_unresolved_frames\":" +
                  std::to_string(pending) + "}");
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Stop = true;
            }
            m_Wake.notify_one();
            if (m_Writer.joinable())
            {
                m_Writer.join();
            }
            m_File.close();
        }
        m_Initialized = false;
        m_Enabled = false;
    }
} // namespace Perf
