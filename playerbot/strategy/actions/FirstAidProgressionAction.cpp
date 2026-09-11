#include "FirstAidProgressionAction.h"

#include "Entities/Player.h"

using namespace ai;

bool FirstAidProgressionAction::Execute(Event& event)
{
    if (!bot->HasSkill(SKILL_FIRST_AID))
        return false;

    uint32 skill = bot->GetSkillValue(SKILL_FIRST_AID);

    // Expert First Aid.
    // Use the original teaching spell so the normal skill-cap increase is
    // handled by the core, without requiring the physical book.
    if (skill >= 125 && !bot->HasSpell(7924))
    {
        bot->CastSpell(bot, 19903, TRIGGERED_OLD_TRIGGERED);
        return true;
    }

    // Heavy Silk Bandage.
    if (skill >= 180 && !bot->HasSpell(7929))
    {
        bot->learnSpell(7929, false);
        return true;
    }

    // Mageweave Bandage.
    if (skill >= 210 && !bot->HasSpell(10840))
    {
        bot->learnSpell(10840, false);
        return true;
    }

    // Artisan First Aid.
    // Triage is skipped; the original teaching spell applies the normal
    // Artisan spell and skill step.
    if (skill >= 225 && bot->GetLevel() >= 35 && !bot->HasSpell(10846))
    {
        bot->CastSpell(bot, 10847, TRIGGERED_OLD_TRIGGERED);
        return true;
    }

    return false;
}
