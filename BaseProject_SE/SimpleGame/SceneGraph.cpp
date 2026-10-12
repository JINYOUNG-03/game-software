#include "stdafx.h"
#include "SceneGraph.h"
#include <algorithm>
#include <cstring>
#include "Profiler.h"

SceneGraph::SceneGraph()
{
    std::unique_ptr<Actor> root(new Actor());
    root->m_Id = m_NextId++;
    root->m_Name = "Scene";
    root->m_Scene = this;
    m_Root = root->Id();
    m_Actors.emplace(m_Root, std::move(root));
    m_ByKind[size_t(ActorKind::Group)].push_back(m_Root);
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
    child->InvalidateWorldPosition();
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
    auto& kindIndex = m_ByKind[size_t(actor->Kind())];
    kindIndex.erase(std::remove(kindIndex.begin(), kindIndex.end(), id), kindIndex.end());
    m_Actors.erase(id);
    return true;
}

std::vector<ActorId> SceneGraph::FindByKind(ActorKind kind, bool activeOnly) const
{
    Perf::Count(Perf::Counter::KindQueries);
    std::vector<ActorId> result;
    if (size_t(kind) >= m_ByKind.size())
    {
        return result;
    }
    const auto& index = m_ByKind[size_t(kind)];
    result.reserve(index.size());
    for (ActorId id : index)
    {
        const Actor* actor = Find(id);
        if (!activeOnly || actor->IsActive())
        {
            result.push_back(id);
        }
    }
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

void SceneGraph::PrepareFrame(const std::function<bool(const Actor&)>& visible)
{
    Perf::Scope scope(Perf::Cpu::ScenePrepare);
    for (auto& list : m_FrameLists)
    {
        list.clear();
    }
    m_TerrainList.clear();
    m_TerrainFingerprint = 1469598103934665603ull;
    Perf::Count(Perf::Counter::ActorsTotal, m_Actors.size());
    CollectFrame(*Find(m_Root), visible);
    Perf::Scope sorting(Perf::Cpu::SceneSort);
    for (size_t pass = 0; pass < m_FrameLists.size(); ++pass)
    {
        auto& list = m_FrameLists[pass];
        Perf::Count(Perf::Counter::SortItems, list.size());
        std::sort(list.begin(),
                  list.end(),
                  [pass](const Actor* a, const Actor* b)
                  {
                      if (a->drawOrder != b->drawOrder)
                      {
                          return a->drawOrder < b->drawOrder;
                      }
                      if (pass != size_t(ScenePass::Overlay))
                      {
                          auto pa = a->WorldPosition(), pb = b->WorldPosition();
                          float da = pa.x + pa.y, db = pb.x + pb.y;
                          if (da != db)
                          {
                              return da < db;
                          }
                      }
                      return a->Id() < b->Id();
                  });
    }
}

void SceneGraph::CollectFrame(const Actor& actor, const std::function<bool(const Actor&)>& visible)
{
    Perf::Count(Perf::Counter::SceneVisits);
    if (!actor.m_Active || !actor.m_Visible)
    {
        return;
    }
    if (actor.drawable)
    {
        if (actor.Kind() == ActorKind::Terrain)
        {
            m_TerrainList.push_back(&actor);
            auto p = actor.WorldPosition();
            // Track actual render data, including public variant edits and parent transforms.
            const float values[] = {p.x, p.y, p.z};
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(values);
            for (size_t i = 0; i < sizeof(values); ++i)
            {
                m_TerrainFingerprint = (m_TerrainFingerprint ^ bytes[i]) * 1099511628211ull;
            }
            m_TerrainFingerprint = (m_TerrainFingerprint ^ actor.Id()) * 1099511628211ull;
            m_TerrainFingerprint = (m_TerrainFingerprint ^ std::uint64_t(actor.variant)) * 1099511628211ull;
            m_TerrainFingerprint = (m_TerrainFingerprint ^ std::uint64_t(actor.drawOrder)) * 1099511628211ull;
        }
        if (actor.pass == ScenePass::Overlay || visible(actor))
        {
            m_FrameLists.at(size_t(actor.pass)).push_back(&actor);
            Perf::Count(Perf::Counter::ActorsVisible);
        }
        else
        {
            Perf::Count(Perf::Counter::ActorsCulled);
        }
    }
    for (ActorId child : actor.Children())
    {
        CollectFrame(*Find(child), visible);
    }
}
