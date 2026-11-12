#include "stdafx.h"
#include "GameSave.h"
#include "HarborWorld.h"
#include "Profiler.h"

#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <tuple>

namespace
{
    using Result = GameSave::Result;
    using Position = Level::Position;
    constexpr int SaveVersion = 1;
    constexpr DWORD MaxSaveBytes = 512 * 1024;
    constexpr int NpcCount = WorldLayout::VillageCount * WorldLayout::NpcsPerVillage;
    constexpr int MaxDrops = 256;
    constexpr int MaxEvents = 6;
    constexpr int OnGround = -1;
    constexpr int WithPlayer = -2;

    std::uint64_t PayloadChecksum(const std::wstring& text)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (wchar_t character : text)
        {
            hash ^= std::uint16_t(character);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::filesystem::path SlotPath()
    {
        std::wstring path(32768, L'\0');
        const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), DWORD(path.size()));
        if (size == 0 || size >= path.size())
        {
            return {};
        }
        path.resize(size);
        return std::filesystem::path(path) / L"Haedong" / L"Saves" / L"slot1.save";
    }

    Result WriteSlot(const std::filesystem::path& path, const std::wstring& content)
    {
        const int size = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, content.data(), int(content.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0 || DWORD(size) > MaxSaveBytes)
        {
            return Result::Invalid;
        }
        std::string bytes(size, '\0');
        if (WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                content.data(),
                                int(content.size()),
                                bytes.data(),
                                size,
                                nullptr,
                                nullptr) != size)
        {
            return Result::Invalid;
        }

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            return Result::IoError;
        }
        auto temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE file = CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return Result::IoError;
        }
        DWORD written = 0;
        const bool flushed = WriteFile(file, bytes.data(), DWORD(size), &written, nullptr) &&
                             written == DWORD(size) && FlushFileBuffers(file);
        const bool closed = CloseHandle(file);
        if (!flushed || !closed ||
            !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            return Result::IoError;
        }
        Perf::Count(Perf::Counter::SaveBytesWritten, written);
        return Result::Success;
    }

    Result ReadSlot(const std::filesystem::path& path, std::wstring& content)
    {
        HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Result::Missing
                                                                                  : Result::IoError;
        }
        LARGE_INTEGER length{};
        const bool sized = GetFileSizeEx(file, &length);
        if (!sized || length.QuadPart <= 0 || length.QuadPart > MaxSaveBytes)
        {
            CloseHandle(file);
            return sized ? Result::Invalid : Result::IoError;
        }
        std::string bytes(size_t(length.QuadPart), '\0');
        DWORD read = 0;
        const bool loaded = ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr);
        CloseHandle(file);
        if (!loaded || read != bytes.size())
        {
            return Result::IoError;
        }
        Perf::Count(Perf::Counter::SaveBytesRead, read);
        const int size =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(read), nullptr, 0);
        if (size <= 0)
        {
            return Result::Invalid;
        }
        content.resize(size);
        return MultiByteToWideChar(
                   CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(read), content.data(), size) == size
                   ? Result::Success
                   : Result::Invalid;
    }

    bool InMap(const Position& p)
    {
        return p.x >= 0 && p.y >= 0 && p.x < Level::MapSize && p.y < Level::MapSize;
    }

    bool InRange(double value, double minimum, double maximum)
    {
        return value >= minimum && value <= maximum;
    }

    template <class... Int> bool ValidCounts(Int... values)
    {
        return ((values >= 0 && values <= 1000000000) && ...);
    }
} // namespace

struct GameSave::State
{
    struct Resident
    {
        Position position, lastLedger;
        std::wstring line;
        int health = 0, maxHealth = 0, coins = 0, hides = 0, food = 0;
        int affinity = 0, karma = 0, experience = 0, enlightenment = 0, knowledge = 0;
        int travelVillage = 0, rumorsReceived = 0, direction = 0;
        float courierWait = 0, raidCooldown = 0;
        double lastRumorMinute = 0;
        int lastRumorSender = OnGround, knownLedgerHolder = OnGround;

