# CousinCustomRates

An **ARK: Survival Ascended** server plugin built with the [ASA API](https://github.com/ArkServerApi/AsaApi) that lets you dynamically change server rate multipliers at runtime — no restart required.

Rates can be changed instantly via RCON or the in-game admin console, switched automatically on a day/hour schedule, or set to run for a fixed duration before automatically reverting. Every change is persisted to disk and automatically re-applied after a server restart.

---

## Features

| Feature | Description |
|---|---|
| **Rate presets** | Define unlimited named presets in `config.json`, each with its own multiplier values |
| **RCON & console commands** | Switch global presets with `changerates <preset_name>` or give one player a harvest-only boost with `changePlayerRate <eosid>` |
| **Hot config reload** | Edit `config.json` and apply changes without restarting with `CousinCustomRates.Reload` |
| **Persistent state** | The active preset is saved to `status.json` and automatically re-applied on every server restart |
| **In-game broadcast** | Sends a server-wide message to all players when rates change (optional per preset) |
| **Discord webhook** | Posts a notification to a Discord channel **only when rates are actively changed** — not on server restart (optional per preset) |
| **Discord rich embed** | Each preset can define a formatted embed with title, color, and description (falls back to plain text if not configured) |
| **Timed presets** | Any preset can have a `Duration` (in minutes) that causes it to automatically revert after the countdown expires |
| **Targeted player harvest boosts** | Give one online player (optionally their whole tribe) a harvest boost from the `ChangePlayerRate` config section - as a multiplier of the current global rate or a fixed rate - without changing server-wide rates; timed boosts automatically fall back to the current global rate |
| **Automatic scheduler** | Optional day/hour schedule that switches presets automatically without any admin input |
| **Config validation** | Every config load/reload validates the JSON structure and logs actionable warnings for any issues found |

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

## Commands

All commands work both via **RCON** and the **in-game admin console** (Tab key).

| Command | Description |
|---|---|
| `changerates <preset_name>` | Activate a named preset immediately |
| `changerates` *(no argument)* | Lists all available preset names |
| `changePlayerRate <eosid>` | Apply the `ChangePlayerRate` config boost to the online target player |
| `changePlayerRate <eosid> off` | Remove that player's harvest boost early |
| `CousinCustomRates.Reload` | Hot-reload `config.json` without restarting |

**Examples:**
```
changerates weekend_rates
changerates event_rates
changePlayerRate 00000000000000000000000000000000
CousinCustomRates.Reload
```

### Targeted player harvest boosts

`changePlayerRate` uses the supplied **EOS ID** to locate an online player and
applies the `ChangePlayerRate` section of `config.json` to them. It changes
only harvesting for that player (or their tribe); it does not modify the
global server rate or other tribes.

```text
changePlayerRate <eosid> [off]
```

Example:

```text
changePlayerRate 00000000000000000000000000000000
changePlayerRate 00000000000000000000000000000000 off
```

The target must be online. The boost is stored by EOS ID together with the
player's ARK player data ID, so it keeps working while they are offline and
also covers their own tamed dinos. With `AlsoForTheTribe: true` the boost
extends to the whole tribe (members and tribe dinos).

#### Harvest-rate behavior

`ChangePlayerRate.Multiplier` controls how `HarvestAmount` is interpreted:

- `true` — **multiplier of the current global rate**. If the active preset has
  `HarvestAmountMultiplier: 2.0` and `HarvestAmount: 10`, boosted players
  harvest at `20x`.
- `false` — **fixed rate**. `HarvestAmount: 10` means a flat `10x` harvest,
  whatever the global rate is.

The implementation marks ARK's harvest-resource grant path, then adjusts only
the matching item-quantity grant to that harvest's destination inventory. It
does not boost crafting, loot transfers, item pickups, admin item grants, or
ordinary inventory operations.

#### Harvest troubleshooting

If a targeted boost does not produce the expected item amount, temporarily add
or set this top-level option in `config.json`:

```json
"TribeHarvestBoostDebug": true
```

Run `CousinCustomRates.Reload`, apply the targeted boost again, and harvest a
resource while it is active. The server log will include `TribeHarvestDebug`
entries showing the team ID, active global multiplier, targeted multiplier,
correction factor, original item quantity, and adjusted quantity. Set the
option back to `false` once testing is complete.

#### Duration and expiry

`ChangePlayerRate.DurationMinutes` sets the boost duration in minutes. At
expiry, the boost is removed and affected players immediately return to the
**currently active global server harvest rate**. The expiry never changes the
global preset or the scheduler.

Reapplying `changePlayerRate` to the same player replaces the prior boost and
resets its duration. `DurationMinutes: 0` creates a permanent boost that lasts
until it is removed with `changePlayerRate <eosid> off`.

Targeted boosts are persisted in `status.json`. A still-valid timed boost is
restored after restart; one that expired while the server was offline is simply
discarded.

#### Private activation and expiry messages

The optional `ActivationMessage` and `ExpiryMessage` fields inside
`ChangePlayerRate` control the private normal-chat messages sent when a boost
is applied or when a timed boost expires:

```json
"ChangePlayerRate": {
  "ActivationMessage": "Bonus de récolte activé : {rate}x pendant {duration} minute(s) !",
  "ExpiryMessage": "Votre bonus de récolte de {rate}x pendant {duration} minute(s) est expiré."
}
```

Supported placeholders are:

- `{rate}` — final effective harvest rate, such as `4` or `40`.
- `{duration}` — the current `ChangePlayerRate.DurationMinutes` value.

Set either message to `""` to suppress that notification. If a message is
omitted, the plugin uses its built-in English message. These messages are never
broadcast server-wide and are never sent to Discord.

For backward compatibility, the top-level
`TribeHarvestBoostExpiredMessage` remains supported as the expiry-message
fallback when `ChangePlayerRate.ExpiryMessage` is omitted.

The optional top-level `ChatSenderName` controls the sender label for the
private boost chat messages: activation and expiry.

```json
"ChatSenderName": "CousinCustomRates"
```

For example, set it to `"Multiplicateurs"` to display that name in chat. If it
is omitted or empty, the plugin uses `CousinCustomRates`. This setting does not
change global notifications or Discord messages.

### Tribe-size harvest balancing

The optional top-level `TribeSizeMultiplier` applies an additional harvest
modifier based on the tribe's **total membership**, including offline members.
It does not use the online player count. A rule applies only to an exact tribe
size; any size not listed receives no extra modifier (`1.0x`).

```json
"TribeSizeMultiplier": {
  "Enabled": true,
  "Multipliers": [
    { "TribeSize": 1, "Multiplier": 4.0 },
    { "TribeSize": 2, "Multiplier": 3.0 },
    { "TribeSize": 3, "Multiplier": 2.0 },
    { "TribeSize": 6, "Multiplier": 0.5 }
  ]
}
```

With a global harvest rate of `2x`, the example gives a one-member tribe `8x`
final harvest and a six-member tribe `1x` final harvest. A tribe with four or
five members is not listed, so it remains at `2x`.

The modifier also combines with `changePlayerRate`. For example, a targeted
tribe rate of `20x` and a one-member size modifier of `4x` produces `80x` final
harvest for that tribe.

Player and ridden-dino harvests read the total member count directly from the
server's tribe data. For unmounted tames, the plugin uses a local cached count
that is refreshed whenever a tribe member is online and saved across restart.
If no member of that tribe has connected to the map since the cache was created,
the unmounted tame safely receives no size modifier until the count becomes
known.

#### Targeted messages and Discord

For `changePlayerRate`, the preset's `BroadcastMessage` is sent as a normal
server chat message **only** to the EOS-ID player in the command. It is not
broadcast to the server. Targeted boosts do not post to the preset's
`Discord_Webhook`; global `changerates` behavior remains unchanged.

---

## config.json Reference

The configuration file is located at:
```
ArkApi/Plugins/CousinCustomRates/config.json
```

The file has three main sections: **`RatePresets`**, **`TimedPresets`**, and
**`Schedule`**. `ChangePlayerRate` contains the optional targeted-harvest
settings, including private activation and expiry message templates.

---

### Section 1 — `RatePresets`

Defines all available rate presets. You can create as many as you like.

```json
"RatePresets": {
    "your_preset_name": { ... },
    "another_preset":   { ... }
}
```

Each preset is identified by its **JSON key** (e.g. `"weekend_rates"`). This is the name you use in the commands and the scheduler.

#### Preset fields

| Field | Type | Required | Description |
|---|---|---|---|
| `TamingSpeedMultiplier` | `float` | ✅ | Taming speed multiplier |
| `XPMultiplier` | `float` | ✅ | XP gain multiplier |
| `HarvestAmountMultiplier` | `float` | ✅ | Harvest amount multiplier |
| `BabyMatureSpeedMultiplier` | `float` | ✅ | Baby maturation speed multiplier |
| `EggHatchSpeedMultiplier` | `float` | ✅ | Egg hatch speed multiplier |
| `Duration` | `integer` | ❌ | **Minutes** before the preset automatically reverts. Requires `TimedPresets.Enabled = true`. Omit for a permanent preset. |
| `BroadcastMessage` | `string` | ❌ | Global `changerates`: message sent to all players. `changePlayerRate`: normal chat sent only to the command target's EOS ID. Leave as `""` to skip. |
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

> **Discord on restart** — Discord notifications (and in-game broadcasts) are **only sent when rates are actively changed** via command or scheduler. Silently restoring the saved preset on server restart does **not** trigger any notifications.

#### Standard preset example

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

---

### Section 2 — `TimedPresets` (optional)

Master switch for the timed preset feature. When disabled, all `Duration` fields are silently ignored and every `changerates` call behaves as a permanent switch.

```json
"TimedPresets": {
    "Enabled": true
}
```

| Field | Type | Description |
|---|---|---|
| `Enabled` | `bool` | Set to `true` to allow presets to have a `Duration` countdown. Default: `false`. |

#### How timed presets work

When you run `changerates event_rates` and `event_rates` has a `Duration` field with `TimedPresets.Enabled = true`:

1. Rates apply immediately — broadcast and Discord fire as normal.
2. A countdown starts for `Duration` minutes.
3. **While the countdown is active**, the automatic scheduler is **paused** — schedule rules cannot override the event.
4. When the countdown expires:
   - If a **Schedule rule** currently matches the server time → that preset activates (the scheduler resumes naturally).
   - Otherwise → the preset that was **active before the event** is restored.
   - The revert fires with **full notifications** (broadcast + Discord).

#### Restart survival

The expiry timestamp is saved to `status.json`. On the next server start:
- If the timer **already expired** during downtime → the fallback is applied immediately with full notifications.
- If time **remains** → the countdown is re-armed with the remaining seconds.

#### Stacking timed presets

If you activate a second timed preset while one is already running, the countdown resets to the new duration but the **fallback target is not changed** — it stays as the original pre-event preset, so you always revert cleanly.

#### Timed preset example

```json
"event_rates": {
    "Duration": 120,
    "BroadcastMessage": "🎉 2-hour EVENT rates are now ACTIVE!",
    "Discord_Webhook": "",
    "Discord_Embed": {
        "Title":       "🎉 Event Rates Active (2h)",
        "Description": "Boosted event rates are live for 2 hours! Rates will revert automatically.",
        "Color":       15844367,
        "Footer":      "CousinCustomRates"
    },
    "TamingSpeedMultiplier":     25.0,
    "XPMultiplier":              25.0,
    "HarvestAmountMultiplier":   50.0,
    "BabyMatureSpeedMultiplier": 100.0,
    "EggHatchSpeedMultiplier":   100.0
}
```

`Duration: 120` = 2 hours. Activate with `changerates event_rates`. Rates revert automatically after 2 hours.

---

### Section 3 — `Schedule` (optional)

Automatically activates presets based on the current day and time. When disabled, **no timer is registered** and there is zero runtime overhead.

> **Timed preset interaction** — While a timed preset countdown is running, the Schedule is **paused**. It resumes automatically when the timed preset expires. If a schedule rule matches at the moment of expiry, that preset is used as the revert target.

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
| `DefaultPreset` | `string` | ❌ Optional. Preset to apply when **no rule matches** the current time. If omitted, the active preset stays unchanged when there are gaps between rules. |
| `Rules` | `array` | Ordered list of schedule rules. Two formats are supported — see below. |

---

#### Rule Format 1 — Day-Range (recommended)

Defines a **continuous time window** that spans from a start day+hour to an end day+hour. This is the cleanest way to express "Friday evening through Sunday afternoon":

| Field | Type | Description |
|---|---|---|
| `Preset` | `string` | Preset to activate when this rule matches |
| `Enabled` | `bool` | Set to `false` to skip this rule without deleting it. Defaults to `true`. |
| `StartDay` | `string` | Day the window opens. Valid values: `"Sunday"` `"Monday"` `"Tuesday"` `"Wednesday"` `"Thursday"` `"Friday"` `"Saturday"` |
| `StartHour` | `integer` | Hour the window opens (0–23, 24h, **inclusive**) |
| `EndDay` | `string` | Day the window closes (same valid values as `StartDay`) |
| `EndHour` | `integer` | Hour the window closes (0–23, 24h, **inclusive**) |

The window is evaluated as a continuous week-position (`dayIndex × 24 + hour`). If `EndDay:EndHour` is earlier in the week than `StartDay:StartHour`, the range **wraps automatically** over the Sunday midnight boundary.

**Example — Friday 16:00 → Sunday 16:00:**
```json
{
  "Preset":    "weekend_rates",
  "StartDay":  "Friday",
  "StartHour": 16,
  "EndDay":    "Sunday",
  "EndHour":   16
}
```
This matches: Friday 16h–23h, all of Saturday, and Sunday 0h–16h. ✅

**Example — disabled rule (temporarily off):**
```json
{
  "Enabled":   false,
  "Preset":    "event_rates",
  "StartDay":  "Saturday",
  "StartHour": 12,
  "EndDay":    "Saturday",
  "EndHour":   18
}
```

---

#### Rule Format 2 — Days Array (legacy, still supported)

Applies the **same hour window to every listed day** independently. Simpler for cases that don't need continuous cross-day ranges:

| Field | Type | Description |
|---|---|---|
| `Preset` | `string` | Preset to activate when this rule matches |
| `Enabled` | `bool` | Optional toggle — same as Format 1 |
| `Days` | `array of strings` | Days this rule applies to |
| `StartHour` | `integer` | Hour to start (0–23, **inclusive**) |
| `EndHour` | `integer` | Hour to end (0–23, **inclusive**). Must be ≥ `StartHour`. |

> Overnight ranges (`StartHour > EndHour`) are **not supported** in this format.

---

#### How rule matching works

1. Rules are evaluated **in order** — the **first matching rule wins**.
2. If the matched preset is **already active**, nothing happens (no Discord spam).
3. If **no rule matches** and `DefaultPreset` is set → the default preset is applied.
4. If **no rule matches** and no `DefaultPreset` is set → the active preset is left unchanged.

> **Timezone:** The scheduler uses the **server machine's local timezone** as configured in the OS.

---

## Config Validation

Every time `config.json` is loaded or reloaded, the plugin automatically validates its structure and logs any issues to the server log. Issues are reported as **warnings** (non-fatal) or **errors** (fatal, e.g. missing `RatePresets`).

Checks include:
- `RatePresets` exists and is an object
- Each preset has all 5 multiplier fields, all are positive numbers
- `Discord_Webhook` URLs start with `https://`
- `Duration` values are positive integers (minutes)
- `TimedPresets.Enabled` is a boolean
- `Schedule` rules have all required fields, valid day names, hours in `[0,23]`, and `StartHour ≤ EndHour`
- Schedule rule `Preset` values reference existing presets

A summary line is always logged: either `"passed validation with no issues"` or `"X issue(s) found — review warnings above"`.

---

## Persistence

When a preset is activated (via command, timed expiry, or the scheduler), the plugin writes a `status.json` file:

```
ArkApi/Plugins/CousinCustomRates/status.json
```

**Standard preset:**
```json
{
  "active_preset": "weekend_rates"
}
```

**Timed preset (while countdown is active):**
```json
{
  "active_preset": "event_rates",
  "timed_expiry":  1748527200,
  "timed_fallback": "normal_rates"
}
```

On every server restart, this file is read during `AShooterGameMode::InitGame` and the saved preset is automatically re-applied **before any players can connect**. If a timed preset was active, the countdown is either resumed or the fallback is applied immediately.

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
    },
    "event_rates": {
      "Duration": 120,
      "BroadcastMessage": "🎉 2-hour EVENT rates are now ACTIVE!",
      "Discord_Webhook": "",
      "Discord_Embed": {
        "Title":       "🎉 Event Rates Active (2h)",
        "Description": "Boosted event rates are live for 2 hours! Rates will revert automatically.",
        "Color":       15844367,
        "Footer":      "CousinCustomRates"
      },
      "TamingSpeedMultiplier":     25.0,
      "XPMultiplier":              25.0,
      "HarvestAmountMultiplier":   50.0,
      "BabyMatureSpeedMultiplier": 100.0,
      "EggHatchSpeedMultiplier":   100.0
    }
  },
  "TimedPresets": {
    "Enabled": true
  },
  "ChangePlayerRate": {
    "Enable": true,
    "AlsoForTheTribe": true,
    "Multiplier": true,
    "HarvestAmount": 10.0,
    "DurationMinutes": 240,
    "ActivationMessage": "Harvest boost active: {rate}x harvest for {duration} minute(s).",
    "ExpiryMessage": "Your harvest-rate boost of {rate}x for {duration} minute(s) has expired."
  },
  "Schedule": {
    "Enabled": false,
    "CheckIntervalSeconds": 60,
    "Rules": [
      {
        "Preset":    "weekend_rates",
        "StartDay":  "Friday",
        "StartHour": 16,
        "EndDay":    "Sunday",
        "EndHour":   16
      },
      {
        "Preset":    "normal_rates",
        "StartDay":  "Sunday",
        "StartHour": 17,
        "EndDay":    "Friday",
        "EndHour":   15
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
