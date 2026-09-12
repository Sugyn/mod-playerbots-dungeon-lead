# ADR-003: DungeonTestBotPool — targeted unattended testing, Phase 1-2

Status: **Phase 1-2 implemented and verified live**: acquire/status/release for Tank, Healer, and
arbitrary-class Dps roles, a full 5-bot party built on demand, one CC-capability check (Mage/
Polymorph). Later phases (fresh-instance provisioning, 10x parallel campaigns, RunResult/
CampaignManager, more CC classes) are **not implemented** — this ADR documents the direction,
only Phase 1-2 exist in code.

**Phase 2 postscript (same day):** the first live 5-bot-party run caught a real bug the Phase 1
acceptance test's small sample size had missed - see "A verification bug caught by the safety net
itself" below. Left in this ADR rather than quietly editing it away, since it's a good example of
the "never trust a guess, verify at runtime" principle in section 8 actually earning its keep.

## Context

AutoBot Canary (ADR-002) solves "run dungeon-lead sessions without a human typing `startdungeon`,"
but its passive mode (Stage 0/1) depends on the right bots organically queuing for the right
dungeon, and its targeted mode (Stage 2, `TriggerTargetedTest`) depends on the *general* 1000-bot
population happening to have enough idle tanks/healers of the right level *right now*. Live testing
showed this is unreliable in the two situations that matter most for development: right after a
restart (population still logging back in) and for genuinely parallel campaigns (10 groups need 10
tanks + 10 healers simultaneously, which the general population's class/spec distribution does not
reliably guarantee at any given moment).

The ask: a small, deterministic pool of test bot identities that Claude can acquire on demand,
with a **guaranteed** role (not "probably has a tank if we ask for enough bots"), independent of
the general population and independent of any human staying logged in.

## Rejected approach: create new bots

The first design considered creating brand-new characters via `RandomPlayerbotFactory::CreateRandomBot`
with a manually-constructed `WorldSession`. Rejected because:
- it requires correctly replicating a 13-argument `WorldSession` constructor and its lifecycle
  ownership (`sessionBots` tracking) with no room for error - a bad session risks crashing the
  whole worldserver, not just failing the test tool;
- it needs the `playerbots_names` name-cache query replicated;
- it depends on the new account being recognized by whatever mechanism later parts of
  mod-playerbots use to distinguish "my own bot accounts" from everything else - unverified before
  the AI review below caught this.

## Rejected approach: AddClass bots under a live master

The second design considered `.playerbots bot addclass <class>`, the existing command real
players use to adopt a specific-class bot into their own party. Rejected because `AddPlayerBot`
registers the bot under the *master's own* `PlayerbotMgr` instance - tied to that player's live
session. Verified in `PlayerbotMgr.cpp`: `HandlePlayerbotMgrCommand` returns "You may only add
bots from an active session" when `handler->GetSession()` is null, which is exactly the case for a
SOAP/console invocation. This would have required the user to stay logged into the game client for
the test bots to keep running - the opposite of "unattended."

## Accepted approach: existing AddClass pool, masterless login

`PlayerbotHolder::AddPlayerBot(ObjectGuid guid, uint32 masterAccountId)` treats `masterAccountId=0`
as `isRndbot = true` - the exact same code path a fully autonomous random bot uses, no live master
session involved at all. Confirmed live: acquiring a bot this way, and its login/logout not
depending on any player being online.

The bot *identities* come from the existing upstream AddClass pool (`playerbots_account_type.account_type = 2`
in the `acore_playerbots` schema) - ~50 accounts, ~500 characters (50 per class), already
provisioned by this server's setup, currently idle/offline. No new account or character creation.

## A real bug this surfaced: player/bot misclassification

`RandomPlayerbotMgr::OnPlayerLogin(Player* player)` runs for every login (human or bot) and ends
with:

```cpp
if (IsRandomBot(player)) { ... }
else { players.push_back(player); }
```

