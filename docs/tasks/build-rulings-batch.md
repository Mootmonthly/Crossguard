# Task: build, test and fix the rulings batch (local PC session)

Run this in a Claude Code session on the owner's Windows PC: environment **Local**, the
Crossguard folder. Read `CLAUDE.md` and `docs/pc-session.md` first. Working rules:
- commit and push to `foundation/core-rules` after each green step;
- flag every assumption;
- keep reports short and plain.

New since the last green run (131 headless, 7 net, 7 visual tests). None of it has been compiled
yet. Read `docs/first-compile-triage.md` "Overnight code (unbuilt)", rows B*, R*, RC*, T*, plus the
UI label row.

| Area | Docs |
| --- | --- |
| Reconnect mid-raid: new `ACrossguardPlayerController` (`PawnLeavingGame`), game mode `HoldForReconnect` / `RestoreHeldPlayer` / `ResumeHeldCharacter`, dev player ids (`pie-<n>`, `-CrossguardPlayerId=`, `-AllowDevPlayerIds`) | `design-decisions.md` "Reconnecting", `persistence.md` |
| A bundle's requirement switches off its included skills (`ResolveLoadoutSources`, `IsSkillSwitchedOn`) | `skill-coverage.md` |
| Throws carry next-attack riders by weapon type; the riposte is not used | `design-decisions.md` (Throwables) |
| Party invites last 180 s; inbox commands `PartyInvites`, `PartyDecline`, `PartyDeclineAll` | `parties.md` |
| Respec paid with 1000 banked gold or 1 token | `design-decisions.md` |
| Tiger attacks 1.3 times a second; Greater Demon at 3× scale with 3× reach (6 m) | `design-decisions.md` |
| Gemstones stack to 3 per kind and rarity (`Items.json`) | `item-data-schema.md` |
| Inventory cells: short labels, tooltips, bigger squares | `ui.md` |

## Steps

1. Run `git pull`, then `Tools\build_editor.bat`. Fix compile errors with the triage rows; keep
   fixes minimal. Commit.

2. Run `Tools\editor_setup.bat`, because DT_Items changed: gem stacks.
   - **RC10:** if `BP_CrossguardGameMode` sets its own Player Controller Class, set it to
     `CrossguardPlayerController`. Its parent class is fine.
   - Also extend `Tools/editor_setup.py` so it sets and verifies the Player Controller Class, the
     same way it does the HUD class. That makes the setup repeatable.

3. Run `Tools\run_editor_tests.bat -SkipBuild`, then the networked and visual runs.
   - Triage as usual. Pay particular attention to:
     - `Reconnect.*`
     - the bundle tests (`MithrilKnightNeedsFullPlate`, `SpearOfLiscorNeedsSpear`)
     - the `Thrown*` tests
     - `Net.PartyInviteInboxDecline`
     - `Hub.RespecPaidWithGold`
     - `Hub.GemstonesStackToThree`
     - the summon tests (demon scale, reach and floor lift)
   - In the visual run, check that the Backpack and Quiver grids still fit side by side with the
     bigger squares. Check the inventory labels too.
   - If `Reconnect.*` fails because the pawn is gone before `HoldForReconnect` runs (RC2), find
     the real 5.8 order in `PlayerController.cpp` / `Controller.cpp` and fix the hook.

4. Save smoke, now possible with dev ids:
   1. In PIE, stash an item.
   2. Stop, then play again.
   3. Check that `Saved/Crossguard/Characters/pie-0.json` exists, the item is still there, and the
      log says "Loaded the save".
   4. Script it if you can (two PIE sessions in one test); otherwise do it by hand.

5. Known issue to fix if it's simple:
   - If a login is refused after the engine has set the unique id (a `JoinTeam` failure),
     `Logout` → `PlayerLeaving` can drop that online player's save entry.
   - Make `PlayerLeaving` ignore players that never finished logging in. Add a test.

6. Report:
   - build fixes, with their triage rows;
   - pass, fail and skip counts for each run;
   - anything weakened, and why;
   - game bugs found;
   - what still needs the owner.