        template <class T> static auto Values(T& n)
        {
            return std::tie(n.health,
                            n.maxHealth,
                            n.coins,
                            n.hides,
                            n.food,
                            n.affinity,
                            n.karma,
                            n.experience,
                            n.enlightenment,
                            n.knowledge,
                            n.travelVillage,
                            n.rumorsReceived,
                            n.courierWait,
                            n.raidCooldown,
                            n.lastRumorMinute,
                            n.lastLedger,
                            n.line);
        }
    };

    struct Artifact
    {
        Position position;
        int owner = OnGround;
    };

    struct Drop
    {
        Position position;
        int kind = 0, amount = 0;
        float lifetime = 0;
    };

    std::uint32_t seed = 0;
    Position player;
    float facingX = 1, facingY = 0;
    int direction = 0;
    Level::Stats stats;
    std::array<Resident, NpcCount> residents;
    std::array<Artifact, 2> artifacts;
    std::array<Drop, MaxDrops> drops;
    int dropCount = 0;
    std::array<std::wstring, MaxEvents> events;
    int eventCount = 0;
    std::array<int, 4> trust{};
    std::wstring consequence;
    double minutes = 0, drainReportedAt = 0;
    bool gateOpen = false, published = false, controlled = false, repaired = false;
    bool copied = false, ledgerWet = false, briefed = false, drainInspected = false;
    bool recoveredLedger = false, recoveredKey = false, returned = false, dealSettled = false;
    bool barrierBlocks = false;
    int hideJob = 0, recordChoice = 0, trailNotice = 0, flood = 0;
    unsigned long accounts = 0;
    size_t trailClues = 0;

    template <class T> static auto StoryValues(T& s)
    {
        return std::tie(s.minutes,
                        s.drainReportedAt,
                        s.gateOpen,
                        s.published,
                        s.controlled,
                        s.repaired,
                        s.copied,
                        s.ledgerWet,
                        s.briefed,
                        s.drainInspected,
                        s.recoveredLedger,
                        s.recoveredKey,
                        s.returned,
                        s.dealSettled,
                        s.flood,
                        s.trailClues,
                        s.trust,
                        s.consequence);
    }

    template <class Visit> bool Fields(Visit visit)
    {
        if (!visit(seed,
                   player.x,
                   player.y,
                   direction,
                   facingX,
                   facingY,
                   stats.level,
                   stats.experience,
                   stats.totalExperience,
                   stats.points,
                   stats.strength,
                   stats.vitality,
                   stats.resilience,
                   stats.health,
                   stats.potions,
                   stats.hides,
                   stats.coins,
                   stats.kills) ||
            !visit(minutes,
                   drainReportedAt,
                   gateOpen,
                   published,
                   controlled,
                   repaired,
                   copied,
                   ledgerWet,
                   briefed,
                   drainInspected,
                   recoveredLedger,
                   recoveredKey,
                   returned,
                   dealSettled,
                   barrierBlocks,
                   hideJob,
                   recordChoice,
                   trailNotice,
                   flood,
                   accounts,
                   trailClues,
                   trust[0],
                   trust[1],
                   trust[2],
                   trust[3],
                   std::quoted(consequence),
                   eventCount,
                   dropCount) ||
            eventCount < 0 || eventCount > MaxEvents || dropCount < 0 || dropCount > MaxDrops)
        {
            return false;
        }
        for (auto& a : artifacts)
        {
            if (!visit(a.owner, a.position.x, a.position.y))
            {
                return false;
            }
        }
        for (auto& n : residents)
        {
            if (!visit(n.position.x,
                       n.position.y,
                       n.direction,
                       n.health,
                       n.maxHealth,
                       n.coins,
                       n.hides,
                       n.food,
                       n.affinity,
                       n.karma,
                       n.experience,
                       n.enlightenment,
                       n.knowledge,
                       n.travelVillage,
                       n.courierWait,
                       n.raidCooldown,
                       n.rumorsReceived,
                       n.lastRumorMinute,
                       n.lastRumorSender,
                       n.knownLedgerHolder,
                       n.lastLedger.x,
                       n.lastLedger.y,
                       std::quoted(n.line)))
            {
                return false;
            }
        }
        for (int i = 0; i < dropCount; ++i)
        {
            auto& d = drops[i];
            if (!visit(d.position.x, d.position.y, d.kind, d.amount, d.lifetime))
            {
                return false;
            }
        }
        for (int i = 0; i < eventCount; ++i)
        {
            if (!visit(std::quoted(events[i])))
            {
                return false;
            }
        }
        return true;
    }

