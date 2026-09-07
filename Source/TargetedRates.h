#pragma once

#include <cmath>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>
#include <API/ARK/Ark.h>
#include <API/ARK/GameMode.h>

// ---------------------------------------------------------------------------
// Targeted player harvest boosts
//
// Boosts are stored by EOS ID (see PlayerHarvestBoost in CousinCustomRates.h).
// The harvest hook matches the boosted player's character and their own dinos
// via the ARK player data ID, and tribe members / tribe dinos via the team ID
// when the boost is tribe-wide. The verified GiveHarvestResource hook changes
// only harvest calculations; it never changes AShooterGameMode's global
// harvest multiplier.
// ---------------------------------------------------------------------------

constexpr const char* kGiveHarvestResourceHook =
    "UPrimalHarvestingComponent.GiveHarvestResource(UPrimalInventoryComponent*,float,TSubclassOf<UDamageType>,AActor*,TArray<FHarvestResourceEntry,TSizedDefaultAllocator<32>>*)";
constexpr const char* kIncrementItemTemplateQuantityHook =
    "UPrimalInventoryComponent.IncrementItemTemplateQuantity(TSubclassOf<UPrimalItem>,int,bool,bool,UPrimalItem**,UPrimalItem**,bool,bool,bool,bool,bool,bool,bool,bool,bool,bool,TSubclassOf<UPrimalItem>)";

DECLARE_HOOK(
    UPrimalHarvestingComponent_GiveHarvestResource,
    void,
    UPrimalHarvestingComponent* harvestingComponent,
    UPrimalInventoryComponent* destinationInventory,
    float harvestAmount,
    TSubclassOf<UDamageType> damageType,
    AActor* harvestingActor,
    TArray<FHarvestResourceEntry, TSizedDefaultAllocator<32>>* resourceEntries
);

DECLARE_HOOK(
    UPrimalInventoryComponent_IncrementItemTemplateQuantity,
    int,
    UPrimalInventoryComponent* inventoryComponent,
    TSubclassOf<UPrimalItem> itemTemplate,
    int amount,
    bool bReplicateToClient,
    bool bIsBlueprint,
    UPrimalItem** useSpecificItem,
    UPrimalItem** incrementedItem,
    bool bRequireExactClassMatch,
    bool bIsCraftingResourceConsumption,
    bool bIsFromUseConsumption,
    bool bIsArkTributeItem,
    bool showHUDNotification,
    bool bDontRecalcSpoilingTime,
    bool bDontExceedMaxItems,
    bool bDontProgressMilestones,
    bool bDontAddToSlot,
    bool bDontBroadcastItemUpdateEvents,
    TSubclassOf<UPrimalItem> ignoreClass
);

// One entry is pushed for every nested GiveHarvestResource call. This makes a
// generic inventory increment eligible only while ARK is inside the matching,
// verified harvest-resource path for the same destination inventory.
struct HarvestQuantityContext
{
    UPrimalInventoryComponent* destinationInventory = nullptr;
    int teamId = 0;
    int tribeMemberCount = 0;
    float globalMultiplier = 1.0f;
    float targetMultiplier = 1.0f;
    float tribeSizeMultiplier = 1.0f;
    float correction = 1.0f;
    bool applies = false;
};

inline thread_local std::vector<HarvestQuantityContext> s_harvestQuantityContexts;

struct ScopedHarvestQuantityContext
{
    explicit ScopedHarvestQuantityContext(const HarvestQuantityContext& context)
    {
        s_harvestQuantityContexts.push_back(context);
    }

    ~ScopedHarvestQuantityContext()
    {
        s_harvestQuantityContexts.pop_back();
    }

    ScopedHarvestQuantityContext(const ScopedHarvestQuantityContext&) = delete;
    ScopedHarvestQuantityContext& operator=(const ScopedHarvestQuantityContext&) = delete;
};

bool IsTribeHarvestBoostDebugEnabled()
{
    return CousinCustomRates::config.value("TribeHarvestBoostDebug", false);
}

