#pragma once

#include "LevelOne.h"
#include "Trade.h"
#include <array>
#include <bitset>
#include <deque>

namespace Harbor
{
    enum class Job
    {
        Engineer,
        Clerk,
        Guard,
        Broker,
        Apprentice,
        Hunter,
        Raider,
        Merchant,
        Scholar,
        Resident
    };
    enum class Action
    {
        Rest,
        Hunt,
        Raid,
        Trade,
        Study,
        Investigate,
        Return,
        Patrol,
        Dead,
        Defend,
        Travel
    };

    enum Knowledge
    {
        Ledger = 1,
        Key = 2,
        Plans = 4,
        Published = 8,
        TrailNames = 16,
        TrailRoute = 32
    };

    enum class RecordChoice
    {
        Undecided,
        Public,
        Authority,
        Sold
    };

    enum class TrailNotice
    {
        None,
        Names,
        Route
    };

    struct Npc : public Actor
    {
        int appearance = 0;
        int homeVillage = 0;
        bool courier = false;
        int travelVillage = -1;
        float courierWait = 0;
        ActorId lastRumorSender = InvalidActorId;
        double lastRumorMinute = 0;
        int rumorsReceived = 0;
        std::wstring name;
        std::wstring line;
        Job job = Job::Resident;
        Action action = Action::Rest;
        int faction = 2;
        int affinity = 0;
        int karma = 0;
        int knowledge = 0;
        int health = 100;
        int maxHealth = 100;
        int coins = 25;
        int hides = 0;
        int food = 3;
        int enlightenment = 0;
        int experience = 0;
        float healthWeight = 1;
        float wealthWeight = 1;
        float enlightenmentWeight = .2f;
        float raidWeight = 0;
        float thinkTime = 0;
        float cooldown = 0;
        float actionTime = 0;
        float threatTime = 0;
        float attackTime = -1;
        Level::Position attackForward;
        float phase = 0;
        float raidCooldown = 0;
        float raidWindow = 0;
        int raidHits = 0;
        std::uint64_t pathRevision = 0;
        Level::Position pathDestination;
        bool moving = false;
        bool scheduled = false;
        ActorId target = InvalidActorId;
        ActorId threat = InvalidActorId;
        ActorId knownLedgerHolder = InvalidActorId;
        bool searchedLedger = false;
        bool ledgerCopy = false;
        Level::Position home;
        Level::Position destination;
        Level::Position lastLedger;
        std::vector<Level::Position> path;
        size_t waypoint = 0;
    };

    struct Landmark : public Actor
    {
        std::wstring label;
        int site = 0;
    };

    enum QuestStep
    {
        TakeHideJob,
        GatherHides,
        DeliverHides,
        MeetMira,
        InspectDrain,
        RecoverEvidence,
        CopyLedger,
        HearLedgerAccounts,
        DecideLedger,
        OpenSluice,
        ReportSluice,
        FirstTrailStep
    };

    struct ObjectiveGuide
    {
        int step = TakeHideJob;
        ActorId target = InvalidActorId;
        std::wstring destination;
        std::wstring reason;
        std::wstring action;
        char interactionKey = 0;
        bool ready = false;
    };

    enum class DialogueEffect
    {
        None,
        AcceptHideJob,
        DeliverHideJob,
        AcceptQuest,
        CompleteQuest,
        RecordLedgerAccount,
        PublishLedger,
        ShareKnowledge,
        RecordTrailClue,
        PostTrailNames,
        PostTrailRoute
    };

    struct DialogueChoice
    {
        std::wstring text;
        int next = -1;
        DialogueEffect effect = DialogueEffect::None;
    };

    struct DialoguePage
    {
        std::wstring speaker;
        std::wstring text;
        std::vector<DialogueChoice> choices;
    };

    struct DialogueState
    {
        ActorId source = InvalidActorId;
        std::vector<DialoguePage> pages;
        size_t page = 0;

        bool Active() const
        {
            return page < pages.size();
        }
    };

    class World
    {
      public:
        explicit World(Level::FirstLevel& level) : level(level)
        {
        }

        void Initialize();
        void Update(float dt);
        void PlayerAttack();
        Npc* NearbyQuestSpeaker();
        void Talk(ActorId target);
        void AdvanceDialogue();
        void ChooseDialogue(size_t choice);
        void EndDialogue();

        const DialogueState& Conversation() const
        {
            return dialogue;
        }

        void Trade();
        void EndTrade();
        bool ExecuteTrade(TradeAction action, int quantity);
        std::vector<TradeOffer> TradeOffers() const;

        const TradeSession& Trading() const
        {
            return trade;
        }

