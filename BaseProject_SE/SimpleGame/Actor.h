#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

using ActorId = std::uint64_t;
constexpr ActorId InvalidActorId = 0;

struct ActorPosition
{
    float x = 0;
    float y = 0;
    float z = 0;
};

enum class ActorKind
{
    Group,
    Terrain,
    Prop,
    Player,
    Npc,
    Enemy,
    Loot,
    Effect,
    UI,
    Count
};

enum class ScenePass
{
    Ground,
    World,
    Effects,
    Overlay,
    Count
};

enum class ActorEffect
{
    Flame,
    Smoke,
    SwordTrail,
    PlayerHurt,
    EnemyStatus,
    NpcStatus,
    WarmLight,
    HarborMist
};

class SceneGraph;

// A model is geometry; an Actor is one independently controlled placement of that model.
class Actor
{
  public:
    virtual ~Actor() = default;
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    ActorId Id() const
    {
        return m_Id;
    }

    ActorId ParentId() const;

    const std::string& Name() const
    {
        return m_Name;
    }

    ActorKind Kind() const
    {
        return m_Kind;
    }

    const std::vector<ActorId>& Children() const
    {
        return m_Children;
    }

    const ActorPosition& LocalPosition() const
    {
        return m_Position;
    }

    ActorPosition WorldPosition() const;
    void SetLocalPosition(const ActorPosition& position);
    void SetWorldPosition(const ActorPosition& position);

    void SetVisible(bool visible)
    {
        m_Visible = visible;
    }

    bool IsVisible() const;
    void SetActive(bool active);
    bool IsActive() const;
    void SetBlocksMovement(bool blocked);
    void SetVariant(int value);

    bool BlocksMovement() const
    {
        return m_BlocksMovement;
    }

    // Group nodes do not render. Children inherit position, visibility and activity.
    ScenePass pass = ScenePass::World;
    std::string model;
    int drawOrder = 0;
    int variant = 0;
    float shadowWidth = 0;
    float shadowHeight = 0;
    float visualHeight = 0;
    bool mirrorModel = false;
    // Derived from shared terrain, not a second collision/position representation.
    // Water/land edge mask, road edge mask, water depth, flooded-lane wetness.
    std::array<float, 4> surfaceData{{0, 0, -1, 0}};
    bool drawable = false;

  protected:
    Actor() = default;

  private:
    friend class SceneGraph;
    SceneGraph* m_Scene = nullptr;
    Actor* m_Parent = nullptr;
    ActorId m_Id = InvalidActorId;
    ActorKind m_Kind = ActorKind::Group;
    std::string m_Name;
    std::vector<ActorId> m_Children;
    void InvalidateWorldPosition();
    mutable ActorPosition m_CachedWorld;
    mutable bool m_WorldDirty = true;
    ActorPosition m_Position;
    bool m_Visible = true;
    bool m_Active = true;
    bool m_BlocksMovement = false;
};
