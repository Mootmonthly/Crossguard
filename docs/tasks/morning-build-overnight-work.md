# Task: build, test and fix the overnight work (local PC session)

Run in a Claude Code session on the owner's Windows PC (environment **Local**, Crossguard folder).
Read `CLAUDE.md` and `docs/pc-session.md` first. Follow the working rules: commit and push to
`foundation/core-rules` after each green step; flag every assumption; short plain reports.

Overnight work was written in a cloud session without an engine. None of the game code below has
been compiled. The rules library builds and its 1817 checks pass.

| Area | Docs | Main files |
| --- | --- | --- |
| Stealth leak fix A: close enemy actor channels when stealth starts | `docs/anti-cheat.md` | `CrossguardCharacter.cpp` (`CloseStealthHiddenChannels`) |
| Crowd avoidance (RVO) for monsters and minions | `design-decisions.md` | `CrossguardAICharacter.cpp` (`UpdateAvoidanceGroups`) |
| Passive requirement gating | `docs/skill-coverage.md` | `CrossguardDataPassiveAbility.cpp` |
| Placeholder UI in C++: HUD, inventory/loot, skill wheel, trade, guild | `docs/ui.md` | `Source/Crossguard/*/UI/*` |
| Saving progression (local JSON backend) | `docs/persistence.md` | `Game/CrossguardSave*`, `CrossguardLocalSaveBackend*`, rules `Save`/`Json` |
| Parties and hub-to-raid launch; seamless travel on | `docs/parties.md` | `Game/CrossguardParty*`, rules `Party`, game mode |
| Extracted players leave the raid and can't loot | `design-decisions.md` (Extraction) | game mode / extraction code |

## Steps

1. Run `git pull`, then `Tools\build_editor.bat`. Fix compile errors using
   `docs/first-compile-triage.md`. Section "Overnight code (unbuilt)" lists every guessed engine
   call with a fix to try. Keep fixes minimal and don't change game rules. Commit when it builds.
2. Run `Tools\editor_setup.bat`. Then run `Tools\run_editor_tests.bat -SkipBuild`, the full
   headless run of about 125 tests.
   - Triage failures the usual way: framework or engine API, then test bug, then game bug.
   - Fix a game bug only when the docs make the intended behaviour clear; otherwise list it.
   - Specific checks:
     - `Crossguard.Net.StealthHidesFromEnemyClient`: the logged `STEALTH LEAK` should be under 0.5 s.
       If the channel close doesn't work in 5.8, report what happens; don't switch to the global
       `RelevantTimeout` fix without the owner.
     - `Crossguard.Scenario.Monsters.MinionAvoidsSummoner` passes, and the other multi-monster tests
       still pass with avoidance on. If both hold, try removing the 3 m offset in `MonsterTeam`.
     - The `Crossguard.Scenario.Save.*` tests pass. The full run must not write save files or warnings
       into other tests; saving is off under automation unless a test enables it.
     - The `Crossguard.Net.Party*` and `Crossguard.Scenario.Hub.PartyRaidLaunchPlan` tests pass.
       PIE clients need distinct ids.
     - `Crossguard.Scenario.UI.WidgetsBind` passes headless; the HUD-made checks skip without a viewport.
     - Seamless travel was turned on (`bUseSeamlessTravel`). Check nothing else regressed.
3. Visual run: `Tools\run_editor_tests.bat -SkipBuild -Visual`. Review the screenshots. If you can
   render the UI, add a `Crossguard.Visual.UI` scene that screenshots the HUD, the inventory with a
   body open, and the trade and guild windows. Report layout problems such as overlap, unreadable
   text or off-screen windows. Don't judge art.
4. Manual smoke in PIE, only if you can drive it, for example with the `-Visual` path or a short
   scripted PIE:
   - Stash an item, stop, play again: the item is still there, `Saved/Crossguard/Characters/<id>.json`
     exists, and the log says "Loaded the save".
   - Otherwise list these for the owner.
5. Report:
   - build fixes made, with triage rows;
   - pass / fail / skip counts for the headless, networked and visual runs;
   - the measured stealth leak;
   - anything weakened or skipped and why;
   - game bugs found;
   - what still needs the owner.
