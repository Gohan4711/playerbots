#include "FirstAidProgressionAction.h"

#include "Entities/Item.h"
#include "Entities/Player.h"

using namespace ai;

bool FirstAidProgressionAction::AddBook(uint32 itemId)
{
    if (bot->HasItemCount(itemId, 1))
        return false;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return false;

    ItemPosCountVec itemVec;
    InventoryResult result = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, itemVec, itemId, 1);

    if (result != EQUIP_ERR_OK)
        return false;

    bot->StoreNewItemInInventorySlot(itemId, 1);
    return true;
}

bool FirstAidProgressionAction::Execute(Event& event)
{
    if (!bot->HasSkill(SKILL_FIRST_AID))
        return false;

    uint32 skill = bot->GetSkillValue(SKILL_FIRST_AID);

    // Expert First Aid - Under Wraps
    if (skill >= 125 && !bot->HasSpell(7924))
        return AddBook(16084);

    // Manual: Heavy Silk Bandage
    if (skill >= 180 && !bot->HasSpell(7929))
        return AddBook(16112);

    // Manual: Mageweave Bandage
    if (skill >= 210 && !bot->HasSpell(10840))
        return AddBook(16113);

    // Artisan First Aid.
    // Triage is deliberately skipped, but the original teaching spell is used
    // so the normal First Aid spell and skill step are applied by the core.
    if (skill >= 225 && bot->GetLevel() >= 35 && !bot->HasSpell(10846))
    {
        bot->CastSpell(bot, 10847, TRIGGERED_OLD_TRIGGERED);
        return true;
    }

    return false;
}
