#include "stdafx.h"
#include "LevelOne.h"
#include "Profiler.h"
#include "CharacterPresentation.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace Level
{
    namespace
    {
        constexpr float HalfTileWidth = 48.0f;
        constexpr float HalfTileHeight = 24.0f;
        constexpr float BodyRadius = 0.18f;
        const Position Camp = WorldLayout::VillageSpawn();

        float Distance(const Position& a, const Position& b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        bool InCamp(int x, int y)
        {
            return WorldLayout::InVillage({float(x), float(y), 0});
        }
    } // namespace

    int Stats::RequiredExperience() const
    {
        return 60 + (level - 1) * 40;
    }

    int Stats::MaxHealth() const
    {
        return 100 + (level - 1) * 20 + vitality * 15;
    }

    int Stats::Attack() const
    {
        return 18 + (level - 1) * 4 + strength * 3;
    }

    int Stats::Defense() const
    {
        return 2 + (level - 1) + resilience * 2;
    }

    void Stats::GainExperience(int amount)
    {
        totalExperience += amount;
        if (level >= MaximumLevel)
        {
            return;
        }

        experience += amount;
        while (level < MaximumLevel && experience >= RequiredExperience())
        {
            experience -= RequiredExperience();
            ++level;
            points += 2;
            health = MaxHealth();
        }
        if (level == MaximumLevel)
        {
            experience = 0;
        }
    }

    bool Stats::Allocate(int attribute)
    {
        if (points <= 0 || attribute < 1 || attribute > 3)
        {
            return false;
        }
        --points;
        if (attribute == 1)
        {
            ++strength;
        }
        if (attribute == 2)
        {
            ++vitality;
            health += 15;
        }
        if (attribute == 3)
        {
            ++resilience;
        }
        return true;
    }

    FirstLevel::FirstLevel()
    {
        Actor& world = scene.Create(scene.Root().Id(), "World");
        terrainRoot = scene.Create(world.Id(), "Terrain").Id();
        propRoot = scene.Create(world.Id(), "Scenery").Id();
        Actor& characters = scene.Create(world.Id(), "Characters");
        npcRoot = scene.Create(characters.Id(), "Villagers").Id();
        enemyRoot = scene.Create(characters.Id(), "Beasts").Id();
        lootRoot = scene.Create(world.Id(), "Loot").Id();
        overlayRoot = scene.Create(scene.Root().Id(), "UI").Id();
        Actor& hero = scene.Create(characters.Id(), "Player", ActorKind::Player);
        playerId = hero.Id();
        hero.SetLocalPosition(Camp);
        hero.drawable = true;
        hero.model = "hero0";
        hero.shadowWidth = 30;
        hero.shadowHeight = 65;
        hero.visualHeight = WorldLayout::CharacterVisualHeight;
        for (auto effect : {ActorEffect::SwordTrail, ActorEffect::PlayerHurt})
        {
            Actor& child = scene.Create(hero.Id(), "PlayerEffect", ActorKind::Effect);
            child.pass = ScenePass::Effects;
            child.drawable = true;
            child.variant = int(effect);
        }
    }

    void FirstLevel::RefreshActorObstacles() const
    {
        if (generating || collisionRevision == scene.CollisionRevision())
        {
            return;
        }
        Perf::Scope scope(Perf::Cpu::CollisionRebuild);
        // Rebuild collision from placed Actors, so moving/removing a node changes gameplay too.
        for (auto& tile : tiles)
        {
            tile = {Terrain::Water, false};
        }
        for (ActorId id : scene.FindByKind(ActorKind::Terrain))
        {
            const Actor& actor = scene.Get(id);
            Position p = ToGrid(actor.WorldPosition());
            int x = int(std::floor(p.x)), y = int(std::floor(p.y));
            if (x >= 0 && y >= 0 && x < MapSize && y < MapSize)
            {
                tiles[y * MapSize + x].terrain = actor.variant == 5   ? Terrain::Water
                                                 : actor.variant == 4 ? Terrain::Road
                                                                      : Terrain::Grass;
            }
        }
        for (ActorId id : scene.FindByKind(ActorKind::Prop))
        {
            const Actor& actor = scene.Get(id);
            if (!actor.BlocksMovement())
            {
                continue;
            }
            Position p = ToGrid(actor.WorldPosition());
            int x = int(std::floor(p.x)), y = int(std::floor(p.y));
            if (x >= 0 && y >= 0 && x < MapSize && y < MapSize)
            {
                tiles[y * MapSize + x].blocked = true;
            }
        }
        collisionRevision = scene.CollisionRevision();
    }

    void FirstLevel::AddProp(const Position& position, int kind, bool blocksMovement)
    {
        int cx = int(position.x) / WorldLayout::ChunkSize;
        int cy = int(position.y) / WorldLayout::ChunkSize;
        ActorId parent = propChunks[cy * WorldLayout::ChunksPerSide + cx];
        Actor& prop = scene.Create(parent ? parent : propRoot, "Prop", ActorKind::Prop);
        prop.SetWorldPosition(ToWorld(position));
        prop.variant = kind;
        prop.drawable = true;
        prop.model = "prop" + std::to_string(kind);
        prop.shadowWidth = kind <= 1                   ? 112.0f
                           : (kind == 7 || kind >= 16) ? 65.0f
                           : kind == 8 || kind == 15   ? 0.0f
                                                       : 34.0f;
        prop.shadowHeight = kind <= 1 ? 105.0f : (kind == 7 || kind >= 16) ? 112.0f : 30.0f;
        if (kind == 15)
        {
            prop.pass = ScenePass::Ground;
        }
        if (kind <= 1)
        {
            Actor& light = scene.Create(prop.Id(), "WindowLight", ActorKind::Effect);
            light.drawable = true;
            light.pass = ScenePass::Effects;
            light.variant = int(ActorEffect::WarmLight);
            light.SetLocalPosition({-.15f, .15f, 36});
        }
        prop.SetBlocksMovement(blocksMovement);
        if (kind <= 1 || kind == 5)
        {
            Actor& effect = scene.Create(prop.Id(), kind == 5 ? "Flame" : "ChimneySmoke", ActorKind::Effect);
            effect.drawable = true;
            effect.pass = ScenePass::Effects;
            effect.variant = int(kind == 5 ? ActorEffect::Flame : ActorEffect::Smoke);
            effect.SetLocalPosition(kind == 5 ? Position{0, 0, 43} : Position{.416667f, -.416667f, 103});
        }
    }

    void FirstLevel::AddLoot(const Position& position, LootKind kind, int amount)
    {
        Loot& loot = scene.Create<Loot>(lootRoot, "Drop", ActorKind::Loot);
        loot.SetWorldPosition(position);
        loot.kind = kind;
        loot.amount = amount;
        loot.drawable = true;
        loot.model = "loot" + std::to_string(int(kind));
        loot.shadowWidth = 20;
    }

    void FirstLevel::UpdateActorModels()
    {
        Perf::Scope scope(Perf::Cpu::Animation);
        int frame = moving ? int(walkPhase * 8 / 6.2831853f) % 8 : 0;
        if (attackAnimation > 0)
        {
            int pose = CharacterPresentation::AttackFrame(attackAnimation);
            int direction = CharacterPresentation::AttackDirection(facingY);
            const char* prefix = direction < 0   ? "hero_attack_up"
                                 : direction > 0 ? "hero_attack_down"
                                                 : "hero_attack";
            Player().model = prefix + std::to_string(pose);
        }
        else
        {
            Player().model = "hero" + std::to_string(frame);
        }
        for (Enemy* enemy : scene.Actors<Enemy>(ActorKind::Enemy))
        {
            int pose = enemy->moving ? int(enemy->phase * 9 * 8 / 6.2831853f) % 8 : 0;
            enemy->model = "beast" + std::to_string(enemy->kind) + "_" + std::to_string(pose);
        }
    }

    Position FirstLevel::ToGrid(const Position& world) const
    {
        Position origin = scene.Get(terrainRoot).WorldPosition();
        return {world.x - origin.x, world.y - origin.y, world.z - origin.z};
    }

    Position FirstLevel::ToWorld(const Position& grid) const
    {
        Position origin = scene.Get(terrainRoot).WorldPosition();
        return {grid.x + origin.x, grid.y + origin.y, grid.z + origin.z};
    }

    const Tile& FirstLevel::At(int x, int y) const
    {
        RefreshActorObstacles();
        static const Tile border = {Terrain::Water, true};
        if (x < 0 || y < 0 || x >= MapSize || y >= MapSize)
        {
            return border;
        }
        return tiles[y * MapSize + x];
    }

    bool FirstLevel::Open(int x, int y) const
    {
        const auto& tile = At(x, y);
        return x > 0 && y > 0 && x < MapSize - 1 && y < MapSize - 1 && !tile.blocked &&
               tile.terrain != Terrain::Water;
    }

    bool FirstLevel::Walkable(float x, float y) const
    {
        Perf::Count(Perf::Counter::CollisionTests);
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            return false;
        }
        Position grid = ToGrid({x, y, 0});
        x = grid.x;
        y = grid.y;
        for (float dx : {-BodyRadius, BodyRadius})
        {
            for (float dy : {-BodyRadius, BodyRadius})
            {
                if (!Open(int(std::floor(x + dx)), int(std::floor(y + dy))))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void FirstLevel::Carve(int x, int y)
    {
        if (x < 1 || y < 1 || x >= MapSize - 1 || y >= MapSize - 1)
        {
            return;
        }
        tiles[y * MapSize + x] = {Terrain::Road, false};
    }

    void FirstLevel::BuildNavigation(bool localOnly)
    {
        Perf::Scope scope(Perf::Cpu::Navigation);
        navigation.fill(-1);
        Position gridPlayer = ToGrid(Player().WorldPosition());
        int x = int(gridPlayer.x), y = int(gridPlayer.y);
        if (!Open(x, y))
        {
            return;
        }
        std::queue<int> pending;
        int start = y * MapSize + x;
        navigation[start] = 0;
        pending.push(start);
        while (!pending.empty())
        {
            int id = pending.front();
            pending.pop();
            Perf::Count(Perf::Counter::NavigationCells);
            const int dx[] = {1, -1, 0, 0};
            const int dy[] = {0, 0, 1, -1};
            for (int i = 0; i < 4; ++i)
            {
                int nx = id % MapSize + dx[i], ny = id / MapSize + dy[i];
                if (localOnly && std::abs(nx - x) + std::abs(ny - y) > 16)
                {
                    continue;
                }
                if (!Open(nx, ny))
                {
                    continue;
                }
                int next = ny * MapSize + nx;
                if (navigation[next] != -1)
                {
                    continue;
                }
                navigation[next] = navigation[id] + 1;
                pending.push(next);
            }
        }
    }

    bool FirstLevel::AllWalkableConnected() const
    {
        std::array<bool, MapSize * MapSize> visited = {};
        std::queue<int> pending;
        const int start = int(Camp.y) * MapSize + int(Camp.x);
        if (!Open(int(Camp.x), int(Camp.y)))
        {
            return false;
        }
        visited[start] = true;
        pending.push(start);
        while (!pending.empty())
        {
            int id = pending.front();
            pending.pop();
            Perf::Count(Perf::Counter::NavigationCells);
            const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k)
            {
                int x = id % MapSize + dx[k], y = id / MapSize + dy[k];
                if (!Open(x, y))
                {
                    continue;
                }
                int next = y * MapSize + x;
                if (!visited[next])
                {
                    visited[next] = true;
                    pending.push(next);
                }
            }
        }
        for (int y = 1; y < MapSize - 1; ++y)
        {
            for (int x = 1; x < MapSize - 1; ++x)
            {
                if (Open(x, y) && !visited[y * MapSize + x])
                {
                    return false;
                }
            }
        }
        return true;
    }

    void FirstLevel::ConnectGround()
    {
        // Each carved corridor joins at least one disconnected component to the camp.
        BuildNavigation(false);
        for (int y = 1; y < MapSize - 1; ++y)
        {
            for (int x = 1; x < MapSize - 1; ++x)
            {
                if (!Open(x, y) || navigation[y * MapSize + x] >= 0)
                {
                    continue;
                }
                int cx = x, cy = y;
                while (cx != int(Camp.x))
                {
                    Carve(cx, cy);
                    cx += cx < Camp.x ? 1 : -1;
                }
                while (cy != int(Camp.y))
                {
                    Carve(cx, cy);
                    cy += cy < Camp.y ? 1 : -1;
                }
                Carve(cx, cy);
                BuildNavigation(false);
            }
        }
    }

    void FirstLevel::Generate(std::uint32_t nextSeed)
    {
        Perf::Scope scope(Perf::Cpu::WorldGenerate);
        seed = nextSeed;
        random.seed(seed);
        tiles.fill(Tile{});
        generating = true;
        scene.DestroyChildren(terrainRoot);
        scene.DestroyChildren(propRoot);
        scene.DestroyChildren(enemyRoot);
        scene.DestroyChildren(lootRoot);
        for (int cy = 0; cy < WorldLayout::ChunksPerSide; ++cy)
        {
            for (int cx = 0; cx < WorldLayout::ChunksPerSide; ++cx)
            {
                int index = cy * WorldLayout::ChunksPerSide + cx;
                terrainChunks[index] = scene.Create(terrainRoot, "TerrainChunk").Id();
                propChunks[index] = scene.Create(propRoot, "SceneryChunk").Id();
            }
        }
        Player().SetWorldPosition(ToWorld(Camp));
        moving = false;
        attackAnimation = 0;
        attackImpactPending = attackImpactReady = false;
        attackCooldown = 0;
        invulnerability = 2;
        navigationTime = 0;
        hurtFlash = 0;
        stats.health = stats.MaxHealth();

        std::uniform_int_distribution<int> coordinate(3, MapSize - 4);
        const std::array<Position, 5> destinations = {WorldLayout::HunterCamp(),
                                                      WorldLayout::Waystation(),
                                                      WorldLayout::OldShrine(),
                                                      WorldLayout::VillageCenter(4),
                                                      WorldLayout::RaiderCamp()};
        for (int lake = 0; lake < 56; ++lake)
        {
            float cx = float(coordinate(random)), cy = float(coordinate(random));
            float rx = 3.5f + float(random() % 65) / 10.0f;
            float ry = 3.5f + float(random() % 65) / 10.0f;
            int minX = (std::max)(1, int(cx - rx - 1)), maxX = (std::min)(MapSize - 2, int(cx + rx + 1));
            int minY = (std::max)(1, int(cy - ry - 1)), maxY = (std::min)(MapSize - 2, int(cy + ry + 1));
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    Position p = {x + .5f, y + .5f, 0};
                    bool reserved = WorldLayout::InStoryDistrict(p) || WorldLayout::InVillage(p);
                    for (const Position& destination : destinations)
                    {
                        reserved = reserved || Distance(p, destination) < 8;
                    }
                    float dx = (x - cx) / rx, dy = (y - cy) / ry;
                    float shoreline = 1.0f + .09f * std::sin(float(x) * .61f + float(lake)) +
                                      .07f * std::cos(float(y) * .73f - float(lake) * .9f);
                    if (!reserved && dx * dx + dy * dy < shoreline)
                    {
                        tiles[y * MapSize + x].terrain = Terrain::Water;
                    }
                }
            }
        }

        for (int y = 6; y <= 27; ++y)
        {
            for (int x = 26; x <= 38; ++x)
            {
                tiles[(y + WorldLayout::TownOffsetY) * MapSize + x + WorldLayout::TownOffsetX].terrain =
                    Terrain::Water;
            }
        }
        auto road = [&](Position from, Position to)
        {
            int x = int(from.x), y = int(from.y), tx = int(to.x), ty = int(to.y);
            while (x != tx || y != ty)
            {
                Carve(x, y);
                Carve(x + 1, y);
                Carve(x, y + 1);
                if (x != tx)
                {
                    x += x < tx ? 1 : -1;
                }
                else
                {
                    y += y < ty ? 1 : -1;
                }
            }
            Carve(x, y);
            Carve(x + 1, y);
            Carve(x, y + 1);
        };
        for (const auto& destination : destinations)
        {
            road(Camp, destination);
        }
        for (const Position& destination : {Position{24.5f, 28.5f, 0},
                                            Position{24.5f, 232.5f, 0},
                                            Position{232.5f, 35.5f, 0},
                                            Position{232.5f, 232.5f, 0}})
        {
            road(Camp, destination);
        }
        road(WorldLayout::HunterCamp(), WorldLayout::OldShrine());
        road(WorldLayout::OldShrine(), WorldLayout::RaiderCamp());
        for (int village = 0; village < WorldLayout::VillageCount; ++village)
        {
            Position c = WorldLayout::VillageCenter(village);
            road(c, WorldLayout::VillageCenter(WorldLayout::VillageNeighbour(village)));
            if (village == 0)
            {
                continue;
            }
            for (int offset = -12; offset <= 12; offset += 6)
            {
                road({c.x - 15, c.y + float(offset), 0}, {c.x + 15, c.y + float(offset), 0});
                road({c.x + float(offset), c.y - 15, 0}, {c.x + float(offset), c.y + 15, 0});
            }
        }
        for (int route = 0; route < 20; ++route)
        {
            road(Camp, {float(coordinate(random)), float(coordinate(random)), 0});
        }
        for (int y = 5; y <= 33; y += 6)
        {
            road(WorldLayout::Town(5, float(y)), WorldLayout::Town(30, float(y)));
        }
        for (int x = 5; x <= 23; x += 6)
        {
            road(WorldLayout::Town(float(x), 5), WorldLayout::Town(float(x), 33));
        }
        road(WorldLayout::Town(13, 12), WorldLayout::Town(25, 12));
        road(WorldLayout::Town(23, 7), WorldLayout::Town(23, 29));
        road(WorldLayout::Town(19, 24), WorldLayout::Town(25, 24));
        road(WorldLayout::Town(21, 17), WorldLayout::Town(30, 17));
        ConnectGround();

        // Removing a tile whose eight-neighbour ring is open cannot disconnect the ground.
        // This local invariant avoids a whole-world flood fill for every tree.
        auto addBlock = [&](int x, int y, int kind)
        {
            if (!Open(x, y) || At(x, y).terrain == Terrain::Road)
            {
                return;
            }
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (!Open(x + dx, y + dy))
                    {
                        return;
                    }
                }
            }
            tiles[y * MapSize + x].blocked = true;
            AddProp({x + .5f, y + .5f, 0}, kind, true);
            Perf::Count(Perf::Counter::WorldPropsAdded);
        };
        for (int x = 8; x <= 23; x += 5)
        {
            for (int y = 8; y <= 32; y += 6)
            {
                addBlock(x + WorldLayout::TownOffsetX, y + WorldLayout::TownOffsetY, int(random() % 2));
            }
        }
        for (Position p : {WorldLayout::Town(12, 9), WorldLayout::Town(14, 9), WorldLayout::Town(18, 15)})
        {
            AddProp(p, 2);
        }
        AddProp(WorldLayout::Town(12, 4), 3);
        AddProp(WorldLayout::Town(14, 4), 3);
        AddProp(WorldLayout::Town(16, 21), 4);
        for (int y = 7; y <= 31; y += 6)
        {
            AddProp(WorldLayout::Town(14, float(y)), 5);
        }
        for (const Position& destination : destinations)
        {
            AddProp({destination.x - 3, destination.y - 2, 0}, 1);
            AddProp({destination.x + 3, destination.y - 2, 0}, 1);
            AddProp({destination.x, destination.y + 2, 0}, 5);
            AddProp({destination.x - 3, destination.y + 3, 0}, 6);
            AddProp({destination.x + 3, destination.y + 3, 0}, 6);
        }
        for (int village = 1; village < WorldLayout::VillageCount; ++village)
        {
            Position c = WorldLayout::VillageCenter(village);
            for (int y = -9; y <= 9; y += 6)
            {
                for (int x = -9; x <= 9; x += 6)
                {
                    addBlock(int(c.x) + x, int(c.y) + y, (x + y + village) % 3 == 0 ? 0 : 1);
                }
            }
            AddProp({c.x - 5, c.y - 3, 0}, 2);
            AddProp({c.x + 5, c.y - 3, 0}, 2);
            AddProp({c.x, c.y + 5, 0}, 6);
            AddProp({c.x - 15, c.y, 0}, 3);
            AddProp({c.x + 15, c.y, 0}, 3);
            for (int offset : {-12, 0, 12})
            {
                AddProp({c.x + float(offset), c.y + 2, 0}, 5);
                AddProp({c.x + float(offset), c.y - 5, 0}, 11);
            }
        }
        for (int attempt = 0; attempt < 19500; ++attempt)
        {
            int x = coordinate(random), y = coordinate(random);
            Position p = {x + .5f, y + .5f, 0};
            bool reserved = WorldLayout::InStoryDistrict(p) || WorldLayout::InVillage(p);
            for (const Position& destination : destinations)
            {
                reserved = reserved || Distance(p, destination) < 7;
            }
            if (!reserved)
            {
                int tree = (x * 13 + y * 7) % 5;
                addBlock(x, y, tree < 2 ? 7 : tree < 4 ? 16 : 17);
            }
        }

        // Keep a spawn list of valid centers. No unbounded rejection loops.
        spawnCandidates.clear();
        for (int y = 2; y < MapSize - 2; ++y)
        {
            for (int x = 2; x < MapSize - 2; ++x)
            {
                Position p = ToWorld({x + .5f, y + .5f, 0});
                if (WorldLayout::BetweenVillages(p) && Walkable(p.x, p.y))
                {
                    spawnCandidates.push_back(p);
                }
            }
        }
        std::shuffle(spawnCandidates.begin(), spawnCandidates.end(), random);
        std::vector<Position> beastHomes;
        for (const auto& p : spawnCandidates)
        {
            bool crowded = false;
            for (const Position& home : beastHomes)
            {
                if (Distance(home, p) < 3.8f)
                {
                    crowded = true;
                    break;
                }
            }
            if (crowded)
            {
                continue;
            }
            Enemy& enemy = scene.Create<Enemy>(enemyRoot, "Beast", ActorKind::Enemy);
            enemy.SetWorldPosition(p);
            enemy.home = enemy.LocalPosition();
            beastHomes.push_back(p);
            enemy.drawable = true;
            enemy.shadowWidth = 30;
            Actor& status = scene.Create(enemy.Id(), "HealthAndHit", ActorKind::Effect);
            status.drawable = true;
            status.pass = ScenePass::Effects;
            status.variant = int(ActorEffect::EnemyStatus);
            enemy.kind = int(random() % 3);
            enemy.health = enemy.maxHealth = 42 + enemy.kind * 18;
            enemy.phase = float(random() % 100) / 10;
            if (beastHomes.size() == WorldLayout::MonsterCount)
            {
                break;
            }
        }

        for (int y = 0; y < MapSize; ++y)
        {
            for (int x = 0; x < MapSize; ++x)
            {
                int chunk =
                    (y / WorldLayout::ChunkSize) * WorldLayout::ChunksPerSide + x / WorldLayout::ChunkSize;
                Actor& tile = scene.Create(terrainChunks[chunk], "Tile", ActorKind::Terrain);
                tile.SetLocalPosition({x + .5f, y + .5f, 0});
                tile.pass = ScenePass::Ground;
                tile.drawable = true;
                Terrain terrain = At(x, y).terrain;
                tile.variant = terrain == Terrain::Water  ? 5
                               : terrain == Terrain::Road ? 4
                                                          : (x * 17 + y * 31) % 4;
                tile.model = "tile" + std::to_string(tile.variant);
            }
        }
        BuildHarborScenery();
        generating = false;
        RefreshActorObstacles();
        RefreshTerrainAppearance();
        UpdateVisibleChunks();
        UpdateActorModels();
        BuildNavigation();
        Notify(
            L"\uc0ac\ub0e5\ud130\uc5d0 \ub3c4\ucc29\ud588\ub2e4. \uc9d0\uc2b9\uc744 \uc7a1\uace0, \uc804\ub9ac\ud488\uc744 \ucc59\uae30\uba70 \ubab8\uc744 \ub2e8\ub828\ud574 \ubcf4\uc790.");
    }

    void FirstLevel::DrainFloodedLane()
    {
        Perf::Scope scope(Perf::Cpu::StoryUpdate);
        // Change shared terrain Actors once, not a render-only overlay or a second tile map.
        for (ActorId id : scene.FindByKind(ActorKind::Terrain))
        {
            Actor& tile = scene.Get(id);
            Position p = tile.WorldPosition();
            float x = p.x - WorldLayout::TownOffsetX;
            float y = p.y - WorldLayout::TownOffsetY;
            if (tile.variant == 5 && x >= 26 && x < 31 && y >= 19 && y < 27)
            {
                tile.SetVariant(4);
                tile.model = "tile4";
                Perf::Count(Perf::Counter::TerrainTilesChanged);
            }
        }
        RefreshActorObstacles();
        RefreshTerrainAppearance();
        navigationTime = 0;
    }

    void FirstLevel::RefreshTerrainAppearance()
    {
        Perf::Scope scope(Perf::Cpu::TerrainAppearance);
        // Rebuild only after generation or a terrain mutation, never from the render loop.
        const auto water = [&](int x, int y)
        {
            return x >= 0 && y >= 0 && x < MapSize && y < MapSize && At(x, y).terrain == Terrain::Water;
        };
        const auto road = [&](int x, int y)
        {
            return x >= 0 && y >= 0 && x < MapSize && y < MapSize && At(x, y).terrain == Terrain::Road;
        };
        for (ActorId id : scene.FindByKind(ActorKind::Terrain, false))
        {
            Actor& tile = scene.Get(id);
            Position p = ToGrid(tile.WorldPosition());
            int x = int(std::floor(p.x)), y = int(std::floor(p.y));
            bool isWater = water(x, y), isRoad = road(x, y);
            int waterMask = 0, roadMask = 0;
            const int dx[] = {-1, 1, 0, 0};
            const int dy[] = {0, 0, -1, 1};
            for (int edge = 0; edge < 4; ++edge)
            {
                if (water(x + dx[edge], y + dy[edge]) != isWater)
                {
                    waterMask |= 1 << edge;
                }
                if (road(x + dx[edge], y + dy[edge]) != isRoad)
                {
                    roadMask |= 1 << edge;
                }
            }
            float depth = 0;
            if (isWater)
            {
                int distance = 4;
                for (int yy = -3; yy <= 3; ++yy)
                {
                    for (int xx = -3; xx <= 3; ++xx)
                    {
                        if (!water(x + xx, y + yy))
                        {
                            distance = (std::min)(distance, std::abs(xx) + std::abs(yy));
                        }
                    }
                }
                depth = float(distance) * .25f;
                if (waterMask)
                {
                    Perf::Count(Perf::Counter::ShorelineTiles);
                }
            }
            bool wetLane = x >= WorldLayout::TownOffsetX + 26 && x < WorldLayout::TownOffsetX + 31 &&
                           y >= WorldLayout::TownOffsetY + 19 && y < WorldLayout::TownOffsetY + 27;
            tile.surfaceData = {{float(waterMask), float(roadMask), depth, wetLane ? 1.0f : 0.0f}};
            Perf::Count(Perf::Counter::TerrainAppearanceTiles);
        }
    }

    void FirstLevel::BuildHarborScenery()
    {
        Perf::Scope scope(Perf::Cpu::SceneryBuild);
        // Decoration never changes collision, road connectivity or navigation fields.
        const auto place = [&](const Position& position, int kind)
        {
            AddProp(position, kind);
            Perf::Count(Perf::Counter::SceneryProps);
        };
        for (int y = 2; y < MapSize - 2; ++y)
        {
            for (int x = 2; x < MapSize - 2; ++x)
            {
                if (At(x, y).terrain != Terrain::Grass || At(x, y).blocked)
                {
                    continue;
                }
                bool shore = At(x - 1, y).terrain == Terrain::Water ||
                             At(x + 1, y).terrain == Terrain::Water ||
                             At(x, y - 1).terrain == Terrain::Water || At(x, y + 1).terrain == Terrain::Water;
                unsigned pattern = (unsigned(x) * 73856093u) ^ (unsigned(y) * 19349663u) ^ seed;
                if (shore && pattern % 4 == 0)
                {
                    place({x + .5f, y + .5f, 0}, pattern % 3 == 0 ? 9 : 8);
                }
            }
        }
        for (const Position& p : {WorldLayout::Town(10.5f, 8.5f),
                                  WorldLayout::Town(20.5f, 14.5f),
                                  WorldLayout::Town(9.5f, 26.5f)})
        {
            place(p, 11); // herb drying frames
        }
        place(WorldLayout::Town(17.5f, 8.5f), 12);
        place(WorldLayout::Town(11.5f, 20.5f), 12);
        place(WorldLayout::Town(17.5f, 9.5f), 13);
        place(WorldLayout::Town(24.5f, 19.5f), 13);
        place(WorldLayout::Town(12.5f, 8.5f), 14);
        place(WorldLayout::Town(25.5f, 24.5f), 9);
        for (int x = 25; x <= 30; ++x)
        {
            if (At(x + WorldLayout::TownOffsetX, 17 + WorldLayout::TownOffsetY).terrain == Terrain::Road)
            {
                place(WorldLayout::Town(x + .5f, 17.5f), 15);
            }
        }
        // A moored boat remains a water prop, not a false walkable platform.
        const Position boat = WorldLayout::Town(34.5f, 22.5f);
        if (At(int(boat.x), int(boat.y)).terrain == Terrain::Water)
        {
            place(boat, 10);
        }
        for (const Position& p : {WorldLayout::Town(29.5f, 10.5f),
                                  WorldLayout::Town(34.5f, 25.5f),
                                  WorldLayout::HunterCamp(),
                                  WorldLayout::OldShrine()})
        {
            int cx = int(p.x) / WorldLayout::ChunkSize, cy = int(p.y) / WorldLayout::ChunkSize;
            Actor& mist = scene.Create(
                propChunks[cy * WorldLayout::ChunksPerSide + cx], "HarborMist", ActorKind::Effect);
            mist.drawable = true;
            mist.pass = ScenePass::Effects;
            mist.variant = int(ActorEffect::HarborMist);
            mist.SetWorldPosition({p.x, p.y, 18});
        }
    }

    void FirstLevel::UpdateVisibleChunks()
    {
        Perf::Scope scope(Perf::Cpu::ChunkVisibility);
        Position hero = ToGrid(Player().WorldPosition());
        for (int cy = 0; cy < WorldLayout::ChunksPerSide; ++cy)
        {
            for (int cx = 0; cx < WorldLayout::ChunksPerSide; ++cx)
            {
                // Project the chunk bounds into the same fixed 1280x800 camera as the renderer.
                // Include tall scenery and the movement margin, not only tile centers.
                float minX = static_cast<float>(cx * WorldLayout::ChunkSize);
                float maxX = minX + WorldLayout::ChunkSize;
                float minY = static_cast<float>(cy * WorldLayout::ChunkSize);
                float maxY = minY + WorldLayout::ChunkSize;
                float left = ((minX - maxY) - (hero.x - hero.y)) * HalfTileWidth;
                float right = ((maxX - minY) - (hero.x - hero.y)) * HalfTileWidth;
                float top = ((minX + minY) - (hero.x + hero.y)) * HalfTileHeight;
                float bottom = ((maxX + maxY) - (hero.x + hero.y)) * HalfTileHeight;
                bool visible = right > -900 && left < 900 && bottom > -650 && top < 760;
                int index = cy * WorldLayout::ChunksPerSide + cx;
                scene.Get(terrainChunks[index]).SetVisible(visible);
                scene.Get(propChunks[index]).SetVisible(visible);
                if (visible)
                {
                    Perf::Count(Perf::Counter::ChunksVisible);
                }
            }
        }
    }

    void FirstLevel::Move(Actor& actor, float dx, float dy)
    {
        Position position = actor.WorldPosition();
        int steps = (std::max)(1, int(std::ceil((std::max)(std::abs(dx), std::abs(dy)) / .1f)));
        dx /= steps;
        dy /= steps;
        for (int i = 0; i < steps; ++i)
        {
            if (Walkable(position.x + dx, position.y))
            {
                position.x += dx;
            }
            if (Walkable(position.x, position.y + dy))
            {
                position.y += dy;
            }
        }
        actor.SetWorldPosition(position);
    }

    void FirstLevel::MovePlayer(float sx, float sy, float dt)
    {
        Perf::Scope scope(Perf::Cpu::Movement);
        float length = std::hypot(sx, sy);
        moving = false;
        if (!Player().IsActive())
        {
            return;
        }
        if (length <= 0)
        {
            return;
        }
        if (attackAnimation <= 0)
        {
            facingX = sx / length;
            facingY = sy / length;
            if (std::abs(facingX) > .1f)
            {
                Player().mirrorModel = facingX < 0;
            }
        }
        Position before = Player().WorldPosition();
        // Travel still follows WASD while the current swing keeps its original direction.
        float moveX = sx / length, moveY = sy / length;
        float dx = (moveX / 96 + moveY / 48) * MoveSpeed * dt;
        float dy = (moveY / 48 - moveX / 96) * MoveSpeed * dt;
        Move(Player(), dx, dy);
        moving = Distance(before, Player().WorldPosition()) > .0001f;
        if (moving)
        {
            walkPhase += dt * 10;
        }
    }

    bool FirstLevel::ClearLine(const Position& from, const Position& to) const
    {
        Position start = ToGrid(from);
        Position finish = ToGrid(to);
        int steps = (std::max)(1, int(std::ceil(Distance(from, to) * 10)));
        for (int i = 0; i <= steps; ++i)
        {
            float t = float(i) / steps;
            if (!Open(int(std::floor(start.x + (finish.x - start.x) * t)),
                      int(std::floor(start.y + (finish.y - start.y) * t))))
            {
                return false;
            }
        }
        return true;
    }

    void FirstLevel::Kill(Enemy& enemy, bool playerCredit)
    {
        enemy.health = 0;
        enemy.respawn = WorldLayout::RespawnDelaySeconds;
        enemy.deathPosition = enemy.WorldPosition();
        enemy.moving = false;
        enemy.SetVisible(false);
        if (!playerCredit)
        {
            return;
        }
        ++stats.kills;
        int previousLevel = stats.level;
        int reward = 25 + enemy.kind * 10;
        stats.GainExperience(reward);

        // Kill state changes before rewards; a held attack cannot award XP twice.
        auto existing = scene.FindByKind(ActorKind::Loot, false);
        if (existing.size() > 180)
        {
            for (size_t i = 0; i < 30; ++i)
            {
                scene.Destroy(existing[i]);
            }
        }
        AddLoot(enemy.WorldPosition(), LootKind::Coins, 3 + enemy.kind * 2);
        AddLoot(enemy.WorldPosition(), random() % 3 == 0 ? LootKind::Potion : LootKind::Hide, 1);
        if (stats.level > previousLevel)
        {
            Notify(
                L"\ubab8\uc774 \ud55c\uacb0 \uac00\ubcbc\uc6cc\uc84c\ub2e4! \ub808\ubca8 " +
                std::to_wstring(stats.level) +
                L" \u00b7 \ub2a5\ub825\uce58\uac00 \uc62c\ub790\ub2e4. 1 / 2 / 3\uc73c\ub85c \ucd94\uac00 \ub2a5\ub825\uce58\ub97c \uace0\ub974\uc790.");
        }
        else
        {
            Notify(L"\uacbd\ud5d8\uce58 +" + std::to_wstring(reward) +
                   L" \u00b7 \ub5a8\uc5b4\uc9c4 \uc804\ub9ac\ud488\uc740 E\ub85c \ucc59\uae30\uc790.");
        }
    }

    bool FirstLevel::RespawnEnemy(Enemy& enemy)
    {
        Perf::Scope scope(Perf::Cpu::EnemyRespawn);
        if (spawnCandidates.empty())
        {
            return false;
        }
        auto enemies = scene.Actors<Enemy>(ActorKind::Enemy);
        auto people = scene.Actors<Actor>(ActorKind::Npc);
        size_t start = size_t(random()) % spawnCandidates.size();
        for (size_t attempt = 0; attempt < 128 && attempt < spawnCandidates.size(); ++attempt)
        {
            const Position& p = spawnCandidates[(start + attempt) % spawnCandidates.size()];
            Perf::Count(Perf::Counter::RespawnCandidates);
            if (!WorldLayout::BetweenVillages(p) || !Walkable(p.x, p.y) ||
                Distance(p, enemy.deathPosition) < 12 || Distance(p, Player().WorldPosition()) < 20)
            {
                continue;
            }
            bool occupied = false;
            for (const Enemy* other : enemies)
            {
                occupied = occupied || (other != &enemy && other->health > 0 &&
                                        Distance(p, other->WorldPosition()) < 2.5f);
            }
            for (const Actor* person : people)
            {
                occupied = occupied || Distance(p, person->WorldPosition()) < 3;
            }
            if (occupied)
            {
                continue;
            }
            enemy.SetWorldPosition(p);
            enemy.home = enemy.LocalPosition();
            enemy.SetVisible(true);
            enemy.health = enemy.maxHealth;
            enemy.cooldown = 1;
            enemy.moving = false;
            Perf::Count(Perf::Counter::EnemyRespawns);
            return true;
        }
        return false;
    }

    bool FirstLevel::Attack()
    {
        Perf::Scope scope(Perf::Cpu::Combat);
        if (!Player().IsActive() || attackCooldown > 0)
        {
            return false;
        }
        attackCooldown = .48f;
        attackAnimation = CharacterPresentation::AttackDuration;
        attackImpactPending = true;
        attackImpactReady = false;
        UpdateActorModels();
        return true;
    }

    bool FirstLevel::ConsumeAttackImpact()
    {
        bool ready = attackImpactReady;
        attackImpactReady = false;
        return ready;
    }

    void FirstLevel::ResolveAttackImpact()
    {
        Perf::Scope scope(Perf::Cpu::Combat);
        for (Enemy* instance : scene.Actors<Enemy>(ActorKind::Enemy))
        {
            Enemy& enemy = *instance;
            if (enemy.health <= 0)
            {
                continue;
            }
            float sx = ((enemy.WorldPosition().x - Player().WorldPosition().x) -
                        (enemy.WorldPosition().y - Player().WorldPosition().y)) *
                       HalfTileWidth;
            float sy = ((enemy.WorldPosition().x - Player().WorldPosition().x) +
                        (enemy.WorldPosition().y - Player().WorldPosition().y)) *
                       HalfTileHeight;
            float length = std::hypot(sx, sy);
            if (length > 78 || (length > 12 && (sx * facingX + sy * facingY) / length < .1f) ||
                !ClearLine(Player().WorldPosition(), enemy.WorldPosition()))
            {
                continue;
            }
            DamageEnemy(enemy, stats.Attack(), true);
        }
    }

    bool FirstLevel::DamageEnemy(Enemy& enemy, int damage, bool playerCredit)
    {
        if (enemy.health <= 0 || !enemy.IsActive() || damage <= 0)
        {
            return false;
        }
        enemy.health -= damage;
        enemy.flash = .18f;
        if (enemy.health <= 0)
        {
            Kill(enemy, playerCredit);
            return true;
        }
        return false;
    }

    void FirstLevel::DamagePlayer(int damage)
    {
        if (invulnerability > 0 || !Player().IsActive())
        {
            return;
        }
        stats.health -= (std::max)(1, damage - stats.Defense());
        invulnerability = .6f;
        hurtFlash = .22f;
        if (stats.health <= 0)
        {
            attackAnimation = 0;
            attackImpactPending = attackImpactReady = false;
            Player().SetWorldPosition(ToWorld(Camp));
            stats.health = stats.MaxHealth();
            invulnerability = 3;
            navigationTime = 0;
            Notify(
                L"\ub9c8\uc744\uc5d0\uc11c \ub208\uc744 \ub5b4\ub2e4. \uc7a0\uae50 \uc228\uc744 \uace0\ub974\uc790.");
        }
    }

    void FirstLevel::MoveActor(Actor& actor, float dx, float dy)
    {
        Move(actor, dx, dy);
    }

    std::vector<Position> FirstLevel::FindPath(const Position& from, const Position& to) const
    {
        Perf::Scope scope(Perf::Cpu::NpcNavigation);
        Position a = ToGrid(from), b = ToGrid(to);
        int sx = int(std::floor(a.x)), sy = int(std::floor(a.y));
        int tx = int(std::floor(b.x)), ty = int(std::floor(b.y));
        if (!Open(sx, sy) || !Open(tx, ty))
        {
            return {};
        }
        if (pathStamp.size() != MapSize * MapSize)
        {
            pathStamp.assign(MapSize * MapSize, 0);
            pathCost.resize(MapSize * MapSize);
            pathParent.resize(MapSize * MapSize);
        }
        if (++pathSearch == 0)
        {
            std::fill(pathStamp.begin(), pathStamp.end(), 0);
            pathSearch = 1;
        }

        struct Visit
        {
            int cell, cost, estimate;

            bool operator<(const Visit& other) const
            {
                return estimate > other.estimate;
            }
        };

        std::priority_queue<Visit> pending;
        int start = sy * MapSize + sx, goal = ty * MapSize + tx;
        auto estimate = [&](int cell)
        {
            return std::abs(cell % MapSize - tx) + std::abs(cell / MapSize - ty);
        };
        pathCost[start] = 0;
        pathParent[start] = start;
        pathStamp[start] = pathSearch;
        pending.push({start, 0, estimate(start)});
        bool found = false;
        while (!pending.empty())
        {
            Visit visit = pending.top();
            pending.pop();
            int cell = visit.cell;
            if (visit.cost != pathCost[cell])
            {
                continue;
            }
            Perf::Count(Perf::Counter::NpcPathCells);
            if (cell == goal)
            {
                found = true;
                break;
            }
            const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k)
            {
                int x = cell % MapSize + dx[k], y = cell / MapSize + dy[k];
                if (!Open(x, y))
                {
                    continue;
                }
                int next = y * MapSize + x, cost = visit.cost + 1;
                if (pathStamp[next] != pathSearch || cost < pathCost[next])
                {
                    pathStamp[next] = pathSearch;
                    pathCost[next] = cost;
                    pathParent[next] = cell;
                    pending.push({next, cost, cost + estimate(next)});
                }
            }
        }
        if (!found)
        {
            return {};
        }
        std::vector<Position> path;
        for (int cell = goal; cell != start; cell = pathParent[cell])
        {
            path.push_back(ToWorld({cell % MapSize + .5f, cell / MapSize + .5f, 0}));
        }
        std::reverse(path.begin(), path.end());
        Position center = ToWorld({sx + .5f, sy + .5f, 0});
        if (Distance(from, center) > .12f)
        {
            path.insert(path.begin(), center);
        }
        path.push_back(to);
        return path;
    }

    bool FirstLevel::PickUp()
    {
        Perf::Scope scope(Perf::Cpu::Loot);
        bool collected = false;
        for (ActorId id : scene.FindByKind(ActorKind::Loot))
        {
            Loot* loot = dynamic_cast<Loot*>(scene.Find(id));
            if (!loot)
            {
                continue;
            }
            Position location = loot->WorldPosition();
            if (Distance(Player().WorldPosition(), location) > 1.3f ||
                !ClearLine(Player().WorldPosition(), location))
            {
                continue;
            }
            if (loot->kind == LootKind::Coins)
            {
                stats.coins += loot->amount;
            }
            if (loot->kind == LootKind::Hide)
            {
                stats.hides += loot->amount;
            }
            if (loot->kind == LootKind::Potion)
            {
                stats.potions += loot->amount;
            }
            scene.Destroy(loot->Id());
            collected = true;
        }
        if (collected)
        {
            Notify(
                L"\uc804\ub9ac\ud488\uc744 \ucc59\uacbc\ub2e4. \ud68c\ubcf5\uc57d\uc740 Q\ub85c \ub9c8\uc2e4 \uc218 \uc788\ub2e4.");
        }
        return collected;
    }

    void FirstLevel::UsePotion()
    {
        if (stats.potions <= 0)
        {
            Notify(
                L"\ud68c\ubcf5\uc57d\uc774 \uc5c6\ub2e4. \ub9c8\uc744\uc5d0\uc11c \uc26c\uac70\ub098 \uc804\ub9ac\ud488\uc744 \ucc3e\uc544\ubcf4\uc790.");
            return;
        }
        if (stats.health >= stats.MaxHealth())
        {
            Notify(
                L"\uc544\uc9c1 \uc57d\uc744 \ub9c8\uc2e4 \ub9cc\ud07c \ub2e4\uce58\uc9c4 \uc54a\uc558\ub2e4.");
            return;
        }
        --stats.potions;
        stats.health = (std::min)(stats.MaxHealth(), stats.health + 45);
        Notify(
            L"\uc4f4 \uc57d\uc744 \uc0bc\ucf30\ub2e4. \uc774\uc81c \uc880 \ubc84\ud2f8 \ub9cc\ud558\ub2e4.");
    }

    void FirstLevel::Allocate(int attribute)
    {
        if (stats.Allocate(attribute))
        {
            Notify(
                L"\ub2a5\ub825\uce58\ub97c \uc62c\ub838\ub2e4. TAB\uc744 \ub20c\ub7ec \uc218\uce58\ub97c \ud655\uc778\ud574 \ubcf4\uc790.");
        }
        else
        {
            Notify(
                L"\uc544\uc9c1 \uc4f8 \uc218 \uc788\ub294 \ub2a5\ub825\uce58 \uc810\uc218\uac00 \uc5c6\ub2e4. \uacbd\ud5d8\uce58\ub97c \ub354 \uc313\uc544\ubcf4\uc790.");
        }
    }

    void FirstLevel::Notify(const std::wstring& text)
    {
        message = text;
        messageTime = 6;
    }

    void FirstLevel::Update(float dt)
    {
        Perf::Scope scope(Perf::Cpu::WorldUpdate);
        attackCooldown = (std::max)(0.0f, attackCooldown - dt);
        attackAnimation = (std::max)(0.0f, attackAnimation - dt);
        if (attackImpactPending && attackAnimation <= CharacterPresentation::AttackDuration -
                                                          CharacterPresentation::AttackImpactTime)
        {
            attackImpactPending = false;
            if (Player().IsActive())
            {
                ResolveAttackImpact();
                attackImpactReady = true;
            }
        }
        hurtFlash = (std::max)(0.0f, hurtFlash - dt);
        invulnerability = (std::max)(0.0f, invulnerability - dt);
        messageTime -= dt;
        navigationTime -= dt;
        if (navigationTime <= 0)
        {
            BuildNavigation();
            navigationTime = .35f;
        }

        for (ActorId id : scene.FindByKind(ActorKind::Loot))
        {
            Loot* loot = dynamic_cast<Loot*>(scene.Find(id));
            if (!loot)
            {
                continue;
            }
            Perf::Count(Perf::Counter::LootUpdated);
            loot->lifetime -= dt;
            if (loot->lifetime <= 0)
            {
                scene.Destroy(loot->Id());
            }
        }

        {
            Perf::Scope aiScope(Perf::Cpu::AI);
            for (Enemy* instance : scene.Actors<Enemy>(ActorKind::Enemy))
            {
                Enemy& enemy = *instance;
                Perf::Count(Perf::Counter::EnemiesUpdated);
                enemy.phase += dt;
                enemy.flash = (std::max)(0.0f, enemy.flash - dt);
                enemy.cooldown = (std::max)(0.0f, enemy.cooldown - dt);
                if (enemy.health <= 0)
                {
                    enemy.respawn -= dt;
                    if (enemy.respawn <= 0 && !RespawnEnemy(enemy))
                    {
                        // Retry cheaply rather than forcing a spawn in a village or on a character.
                        enemy.respawn = .5f;
                    }
                    continue;
                }

                float distance = Distance(Player().WorldPosition(), enemy.WorldPosition());
                Position target = enemy.HomePosition();
                bool chasing =
                    distance < 6 && Player().IsActive() &&
                    !InCamp(int(ToGrid(Player().WorldPosition()).x), int(ToGrid(Player().WorldPosition()).y));
                if (chasing)
                {
                    if (ClearLine(enemy.WorldPosition(), Player().WorldPosition()))
                    {
                        target = Player().WorldPosition();
                    }
                    else
                    {
                        Position gridEnemy = ToGrid(enemy.WorldPosition());
                        int x = int(std::floor(gridEnemy.x)), y = int(std::floor(gridEnemy.y));
                        int best =
                            x >= 0 && y >= 0 && x < MapSize && y < MapSize ? navigation[y * MapSize + x] : -1;
                        target = enemy.WorldPosition();
                        const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
                        for (int k = 0; k < 4; ++k)
                        {
                            if (!Open(x + dx[k], y + dy[k]))
                            {
                                continue;
                            }
                            int value = navigation[(y + dy[k]) * MapSize + x + dx[k]];
                            if (value >= 0 && (best < 0 || value < best))
                            {
                                best = value;
                                target = ToWorld({x + dx[k] + .5f, y + dy[k] + .5f, 0});
                            }
                        }
                    }
                }
                else
                {
                    Position home = enemy.HomePosition();
                    Position wander = {
                        home.x + std::sin(enemy.phase * .3f), home.y + std::cos(enemy.phase * .27f), home.z};
                    if (Walkable(wander.x, wander.y))
                    {
                        target = wander;
                    }
                }

                float dx = target.x - enemy.WorldPosition().x, dy = target.y - enemy.WorldPosition().y;
                float length = std::hypot(dx, dy);
                Position before = enemy.WorldPosition();
                if (length > .15f && (!chasing || distance > .55f))
                {
                    float speed = chasing ? 1.15f + enemy.kind * .12f : .35f;
                    float amount = (std::min)(speed * dt, length);
                    Move(enemy, dx / length * amount, dy / length * amount);
                }
                enemy.moving = Distance(before, enemy.WorldPosition()) > .0001f;

                if (chasing && distance < .9f && enemy.cooldown <= 0 && invulnerability <= 0 &&
                    ClearLine(enemy.WorldPosition(), Player().WorldPosition()))
                {
                    enemy.cooldown = 1.1f;
                    DamagePlayer(11 + enemy.kind * 3);
                }
            }
        }
        UpdateActorModels();
    }
} // namespace Level