// Returns the configured multiplier for an exact total tribe member count.
// Missing/invalid entries and unlisted sizes deliberately leave rates unchanged.
float GetTribeSizeHarvestMultiplier(const int tribeMemberCount)
{
    if (tribeMemberCount <= 0 ||
        !CousinCustomRates::config.contains("TribeSizeMultiplier") ||
        !CousinCustomRates::config["TribeSizeMultiplier"].is_object())
    {
        return 1.0f;
    }

    const nlohmann::json& settings = CousinCustomRates::config["TribeSizeMultiplier"];
    if (!settings.value("Enabled", false) ||
        !settings.contains("Multipliers") || !settings["Multipliers"].is_array())
    {
        return 1.0f;
    }

    for (const auto& entry : settings["Multipliers"])
    {
        if (!entry.is_object() || entry.value("TribeSize", 0) != tribeMemberCount)
            continue;

        const float multiplier = entry.value("Multiplier", 1.0f);
        return multiplier > 0.0f ? multiplier : 1.0f;
    }

    return 1.0f;
}

// Reads tribe member count using hybrid approach:
// 1. Scans server's active TribesDataField cache (instant, safe)
// 2. Falls back to lazy-loaded MyTribeDataField if not in cache
// 3. Returns 1 as safe default if neither method succeeds
// Solo players (not in a tribe) return 1.
int GetAndCacheTribeMemberCount(AShooterPlayerController* player, bool* cacheChanged = nullptr)
{
    if (cacheChanged)
        *cacheChanged = false;

    if (!player)
        return 0;

    APlayerState* playerStateBase = player->PlayerStateField().Get();
    if (!playerStateBase || !playerStateBase->IsA(AShooterPlayerState::GetPrivateStaticClass()))
    {
        return 0;
    }

    auto* playerState = static_cast<AShooterPlayerState*>(playerStateBase);
    
    // Solo players count as tribe of 1
    if (!playerState->IsInTribe())
    {
        return 1;
    }

    const int teamId = player->TargetingTeamField();
    if (teamId == 0)
        return 0;

    // METHOD 1: Scan server's active tribe cache (instant, no allocation)
    try
    {
        AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
        if (gameMode)
        {
            TArray<FTribeData, TSizedDefaultAllocator<32>>& activeTribes = gameMode->TribesDataField();
            
            for (int i = 0; i < activeTribes.Num(); ++i)
            {
                FTribeData& tribe = activeTribes[i];
                if (tribe.TribeIDField() == teamId)
                {
                    const int memberCount = tribe.MembersPlayerDataIDField().Num();
                    if (memberCount > 0)
                    {
                        const auto cachedCount = CousinCustomRates::tribeMemberCountsByTeam.find(teamId);
                        if (cachedCount == CousinCustomRates::tribeMemberCountsByTeam.end() || 
                            cachedCount->second != memberCount)
                        {
                            CousinCustomRates::tribeMemberCountsByTeam[teamId] = memberCount;
                            if (cacheChanged)
                                *cacheChanged = true;
                            
                            Log::GetLog()->info("GetAndCacheTribeMemberCount: SUCCESS (TribesDataField cache) - teamId={}, count={}", 
                                teamId, memberCount);
                        }
                        return memberCount;
                    }
                }
            }
            
            Log::GetLog()->info("GetAndCacheTribeMemberCount: Tribe not found in TribesDataField cache, trying fallback");
        }
    }
    catch (...)
    {
        Log::GetLog()->warn("GetAndCacheTribeMemberCount: TribesDataField scan failed, using fallback");
    }

    // METHOD 2: Fallback to lazy-loaded MyTribeDataField
    try
    {
        const int memberCount = playerState->MyTribeDataField().MembersPlayerDataIDSet_ServerField().Num();
        
        if (memberCount > 0)
        {
            const auto cachedCount = CousinCustomRates::tribeMemberCountsByTeam.find(teamId);
            if (cachedCount == CousinCustomRates::tribeMemberCountsByTeam.end() || 
                cachedCount->second != memberCount)
            {
                CousinCustomRates::tribeMemberCountsByTeam[teamId] = memberCount;
                if (cacheChanged)
                    *cacheChanged = true;
                
                Log::GetLog()->info("GetAndCacheTribeMemberCount: SUCCESS (MyTribeDataField fallback) - teamId={}, count={}", 
                    teamId, memberCount);
            }
            return memberCount;
        }
    }
    catch (...)
    {
        Log::GetLog()->warn("GetAndCacheTribeMemberCount: MyTribeDataField also failed");
    }

    // If both methods fail, return 1 as safe default (better than 0 which disables multiplier)
    Log::GetLog()->info("GetAndCacheTribeMemberCount: Both methods failed, returning 1 as safe default");
    return 1;
}