`IsRandomBot()` checks the `account_type = 1` list specifically. A masterless AddClass bot
(`account_type = 2`) fails that check and would fall into the `else` branch - `players`, the same
collection real human logins use elsewhere in this class for population/LFG-queue observation.
Fixed with one additional branch using the already-existing `IsAddclassBot()` check:

```cpp
else if (IsAddclassBot(player)) { player->SetPvP(sWorld->IsPvPRealm()); }
else { players.push_back(player); }
```

This is an upstream `mod-playerbots` file (`RandomPlayerbotMgr.cpp`), not part of Dungeon Lead's
own code, but the fix is a direct prerequisite for `DungeonTestBotPool` to be safe to use at all -
without it, every acquired test bot would silently corrupt whatever `players` feeds into.

## Design

- `TestBotRole` (`Tank`, `Healer` - Phase 1 scope only).
- `TestBotLeaseState`: `LoggingIn -> Preparing -> Ready` or `Failed`. In-memory, session-scoped
  (resets on restart, same as every other Dungeon Lead session state) - no lease persistence layer
  in Phase 1.
- `AcquireTestBot(role, targetLevel, outMessage)`: finds one offline AddClass character of the
  matching class (via a `PlayerbotsDatabase` query for eligible accounts, then a `CharacterDatabase`
  query for an offline character among them), calls `AddPlayerBot(guid, 0)`, records a `LoggingIn`
  lease, returns immediately (login is async).
- `TestBotPoolTick()` (called from `PlayerbotsWorldScript::OnUpdate`, ~3s throttle): polls each
  `LoggingIn` lease for the bot appearing in world; once found, runs profile preparation
  synchronously (cheap - in-memory/DB Player mutation, no further async step) and transitions to
  `Ready` or `Failed`. A login that never lands within 30s also becomes `Failed`.
- Profile preparation reuses `PlayerbotFactory::Randomize()` (the same proven level/skills/spells/
  quests/equipment pipeline every one of the ~1500 existing bots was built with) rather than
  reimplementing its dozens of sub-steps, then overrides just the one piece that needs to be
  deterministic instead of random: `PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true)` for
  the class/role, followed by one more `InitEquipment()` pass so gear matches the *final* spec
  rather than whatever `Randomize()` picked first.
- **Never trusts `specNo` blindly**: after preparation, `VerifyRole()` re-checks with the same
  `PlayerbotAI::IsTank()`/`IsHeal()` the rest of Dungeon Lead already uses. A wrong class/specNo
  mapping surfaces as `Failed`, not a silently-wrong `Ready` bot. (Both guesses - Warrior specNo=2
  for Tank, Priest specNo=1 for Healer, both derived from `LfgJoinAction::GetRoles()` in
  `LfgActions.cpp` - verified correct on the very first live test.)
- `ReleaseTestBot(name)`: stops any dungeon-lead session on the bot first (never leaves one
  dangling), then `LogoutPlayerBot()` - the same normal bot-logout path, not a forced disconnect.
