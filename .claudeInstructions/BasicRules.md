You are helping me write C++ plugins for Ark: Survival Ascended using the ASA Server API only.
Rules:
This is ASA, not ASE. Do not use ASE-only code, Steam ID logic, or old Ark Server API assumptions.
Always use EOS ID, not Steam ID. Prefer GetEOSId(), GetEOSIDFromController(), FindPlayerFromEOSID(), or related ASA API helpers.
Use the ASA Server API headers/docs I provide as the source of truth. Do not invent classes, methods, hook signatures, or field names.
When accessing ASA fields, use the generated Field() accessors, for example TargetingTeamField(), PlayerStateField(), PlayerControllerListField(), etc.
For UObject/AActor type checks, do not blindly cast. First check null, then use IsA(SomeClass::StaticClass() or GetPrivateStaticClass()) when the real runtime type is uncertain.
Avoid static_cast for UObject/AActor downcasts unless the source type is already guaranteed by the callback signature or has just been checked with IsA().
Do not use dynamic_cast for ASA UObject types.
Do not use reinterpret_cast except when matching ASA hook/trampoline patterns that explicitly require it.
For hooks, use AsaApi::GetHooks().SetHook(...) and always remove hooks with DisableHook(...) on unload.
For chat/console/RCON/timer callbacks, use AsaApi::GetCommands() and always remove registered commands/callbacks on unload.
Always include full plugin lifecycle code: Load(), Unload(), command registration, command removal, hook registration, hook removal, and config loading if needed.
Use nlohmann::json safely: check file exists, parse with try/catch, use .value() for defaults, and do not rely on deprecated stream/operator patterns.
Never assume a pointer is valid. Check nullptr before using controllers, characters, inventories, tribes, player states, worlds, and game modes.
When generating code, output copy-paste-ready C++ for ASA Server API, with required includes and function signatures.
If unsure about a method, class, hook name, or parameter list, say you are unsure instead of inventing it.
Prefer existing ASA helper functions such as SendServerMessage, SendNotification, FindPlayerFromEOSID, GetWorld, GetShooterGameMode, and GetStatus when available.
When looping player controllers from PlayerControllerListField(), remember entries are weak pointers; call Get(), check the result, and only cast when safe.
Do not provide Blueprint/mod kit code unless I specifically ask. This is server-side ASA plugin C++