bool RefreshKnownTribeMemberCounts()
{
    UWorld* world = AsaApi::GetApiUtils().GetWorld();
    if (!world)
        return false;

    bool changed = false;
    auto& controllers = world->PlayerControllerListField();
    for (TWeakObjectPtr<APlayerController>& controllerWeak : controllers)
    {
        APlayerController* controller = controllerWeak.Get();
        if (!controller || !controller->IsA(AShooterPlayerController::GetPrivateStaticClass()))
            continue;

        bool playerCountChanged = false;
        GetAndCacheTribeMemberCount(static_cast<AShooterPlayerController*>(controller), &playerCountChanged);
        changed = changed || playerCountChanged;
    }

    return changed;
}

// Resolves the team that owns a harvest. Player harvests use the owning
// controller's TargetingTeam; dino harvests use the dino's TamingTeamID.
int GetHarvestingTeamId(AActor* harvestingActor, int& tribeMemberCount)
{
	tribeMemberCount = 0;
    if (!harvestingActor)
        return 0;

    if (harvestingActor->IsA(APrimalDinoCharacter::GetPrivateStaticClass()))
    {
        auto* dino = static_cast<APrimalDinoCharacter*>(harvestingActor);
        const int teamId = dino->TamingTeamIDField();
        const auto cachedCount = CousinCustomRates::tribeMemberCountsByTeam.find(teamId);
        if (cachedCount != CousinCustomRates::tribeMemberCountsByTeam.end())
            tribeMemberCount = cachedCount->second;
        return teamId;
    }

    if (harvestingActor->IsA(AShooterCharacter::GetPrivateStaticClass()))
    {
        auto* character = static_cast<AShooterCharacter*>(harvestingActor);
        APlayerController* ownerBase = character->GetOwnerController();
        if (!ownerBase || !ownerBase->IsA(AShooterPlayerController::GetPrivateStaticClass()))
            return 0;

        auto* player = static_cast<AShooterPlayerController*>(ownerBase);
        tribeMemberCount = GetAndCacheTribeMemberCount(player);
        return player->TargetingTeamField();
    }

    return 0;
}

// Formats a rate for chat/log output without trailing zeros (20.0 -> "20").
std::string FormatHarvestRate(float rate)
{
    std::ostringstream ss;
    ss << rate;
    return ss.str();
}

// Replaces the supported ChangePlayerRate message-template placeholders.
// Unknown text, including unsupported placeholders, is intentionally retained.
std::string FormatPlayerHarvestBoostMessage(
    std::string message, const float effectiveRate, const int64_t durationMinutes)
{
    const auto replaceAll = [&message](const std::string& placeholder, const std::string& value)
    {
        size_t position = 0;
        while ((position = message.find(placeholder, position)) != std::string::npos)
        {
            message.replace(position, placeholder.length(), value);
            position += value.length();
        }
    };

    replaceAll("{rate}", FormatHarvestRate(effectiveRate));
    replaceAll("{duration}", std::to_string(durationMinutes));
    return message;
}

