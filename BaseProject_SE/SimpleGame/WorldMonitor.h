#pragma once

#include <memory>

namespace Harbor
{
    class World;
}

namespace Monitor
{
    // The worker owns sockets and immutable JSON only. It never reads the SceneGraph or OpenGL.
    class Server
    {
      public:
        Server();
        ~Server();
        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        bool Start();
        void Stop();
        void Tick(const Harbor::World& world, bool paused, double frameIntervalSeconds);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Monitor
