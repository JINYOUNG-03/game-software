#pragma once

namespace Level
{
    class FirstLevel;
}

namespace Harbor
{
    class World;
}

class GameSave
{
  public:
    enum class Result
    {
        Success,
        Missing,
        Invalid,
        IoError
    };

    static Result Save(const Level::FirstLevel& level, const Harbor::World& world);
    static Result Load(Level::FirstLevel& level, Harbor::World& world);

  private:
    struct State;
    static bool Capture(State& state, const Level::FirstLevel& level, const Harbor::World& world);
    static void Restore(const State& state, Level::FirstLevel& level, Harbor::World& world);
};
