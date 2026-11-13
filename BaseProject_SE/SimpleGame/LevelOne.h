#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "SceneGraph.h"
#include "WorldLayout.h"

class GameSave;

namespace Level
{
    constexpr int MapSize = WorldLayout::MapSize;
    constexpr float MoveSpeed = 300.0f;
    constexpr float DodgeDuration = .18f;
    constexpr float DodgeProtection = .12f;
    constexpr float DodgeCooldown = .85f;
    constexpr float DodgeSpeedScale = 2.5f;
    constexpr int MaximumLevel = 30;

    using Position = ActorPosition;

    enum class PathPurpose
    {
        Npc,
        Quest
    };

    enum class Terrain
    {
        Grass,
        Road,
        Water
    };

    struct Tile
    {
        Terrain terrain = Terrain::Grass;
        bool blocked = false;
    };

    struct Stats
    {
        static constexpr int StrengthBonus = 3;
        static constexpr int VitalityBonus = 15;
        static constexpr int ResilienceBonus = 2;

        int level = 1;
        int experience = 0;
        int totalExperience = 0;
        int points = 0;
        int strength = 0;
        int vitality = 0;
        int resilience = 0;
        int health = 100;
        int potions = 3;
        int hides = 0;
        int coins = 0;
        int kills = 0;

        int RequiredExperience() const;
        int MaxHealth() const;
        int Attack() const;
        int Defense() const;
        void GainExperience(int amount);
        bool Allocate(int attribute);
    };

    std::wstring DescribeAttributeBonus(int attribute);

    struct Enemy : public Actor
    {
        Position home;
        int kind = 0;
        int health = 0;
        int maxHealth = 0;
        float phase = 0;
        float locomotionPhase = 0;
        float attackTime = -1;
        bool attackImpactPending = false;
        ActorId attackTarget = InvalidActorId;
        Position attackForward;
        float cooldown = 0;
        float respawn = 0;
        Position deathPosition;
        float flash = 0;
        bool moving = false;
        bool running = false;

        Position HomePosition() const
        {
            Position world = WorldPosition();
            Position local = LocalPosition();
            return {world.x - local.x + home.x, world.y - local.y + home.y, world.z - local.z + home.z};
        }
    };

    struct EnemyAttackImpact
    {
        ActorId source = InvalidActorId;
        ActorId target = InvalidActorId;
        int damage = 0;
    };

    struct HitImpact : public Actor
    {
        float age = 0;
        float duration = .46f;
        float directionX = 1;
        float directionY = 0;
        int damage = 0;
        bool outgoing = false;
        bool incoming = false;
    };

    enum class LootKind
    {
        Coins,
        Potion,
        Hide
    };

    std::wstring DescribeLoot(LootKind kind, int amount);

    struct Loot : public Actor
    {
        LootKind kind = LootKind::Coins;
        int amount = 1;
        float lifetime = 90;
    };

    class FirstLevel
    {
      public:
        FirstLevel();
        void Generate(std::uint32_t seed);
        void Update(float dt);
        void MovePlayer(float screenX, float screenY, float dt);
        bool BeginDodge(float screenX, float screenY);

        bool Dodging() const
        {
            return dodgeRemaining > 0;
        }

        bool CanDodge() const
        {
            return Player().IsActive() && dodgeCooldown <= 0 && attackAnimation <= 0;
        }

        float DodgeReadiness() const
        {
            return 1 - dodgeCooldown / DodgeCooldown;
        }

        bool Attack();
        bool ConsumeAttackImpact();
        bool DamageEnemy(Enemy& enemy, int damage, bool playerCredit, ActorId source = InvalidActorId);
        void ReportHit(Actor& target, int damage, ActorId source);
        float HitFlash(const Actor& actor) const;
        Position HitCameraOffset() const;
        bool BeginEnemyAttack(Enemy& enemy, ActorId target);
        void CancelEnemyAttacksTargeting(ActorId target);
        std::vector<EnemyAttackImpact> ConsumeEnemyAttackImpacts();
        bool DamagePlayer(int damage, ActorId source = InvalidActorId);
        void MoveActor(Actor& actor, float dx, float dy);
        std::vector<Position> FindPath(const Position& from,
                                       const Position& to,
                                       PathPurpose purpose = PathPurpose::Npc) const;
        bool PickUp();
        void UsePotion();
        void Allocate(int attribute);
        bool Walkable(float x, float y) const;
        bool ClearLine(const Position& from, const Position& to) const;
        bool AllWalkableConnected() const;
        const Tile& At(int x, int y) const;
        void UpdateVisibleChunks();
        void DrainFloodedLane();

