#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    class FirstAidProgressionAction : public Action
    {
    public:
        FirstAidProgressionAction(PlayerbotAI* ai, std::string name = "first aid progression") : Action(ai, name) {}

        bool Execute(Event& event) override;

    private:
        bool AddBook(uint32 itemId);
    };
}