    bool Valid() const
    {
        if (!InMap(player) || !InRange(direction, 0, 7) ||
            !InRange(std::hypot(facingX, facingY), .99, 1.01) ||
            !InRange(stats.level, 1, Level::MaximumLevel) ||
            !ValidCounts(stats.experience,
                         stats.totalExperience,
                         stats.points,
                         stats.strength,
                         stats.vitality,
                         stats.resilience,
                         stats.potions,
                         stats.hides,
                         stats.coins,
                         stats.kills) ||
            std::int64_t(stats.points) + stats.strength + stats.vitality + stats.resilience !=
                (stats.level - 1) * 2 ||
            stats.experience >= stats.RequiredExperience() || !InRange(stats.health, 1, stats.MaxHealth()) ||
            !InRange(minutes, 360, 100000000) || !InRange(drainReportedAt, 360, minutes) ||
            !InRange(hideJob, 0, 2) || !InRange(recordChoice, 0, 3) || !InRange(trailNotice, 0, 2) ||
            !InRange(flood, 0, 100) || accounts > 15 || trailClues > 3 || consequence.size() > 2048)
        {
            return false;
        }
        if ((briefed && hideJob != 2) || (drainInspected && !briefed) ||
            ((recoveredLedger || recoveredKey) && !drainInspected) || (copied && !recoveredLedger) ||
            (accounts && (!copied || !recoveredKey)) || (recordChoice && accounts != 15) ||
            (gateOpen && !recoveredKey) || published != (recordChoice == int(Harbor::RecordChoice::Public)) ||
            (controlled && recordChoice != int(Harbor::RecordChoice::Authority)) ||
            (barrierBlocks && !controlled) || (repaired && !briefed) ||
            (dealSettled && recordChoice != int(Harbor::RecordChoice::Sold)) ||
            (returned && (!gateOpen || !recordChoice)) || (trailClues && !returned) ||
            (trailNotice && trailClues != 3))
        {
            return false;
        }
        if ((!recoveredLedger && artifacts[0].owner != OnGround) ||
            (!recoveredKey && artifacts[1].owner != OnGround))
        {
            return false;
        }
        for (const auto& a : artifacts)
        {
            if (!InMap(a.position) || !InRange(a.owner, WithPlayer, NpcCount - 1) ||
                (a.owner >= 0 && residents[a.owner].health <= 0))
            {
                return false;
            }
        }
        for (const auto& n : residents)
        {
            if (!InMap(n.position) || !InMap(n.lastLedger) || !InRange(n.direction, 0, 7) ||
                !InRange(n.maxHealth, 1, 100000000) || !InRange(n.health, 0, n.maxHealth) ||
                !ValidCounts(n.coins, n.hides, n.food, n.experience, n.enlightenment, n.rumorsReceived) ||
                !InRange(n.affinity, -100, 100) || !InRange(n.karma, -100, 100) ||
                !InRange(n.knowledge, 0, 63) || !InRange(n.travelVillage, 0, WorldLayout::VillageCount - 1) ||
                !InRange(n.courierWait, 0, 8) || !InRange(n.raidCooldown, 0, 180) ||
                !InRange(n.lastRumorMinute, 0, minutes) ||
                !InRange(n.lastRumorSender, OnGround, NpcCount - 1) ||
                !InRange(n.knownLedgerHolder, WithPlayer, NpcCount - 1) || n.line.size() > 2048)
            {
                return false;
            }
        }
        for (int value : trust)
        {
            if (!InRange(value, -100, 100))
            {
                return false;
            }
        }
        for (int i = 0; i < dropCount; ++i)
        {
            const auto& d = drops[i];
            if (!InMap(d.position) || !InRange(d.kind, 0, int(Level::LootKind::Hide)) ||
                !InRange(d.amount, 1, 7) || !InRange(d.lifetime, 0, 90))
            {
                return false;
            }
        }
        for (int i = 0; i < eventCount; ++i)
        {
            if (events[i].size() > 2048)
            {
                return false;
            }
        }
        return true;
    }
};

