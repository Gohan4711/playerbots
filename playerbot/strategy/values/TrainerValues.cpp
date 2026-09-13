
#include "playerbot/playerbot.h"
#include "TrainerValues.h"
#include "SharedValueContext.h"
#include "playerbot/PlayerbotHelpMgr.h"

#include <array>
#include <unordered_set>

using namespace ai;

namespace
{
    // Dedicated seed slot for profession assignment. BotTypeNumber currently uses 1..8.
    // Keeping professions on their own seed prevents coupling them to any other fixed bot property.
    constexpr BotTypeNumber PROFESSION_NUMBER = static_cast<BotTypeNumber>(9);

    const std::array<uint32, 9>& PrimaryProfessions()
    {
        static const std::array<uint32, 9> professions =
        {
            SKILL_ALCHEMY,
            SKILL_BLACKSMITHING,
            SKILL_ENCHANTING,
            SKILL_ENGINEERING,
            SKILL_HERBALISM,
            SKILL_LEATHERWORKING,
            SKILL_MINING,
            SKILL_SKINNING,
            SKILL_TAILORING
        };

        return professions;
    }

    bool IsPrimaryProfession(uint32 skill)
    {
        const auto& professions = PrimaryProfessions();
        return std::find(professions.begin(), professions.end(), skill) != professions.end();
    }

    std::pair<uint32, uint32> GetPrimaryProfessionPair(PlayerbotAI* ai)
    {
        const auto& professions = PrimaryProfessions();

        // Nine professions have 36 unique two-profession combinations.
        // GetFixedBotNumber(..., 35, 0) returns a stable value in the range 0..35.
        uint32 pairIndex = ai->GetFixedBotNumber(PROFESSION_NUMBER, 35, 0);
        uint32 currentPair = 0;

        for (uint32 first = 0; first < professions.size(); ++first)
        {
            for (uint32 second = first + 1; second < professions.size(); ++second)
            {
                if (currentPair == pairIndex)
                    return { professions[first], professions[second] };

                ++currentPair;
            }
        }

        // Defensive fallback; pairIndex is always 0..35 so this should never be reached.
        return { SKILL_MINING, SKILL_BLACKSMITHING };
    }

    void ResetPrimaryProfessionsOnce(Player* bot)
    {
        if (!bot || !sRandomPlayerbotMgr.IsRandomBot(bot))
            return;

        // Avoid a database lookup every time TrainableSpellsValue recalculates.
        static std::unordered_set<uint32> checkedBots;
        uint32 botGuid = bot->GetGUIDLow();
        if (checkedBots.find(botGuid) != checkedBots.end())
            return;

        checkedBots.insert(botGuid);

        // Persistent migration marker. We deliberately check row existence directly instead of
        // RandomPlayerbotMgr::GetValue because ordinary random-bot events expire.
        auto resetMarker = CharacterDatabase.PQuery(
            "SELECT 1 FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' AND event = 'profession_reset_v1' LIMIT 1",
            botGuid);

        if (resetMarker)
            return;

        bool resetAnyProfession = false;
        for (uint32 profession : PrimaryProfessions())
        {
            if (!bot->HasSkill(profession))
                continue;

            // Core-native unlearn path: removing the skill step also removes spells/recipes
            // that were learned through that profession.
            bot->SetSkillStep(uint16(profession), 0);
            resetAnyProfession = true;
        }

        // Keep the marker effectively permanent. It is only used as a row-existence marker,
        // but a long validity also prevents generic event cleanup from treating it as stale.
        CharacterDatabase.PExecute(
            "INSERT INTO ai_playerbot_random_bots (owner, bot, `time`, validIn, event, `value`) VALUES (0, '%u', '%u', '2147483647', 'profession_reset_v1', 1)",
            botGuid, (uint32)time(0));

        if (resetAnyProfession)
            sLog.outDetail("Bot %u primary professions reset for profession-pair migration", botGuid);
    }
}


