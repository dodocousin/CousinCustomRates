#pragma once

#include <cmath>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>
#include <API/ARK/Ark.h>
#include <API/ARK/GameMode.h>

// ---------------------------------------------------------------------------
// Targeted tribe harvest boosts
//
// A boost is stored by ARK tribe/team ID, not by EOS ID. EOS is used only to
// find the online command target and to send that one player private chat.
// The verified GiveHarvestResource hook changes only harvest calculations;
// it never changes AShooterGameMode's global harvest multiplier.
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

// Reads the authoritative server-side membership set using GetOrLoadTribeData.
// Falls back to lazy-loaded MyTribeDataField if GetOrLoadTribeData fails.
// Solo players (not in a tribe) return 1.
int GetAndCacheTribeMemberCount(AShooterPlayerController* player, bool* cacheChanged = nullptr)
{
    if (cacheChanged)
        *cacheChanged = false;

    if (!player)
        return 0;

    // METHOD 1: Try server-side authoritative loading (preferred)
    try
    {
        AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
        if (gameMode)
        {
            AShooterPlayerState* playerState = static_cast<AShooterPlayerState*>(player->PlayerStateField().Get());
            if (playerState)
            {
                // Check if solo player
                try
                {
                    if (!playerState->IsInTribe())
                    {
                        Log::GetLog()->info("GetAndCacheTribeMemberCount: Solo player detected");
                        return 1;
                    }
                }
                catch (...)
                {
                    Log::GetLog()->warn("GetAndCacheTribeMemberCount: IsInTribe() failed - continuing");
                }

                const int teamId = player->TargetingTeamField();
                if (teamId > 0)
                {
                    try
                    {
                        FTribeData loadedTribeData;
                        bool success = gameMode->GetOrLoadTribeData(
                            teamId, 
                            &loadedTribeData, 
                            ETribeDataExclude::TribeLogAndTrackingPoints
                        );

                        if (success)
                        {
                            const int memberCount = loadedTribeData.MembersPlayerDataIDField().Num();
                            if (memberCount > 0)
                            {
                                const auto cachedCount = CousinCustomRates::tribeMemberCountsByTeam.find(teamId);
                                if (cachedCount == CousinCustomRates::tribeMemberCountsByTeam.end() || 
                                    cachedCount->second != memberCount)
                                {
                                    CousinCustomRates::tribeMemberCountsByTeam[teamId] = memberCount;
                                    if (cacheChanged)
                                        *cacheChanged = true;
                                    
                                    Log::GetLog()->info("GetAndCacheTribeMemberCount: SUCCESS (authoritative) - teamId={}, count={}", 
                                        teamId, memberCount);
                                }
                                return memberCount;
                            }
                            Log::GetLog()->warn("GetAndCacheTribeMemberCount: GetOrLoadTribeData returned 0 members");
                        }
                        else
                        {
                            Log::GetLog()->warn("GetAndCacheTribeMemberCount: GetOrLoadTribeData failed");
                        }
                    }
                    catch (...)
                    {
                        Log::GetLog()->error("GetAndCacheTribeMemberCount: GetOrLoadTribeData crashed - using fallback");
                    }
                }
            }
        }
    }
    catch (...)
    {
        Log::GetLog()->error("GetAndCacheTribeMemberCount: Method 1 failed - using fallback");
    }

    // METHOD 2: FALLBACK - Use lazy-loaded MyTribeDataField (safe but requires UI open)
    try
    {
        const int teamId = player->TargetingTeamField();
        APlayerState* playerStateBase = player->PlayerStateField().Get();
        
        if (teamId == 0 || !playerStateBase ||
            !playerStateBase->IsA(AShooterPlayerState::GetPrivateStaticClass()))
        {
            return 0;
        }

        auto* playerState = static_cast<AShooterPlayerState*>(playerStateBase);
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
                
                Log::GetLog()->info("GetAndCacheTribeMemberCount: SUCCESS (fallback) - teamId={}, count={}", 
                    teamId, memberCount);
            }
            return memberCount;
        }
    }
    catch (...)
    {
        Log::GetLog()->error("GetAndCacheTribeMemberCount: Fallback method also crashed");
    }

    Log::GetLog()->warn("GetAndCacheTribeMemberCount: All methods failed - returning 0");
    return 0;
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
    auto boostIt = CousinCustomRates::tribeHarvestBoosts.find(teamId);
    HarvestQuantityContext context;

    if (teamId != 0)
    {
        AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
        if (gameMode)
        {
            const float globalMultiplier = gameMode->HarvestAmountMultiplierField();
            if (globalMultiplier > 0.0f)
            {
                float targetMultiplier = globalMultiplier;
                if (boostIt != CousinCustomRates::tribeHarvestBoosts.end())
                {
                    const int64_t now = static_cast<int64_t>(std::time(nullptr));
                    if (boostIt->second.expiryUnixTime == 0 || boostIt->second.expiryUnixTime > now)
                    {
                        targetMultiplier = boostIt->second.harvestMultiplier;
                    }
                }

                const float sizeMultiplier = GetTribeSizeHarvestMultiplier(tribeMemberCount);
                context.destinationInventory = destinationInventory;
                context.teamId = teamId;
                context.tribeMemberCount = tribeMemberCount;
                context.globalMultiplier = globalMultiplier;
                context.targetMultiplier = targetMultiplier;
                context.tribeSizeMultiplier = sizeMultiplier;
                context.correction = (targetMultiplier * sizeMultiplier) / globalMultiplier;
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

// Expiry removes the tribe exception only. The tribe immediately resumes the
// current global preset's harvest rate; no global preset is applied.
void ExpireTribeHarvestBoosts()
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    bool changed = false;

    for (auto it = CousinCustomRates::tribeHarvestBoosts.begin();
        it != CousinCustomRates::tribeHarvestBoosts.end();)
    {
        const CousinCustomRates::TribeHarvestBoost& boost = it->second;
        if (boost.expiryUnixTime == 0 || boost.expiryUnixTime > now)
        {
            ++it;
            continue;
        }

        const std::string eosId = boost.notificationEosId;
        Log::GetLog()->info("Tribe harvest boost for team {} using preset '{}' expired.",
            it->first, boost.presetName);
        it = CousinCustomRates::tribeHarvestBoosts.erase(it);
        changed = true;

        // The original target may be offline. Do not broadcast or queue a
        // delayed message; targeted boost chat is intentionally private.
        if (!eosId.empty())
        {
            AShooterPlayerController* target =
                AsaApi::GetApiUtils().FindPlayerFromEOSID(FString(eosId.c_str()));
            const std::string expiryMessage = CousinCustomRates::config.value(
                "TribeHarvestBoostExpiredMessage",
                "Your tribe harvest-rate boost has expired. Your tribe is now using the current server harvest rates.");
            SendTargetedBoostChat(target, expiryMessage);
        }
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

    ExpireTribeHarvestBoosts();
}

void AddOrRemoveTribeHarvestBoostTimer(bool addTimer = true)
{
    if (addTimer)
        AsaApi::GetCommands().AddOnTimerCallback(
            "CousinCustomRatesTribeBoostTimer", &TribeHarvestBoostTimerCallback);
    else
        AsaApi::GetCommands().RemoveOnTimerCallback("CousinCustomRatesTribeBoostTimer");
}

// Resolves the supplied online EOS target and stores the selected preset against
// its tribe team. Duration follows the existing TimedPresets.Enabled rule.
bool ApplyTribeHarvestBoost(const FString& presetName, const FString& eosId, FString& result)
{
    const std::string presetKey = presetName.ToString();
    if (!CousinCustomRates::config.contains("RatePresets") ||
        !CousinCustomRates::config["RatePresets"].contains(presetKey))
    {
        result = FString("Preset '") + presetName + FString("' does not exist.");
        return false;
    }

    AShooterPlayerController* target = AsaApi::GetApiUtils().FindPlayerFromEOSID(eosId);
    if (!target)
    {
        result = FString("No online player was found for EOS ID '") + eosId + FString("'.");
        return false;
    }

    const int teamId = target->TargetingTeamField();
    if (teamId == 0)
    {
        result = "The target player is not in a tribe, so a tribe harvest boost cannot be applied.";
        return false;
    }

    GetAndCacheTribeMemberCount(target);

    const nlohmann::json& preset = CousinCustomRates::config["RatePresets"][presetKey];
    const float multiplier = preset.value("HarvestAmountMultiplier", 0.0f);
    if (multiplier <= 0.0f)
    {
        result = FString("Preset '") + presetName + FString("' has an invalid HarvestAmountMultiplier.");
        return false;
    }

    int64_t expiry = 0;
    const bool timedEnabled = CousinCustomRates::config.contains("TimedPresets") &&
        CousinCustomRates::config["TimedPresets"].value("Enabled", false);
    if (timedEnabled && preset.contains("Duration") && preset["Duration"].is_number_integer())
    {
        const int64_t minutes = preset["Duration"].get<int64_t>();
        if (minutes > 0)
            expiry = static_cast<int64_t>(std::time(nullptr)) + minutes * 60LL;
    }

    CousinCustomRates::tribeHarvestBoosts[teamId] = {
        presetKey, multiplier, expiry, eosId.ToString()
    };
    SaveTargetedBoostState();

    // BroadcastMessage is deliberately private for targeted boosts. Webhooks
    // and global HUD notifications are exclusive to global changerates.
    SendTargetedBoostChat(target, preset.value("BroadcastMessage", ""));

    result = FString("Applied tribe harvest preset '") + presetName +
        FString("' to EOS ID '") + eosId + FString("'.");
    if (expiry > 0)
    {
        result += FString(" It expires in ") +
            FString(std::to_string(preset["Duration"].get<int64_t>()).c_str()) +
            FString(" minute(s).");
    }

    Log::GetLog()->info("Applied tribe harvest preset '{}' to team {} for EOS ID '{}'.",
        presetKey, teamId, eosId.ToString());
    return true;
}

// RCON / console command: changePlayerRate <preset_name> <eosid>
void ChangePlayerRateRcon(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
{
    if (!connection || !packet)
        return;

    std::istringstream input(packet->Body.ToString());
    std::string command;
    std::string preset;
    std::string eosId;
    input >> command >> preset >> eosId;

    FString reply;
    if (preset.empty() || eosId.empty())
        reply = "Usage: changePlayerRate <preset_name> <eosid>";
    else
        ApplyTribeHarvestBoost(FString(preset.c_str()), FString(eosId.c_str()), reply);

    connection->SendMessageW(packet->Id, 0, &reply);
}

void ChangePlayerRateConsole(APlayerController* playerBase, FString* message, bool /*writtenToConsole*/)
{
    if (!playerBase || !message || !playerBase->IsA(AShooterPlayerController::GetPrivateStaticClass()))
        return;

    auto* admin = static_cast<AShooterPlayerController*>(playerBase);
    std::istringstream input(message->ToString());
    std::string command;
    std::string preset;
    std::string eosId;
    input >> command >> preset >> eosId;

    FString reply;
    if (preset.empty() || eosId.empty())
        reply = "Usage: changePlayerRate <preset_name> <eosid>";
    else
        ApplyTribeHarvestBoost(FString(preset.c_str()), FString(eosId.c_str()), reply);

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