bool GameSave::Capture(State& state, const Level::FirstLevel& level, const Harbor::World& world)
{
    const auto& scene = level.Scene();
    const auto number = [&](ActorId id)
    {
        if (id == level.Player().Id())
        {
            return WithPlayer;
        }
        const auto* resident = dynamic_cast<const Harbor::Npc*>(scene.Find(id));
        return resident ? resident->variant : OnGround;
    };

    state.seed = level.seed;
    state.player = level.Player().WorldPosition();
    state.direction = level.Player().visualDirection;
    state.facingX = level.facingX;
    state.facingY = level.facingY;
    state.stats = level.stats;
    State::StoryValues(state) = State::StoryValues(world);
    state.hideJob = int(world.hideJob);
    state.recordChoice = int(world.recordChoice);
    state.trailNotice = int(world.trailNotice);
    state.accounts = world.ledgerAccountsHeard.to_ulong();
    state.barrierBlocks = scene.Get(world.barrier).BlocksMovement();
    for (ActorId id : {world.ledger, world.key})
    {
        auto& artifact = state.artifacts[id == world.ledger ? 0 : 1];
        artifact.owner = number(world.Holder(id));
        artifact.position = scene.Get(id).WorldPosition();
    }

    std::bitset<NpcCount> savedResidents;
    for (const Harbor::Npc* npc : scene.Actors<Harbor::Npc>(ActorKind::Npc))
    {
        if (npc->variant < 0 || npc->variant >= NpcCount || savedResidents[npc->variant])
        {
            return false;
        }
        savedResidents.set(npc->variant);
        auto& resident = state.residents[npc->variant];
        State::Resident::Values(resident) = State::Resident::Values(*npc);
        resident.position = npc->WorldPosition();
        resident.direction = npc->visualDirection;
        resident.lastRumorSender = number(npc->lastRumorSender);
        resident.knownLedgerHolder = number(npc->knownLedgerHolder);
    }
    if (!savedResidents.all())
    {
        return false;
    }

    const auto loot = level.Drops();
    if (loot.size() > state.drops.size() || world.history.size() > state.events.size())
    {
        return false;
    }
    for (const Level::Loot* drop : loot)
    {
        state.drops[state.dropCount++] = {
            drop->WorldPosition(), int(drop->kind), drop->amount, drop->lifetime};
    }
    for (const auto& event : world.history)
    {
        state.events[state.eventCount++] = event;
    }
    return true;
}

