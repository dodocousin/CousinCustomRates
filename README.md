# CousinCustomRates

An **ARK: Survival Ascended** server plugin built with the [ASA API](https://github.com/ArkServerApi/AsaApi) that lets you dynamically change server rate multipliers at runtime — no restart required.

Rates can be changed instantly via RCON, or switched automatically on a day/hour schedule. Every change is persisted to disk and automatically re-applied after a server restart.

---

## Features

| Feature | Description |
|---|---|
| **Rate presets** | Define unlimited named presets in `config.json`, each with its own multiplier values |
| **RCON command** | Switch any preset instantly with `changerates <preset_name>` |
| **Hot config reload** | Edit `config.json` and apply changes without restarting with `CousinCustomRates.Reload` |
| **Persistent state** | The active preset is saved to `status.json` and automatically re-applied on every server restart |
| **In-game broadcast** | Sends a server-wide message to all players when rates change (optional per preset) |
| **Discord webhook** | Posts a notification to a Discord channel when rates change (optional per preset) |
| **Discord rich embed** | Each preset can define a formatted embed with title, color, and description (falls back to plain text if not configured) |
| **Automatic scheduler** | Optional day/hour schedule that switches presets automatically without any admin input |

---

## Installation

1. Build the plugin (Release x64) using Visual Studio 2022.
2. Copy the output files to your server:
   ```
   ShooterGame/Binaries/Win64/ArkApi/Plugins/CousinCustomRates/
     CousinCustomRates.dll
     CousinCustomRates.dll.arkapi
     config.json
     PluginInfo.json
   ```
3. Edit `config.json` to define your rate presets (see below).
4. Start or restart the server — the plugin will load automatically.

---

## RCON Commands

| Command | Description |
|---|---|
| `changerates <preset_name>` | Activate a named preset immediately |
| `changerates` *(no argument)* | Lists all available preset names |
| `CousinCustomRates.Reload` | Hot-reload `config.json` without restarting |

**Examples:**
```
changerates weekend_rates
changerates normal_rates
CousinCustomRates.Reload
```

---

## config.json Reference

The configuration file is located at:
```
ArkApi/Plugins/CousinCustomRates/config.json
```

The file has two top-level sections: **`RatePresets`** and **`Schedule`**.

---

### Section 1 — `RatePresets`

Defines all available rate presets. You can create as many as you like.

```json
"RatePresets": {
    "your_preset_name": { ... },
    "another_preset":   { ... }
}
```

Each preset is identified by its **JSON key** (e.g. `"weekend_rates"`). This is the name you use in the RCON command and the scheduler.

#### Preset fields

| Field | Type | Required | Description |
|---|---|---|---|
| `TamingSpeedMultiplier` | `float` | ✅ | Taming speed multiplier |
| `XPMultiplier` | `float` | ✅ | XP gain multiplier |
| `HarvestAmountMultiplier` | `float` | ✅ | Harvest amount multiplier |
| `BabyMatureSpeedMultiplier` | `float` | ✅ | Baby maturation speed multiplier |
| `EggHatchSpeedMultiplier` | `float` | ✅ | Egg hatch speed multiplier |
| `BroadcastMessage` | `string` | ❌ | In-game message sent to all players when this preset activates. Leave as `""` to skip. |
| `Discord_Webhook` | `string` | ❌ | Full Discord webhook URL. Leave as `""` to skip Discord entirely — no errors occur. |
| `Discord_Embed` | `object` | ❌ | Rich embed config for Discord (see below). If absent, a plain text message is sent instead. |

#### `Discord_Embed` fields

| Field | Type | Required | Description |
|---|---|---|---|
| `Title` | `string` | ❌ | Embed title (default: `"Rate Change"`) |
| `Description` | `string` | ❌ | Embed body text |
| `Color` | `integer` | ❌ | Left-side color bar as a **decimal RGB integer** (default: `3447003` = blue). See color reference below. |
| `Footer` | `string` | ❌ | Small footer text at the bottom of the embed |

> **Discord Color Reference**
> Convert any hex color to decimal at [binaryhexconverter.com](https://www.binaryhexconverter.com/hex-to-decimal-converter).
> Common values: `3066993` = green · `15158332` = red · `16776960` = yellow · `3447003` = blue · `10070709` = grey

> **Rate fields in embeds** — Taming, XP, Harvest, Baby Mature, and Egg Hatch values are **always added automatically** as inline fields from the preset multipliers. You do not need to list them manually in `Discord_Embed`.

#### Full preset example

```json
"weekend_rates": {
    "BroadcastMessage": "Weekend rates are now ACTIVE! Enjoy the boosted rates!",

    "Discord_Webhook": "https://discord.com/api/webhooks/1234567890/YOUR_TOKEN",

    "Discord_Embed": {
        "Title":       "⚡ Weekend Rates Activated",
        "Description": "Server is now running boosted weekend rates!",
        "Color":       3066993,
        "Footer":      "CousinCustomRates"
    },

    "TamingSpeedMultiplier":     10.0,
    "XPMultiplier":              10.0,
    "HarvestAmountMultiplier":   20.0,
    "BabyMatureSpeedMultiplier": 50.0,
    "EggHatchSpeedMultiplier":   50.0
}
```

The Discord embed produced by the above will look like this:

```
┌─────────────────────────────────────────────┐  ← green left border
│ ⚡ Weekend Rates Activated                   │  ← Title
│ Server is now running boosted weekend rates! │  ← Description
│─────────────────────────────────────────────│
│ Preset:        weekend_rates                │
│ Taming: 10x  │ XP: 10x  │ Harvest: 20x     │
│ Baby Mature: 50x  │ Egg Hatch: 50x          │
│─────────────────────────────────────────────│
│ CousinCustomRates                           │  ← Footer
└─────────────────────────────────────────────┘
```

---

### Section 2 — `Schedule` (optional)

Automatically activates presets based on the current day of the week and hour. When disabled, **no timer is registered** and there is zero runtime overhead.

```json
"Schedule": {
    "Enabled": false,
    "CheckIntervalSeconds": 60,
    "Rules": [ ... ]
}
```

| Field | Type | Description |
|---|---|---|
| `Enabled` | `bool` | Set to `true` to activate the scheduler. Default: `false`. |
| `CheckIntervalSeconds` | `integer` | How often the rules are evaluated (in seconds). Minimum useful value: `60`. |
| `Rules` | `array` | Ordered list of schedule rules (see below). |

#### Rule fields

| Field | Type | Description |
|---|---|---|
| `Preset` | `string` | The preset key to activate when this rule matches (must exist in `RatePresets`) |
| `Days` | `array of strings` | Days of the week this rule applies to. Valid values: `"Sunday"` `"Monday"` `"Tuesday"` `"Wednesday"` `"Thursday"` `"Friday"` `"Saturday"` |
| `StartHour` | `integer` | Hour to start (0–23, 24h format, **inclusive**) |
| `EndHour` | `integer` | Hour to end (0–23, 24h format, **inclusive**). Must be ≥ `StartHour`. |

#### How rule matching works

1. Rules are evaluated **in order** — the **first matching rule wins**.
2. A rule matches when **both** conditions are true:
   - The current day name is in the `Days` list.
   - The current hour (0–23) is between `StartHour` and `EndHour` inclusive.
3. If the matching preset is **already active**, nothing happens (no redundant re-application or Discord spam).
4. If **no rule matches** the current time slot, the active preset is left unchanged.

> **Timezone:** The scheduler uses the **server machine's local timezone** as configured in the OS. Make sure your server clock is set to your intended timezone.

> **Overnight ranges** (e.g. 22:00 – 02:00) are **not supported** in a single rule. Use two rules as a workaround:
> ```json
> { "Preset": "night_rates", "Days": [...], "StartHour": 22, "EndHour": 23 },
> { "Preset": "night_rates", "Days": [...], "StartHour": 0,  "EndHour": 2  }
> ```

#### Schedule example

```json
"Schedule": {
    "Enabled": true,
    "CheckIntervalSeconds": 60,
    "Rules": [
        {
            "Preset": "weekend_rates",
            "Days":   ["Friday", "Saturday", "Sunday"],
            "StartHour": 18,
            "EndHour":   23
        },
        {
            "Preset": "normal_rates",
            "Days":   ["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"],
            "StartHour": 0,
            "EndHour":   17
        }
    ]
}
```

This configuration:
- Activates `weekend_rates` every **Friday, Saturday, and Sunday from 6 PM to 11:59 PM**.
- Activates `normal_rates` every day from **midnight to 5:59 PM**.
- Between midnight and 6 PM on weekends, the `normal_rates` rule also matches (it covers all days, 0–17).

---

## Persistence

When a preset is activated (via RCON or the scheduler), the plugin writes a `status.json` file:

```
ArkApi/Plugins/CousinCustomRates/status.json
```

```json
{
  "active_preset": "weekend_rates"
}
```

On every server restart, this file is read during `AShooterGameMode::InitGame` and the saved preset is automatically re-applied — **before any players can connect** — so rates are always consistent.

To clear the saved preset and revert to native `GameUserSettings.ini` defaults on next restart, simply delete `status.json`.

---

## Complete config.json Example

```json
{
  "RatePresets": {
    "weekend_rates": {
      "BroadcastMessage": "Weekend rates are now ACTIVE! Enjoy the boosted rates!",
      "Discord_Webhook": "https://discord.com/api/webhooks/YOUR_ID/YOUR_TOKEN",
      "Discord_Embed": {
        "Title":       "⚡ Weekend Rates Activated",
        "Description": "Server is now running boosted weekend rates!",
        "Color":       3066993,
        "Footer":      "CousinCustomRates"
      },
      "TamingSpeedMultiplier":     10.0,
      "XPMultiplier":              10.0,
      "HarvestAmountMultiplier":   20.0,
      "BabyMatureSpeedMultiplier": 50.0,
      "EggHatchSpeedMultiplier":   50.0
    },
    "normal_rates": {
      "BroadcastMessage": "Normal rates are now active.",
      "Discord_Webhook": "",
      "Discord_Embed": {
        "Title":       "📋 Normal Rates Restored",
        "Description": "Server has returned to standard rates.",
        "Color":       10070709,
        "Footer":      "CousinCustomRates"
      },
      "TamingSpeedMultiplier":     5.0,
      "XPMultiplier":              5.0,
      "HarvestAmountMultiplier":   10.0,
      "BabyMatureSpeedMultiplier": 25.0,
      "EggHatchSpeedMultiplier":   25.0
    }
  },
  "Schedule": {
    "Enabled": false,
    "CheckIntervalSeconds": 60,
    "Rules": [
      {
        "Preset": "weekend_rates",
        "Days": ["Friday", "Saturday", "Sunday"],
        "StartHour": 18,
        "EndHour": 23
      },
      {
        "Preset": "normal_rates",
        "Days": ["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"],
        "StartHour": 0,
        "EndHour": 17
      }
    ]
  }
}
```

---

## Building from Source

**Requirements:**
- Visual Studio 2022 (v143 toolset)
- [ASA API](https://github.com/ArkServerApi/AsaApi) cloned as a sibling folder: `..\AsaApi\`
- vcpkg (for `fmt` dependency, configured via `vcpkg.json`)

**Build:**
1. Open `CousinCustomRates.sln` in Visual Studio 2022.
2. Select **Release | x64**.
3. Build → the output is placed in `Output\CousinCustomRates\`.

---

## License

See [LICENSE](LICENSE).