// Finds the active (non-expired) player harvest boost that applies to this
// harvest. Matching rules:
//   1. Player character harvest -> boost whose playerDataId matches the
//      character's LinkedPlayerDataID (the boosted player themselves).
//   2. Dino harvest             -> boost whose playerDataId matches the dino's
//      OwningPlayerID (the boosted player's own tames), even for player-only
//      boosts.
//   3. Any harvest              -> tribe-wide boost (alsoForTribe) whose
//      teamId matches the harvesting team (tribe members and tribe dinos).
// Expired entries are skipped here; the boost timer removes them.
const CousinCustomRates::PlayerHarvestBoost* FindActiveBoostForHarvest(
    AActor* harvestingActor, int teamId)
{
    if (CousinCustomRates::playerHarvestBoosts.empty())
        return nullptr;

    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    // The harvester's own ARK player data ID (0 = none). Player characters
    // expose it as LinkedPlayerDataID (uint64); dinos expose their owner's ID
    // as OwningPlayerID (int32), so dino matches compare the low 32 bits.
    unsigned long long harvesterPlayerDataId = 0;
    bool harvesterIsDino = false;
    if (harvestingActor)
    {
        if (harvestingActor->IsA(APrimalDinoCharacter::GetPrivateStaticClass()))
        {
            harvesterIsDino = true;
            const int ownerId =
                static_cast<APrimalDinoCharacter*>(harvestingActor)->OwningPlayerIDField();
            if (ownerId > 0)
                harvesterPlayerDataId = static_cast<unsigned int>(ownerId);
        }
        else if (harvestingActor->IsA(AShooterCharacter::GetPrivateStaticClass()))
        {
            harvesterPlayerDataId =
                static_cast<AShooterCharacter*>(harvestingActor)->LinkedPlayerDataIDField();
        }
    }

    const CousinCustomRates::PlayerHarvestBoost* tribeBoost = nullptr;
    for (const auto& [eosId, boost] : CousinCustomRates::playerHarvestBoosts)
    {
        if (boost.expiryUnixTime != 0 && boost.expiryUnixTime <= now)
            continue;

        // Direct owner match: the boosted player's character or own dino.
        if (harvesterPlayerDataId != 0 && boost.playerDataId != 0)
        {
            const bool idMatch = harvesterIsDino
                ? static_cast<unsigned int>(boost.playerDataId) ==
                      static_cast<unsigned int>(harvesterPlayerDataId)
                : boost.playerDataId == harvesterPlayerDataId;
            if (idMatch)
                return &boost;
        }

        // Tribe-wide match: first active tribe boost for this team wins.
        if (!tribeBoost && boost.alsoForTribe && teamId != 0 && boost.teamId == teamId)
            tribeBoost = &boost;
    }

    return tribeBoost;
}

// GiveHarvestResource is the cache-verified ARK harvest-only resource-award
// path. Its float argument is harvest-calculation input, not a documented final
// item quantity, so this hook only establishes the scoped harvest context.
void Hook_UPrimalHarvestingComponent_GiveHarvestResource(
    UPrimalHarvestingComponent* harvestingComponent,
    UPrimalInventoryComponent* destinationInventory,
    float harvestAmount,
    TSubclassOf<UDamageType> damageType,
    AActor* harvestingActor,
    TArray<FHarvestResourceEntry, TSizedDefaultAllocator<32>>* resourceEntries)
{
    int tribeMemberCount = 0;
    const int teamId = GetHarvestingTeamId(harvestingActor, tribeMemberCount);
    const CousinCustomRates::PlayerHarvestBoost* boost =
        FindActiveBoostForHarvest(harvestingActor, teamId);
    HarvestQuantityContext context;

    if (teamId != 0)
    {
        AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
        if (gameMode)
        {
            const float globalMultiplier = gameMode->HarvestAmountMultiplierField();
            if (globalMultiplier > 0.0f)
            {
                // Multiplier boost: stacks with the active global and tribe-size
                // harvest rates (global 2.0 x tribe size 4.0 x boost 10 = 80x).
                // Fixed boost: an absolute final rate that replaces both rates
                // for this harvest only.
                float targetMultiplier = globalMultiplier;
                if (boost)
                {
                    targetMultiplier = boost->isMultiplier
                        ? globalMultiplier * boost->harvestAmount
                        : boost->harvestAmount;
                }

                const float sizeMultiplier = GetTribeSizeHarvestMultiplier(tribeMemberCount);
                context.destinationInventory = destinationInventory;
                context.teamId = teamId;
                context.tribeMemberCount = tribeMemberCount;
                context.globalMultiplier = globalMultiplier;
                context.targetMultiplier = targetMultiplier;
                context.tribeSizeMultiplier = sizeMultiplier;
                context.correction = (boost && !boost->isMultiplier)
                    ? targetMultiplier / globalMultiplier
                    : (targetMultiplier * sizeMultiplier) / globalMultiplier;
                context.applies = context.correction > 0.0f && context.correction != 1.0f;

                if (context.applies && IsTribeHarvestBoostDebugEnabled())
                {
                    Log::GetLog()->info(
                        "TribeHarvestDebug: context started. team={} tribeSize={} sizeMultiplier={} global={} target={} correction={} inventory={}",
                        context.teamId, context.tribeMemberCount, context.tribeSizeMultiplier,
                        context.globalMultiplier, context.targetMultiplier,
                        context.correction, static_cast<const void*>(destinationInventory));
                }
            }
        }
    }

    ScopedHarvestQuantityContext scopedContext(context);
    UPrimalHarvestingComponent_GiveHarvestResource_original(
        harvestingComponent, destinationInventory, harvestAmount, damageType, harvestingActor, resourceEntries);
}

