#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Dependencies/glew.h"

namespace Perf
{
    enum class Cpu
    {
        Frame,
        Update,
        Movement,
        AI,
        Navigation,
        CollisionRebuild,
        Interaction,
        Combat,
        Loot,
        Animation,
        ScenePrepare,
        SceneSort,
        RenderGround,
        RenderShadow,
        RenderWorld,
        RenderEffects,
        RenderUI,
        MiniMap,
        MiniMapRebuild,
        Text,
        GlyphRasterize,
        BatchSubmit,
        Postprocess,
        Swap,
        WorldGenerate,
        ModelLoad,
        ModelBuild,
        ModelSave,
        RendererInit,
        FontInit,
        WorldUpdate,
        NpcUpdate,
        NpcThink,
        NpcNavigation,
        NpcCombat,
        NpcTrade,
        StoryUpdate,
        WorldSnapshot,
        ChunkVisibility,
        QuestUpdate,
        MonitorSnapshot,
        TerrainAppearance,
        SceneryBuild,
        Lighting,
        CharacterBuild,
        RumorUpdate,
        VillageStatistics,
        EnemyRespawn,
        ResourceLoad,
        Count
    };
    enum class Counter
    {
        DrawCalls,
        Instances,
        Glyphs,
        UploadBytes,
        UploadCalls,
        BufferReuse,
        BatchCapacityFlush,
        TextureFlush,
        ActorsTotal,
        ActorsVisible,
        ActorsCulled,
        SceneVisits,
        SortItems,
        KindQueries,
        MiniMapCacheHit,
        MiniMapCacheMiss,
        GlyphCacheMiss,
        GlyphFailure,
        NavigationCells,
        CollisionTests,
        EnemiesUpdated,
        LootUpdated,
        ModelCacheHit,
        ModelCacheMiss,
        GpuSkipped,
        GpuPending,
        NpcsUpdated,
        NpcDecisions,
        NpcPathCells,
        NpcPathFailures,
        NpcCombatHits,
        NpcTrades,
        NpcRaidCoins,
        NpcDeaths,
        StoryEvents,
        ChunksVisible,
        WorldPropsAdded,
        NpcPathReuse,
        RaidAttemptsBlocked,
        QuestTransitions,
        TerrainTilesChanged,
        UiLabelsCulled,
        WorldSnapshotNpcs,
        MonitorSnapshots,
        MonitorSnapshotBytes,
        MonitorRequests,
        MonitorResponseBytes,
        MonitorErrors,
        MonitorIoMicroseconds,
        TerrainAppearanceTiles,
        ShorelineTiles,
        SceneryProps,
        LightsConsidered,
        LightsSelected,
        ShadowCasters,
        CharacterModelsBuilt,
        CharacterInstances,
        MirroredCharacterInstances,
        RumorCandidates,
        RumorTransfers,
        CourierDepartures,
        VillageMoodSamples,
        RespawnCandidates,
        EnemyRespawns,
        ResourceBytesRead,
        ResourceAtlasBytes,
        ResourceSpritesLoaded,
        ResourceLookupHit,
        ResourceLookupMiss,
        Count
    };

    class Profiler
    {
      public:
        static Profiler& Get();
        ~Profiler();
        void Initialize();
        void Shutdown();
        void BeginFrame();
        void EndFrame(const char* event = "frame");
        void Add(Cpu key, double ms);

        std::uint64_t Value(Counter key) const
        {
            return m_Counts[size_t(key)];
        }

        void AddWork(Cpu key, std::uint64_t draws, std::uint64_t bytes)
        {
            m_ScopeDraws[size_t(key)] += draws;
            m_ScopeBytes[size_t(key)] += bytes;
        }

        void SetSceneState(
            std::uint32_t seed, float x, float y, double dt, bool paused, int width, int height);
        void Count(Counter key, std::uint64_t amount = 1);
        void GpuMark(const char* name);
        void WorldSnapshot(const std::string& state);

        bool Enabled() const
        {
            return m_Enabled;
        }

      private:
        void Queue(std::string line);
        void Writer();
        void PollGpu();

        struct GpuFrame
        {
            std::array<GLuint, 12> queries{};
            std::array<const char*, 12> labels{};
            unsigned count = 0;
            std::uint64_t frame = 0;
            bool pending = false;
        };

        bool m_Enabled = true;
        bool m_Initialized = false;
        bool m_GpuEnabled = false;
        bool m_HaveRenderedFrame = false;
        std::string m_State = "{}";
        std::string m_SessionHeader;
        std::string m_SessionId;
        std::wstring m_FileStem;
        unsigned m_FileSegment = 0;
        std::uint64_t m_Rotation = 0;
        std::array<std::uint64_t, size_t(Cpu::Count)> m_ScopeDraws{}, m_ScopeBytes{};
        std::atomic<std::uint64_t> m_WriterMicroseconds{0};
        std::uint64_t m_Frame = 0;
        std::array<double, size_t(Cpu::Count)> m_Cpu{};
        std::array<std::uint64_t, size_t(Cpu::Count)> m_Calls{};
        std::array<std::uint64_t, size_t(Counter::Count)> m_Counts{};
        std::array<GpuFrame, 8> m_Gpu{};
        GpuFrame* m_CurrentGpu = nullptr;
        std::chrono::steady_clock::time_point m_Start{}, m_Previous{}, m_SummaryStart{};
        double m_Interval = 0;
        std::vector<double> m_FrameTimes;
        std::ofstream m_File;
        std::thread m_Writer;
        std::mutex m_Mutex;
        std::condition_variable m_Wake;
        std::deque<std::string> m_Lines;
        bool m_Stop = false;
        std::atomic<std::uint64_t> m_Dropped{0};
        std::atomic<bool> m_FileFailed{false};
        std::uint64_t m_FileBytes = 0;
    };

    class Scope
    {
      public:
        explicit Scope(Cpu key) : m_Key(key), m_Enabled(Profiler::Get().Enabled())
        {
            if (m_Enabled)
            {
                m_Start = std::chrono::steady_clock::now();
                m_DrawStart = Profiler::Get().Value(Counter::DrawCalls);
                m_ByteStart = Profiler::Get().Value(Counter::UploadBytes);
            }
        }

        ~Scope()
        {
            if (m_Enabled)
            {
                Profiler::Get().AddWork(m_Key,
                                        Profiler::Get().Value(Counter::DrawCalls) - m_DrawStart,
                                        Profiler::Get().Value(Counter::UploadBytes) - m_ByteStart);
                Profiler::Get().Add(
                    m_Key,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start)
                        .count());
            }
        }

      private:
        std::uint64_t m_DrawStart = 0, m_ByteStart = 0;
        Cpu m_Key;
        bool m_Enabled;
        std::chrono::steady_clock::time_point m_Start;
    };

    inline void Count(Counter key, std::uint64_t amount = 1)
    {
        Profiler::Get().Count(key, amount);
    }
} // namespace Perf