        std::uint64_t MapRevision() const
        {
            return (std::uint64_t(seed) << 32) ^ scene.CollisionRevision();
        }

        std::vector<const Actor*> Props() const
        {
            return scene.Actors<Actor>(ActorKind::Prop);
        }

        std::vector<const Enemy*> Enemies() const
        {
            return scene.Actors<Enemy>(ActorKind::Enemy);
        }

        std::vector<const Loot*> Drops() const
        {
            return scene.Actors<Loot>(ActorKind::Loot);
        }

        SceneGraph& Scene()
        {
            return scene;
        }

        const SceneGraph& Scene() const
        {
            return scene;
        }

        Actor& Player()
        {
            return scene.Get(playerId);
        }

        const Actor& Player() const
        {
            return scene.Get(playerId);
        }

        ActorId NpcRoot() const
        {
            return npcRoot;
        }

        ActorId OverlayRoot() const
        {
            return overlayRoot;
        }

        Stats stats;
        std::uint32_t seed = 0;
        bool moving = false;
        float walkPhase = 0;
        float attackAnimation = 0;
        float facingX = 1;
        float facingY = 0;
        float hurtFlash = 0;
        float messageTime = 0;
        std::wstring message;

      private:
        friend class ::GameSave;

        mutable std::array<Tile, MapSize * MapSize> tiles;
        std::array<int, MapSize * MapSize> navigation;
        std::array<ActorId, WorldLayout::ChunksPerSide * WorldLayout::ChunksPerSide> terrainChunks{};
        std::array<ActorId, WorldLayout::ChunksPerSide * WorldLayout::ChunksPerSide> propChunks{};
        mutable std::vector<int> pathCost, pathParent;
        mutable std::vector<std::uint32_t> pathStamp;
        mutable std::uint32_t pathSearch = 0;
        SceneGraph scene;
        ActorId playerId = InvalidActorId;
        ActorId terrainRoot = InvalidActorId;
        ActorId propRoot = InvalidActorId;
        ActorId enemyRoot = InvalidActorId;
        ActorId lootRoot = InvalidActorId;
        ActorId npcRoot = InvalidActorId;
        ActorId overlayRoot = InvalidActorId;
        ActorId impactRoot = InvalidActorId;
        std::array<ActorId, 48> impactActors{};
        size_t impactCount = 0;
        size_t nextImpact = 0;
        float feedbackTime = 0;
        float cameraImpulse = 0;
        mutable std::uint64_t collisionRevision = 0;
        bool generating = false;
        std::mt19937 random;
        std::vector<Position> spawnCandidates;
        std::vector<EnemyAttackImpact> enemyAttackImpacts;
        float attackCooldown = 0;
        bool attackImpactPending = false;
        bool attackImpactReady = false;
        float navigationTime = 0;
        float invulnerability = 0;
        float dodgeRemaining = 0;
        float dodgeCooldown = 0;
        float dodgeX = 0;
        float dodgeY = 0;

        bool Open(int x, int y) const;
        void InitializeScene();
        void Carve(int x, int y);
        void ConnectGround();
        void BuildNavigation(bool localOnly = true);
        void Move(Actor& actor, float dx, float dy);
        void RefreshActorObstacles() const;
        void AddProp(const Position& position, int kind, bool blocksMovement = false);
        Loot& AddLoot(const Position& position, LootKind kind, int amount);
        void UpdateActorModels();
        void ResolveAttackImpact();
        void UpdateEnemyAttack(Enemy& enemy, float dt);
        void CancelEnemyAttack(Enemy& enemy);
        void UpdateHitFeedback(float dt);
        void RefreshTerrainAppearance();
        void BuildHarborScenery();
        Position ToGrid(const Position& world) const;
        Position ToWorld(const Position& grid) const;
        void Kill(Enemy& enemy, bool playerCredit);
        bool RespawnEnemy(Enemy& enemy);
        void Notify(const std::wstring& text);
    };
} // namespace Level