// This is the actual item-quantity grant inside the scoped harvest path above.
// Generic inventory additions are untouched because they have no active context.
int Hook_UPrimalInventoryComponent_IncrementItemTemplateQuantity(
    UPrimalInventoryComponent* inventoryComponent,
    TSubclassOf<UPrimalItem> itemTemplate,
    int amount,
    bool bReplicateToClient,
    bool bIsBlueprint,
    UPrimalItem** useSpecificItem,
    UPrimalItem** incrementedItem,
    bool bRequireExactClassMatch,
    bool bIsCraftingResourceConsumption,
    bool bIsFromUseConsumption,
    bool bIsArkTributeItem,
    bool showHUDNotification,
    bool bDontRecalcSpoilingTime,
    bool bDontExceedMaxItems,
    bool bDontProgressMilestones,
    bool bDontAddToSlot,
    bool bDontBroadcastItemUpdateEvents,
    TSubclassOf<UPrimalItem> ignoreClass)
{
    if (!s_harvestQuantityContexts.empty())
    {
        const HarvestQuantityContext& context = s_harvestQuantityContexts.back();
        if (context.applies && inventoryComponent == context.destinationInventory && amount > 0 &&
            !bIsCraftingResourceConsumption && !bIsFromUseConsumption && !bIsArkTributeItem)
        {
            // Windows headers define a function-like max macro, so use the
            // explicit signed 32-bit integer upper bound instead.
            constexpr int kMaxInt = 2147483647;
            const int originalAmount = amount;
            const double adjustedAmount = static_cast<double>(amount) * context.correction;
            if (adjustedAmount >= static_cast<double>(kMaxInt))
                amount = kMaxInt;
            else
                amount = static_cast<int>(std::round(adjustedAmount));

            if (IsTribeHarvestBoostDebugEnabled())
            {
                Log::GetLog()->info(
                    "TribeHarvestDebug: quantity adjusted. team={} tribeSize={} sizeMultiplier={} global={} target={} correction={} original={} adjusted={} inventory={}",
                    context.teamId, context.tribeMemberCount, context.tribeSizeMultiplier,
                    context.globalMultiplier, context.targetMultiplier,
                    context.correction, originalAmount, amount,
                    static_cast<const void*>(inventoryComponent));
            }
        }
    }

    return UPrimalInventoryComponent_IncrementItemTemplateQuantity_original(
        inventoryComponent, itemTemplate, amount, bReplicateToClient, bIsBlueprint,
        useSpecificItem, incrementedItem, bRequireExactClassMatch,
        bIsCraftingResourceConsumption, bIsFromUseConsumption, bIsArkTributeItem,
        showHUDNotification, bDontRecalcSpoilingTime, bDontExceedMaxItems,
        bDontProgressMilestones, bDontAddToSlot, bDontBroadcastItemUpdateEvents,
        ignoreClass);
}

// Targeted boosts never invoke global BroadcastRateChange or Discord.
void SendTargetedBoostChat(AShooterPlayerController* player, const std::string& message)
{
    if (!player || message.empty())
        return;

    std::string senderName = CousinCustomRates::config.value("ChatSenderName", "CousinCustomRates");
    if (senderName.empty())
        senderName = "CousinCustomRates";

    AsaApi::GetApiUtils().SendChatMessage(
        player, FString(senderName.c_str()), "{}", message.c_str());
}

void SaveTargetedBoostState()
{
    SaveState(CousinCustomRates::lastPreset);
}

