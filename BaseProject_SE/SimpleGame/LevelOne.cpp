#include "stdafx.h"
#include "LevelOne.h"

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
        const Position Camp = {13.5f, 12.5f, 0};

        float Distance(const Position& a, const Position& b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        bool InCamp(int x, int y)
        {
            // Includes every village NPC, including the laundry area south of the square.
            return x >= 8 && x <= 18 && y >= 6 && y <= 20;
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

    FirstLevel::FirstLevel() = default;

    const Tile& FirstLevel::At(int x, int y) const
    {
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
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            return false;
        }
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

    void FirstLevel::BuildNavigation()
    {
        navigation.fill(-1);
        int x = int(player.x), y = int(player.y);
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
            const int dx[] = {1, -1, 0, 0};
            const int dy[] = {0, 0, 1, -1};
            for (int i = 0; i < 4; ++i)
            {
                int nx = id % MapSize + dx[i], ny = id / MapSize + dy[i];
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
        BuildNavigation();
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
                BuildNavigation();
            }
        }
    }

    void FirstLevel::Generate(std::uint32_t nextSeed)
    {
        seed = nextSeed;
        random.seed(seed);
        tiles.fill(Tile{});
        props.clear();
        enemies.clear();
        drops.clear();
        player = Camp;
        moving = false;
        attackAnimation = 0;
        attackCooldown = 0;
        invulnerability = 2;
        navigationTime = 0;
        hurtFlash = 0;
        stats.health = stats.MaxHealth();

        std::uniform_int_distribution<int> coordinate(3, MapSize - 4);
        for (int lake = 0; lake < 7; ++lake)
        {
            float cx = float(coordinate(random)), cy = float(coordinate(random));
            float rx = 2.5f + float(random() % 35) / 10.0f;
            float ry = 2.5f + float(random() % 35) / 10.0f;
            for (int y = 1; y < MapSize - 1; ++y)
            {
                for (int x = 1; x < MapSize - 1; ++x)
                {
                    if (InCamp(x, y))
                    {
                        continue;
                    }
                    float dx = (x - cx) / rx, dy = (y - cy) / ry;
                    if (dx * dx + dy * dy < 1)
                    {
                        tiles[y * MapSize + x].terrain = Terrain::Water;
                    }
                }
            }
        }

        // Random destinations joined by broad roads; village is the only fixed safe landmark.
        for (int route = 0; route < 7; ++route)
        {
            int x = int(Camp.x), y = int(Camp.y);
            int tx = coordinate(random), ty = coordinate(random);
            while (x != tx || y != ty)
            {
                Carve(x, y);
                Carve(x + 1, y);
                if (x != tx && (y == ty || random() % 2 == 0))
                {
                    x += x < tx ? 1 : -1;
                }
                else
                {
                    y += y < ty ? 1 : -1;
                }
            }
        }
        for (int x = 8; x <= 19; ++x)
        {
            Carve(x, 12);
        }
        for (int y = 5; y <= 19; ++y)
        {
            Carve(13, y);
        }
        ConnectGround();

        // A prop is accepted only when removing its footprint preserves connectivity.
        auto addBlock = [&](int x, int y, int kind)
        {
            if (!Open(x, y) || At(x, y).terrain == Terrain::Road)
            {
                return;
            }
            tiles[y * MapSize + x].blocked = true;
            if (!AllWalkableConnected())
            {
                tiles[y * MapSize + x].blocked = false;
                return;
            }
            props.push_back({{x + .5f, y + .5f, 0}, kind});
        };
        for (int x : {9, 17})
        {
            for (int y : {7, 11, 15})
            {
                addBlock(x, y, int(random() % 2));
            }
        }
        props.push_back({{12, 9, 0}, 2});
        props.push_back({{14, 9, 0}, 2});
        props.push_back({{12, 5, 0}, 3});
        props.push_back({{14, 5, 0}, 3});
        for (int y = 7; y <= 17; y += 3)
        {
            props.push_back({{14, float(y), 0}, 5});
        }
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            int x = coordinate(random), y = coordinate(random);
            if (!InCamp(x, y))
            {
                addBlock(x, y, 7);
            }
        }

        // Keep a spawn list of valid centers. No unbounded rejection loops.
        std::vector<Position> candidates;
        for (int y = 2; y < MapSize - 2; ++y)
        {
            for (int x = 2; x < MapSize - 2; ++x)
            {
                Position p = {x + .5f, y + .5f, 0};
                if (!InCamp(x, y) && Walkable(p.x, p.y) && Distance(p, Camp) > 6)
                {
                    candidates.push_back(p);
                }
            }
        }
        std::shuffle(candidates.begin(), candidates.end(), random);
        for (const auto& p : candidates)
        {
            bool crowded = false;
            for (const auto& e : enemies)
            {
                if (Distance(e.home, p) < 2.3f)
                {
                    crowded = true;
                    break;
                }
            }
            if (crowded)
            {
                continue;
            }
            Enemy enemy;
            enemy.position = enemy.home = p;
            enemy.kind = int(random() % 3);
            enemy.health = enemy.maxHealth = 42 + enemy.kind * 18;
            enemy.phase = float(random() % 100) / 10;
            enemies.push_back(enemy);
            if (enemies.size() == 36)
            {
                break;
            }
        }

        BuildNavigation();
        Notify(
            L"\uc0ac\ub0e5\ud130\uc5d0 \ub3c4\ucc29\ud588\ub2e4. \uc9d0\uc2b9\uc744 \uc7a1\uace0, \uc804\ub9ac\ud488\uc744 \ucc59\uae30\uba70 \ubab8\uc744 \ub2e8\ub828\ud574 \ubcf4\uc790.");
    }

    void FirstLevel::Move(Position& position, float dx, float dy)
    {
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
    }

    void FirstLevel::MovePlayer(float sx, float sy, float dt)
    {
        float length = std::hypot(sx, sy);
        moving = false;
        if (length <= 0)
        {
            return;
        }
        facingX = sx / length;
        facingY = sy / length;
        Position before = player;
        float dx = (facingX / 96 + facingY / 48) * MoveSpeed * dt;
        float dy = (facingY / 48 - facingX / 96) * MoveSpeed * dt;
        Move(player, dx, dy);
        moving = Distance(before, player) > .0001f;
        if (moving)
        {
            walkPhase += dt * 10;
        }
    }

    bool FirstLevel::ClearLine(const Position& from, const Position& to) const
    {
        int steps = (std::max)(1, int(std::ceil(Distance(from, to) * 10)));
        for (int i = 0; i <= steps; ++i)
        {
            float t = float(i) / steps;
            if (!Open(int(std::floor(from.x + (to.x - from.x) * t)),
                      int(std::floor(from.y + (to.y - from.y) * t))))
            {
                return false;
            }
        }
        return true;
    }

    void FirstLevel::Kill(Enemy& enemy)
    {
        enemy.health = 0;
        enemy.respawn = 14;
        enemy.moving = false;
        ++stats.kills;
        int previousLevel = stats.level;
        int reward = 25 + enemy.kind * 10;
        stats.GainExperience(reward);

        // Kill state changes before rewards; a held attack cannot award XP twice.
        if (drops.size() > 180)
        {
            drops.erase(drops.begin(), drops.begin() + 30);
        }
        drops.push_back({enemy.position, LootKind::Coins, 3 + enemy.kind * 2, 90});
        Position second = enemy.position;
        drops.push_back({second, random() % 3 == 0 ? LootKind::Potion : LootKind::Hide, 1, 90});
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

    void FirstLevel::Attack()
    {
        if (attackCooldown > 0)
        {
            return;
        }
        attackCooldown = .48f;
        attackAnimation = .22f;
        for (auto& enemy : enemies)
        {
            if (enemy.health <= 0)
            {
                continue;
            }
            float sx = ((enemy.position.x - player.x) - (enemy.position.y - player.y)) * HalfTileWidth;
            float sy = ((enemy.position.x - player.x) + (enemy.position.y - player.y)) * HalfTileHeight;
            float length = std::hypot(sx, sy);
            if (length > 78 || (length > 12 && (sx * facingX + sy * facingY) / length < .1f) ||
                !ClearLine(player, enemy.position))
            {
                continue;
            }
            enemy.health -= stats.Attack();
            enemy.flash = .18f;
            if (enemy.health <= 0)
            {
                Kill(enemy);
            }
        }
    }

    bool FirstLevel::PickUp()
    {
        bool collected = false;
        for (auto it = drops.begin(); it != drops.end();)
        {
            if (Distance(player, it->position) > 1.3f || !ClearLine(player, it->position))
            {
                ++it;
                continue;
            }
            if (it->kind == LootKind::Coins)
            {
                stats.coins += it->amount;
            }
            if (it->kind == LootKind::Hide)
            {
                stats.hides += it->amount;
            }
            if (it->kind == LootKind::Potion)
            {
                stats.potions += it->amount;
            }
            it = drops.erase(it);
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
        attackCooldown = (std::max)(0.0f, attackCooldown - dt);
        attackAnimation = (std::max)(0.0f, attackAnimation - dt);
        hurtFlash = (std::max)(0.0f, hurtFlash - dt);
        invulnerability = (std::max)(0.0f, invulnerability - dt);
        messageTime -= dt;
        navigationTime -= dt;
        if (navigationTime <= 0)
        {
            BuildNavigation();
            navigationTime = .35f;
        }

        for (auto it = drops.begin(); it != drops.end();)
        {
            it->lifetime -= dt;
            if (it->lifetime <= 0)
            {
                it = drops.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto& enemy : enemies)
        {
            enemy.phase += dt;
            enemy.flash = (std::max)(0.0f, enemy.flash - dt);
            enemy.cooldown = (std::max)(0.0f, enemy.cooldown - dt);
            if (enemy.health <= 0)
            {
                enemy.respawn -= dt;
                if (enemy.respawn <= 0 && Distance(player, enemy.home) > 3)
                {
                    enemy.position = enemy.home;
                    enemy.health = enemy.maxHealth;
                    enemy.cooldown = 1;
                }
                continue;
            }

            float distance = Distance(player, enemy.position);
            Position target = enemy.home;
            bool chasing = distance < 6 && !InCamp(int(player.x), int(player.y));
            if (chasing)
            {
                if (ClearLine(enemy.position, player))
                {
                    target = player;
                }
                else
                {
                    int x = int(enemy.position.x), y = int(enemy.position.y);
                    int best = navigation[y * MapSize + x];
                    target = enemy.position;
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
                            target = {x + dx[k] + .5f, y + dy[k] + .5f, 0};
                        }
                    }
                }
            }
            else
            {
                Position wander = {enemy.home.x + std::sin(enemy.phase * .3f),
                                   enemy.home.y + std::cos(enemy.phase * .27f),
                                   0};
                if (Walkable(wander.x, wander.y))
                {
                    target = wander;
                }
            }

            float dx = target.x - enemy.position.x, dy = target.y - enemy.position.y;
            float length = std::hypot(dx, dy);
            Position before = enemy.position;
            if (length > .15f && (!chasing || distance > .55f))
            {
                float speed = chasing ? 1.15f + enemy.kind * .12f : .35f;
                float amount = (std::min)(speed * dt, length);
                Move(enemy.position, dx / length * amount, dy / length * amount);
            }
            enemy.moving = Distance(before, enemy.position) > .0001f;

            if (chasing && distance < .9f && enemy.cooldown <= 0 && invulnerability <= 0 &&
                ClearLine(enemy.position, player))
            {
                int damage = (std::max)(1, 11 + enemy.kind * 3 - stats.Defense());
                stats.health -= damage;
                enemy.cooldown = 1.1f;
                invulnerability = .6f;
                hurtFlash = .22f;
                if (stats.health <= 0)
                {
                    player = Camp;
                    stats.health = stats.MaxHealth();
                    invulnerability = 3;
                    navigationTime = 0;
                    Notify(
                        L"\ub9c8\uc744 \uc0ac\ub78c\ub4e4\uc774 \ub370\ub824\ub2e4 \uc8fc\uc5c8\ub2e4. \uacbd\ud5d8\uce58\ub294 \uadf8\ub300\ub85c\ub2e4. \uc228\uc744 \uace0\ub974\uace0 \ub2e4\uc2dc \ub098\uac00\uc790.");
                    break;
                }
            }
        }
    }
} // namespace Level
