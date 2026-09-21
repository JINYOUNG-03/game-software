#include "stdafx.h"
#include "SceneGraph.h"
#include <algorithm>

SceneGraph::SceneGraph()
{
    std::unique_ptr<Actor> root(new Actor());
    root->m_Id = m_NextId++;
    root->m_Name = "Scene";
    root->m_Scene = this;
    m_Root = root->Id();
    m_Actors.emplace(m_Root, std::move(root));
}

Actor* SceneGraph::Find(ActorId id)
{
    auto it = m_Actors.find(id);
    return it == m_Actors.end() ? nullptr : it->second.get();
}

const Actor* SceneGraph::Find(ActorId id) const
{
    auto it = m_Actors.find(id);
    return it == m_Actors.end() ? nullptr : it->second.get();
}

void SceneGraph::InvalidateCollisionSubtree(const Actor& actor)
{
    if (actor.BlocksMovement() || actor.Kind() == ActorKind::Terrain)
    {
        ++m_CollisionRevision;
    }
    for (ActorId child : actor.Children())
    {
        InvalidateCollisionSubtree(*Find(child));
    }
}

bool SceneGraph::Reparent(ActorId childId, ActorId parentId, bool preserveWorldPosition)
{
    Actor* child = Find(childId);
    Actor* parent = Find(parentId);
    if (!child || !parent || childId == m_Root || child == parent)
    {
        return false;
    }
    for (Actor* ancestor = parent; ancestor; ancestor = ancestor->m_Parent)
    {
        if (ancestor == child)
        {
            return false;
        }
    }
    ActorPosition world = child->WorldPosition();
    auto& siblings = child->m_Parent->m_Children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), childId), siblings.end());
    parent->m_Children.push_back(childId);
    child->m_Parent = parent;
    if (preserveWorldPosition)
    {
        child->SetWorldPosition(world);
    }
    InvalidateCollisionSubtree(*child);
    return true;
}

void SceneGraph::DestroyChildren(ActorId parent)
{
    Actor* actor = Find(parent);
    if (!actor)
    {
        return;
    }
    const auto children = actor->Children();
    for (ActorId child : children)
    {
        Destroy(child);
    }
}

bool SceneGraph::Destroy(ActorId id)
{
    Actor* actor = Find(id);
    if (!actor || id == m_Root)
    {
        return false;
    }
    InvalidateCollisionSubtree(*actor);
    DestroyChildren(id);
    auto& siblings = actor->m_Parent->m_Children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    m_Actors.erase(id);
    return true;
}

std::vector<ActorId> SceneGraph::FindByKind(ActorKind kind, bool activeOnly) const
{
    std::vector<ActorId> result;
    for (const auto& item : m_Actors)
    {
        if (item.second->Kind() == kind && (!activeOnly || item.second->IsActive()))
        {
            result.push_back(item.first);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void SceneGraph::Collect(const Actor& actor, ScenePass pass, std::vector<const Actor*>& result) const
{
    if (!actor.IsActive() || !actor.IsVisible())
    {
        return;
    }
    if (actor.drawable && actor.pass == pass)
    {
        result.push_back(&actor);
    }
    for (ActorId child : actor.Children())
    {
        Collect(*Find(child), pass, result);
    }
}

std::vector<const Actor*> SceneGraph::RenderList(ScenePass pass) const
{
    std::vector<const Actor*> result;
    Collect(*Find(m_Root), pass, result);
    std::stable_sort(result.begin(),
                     result.end(),
                     [pass](const Actor* a, const Actor* b)
                     {
                         if (a->drawOrder != b->drawOrder)
                         {
                             return a->drawOrder < b->drawOrder;
                         }
                         if (pass != ScenePass::Overlay)
                         {
                             auto pa = a->WorldPosition();
                             auto pb = b->WorldPosition();
                             float da = pa.x + pa.y;
                             float db = pb.x + pb.y;
                             if (da != db)
                             {
                                 return da < db;
                             }
                         }
                         return a->Id() < b->Id();
                     });
    return result;
}