- `Dps` role (Phase 2): `AcquireDpsTestBot(classId, ...)` - any of the ten classes, no forced
  talent spec (a pure-DPS class doesn't need one optimized spec the way Tank/Healer do). If the
  class has a known CC spell (`CcSpellFor()` - currently only Mage/Polymorph, spell 118, a
  baseline class spell not gated behind a specific talent spec), `VerifyReady()` additionally
  checks `bot->HasSpell()` for it before reporting `Ready`.

## A verification bug caught by the safety net itself

The first full 5-bot-party run (Phase 2, same day as Phase 1) reused the same Priest character
Phase 1 had already proven `Ready` once. This time it came back `Failed` - same character, same
`AcquireTestBot`/`PrepareProfile` call, different outcome. Investigated rather than retried:

`PlayerbotAI::IsTank`/`IsHeal` take a `bySpec` parameter, **default `false`**:

```cpp
bool PlayerbotAI::IsHeal(Player* player, bool bySpec)
{
    PlayerbotAI* botAi = GET_PLAYERBOT_AI(player);
    if (!bySpec && botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_HEAL);
    // ... bySpec=true path checks AiFactory::GetPlayerSpecTab() instead ...
}
```

`VerifyReady()` had called the two-argument form, so it silently took the `bySpec=false` branch:
checking the bot's current AI **strategy** assignment, not the talent spec `PrepareProfile()` had
just set. That strategy gets (re)computed by the AI engine itself, on its own schedule relative to
`ResetStrategies()` - not guaranteed to already reflect a talent change from the same tick. Fixed
by passing `bySpec=true` explicitly, which reads `AiFactory::GetPlayerSpecTab()` →
`bot->GetTalentMap()` directly - the literal state `InitTalentsBySpecNo()` just wrote, no
intermediate strategy-engine step to lag behind.

Worth noting what this bug was **not**: the `specNo` guesses themselves (`WARRIOR_TAB_PROTECTION=2`,
`PRIEST_TAB_HOLY=1`, confirmed against the real enum values in `PlayerbotAI.h`) were correct the
entire time. A less careful investigation could easily have "fixed" this by second-guessing the
spec numbers instead of the verification call - they weren't the problem.

## Operator interface

`.playerbots testbotpool acquire tank|healer|dps <class> [level]` / `status` / `release <name>` -
GM console commands, `Console::Yes` (SOAP-reachable), matching `canarytest`/`lfgstate`'s pattern
from ADR-002.

## Phase 1 acceptance test (from the reviewed plan, verified live 2026-09-12)

1. Clean worldserver restart, zero real clients connected. ✅
2. Acquire one AddClass Warrior. ✅
3. Acquire one AddClass Priest. ✅
4. Both log in through the standard async masterless path. ✅
5. Both normalized to target level (18). ✅
6. Deterministic Tank/Healer profiles applied. ✅
7. Roles verified (`IsTank`/`IsHeal`) - both `true` on the first attempt (before the `bySpec` bug
   above was found - see that section for the retest). ✅
8. Released cleanly (`LogoutPlayerBot`, confirmed offline in DB). ✅
9. Re-acquired the same identity, repeated the full cycle successfully. ✅

Not yet separately proven in isolation (running, not blocking Phase 1-2's completion): 30-minute
idle stability without RandomBot-lifecycle interference, and direct confirmation that the
`OnPlayerLogin` fix keeps these bots out of whatever `players` feeds into (fixed by inspection and
by the exact mechanism described above, not yet independently re-verified via a second live probe).

## Phase 2 acceptance test: full 5-bot party (verified live 2026-09-12, after the `bySpec` fix)

Clean restart → acquire Tank (Warrior) → acquire Healer (Priest) → acquire Dps ×3 (Mage, Rogue,
Hunter) → all five logged in, prepared, and reported `Ready` (Mage additionally verified to know
Polymorph) → confirmed in the character DB (correct level 18, correct class per slot). Zero
`Failed` results across all five in this run.

## Explicitly deferred (later phases, not implemented)

- CC verification for classes beyond Mage (Warlock/Druid/Rogue/Hunter - each is talent- or
  ability-gated differently and needs its own investigation, not a guessed spell id).
- Fresh dungeon instance provisioning per run.
- `DungeonRunResult`/campaign aggregation contract.
- `DungeonTestOrchestrator` and the BotPool/ProfileManager/PartyBuilder/InstanceProvisioner/
  ScenarioRunner/ResultCollector/CampaignManager split.
- 10-parallel-instance scaling (50 leased bots at once).
- Lease ownership/concurrency hardening beyond "one in-memory list, single-threaded access on the
  world thread" (fine for Phase 1's manual, one-at-a-time usage; not yet a real concurrency-safe
  reservation system).