        void InteractSite(ActorId target);
        void Reveal();
        Npc* Nearest(float radius = 1.5f);
        ActorId NearbySite() const;
        std::wstring InspectionLabel(ActorId target) const;
        std::wstring Clock() const;
        std::wstring Status() const;
        std::wstring AreaName() const;
        std::vector<std::wstring> Journal() const;
        std::wstring Objective() const;
        std::wstring ObjectiveHint() const;
        std::wstring Consequence() const;
        ActorId ObjectiveTarget() const;
        ObjectiveGuide Guide() const;
        std::string SnapshotJson() const;
        bool PlayerOwnsLedger() const;
        bool PlayerOwnsKey() const;

        bool PlayerHasLedgerCopy() const
        {
            return copied;
        }

        static const wchar_t* JobLabel(Job job);
        static const wchar_t* ActionLabel(Action action);

        const std::array<ActorId, 10>& LandmarkIds() const
        {
            return sites;
        }

      private:
        friend class ::GameSave;

        using Position = Level::Position;

        enum class HideJob
        {
            Offered,
            Accepted,
            Delivered
        };

        Level::FirstLevel& level;
        ActorId worldRoot = InvalidActorId;
        ActorId ledger = InvalidActorId;
        ActorId key = InvalidActorId;
        ActorId barrier = InvalidActorId;
        std::array<ActorId, 10> sites{};

        struct VillageStatistics
        {
            std::array<double, 8> moodSeconds{};
            double observedSeconds = 0;
            std::uint64_t rumorDeliveries = 0;
        };

        std::array<VillageStatistics, WorldLayout::VillageCount> villageStatistics{};
        float statisticsTime = 0;
        std::array<int, 4> trust{};
        std::deque<std::wstring> history;
        std::mt19937 random;
        double minutes = 360;
        float logTime = 0;
        float gossipTime = 0;
        bool gateOpen = false;
        bool published = false;
        bool controlled = false;
        bool repaired = false;
        int flood = 0;
        bool copied = false;
        bool ledgerWet = false;
        HideJob hideJob = HideJob::Offered;
        double drainReportedAt = 360;
        bool briefed = false;
        bool drainInspected = false;
        bool recoveredLedger = false;
        bool recoveredKey = false;
        bool returned = false;
        bool dealSettled = false;
        RecordChoice recordChoice = RecordChoice::Undecided;
        std::bitset<4> ledgerAccountsHeard;
        size_t trailClues = 0;
        TrailNotice trailNotice = TrailNotice::None;
        int questStep = TakeHideJob;
        std::wstring consequence;
        DialogueState dialogue;
        TradeSession trade;

        bool CanTradeWith(const Npc& npc) const;
        bool CanInspect(ActorId target) const;

        Level::Position Site(int index) const;
        ActorId Holder(ActorId artifact) const;
        void Transfer(ActorId artifact, ActorId holder);
        void Observe(Npc& npc);
        void Think(Npc& npc, const std::vector<Npc*>& npcs, const std::vector<Level::Enemy*>& beasts);
        void Step(Npc& npc, float dt);
        bool Hurt(Npc& victim, int damage, ActorId attacker);
        bool CanEngage(const Npc& npc, Action action, const Actor& target) const;
        void Retreat(Npc& npc);
        bool BeginNpcAttack(Npc& npc, Actor& target);
        void AdvanceNpcAttack(Npc& npc, float dt);
        void CancelNpcAttack(Npc& npc);
        bool ResolveNpcHit(Npc& npc, Actor& target);
        void Witness(ActorId attacker, const Position& where, int affinity, int karma);
        void Exchange(Npc& seller, Npc& buyer);
        void Event(const std::wstring& text, bool notifyPlayer = true);
        void Log();
        void Disclose(Npc* speaker);
        void OpenGate(Npc* actor);
        void PlaceDrainSupport();
        Level::Position Reachable(const Position& requested) const;
        bool RaidAllowed(const Npc& attacker, const Actor& target) const;
        Npc* Person(int definitionId) const;
        void ReactToPlayer(const std::wstring& result);
        void UpdateQuest();
        void SpreadRumors(const std::vector<Npc*>& npcs);
        int VillageMood(int village, const std::vector<const Npc*>& npcs) const;
        void UpdateVillageStatistics(float dt, const std::vector<Npc*>& npcs);
        void ChooseRecord(RecordChoice choice);
        size_t NextLedgerAccount() const;
        size_t UnheardLedgerAccount(const Npc* npc) const;
        void BeginLedgerAccount(size_t index, const Npc* npc);
        void BeginHideJob();
        void BeginDialogue(Npc* npc);
        void CompleteFirstQuest();
        void BeginTrailDialogue(const Landmark& board);
        void ApplyTrailChoice(ActorId board, DialogueEffect effect);
        const wchar_t* TrailResult() const;
    };
} // namespace Harbor
