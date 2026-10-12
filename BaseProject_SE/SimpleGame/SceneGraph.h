#pragma once

#include "Actor.h"
#include <memory>
#include <array>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

class SceneGraph
{
  public:
    SceneGraph();
    SceneGraph(const SceneGraph&) = delete;
    SceneGraph& operator=(const SceneGraph&) = delete;

    Actor& Root()
    {
        return *m_Actors.at(m_Root);
    }

    Actor* Find(ActorId id);
    const Actor* Find(ActorId id) const;

    Actor& Get(ActorId id)
    {
        return *m_Actors.at(id);
    }

    const Actor& Get(ActorId id) const
    {
        return *m_Actors.at(id);
    }

    template <class T> std::vector<T*> Actors(ActorKind kind)
    {
        std::vector<T*> result;
        for (ActorId id : FindByKind(kind))
        {
            if (auto actor = dynamic_cast<T*>(Find(id)))
            {
                result.push_back(actor);
            }
        }
        return result;
    }

    template <class T> std::vector<const T*> Actors(ActorKind kind) const
    {
        std::vector<const T*> result;
        for (ActorId id : FindByKind(kind))
        {
            if (auto actor = dynamic_cast<const T*>(Find(id)))
            {
                result.push_back(actor);
            }
        }
        return result;
    }

    template <class T = Actor>
    T& Create(ActorId parent, const std::string& name, ActorKind kind = ActorKind::Group)
    {
        static_assert(std::is_base_of<Actor, T>::value, "Scene nodes must derive from Actor.");
        if (size_t(kind) >= m_ByKind.size())
        {
            throw std::invalid_argument("Invalid Actor kind.");
        }
        Actor* owner = Find(parent);
        if (!owner)
        {
            throw std::invalid_argument("SceneGraph parent does not exist.");
        }

        std::unique_ptr<T> actor(new T());
        actor->m_Id = m_NextId++;
        actor->m_Name = name;
        actor->m_Kind = kind;
        actor->m_Parent = owner;
        actor->m_Scene = this;
        T* result = actor.get();
        m_Actors.emplace(result->Id(), std::move(actor));
        owner->m_Children.push_back(result->Id());
        m_ByKind[size_t(kind)].push_back(result->Id());
        InvalidateCollisionSubtree(*result);
        return *result;
    }

    bool Reparent(ActorId child, ActorId parent, bool preserveWorldPosition = true);
    bool Destroy(ActorId id);
    void DestroyChildren(ActorId parent);
    std::vector<ActorId> FindByKind(ActorKind kind, bool activeOnly = true) const;
    std::vector<const Actor*> RenderList(ScenePass pass) const;

    // Prepare after Update; do not mutate the graph until all render consumers finish.
    void PrepareFrame(const std::function<bool(const Actor&)>& visible);

    const std::vector<const Actor*>& FrameList(ScenePass pass) const
    {
        return m_FrameLists.at(size_t(pass));
    }

    const std::vector<const Actor*>& TerrainList() const
    {
        return m_TerrainList;
    }

    std::uint64_t TerrainFingerprint() const
    {
        return m_TerrainFingerprint;
    }

    std::uint64_t CollisionRevision() const
    {
        return m_CollisionRevision;
    }

  private:
    friend class Actor;
    void InvalidateCollisionSubtree(const Actor& actor);
    void Collect(const Actor& actor, ScenePass pass, std::vector<const Actor*>& result) const;
    void CollectFrame(const Actor& actor, const std::function<bool(const Actor&)>& visible);
    std::array<std::vector<ActorId>, size_t(ActorKind::Count)> m_ByKind;
    std::array<std::vector<const Actor*>, size_t(ScenePass::Count)> m_FrameLists;
    std::vector<const Actor*> m_TerrainList;
    std::uint64_t m_TerrainFingerprint = 0;
    std::unordered_map<ActorId, std::unique_ptr<Actor>> m_Actors;
    ActorId m_Root = InvalidActorId;
    ActorId m_NextId = 1;
    std::uint64_t m_CollisionRevision = 1;
};