// Expiry removes the player boost only. Affected players immediately resume
// the current global preset's harvest rate; no global preset is applied.
void ExpirePlayerHarvestBoosts()
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    bool changed = false;

    for (auto it = CousinCustomRates::playerHarvestBoosts.begin();
        it != CousinCustomRates::playerHarvestBoosts.end();)
    {
        const CousinCustomRates::PlayerHarvestBoost boost = it->second;
        if (boost.expiryUnixTime == 0 || boost.expiryUnixTime > now)
        {
            ++it;
            continue;
        }

        Log::GetLog()->info("Player harvest boost for EOS ID '{}' (team {}) expired.",
            boost.targetEosId, boost.teamId);
        it = CousinCustomRates::playerHarvestBoosts.erase(it);
        changed = true;

        // The target may be offline. Do not broadcast or queue a delayed
        // message; targeted boost chat is intentionally private.
        AShooterPlayerController* target =
            AsaApi::GetApiUtils().FindPlayerFromEOSID(FString(boost.targetEosId.c_str()));
        const nlohmann::json* settings = nullptr;
        if (CousinCustomRates::config.contains("ChangePlayerRate") &&
            CousinCustomRates::config["ChangePlayerRate"].is_object())
        {
            settings = &CousinCustomRates::config["ChangePlayerRate"];
        }

        const int64_t durationMinutes = settings
            ? settings->value("DurationMinutes", static_cast<int64_t>(0))
            : 0;
        float globalMultiplier = 1.0f;
        AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
        if (gameMode)
            globalMultiplier = gameMode->HarvestAmountMultiplierField();

        const float effectiveRate = boost.isMultiplier
            ? globalMultiplier * boost.harvestAmount
            : boost.harvestAmount;
        const std::string expiryTemplate = settings
            ? settings->value("ExpiryMessage", CousinCustomRates::config.value(
                "TribeHarvestBoostExpiredMessage",
                "Your harvest-rate boost has expired. You are now using the current server harvest rates."))
            : CousinCustomRates::config.value(
                "TribeHarvestBoostExpiredMessage",
                "Your harvest-rate boost has expired. You are now using the current server harvest rates.");
        SendTargetedBoostChat(target,
            FormatPlayerHarvestBoostMessage(expiryTemplate, effectiveRate, durationMinutes));
    }

    if (changed)
        SaveTargetedBoostState();
}

void TribeHarvestBoostTimerCallback()
{
    static int refreshCounter = 0;
    if (++refreshCounter >= 60)
    {
        refreshCounter = 0;
        if (RefreshKnownTribeMemberCounts())
            SaveTargetedBoostState();
    }

    ExpirePlayerHarvestBoosts();
}

void AddOrRemoveTribeHarvestBoostTimer(bool addTimer = true)
{
    if (addTimer)
        AsaApi::GetCommands().AddOnTimerCallback(
            "CousinCustomRatesTribeBoostTimer", &TribeHarvestBoostTimerCallback);
    else
        AsaApi::GetCommands().RemoveOnTimerCallback("CousinCustomRatesTribeBoostTimer");
}