void GameSave::Restore(const State& state, Level::FirstLevel& level, Harbor::World& world)
{
    auto& scene = level.Scene();
    scene.Reset();
    level.InitializeScene();
    level.Generate(state.seed);
    world.Initialize();

    State::StoryValues(world) = State::StoryValues(state);
    world.hideJob = Harbor::World::HideJob(state.hideJob);
    world.recordChoice = Harbor::RecordChoice(state.recordChoice);
    world.trailNotice = Harbor::TrailNotice(state.trailNotice);
    world.ledgerAccountsHeard = std::bitset<4>(state.accounts);
    world.history.assign(state.events.begin(), state.events.begin() + state.eventCount);

    if (world.gateOpen)
    {
        scene.Get(world.sites[1]).model = "sluice_open";
        level.DrainFloodedLane();
    }
    if (world.repaired)
    {
        world.PlaceDrainSupport();
    }
    Actor& barrier = scene.Get(world.barrier);
    barrier.SetVisible(world.controlled);
    barrier.SetBlocksMovement(state.barrierBlocks);
    if (state.barrierBlocks && !level.AllWalkableConnected())
    {
        barrier.SetBlocksMovement(false);
    }

    const auto walkable = [&](const Position& position)
    {
        return level.Walkable(position.x, position.y) ? position : world.Reachable(position);
    };
    level.stats = state.stats;
    level.Player().SetWorldPosition(walkable(state.player));
    level.Player().visualDirection = state.direction;
    level.facingX = state.facingX;
    level.facingY = state.facingY;
    level.walkPhase = 0;

    const auto residents = scene.Actors<Harbor::Npc>(ActorKind::Npc);
    std::array<ActorId, NpcCount> ids{};
    for (const Harbor::Npc* npc : residents)
    {
        ids[npc->variant] = npc->Id();
    }
    const auto actorFor = [&](int number)
    {
        return number >= 0 ? ids[number] : number == WithPlayer ? level.Player().Id() : InvalidActorId;
    };
    for (Harbor::Npc* npc : residents)
    {
        const auto& resident = state.residents[npc->variant];
        State::Resident::Values(*npc) = State::Resident::Values(resident);
        npc->SetWorldPosition(walkable(resident.position));
        npc->destination = npc->WorldPosition();
        npc->visualDirection = resident.direction;
        npc->lastRumorSender = actorFor(resident.lastRumorSender);
        npc->knownLedgerHolder = actorFor(resident.knownLedgerHolder);
        if (npc->health == 0)
        {
            npc->action = Harbor::Action::Dead;
            npc->model = "corpse";
        }
    }

    for (ActorId id : {world.ledger, world.key})
    {
        const auto& artifact = state.artifacts[id == world.ledger ? 0 : 1];
        world.Transfer(id, actorFor(artifact.owner));
        auto& item = scene.Get(id);
        item.SetVisible(world.drainInspected);
        if (artifact.owner == OnGround)
        {
            item.SetWorldPosition(walkable(artifact.position));
        }
    }
    for (int i = 0; i < state.dropCount; ++i)
    {
        const auto& d = state.drops[i];
        level.AddLoot(walkable(d.position), Level::LootKind(d.kind), d.amount).lifetime = d.lifetime;
    }

    world.UpdateQuest();
    level.UpdateVisibleChunks();
}

GameSave::Result GameSave::Save(const Level::FirstLevel& level, const Harbor::World& world)
{
    Perf::Scope scope(Perf::Cpu::SaveGame);
    const auto path = SlotPath();
    if (path.empty())
    {
        return Result::IoError;
    }
    State state{};
    if (!Capture(state, level, world) || !state.Valid())
    {
        return Result::Invalid;
    }
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    const bool encoded = state.Fields(
        [&](const auto&... values)
        {
            ((out << values << L' '), ...);
            out << L'\n';
            return bool(out);
        });
    if (!encoded)
    {
        return Result::IoError;
    }
    const std::wstring payload = out.str();
    const std::wstring header = L"HAEDONG_SAVE " + std::to_wstring(SaveVersion) + L" " +
                                std::to_wstring(PayloadChecksum(payload)) + L"\n";
    return WriteSlot(path, header + payload);
}

GameSave::Result GameSave::Load(Level::FirstLevel& level, Harbor::World& world)
{
    Perf::Scope scope(Perf::Cpu::LoadGame);
    const auto path = SlotPath();
    if (path.empty())
    {
        return Result::IoError;
    }
    std::wstring content;
    const Result read = ReadSlot(path, content);
    if (read != Result::Success)
    {
        return read;
    }

    const size_t lineEnd = content.find(L'\n');
    if (lineEnd == std::wstring::npos)
    {
        return Result::Invalid;
    }
    std::wistringstream header(content.substr(0, lineEnd));
    header.imbue(std::locale::classic());
    std::wstring magic;
    int version = 0;
    std::uint64_t checksum = 0;
    if (!(header >> magic >> version >> checksum) || !(header >> std::ws).eof() || magic != L"HAEDONG_SAVE" ||
        version != SaveVersion)
    {
        return Result::Invalid;
    }
    const std::wstring payload = content.substr(lineEnd + 1);
    if (PayloadChecksum(payload) != checksum)
    {
        return Result::Invalid;
    }
    std::wistringstream in(payload);
    in.imbue(std::locale::classic());
    State state{};
    const bool decoded = state.Fields(
        [&](auto&&... values)
        {
            ((in >> values), ...);
            return bool(in);
        });
    if (!decoded || !(in >> std::ws).eof() || !state.Valid())
    {
        return Result::Invalid;
    }

    Restore(state, level, world);
    return Result::Success;
}
