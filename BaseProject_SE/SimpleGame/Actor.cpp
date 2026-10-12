#include "stdafx.h"
#include "Actor.h"
#include "SceneGraph.h"

ActorId Actor::ParentId() const
{
    return m_Parent ? m_Parent->Id() : InvalidActorId;
}

ActorPosition Actor::WorldPosition() const
{
    if (m_WorldDirty)
    {
        m_CachedWorld = m_Position;
        if (m_Parent)
        {
            auto parent = m_Parent->WorldPosition();
            m_CachedWorld.x += parent.x;
            m_CachedWorld.y += parent.y;
            m_CachedWorld.z += parent.z;
        }
        m_WorldDirty = false;
    }
    return m_CachedWorld;
}

void Actor::InvalidateWorldPosition()
{
    m_WorldDirty = true;
    if (m_Scene)
    {
        for (ActorId child : m_Children)
        {
            if (auto actor = m_Scene->Find(child))
            {
                actor->InvalidateWorldPosition();
            }
        }
    }
}

void Actor::SetLocalPosition(const ActorPosition& position)
{
    if (m_Position.x == position.x && m_Position.y == position.y && m_Position.z == position.z)
    {
        return;
    }
    m_Position = position;
    InvalidateWorldPosition();
    if (m_Scene)
    {
        m_Scene->InvalidateCollisionSubtree(*this);
    }
}

void Actor::SetWorldPosition(const ActorPosition& position)
{
    ActorPosition local = position;
    if (m_Parent)
    {
        ActorPosition parent = m_Parent->WorldPosition();
        local.x -= parent.x;
        local.y -= parent.y;
        local.z -= parent.z;
    }
    SetLocalPosition(local);
}

bool Actor::IsVisible() const
{
    return m_Visible && (!m_Parent || m_Parent->IsVisible());
}

bool Actor::IsActive() const
{
    return m_Active && (!m_Parent || m_Parent->IsActive());
}

void Actor::SetActive(bool active)
{
    if (m_Active != active)
    {
        m_Active = active;
        if (m_Scene)
        {
            m_Scene->InvalidateCollisionSubtree(*this);
        }
    }
}

void Actor::SetBlocksMovement(bool blocked)
{
    if (m_BlocksMovement != blocked)
    {
        m_BlocksMovement = blocked;
        if (m_Scene)
        {
            ++m_Scene->m_CollisionRevision;
        }
    }
}

void Actor::SetVariant(int value)
{
    if (variant != value)
    {
        variant = value;
        if (m_Scene && Kind() == ActorKind::Terrain)
        {
            m_Scene->InvalidateCollisionSubtree(*this);
        }
    }
}