// Applies the ChangePlayerRate config section to one online player. Depending
// on config, the boost covers only that player (character + own dinos) or
// their entire tribe. Duration comes from ChangePlayerRate.DurationMinutes.
bool ApplyPlayerHarvestBoost(const FString& eosId, FString& result)
{
    if (!CousinCustomRates::config.contains("ChangePlayerRate") ||
        !CousinCustomRates::config["ChangePlayerRate"].is_object())
    {
        result = "The 'ChangePlayerRate' section is missing from config.json.";
        return false;
    }

    const nlohmann::json& settings = CousinCustomRates::config["ChangePlayerRate"];
    if (!settings.value("Enable", false))
    {
        result = "ChangePlayerRate is disabled in config.json (Enable = false).";
        return false;
    }

    const float harvestAmount = settings.value("HarvestAmount", 0.0f);
    if (harvestAmount <= 0.0f)
    {
        result = "ChangePlayerRate.HarvestAmount must be a number greater than 0.";
        return false;
    }

    AShooterPlayerController* target = AsaApi::GetApiUtils().FindPlayerFromEOSID(eosId);
    if (!target)
    {
        result = FString("No online player was found for EOS ID '") + eosId + FString("'.");
        return false;
    }

    const int teamId = target->TargetingTeamField();
    const bool isMultiplier = settings.value("Multiplier", true);
    const int64_t durationMinutes = settings.value("DurationMinutes", static_cast<int64_t>(0));

    // AlsoForTheTribe requires an actual tribe; solo players always get a
    // player-only boost (their character and their own dinos).
    const bool alsoForTribe = settings.value("AlsoForTheTribe", false) && teamId != 0;

    int64_t expiry = 0;
    if (durationMinutes > 0)
        expiry = static_cast<int64_t>(std::time(nullptr)) + durationMinutes * 60LL;

    GetAndCacheTribeMemberCount(target);

    // Capture the player's data ID so the harvest hook can match their
    // character and their own dinos even while they are offline.
    unsigned long long playerDataId = 0;
    AShooterCharacter* character = target->GetPlayerCharacter();
    if (character)
        playerDataId = character->LinkedPlayerDataIDField();

    CousinCustomRates::PlayerHarvestBoost boost;
    boost.targetEosId = eosId.ToString();
    boost.teamId = teamId;
    boost.playerDataId = playerDataId;
    boost.harvestAmount = harvestAmount;
    boost.isMultiplier = isMultiplier;
    boost.alsoForTribe = alsoForTribe;
    boost.expiryUnixTime = expiry;

    CousinCustomRates::playerHarvestBoosts[boost.targetEosId] = boost;
    SaveTargetedBoostState();

    // Describe the effective rate using the current global harvest multiplier.
    float globalMultiplier = 1.0f;
    AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
    if (gameMode)
        globalMultiplier = gameMode->HarvestAmountMultiplierField();

    const float effectiveRate = isMultiplier
        ? globalMultiplier * harvestAmount
        : harvestAmount;

    // Targeted boost chat is deliberately private. Webhooks and global HUD
    // notifications are exclusive to global changerates.
    const std::string activationTemplate = settings.value(
        "ActivationMessage",
        "Harvest boost active: {rate}x harvest for {duration} minute(s).");
    SendTargetedBoostChat(target,
        FormatPlayerHarvestBoostMessage(activationTemplate, effectiveRate, durationMinutes));

    result = FString("Applied harvest boost to EOS ID '") + eosId + FString("': ") +
        FString(FormatHarvestRate(harvestAmount).c_str()) +
        FString(isMultiplier ? "x multiplier on the global rate (effective " : "x fixed rate (effective ") +
        FString(FormatHarvestRate(effectiveRate).c_str()) + FString("x), ") +
        FString(alsoForTribe ? "tribe-wide" : "player + own dinos");
    if (expiry > 0)
    {
        result += FString(", expires in ") +
            FString(std::to_string(durationMinutes).c_str()) + FString(" minute(s)");
    }
    result += FString(".");

    Log::GetLog()->info(
        "Applied player harvest boost to EOS ID '{}' (team {}, playerDataId {}): amount={} multiplier={} tribe={} expiry={}.",
        boost.targetEosId, teamId, playerDataId, harvestAmount, isMultiplier, alsoForTribe, expiry);
    return true;
}

// Removes an active player harvest boost early (changePlayerRate <eosid> off).
bool RemovePlayerHarvestBoost(const std::string& eosId, FString& result)
{
    auto it = CousinCustomRates::playerHarvestBoosts.find(eosId);
    if (it == CousinCustomRates::playerHarvestBoosts.end())
    {
        result = FString("No active harvest boost was found for EOS ID '") +
            FString(eosId.c_str()) + FString("'.");
        return false;
    }

    CousinCustomRates::playerHarvestBoosts.erase(it);
    SaveTargetedBoostState();

    AShooterPlayerController* target =
        AsaApi::GetApiUtils().FindPlayerFromEOSID(FString(eosId.c_str()));
    SendTargetedBoostChat(target,
        "Your harvest-rate boost has been removed. You are now using the current server harvest rates.");

    Log::GetLog()->info("Removed player harvest boost for EOS ID '{}'.", eosId);
    result = FString("Removed harvest boost for EOS ID '") + FString(eosId.c_str()) + FString("'.");
    return true;
}