trainableSpellMap* TrainableSpellMapValue::Calculate()
{
    trainableSpellMap* spellMap = new trainableSpellMap;

    //           template, trainer
    std::unordered_map <uint32, std::vector<CreatureInfo const*>> trainerTemplateIds;

    //Select all trainer lists and their trainers.
    for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
    {
        CreatureInfo const* creatureInfo = sCreatureStorage.LookupEntry<CreatureInfo>(id);
        if (!creatureInfo)
            continue;

        if (!creatureInfo->TrainerType && !creatureInfo->TrainerClass)
            continue;

        if(creatureInfo->TrainerTemplateId)
            trainerTemplateIds[creatureInfo->TrainerTemplateId].push_back(creatureInfo);
        else
            trainerTemplateIds[id].push_back(creatureInfo);
    }

    for (auto& [templateOrEntryId, trainers] : trainerTemplateIds)
    {
        TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(templateOrEntryId);
        if (!trainer_spells)
            trainer_spells = sObjectMgr.GetNpcTrainerSpells(templateOrEntryId);

        if (!trainer_spells)
            continue;

        CreatureInfo const* firstTrainer = trainers.front();

        TrainerType trainerType = (TrainerType)firstTrainer->TrainerType;

        uint32 spellRequirement;
        if (trainerType == TRAINER_TYPE_CLASS || trainerType == TRAINER_TYPE_PETS)
            spellRequirement = firstTrainer->TrainerClass;
        else if (trainerType == TRAINER_TYPE_MOUNTS)
            spellRequirement = firstTrainer->TrainerRace;

        for (auto& [id, trainerSpell] : trainer_spells->spellList)
        {
            const TrainerSpell* sameTrainerSpell = &trainerSpell;
            for (auto& [otherTrainerSpell, trainers] : (*spellMap)[trainerType][spellRequirement])
            {
                if (otherTrainerSpell->spell != trainerSpell.spell)
                    continue;

                if (otherTrainerSpell->spellCost != trainerSpell.spellCost)
                    continue;

                if (otherTrainerSpell->reqSkill != trainerSpell.reqSkill)
                    continue;

                if (otherTrainerSpell->reqSkillValue != trainerSpell.reqSkillValue)
                    continue;

                if (otherTrainerSpell->reqLevel != trainerSpell.reqLevel)
                    continue;

#ifndef MANGOSBOT_TWO
                if (otherTrainerSpell->learnedSpell != trainerSpell.learnedSpell)
#else
                if (otherTrainerSpell->learnedSpell[0] != trainerSpell.learnedSpell[0])
#endif
                    continue;

                if (otherTrainerSpell->conditionId != trainerSpell.conditionId)
                    continue;

                sameTrainerSpell = otherTrainerSpell;
                break;
            }

            if (trainerType == TRAINER_TYPE_TRADESKILLS)
            {
                if (trainerSpell.reqSkill)
                    spellRequirement = trainerSpell.reqSkill;
                else
                {
                    // exist, already checked at loading
#ifdef MANGOSBOT_ZERO
                    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(trainerSpell.learnedSpell);
#else
                    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(trainerSpell.learnedSpell[0]);
#endif

                    spellRequirement = spell->EffectMiscValue[1];
                }
            }

            for (auto& trainer : trainers)
                (*spellMap)[trainerType][spellRequirement][sameTrainerSpell].push_back(trainer->Entry);
        }
    }

    return spellMap;
}

std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()
{
    std::vector<TrainerSpell const*> trainableSpells;

    bool enforcePrimaryProfessionPair = sRandomPlayerbotMgr.IsRandomBot(bot);
    ResetPrimaryProfessionsOnce(bot);
    auto [primaryProfessionOne, primaryProfessionTwo] = GetPrimaryProfessionPair(ai);

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->getClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->getRace())
                continue;

            // Primary professions are a permanent per-random-bot choice. Player-owned bots are
            // intentionally left untouched. Secondary professions such as Cooking, First Aid
            // and Fishing also pass through normally.
            if (enforcePrimaryProfessionPair && trainerType == TRAINER_TYPE_TRADESKILLS &&
                IsPrimaryProfession(requirement) && requirement != primaryProfessionOne &&
                requirement != primaryProfessionTwo)
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                uint32 reqLevel = 0;

                reqLevel = trainerSpell->isProvidedReqLevel ? trainerSpell->reqLevel : std::max(reqLevel, trainerSpell->reqLevel);
                TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell, reqLevel);
                if (state != TRAINER_SPELL_GREEN)
                    continue;

                //Skip initial profession training.
#ifdef MANGOSBOT_ZERO
                if (bot->GetLevel() < 10 && sSpellMgr.IsProfessionSpell(trainerSpell->learnedSpell) && sSpellMgr.GetSpellRank(trainerSpell->learnedSpell) == 1)
#else
                if (bot->GetLevel() < 10 && sSpellMgr.IsProfessionSpell(trainerSpell->learnedSpell[0]) && sSpellMgr.GetSpellRank(trainerSpell->learnedSpell[0]) == 1)
#endif
                    continue;

                trainableSpells.push_back(trainerSpell);
            }
        }
    }   

    return trainableSpells;
}

std::string TrainableSpellsValue::Format()
{
    std::vector<std::string> vec;  
    for (auto t : value) {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(t->spell);
        if (!spell)
            continue;
        vec.push_back(chat->formatSpell(spell));
    } 
    
    return sPlayerbotHelpMgr.makeList(vec, "[<part>]");
}

std::vector<int32> AvailableTrainersValue::Calculate()
{
    std::vector<TrainerSpell const*> trainableSpells = AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier());;
    std::vector<int32> retTrainers;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->getClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->getRace())
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                if (std::find(trainableSpells.begin(), trainableSpells.end(), trainerSpell) == trainableSpells.end())
                    continue;

                for (auto& trainer : trainers)
                {
                    if(std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                        retTrainers.push_back(trainer);
                }
            }
        }
    }

    return retTrainers;
}

uint32 TrainCostValue::Calculate()
{
    uint32 TotalCost = 0;

    for (auto& spells : AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier()))
        TotalCost += spells->spellCost;
   
    return TotalCost;
}
