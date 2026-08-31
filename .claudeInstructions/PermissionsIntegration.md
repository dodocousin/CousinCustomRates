# Permissions Plugin Integration Guide

This document explains how to integrate the official **Permissions** plugin into any of your ASA server plugins.
Reference implementations: `CousinDiscordLinker`, `Cousin_TribeLogRelay`.

---

## 1. Project File (.vcxproj) Changes

Two things are required in the `<Link>` section:

### a) Add Permissions.lib to AdditionalDependencies
```xml
<AdditionalDependencies>
  $(SolutionDir)..\AsaApi\out_lib\AsaApi.lib;
  $(SolutionDir)..\ASA-Plugins\Permissions\out_lib\Permissions.lib;
  ... other libs ...
  %(AdditionalDependencies)
</AdditionalDependencies>
```

### b) Delay-load Permissions.dll (CRITICAL — prevents error 126 on startup)
```xml
<DelayLoadDLLs>Permissions.dll;%(DelayLoadDLLs)</DelayLoadDLLs>
```

> **Why delay-load?**
> Without `/DELAYLOAD`, Windows tries to resolve `Permissions.dll` the moment your plugin DLL is loaded —
> but at that point the Permissions plugin may not be loaded yet and its DLL folder isn't in the standard
> search path. Error 126 (`ERROR_MOD_NOT_FOUND`) results. Delay-loading defers resolution to the first
> actual function call, which happens well after the server is fully running.
> `delayimp.lib` is NOT needed explicitly — MSVC v143 links it automatically.

### c) Add Permissions public headers to AdditionalIncludeDirectories
```xml
<AdditionalIncludeDirectories>
  ...existing paths...;
  $(SolutionDir)..\ASA-Plugins\Permissions\Permissions\Public;
  %(AdditionalIncludeDirectories)
</AdditionalIncludeDirectories>
```

---

## 2. Include the Header

In whatever `.h` file uses Permissions functions (e.g. `Utils.h`):

```cpp
#include <Permissions.h>
```

The `Permissions.h` public header lives at:
`G:\GitHubRepo\ASA-Plugins\Permissions\Permissions\Public\Permissions.h`

---

## 3. Key API Functions

```cpp
// Add a player to a group (returns nullopt on success, error string on failure)
std::optional<std::string> Permissions::AddPlayerToGroup(const FString& eos_id, const FString& group);

// Remove a player from a group
std::optional<std::string> Permissions::RemovePlayerFromGroup(const FString& eos_id, const FString& group);

// Check if a player is in a group
bool Permissions::IsPlayerInGroup(const FString& eos_id, const FString& group);

// Check if a player has a specific permission
bool Permissions::IsPlayerHasPermission(const FString& eos_id, const FString& permission);

// Get all groups a player belongs to
TArray<FString> Permissions::GetPlayerGroups(const FString& eos_id);

// Add/remove a group itself
std::optional<std::string> Permissions::AddGroup(const FString& group);
std::optional<std::string> Permissions::RemoveGroup(const FString& group);
```

All functions take `FString` (not `std::string`). Convert with:
```cpp
const FString eosIdFStr(eosIdStr.c_str());  // std::string → FString
const std::string str = fstr.ToString();     // FString → std::string
```

---

## 4. Config Pattern (Optional Integration)

Use an empty string as "disabled" so the feature is inert when not configured:

**config.json:**
```json
{
  "PermissionGroup": ""
}
```

**Namespace header (e.g. MyPlugin.h):**
```cpp
inline std::string permissionGroup;  // empty = feature disabled
```

**ReadConfig():**
```cpp
MyPlugin::permissionGroup = MyPlugin::config.value("PermissionGroup", "");
```

---

## 5. Standard Helper Function Pattern

Always guard with an empty check (feature off) and an idempotency check (already in group):

```cpp
void AddPlayerToPermissionGroup(const FString& eosId)
{
    if (MyPlugin::permissionGroup.empty())
        return; // feature disabled

    const FString group(MyPlugin::permissionGroup.c_str());

    // Idempotent: skip if already a member
    if (Permissions::IsPlayerInGroup(eosId, group))
    {
        if (MyPlugin::isDebug)
            Log::GetLog()->info("AddToPermGroup: {} already in '{}'",
                eosId.ToString(), MyPlugin::permissionGroup);
        return;
    }

    auto err = Permissions::AddPlayerToGroup(eosId, group);
    if (err.has_value())
    {
        Log::GetLog()->error("AddToPermGroup: failed to add {} to '{}' – {}",
            eosId.ToString(), MyPlugin::permissionGroup, err.value());
    }
    else
    {
        Log::GetLog()->info("AddToPermGroup: {} added to '{}'",
            eosId.ToString(), MyPlugin::permissionGroup);
    }
}
```

---

## 6. Rules & Gotchas

- **Always use EOS ID** (`FString`) — never Steam ID. Use `GetEOSId()` or `GetEOSIDFromController()`.
- **The Permissions group must already exist** in the Permissions plugin before you can add players to it.
  If the group doesn't exist, `AddPlayerToGroup` will return an error string.
- **Check `std::optional::has_value()`** — `nullopt` = success, `has_value()` = error.
- **Never call Permissions functions at plugin load time** (before the server is ready). Only call them
  from hooks, timers, or commands that fire after `OnServerReady()`.
- **Delay-load is mandatory** — without it the plugin fails to load with error 126.
- The Permissions `.dll` must be present on the server for the feature to work at runtime.
  If Permissions is not installed and a player triggers the code path, it will crash.
  Guard runtime calls if Permissions may not always be present (or keep the empty-string guard).

---

## 7. config-commented.json Entry

```jsonc
// (Optional) Permissions plugin integration.
// Set this to the name of a Permissions group that qualifying players should be
// automatically added to. Leave empty (or omit) to disable — no Permissions API
// calls will be made.
// The group must already exist in the Permissions plugin.
// Example: "PermissionGroup": "verified"
"PermissionGroup": "",
```

---

## 8. File Locations Reference

| Item | Path |
|------|------|
| Permissions public header | `G:\GitHubRepo\ASA-Plugins\Permissions\Permissions\Public\Permissions.h` |
| Permissions import lib | `G:\GitHubRepo\ASA-Plugins\Permissions\out_lib\Permissions.lib` |
| DB helper header | `G:\GitHubRepo\ASA-Plugins\Permissions\Permissions\Public\DBHelper.h` |