// RCON / console command: changePlayerRate <eosid> [off]
void ChangePlayerRateRcon(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
{
    if (!connection || !packet)
        return;

    std::istringstream input(packet->Body.ToString());
    std::string command;
    std::string eosId;
    std::string action;
    input >> command >> eosId >> action;

    FString reply;
    if (eosId.empty())
        reply = "Usage: changePlayerRate <eosid> [off] - 'off' removes an active boost.";
    else if (action == "off" || action == "remove")
        RemovePlayerHarvestBoost(eosId, reply);
    else
        ApplyPlayerHarvestBoost(FString(eosId.c_str()), reply);

    connection->SendMessageW(packet->Id, 0, &reply);
}

void ChangePlayerRateConsole(APlayerController* playerBase, FString* message, bool /*writtenToConsole*/)
{
    if (!playerBase || !message || !playerBase->IsA(AShooterPlayerController::GetPrivateStaticClass()))
        return;

    auto* admin = static_cast<AShooterPlayerController*>(playerBase);
    std::istringstream input(message->ToString());
    std::string command;
    std::string eosId;
    std::string action;
    input >> command >> eosId >> action;

    FString reply;
    if (eosId.empty())
        reply = "Usage: changePlayerRate <eosid> [off] - 'off' removes an active boost.";
    else if (action == "off" || action == "remove")
        RemovePlayerHarvestBoost(eosId, reply);
    else
        ApplyPlayerHarvestBoost(FString(eosId.c_str()), reply);

    AsaApi::GetApiUtils().SendServerMessage(
        admin, FLinearColor(1.0f, 0.9f, 0.0f, 1.0f), "{}", reply.ToString());
}

void AddOrRemovePlayerRateCommands(bool addCommands = true)
{
    if (addCommands)
    {
        AsaApi::GetCommands().AddRconCommand("changePlayerRate", &ChangePlayerRateRcon);
        AsaApi::GetCommands().AddConsoleCommand("changePlayerRate", &ChangePlayerRateConsole);
        Log::GetLog()->info("Command 'changePlayerRate' registered (RCON + console).");
    }
    else
    {
        AsaApi::GetCommands().RemoveRconCommand("changePlayerRate");
        AsaApi::GetCommands().RemoveConsoleCommand("changePlayerRate");
    }
}

// ---------------------------------------------------------------------------
// PostLogin hook - immediately cache tribe member count when player joins
// ---------------------------------------------------------------------------
DECLARE_HOOK(AShooterGameMode_PostLogin, void, AShooterGameMode*, APlayerController*);

void Hook_AShooterGameMode_PostLogin(AShooterGameMode* gameMode, APlayerController* newPlayer)
{
    AShooterGameMode_PostLogin_original(gameMode, newPlayer);
    
    // Wrapped in try-catch to prevent server crashes from tribe data loading issues
    try
    {
        if (newPlayer && newPlayer->IsA(AShooterPlayerController::GetPrivateStaticClass()))
        {
            auto* shooterPC = static_cast<AShooterPlayerController*>(newPlayer);
            bool changed = false;
            
            // Try to cache tribe member count immediately (will fallback to lazy-load if needed)
            GetAndCacheTribeMemberCount(shooterPC, &changed);
            
            if (changed)
            {
                SaveTargetedBoostState();
                Log::GetLog()->info("PostLogin: cached tribe member count for player (team={}).", 
                    shooterPC->TargetingTeamField());
            }
        }
    }
    catch (const std::exception& e)
    {
        Log::GetLog()->error("PostLogin: Exception during tribe member count caching: {}", e.what());
    }
    catch (...)
    {
        Log::GetLog()->error("PostLogin: Unknown exception during tribe member count caching");
    }
}

void SetTribeHarvestBoostHooks(bool addHooks = true)
{
    if (addHooks)
    {
        AsaApi::GetHooks().SetHook(
            kGiveHarvestResourceHook,
            Hook_UPrimalHarvestingComponent_GiveHarvestResource,
            &UPrimalHarvestingComponent_GiveHarvestResource_original
        );
        AsaApi::GetHooks().SetHook(
            kIncrementItemTemplateQuantityHook,
            Hook_UPrimalInventoryComponent_IncrementItemTemplateQuantity,
            &UPrimalInventoryComponent_IncrementItemTemplateQuantity_original
        );
        AsaApi::GetHooks().SetHook(
            "AShooterGameMode.PostLogin(APlayerController*)",
            Hook_AShooterGameMode_PostLogin,
            &AShooterGameMode_PostLogin_original
        );
        Log::GetLog()->info("Tribe harvest context, quantity, and PostLogin hooks registered.");
    }
    else
    {
        AsaApi::GetHooks().DisableHook(
            kGiveHarvestResourceHook,
            Hook_UPrimalHarvestingComponent_GiveHarvestResource
        );
        AsaApi::GetHooks().DisableHook(
            kIncrementItemTemplateQuantityHook,
            Hook_UPrimalInventoryComponent_IncrementItemTemplateQuantity
        );
        AsaApi::GetHooks().DisableHook(
            "AShooterGameMode.PostLogin(APlayerController*)",
            Hook_AShooterGameMode_PostLogin
        );
    }
}
