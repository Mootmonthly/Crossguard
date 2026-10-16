// Gathering nodes: herbs and ore placed in raid maps (docs/design-decisions.md, Gathering). A
// player channels at a node; when the channel completes the node's loot table is rolled into their
// inventory, they earn GatherNode XP, and the node is depleted. Engine-free; the game actor
// (ACrossguardGatherNode) measures movement, damage and attacks and asks these rules.
//
// Ruling: one gathered node gives FXpConfig::GatherNode (10) XP. Every number below is a
// PLACEHOLDER until ruled.

#pragma once

#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	struct FGatherConfig
	{
		// Placeholder: seconds the gatherer channels at the node.
		double GatherSeconds = 2.0;
		// Placeholder: seconds after depletion before the node can be gathered again. 0: once per
		// raid (never respawns).
		double RespawnSeconds = 0.0;
		// Placeholder: the channel breaks once the gatherer has moved this far (cm) from where it
		// started. A little slack so standing still on uneven ground does not break it.
		double MoveTolerance = 30.0;
	};

	enum class EGatherStartError
	{
		None,
		// Already gathered (and not respawned yet).
		Depleted,
		// Someone else is channeling at it.
		Busy,
		GathererDead,
	};

	// Whether a gatherer may start channeling at a node. bGatheringByOther: another character's
	// channel at this node is in progress.
	CROSSGUARDRULES_API EGatherStartError CanStartGather(bool bDepleted, bool bGatheringByOther, bool bGathererDead);

	enum class EGatherInterrupt
	{
		None,
		Died,
		// Moved more than MoveTolerance from where the channel started.
		Moved,
		// Took damage (any amount) since the channel started.
		Damaged,
		// Attacked or used an ability (weapon attack, skill, guard, throw).
		Attacked,
		// Left the node's interact range (pushed, pulled).
		OutOfRange,
	};

	// Whether a channel in progress breaks, checked while it runs. MovedDistance: cm from where it
	// started; HealthLost: Health lost since the last check (heals do not count against it). The
	// first reason that applies, in the enum's order.
	CROSSGUARDRULES_API EGatherInterrupt CheckGatherInterrupt(const FGatherConfig& Config, bool bDead, double MovedDistance,
		double HealthLost, bool bAttacked, bool bInRange);

	// Whether a depleted node is back: never with RespawnSeconds 0 (once per raid), otherwise once
	// SecondsSinceDepleted reaches it.
	CROSSGUARDRULES_API bool HasGatherNodeRespawned(const FGatherConfig& Config, double SecondsSinceDepleted);
}
