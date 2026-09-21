#include "stdafx.h"
#include "Actor.h"
#include "SceneGraph.h"

ActorId Actor::ParentId() const
{
    return m_Parent ? m_Parent->Id() : InvalidActorId;
}

ActorPosition Actor::WorldPosition() const
{
    ActorPosition result = m_Position;
    for (const Actor* parent = m_Parent; parent; parent = parent->m_Parent)
    {
        result.x += parent->m_Position.x;
        result.y += parent->m_Position.y;
        result.z += parent->m_Position.z;
    }
    return result;
}

void Actor::SetLocalPosition(const ActorPosition& position)
{
    m_Position = position;
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
