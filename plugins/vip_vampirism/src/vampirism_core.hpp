#pragma once
// vip_vampirism — the engine-free half of the plugin: the slot range, the self-damage test and the
// healing arithmetic of the original VIP_Vampirism (vip_vampirism.cpp):
//
//   int iHealth = pPlayerPawn->m_iHealth() + round(pEvent->GetInt("dmg_health") * iVamp / 100);
//   if (iHealth > pPlayerPawn->m_iMaxHealth()) iHealth = pPlayerPawn->m_iMaxHealth();
//   pPlayerPawn->m_iHealth() = iHealth;
//
// Shared verbatim by src/plugin.cpp and tests/vip_vampirism_tests.cpp, so the arithmetic the heal
// depends on is tested without the engine and without a single s2sdk, plugify or vip symbol.

#include <limits>

namespace vip_vampirism {

// CS2 has exactly 64 player slots (PITFALLS C2); the original's state table was g_bvampirism[64].
constexpr int kMaxSlots = 64;

// The original never checked the slot it read from the event: "attacker" is -1 when the event has
// no player attacker (world damage) and a malicious/garbled value is just as possible. Every
// s2sdk client call must be kept away from such a slot (PITFALLS C2).
constexpr bool IsSlotInRange(int slot) {
	return slot >= 0 && slot < kMaxSlots;
}

// The original's "if (iAttacker == pEvent->GetInt("userid")) return;". It compared the attacker
// SLOT against the raw user id, which in CS2 is not a slot; the port compares the two slots the
// event resolves to (see the file header of plugin.cpp), which is what the line was meant to do.
constexpr bool IsSelfDamage(int attackerSlot, int victimSlot) {
	return attackerSlot == victimSlot;
}

// --- the round policy -------------------------------------------------------------------------
//
// The module's optional per-group key in the existing vip/groups.ini (read through the core's
// VIP_GetClientFeatureInt, exactly like the "Vampirism" value itself). The key follows the
// vip_bf/vip_items "round_min" convention, with a fail-closed floor for this module:
//   * the key is optional, and the core answers -1 when it is missing (or the slot has no VIP
//     record/group) - that fallback is 1, because the reported bug is the effects running on the
//     first (pistol) round and a missing setting must not allow them;
//   * 0 explicitly allows every round, pistol rounds included;
//   * N >= 1 allows only a non-pistol round with at least N completed rounds. totalRounds is
//     m_totalRoundsPlayed, the same threshold vip_bf/vip_items compare - a count of completed
//     rounds, not the displayed round number. Requiring >= 1 also makes the first map fail
//     closed: before the first round_prestart the pistol flag is still false while no round has
//     completed, so "not pistol" alone would already allow the first round. The second-half
//     pistol round is refused through VIP_PistolRound (the core latches the team switch there),
//     so the pistol flag is authoritative and the total alone is not enough.
//   * a negative totalRounds is the SDK's "round state unavailable" answer (-1) and refuses the
//     effect even for an explicit 0: with no trustworthy round answer nothing may be enabled.
inline constexpr char kRoundMinFeature[] = "Vampirism_round_min";
inline constexpr int kDefaultRoundMin = 1;

constexpr bool RoundAllowed(int roundMin, bool pistolRound, int totalRounds) {
	if (totalRounds < 0) {
		return false;
	}
	const int minRounds = roundMin < 0 ? kDefaultRoundMin : roundMin;
	return minRounds == 0 || (!pistolRound && totalRounds >= minRounds);
}

// The health to write, and whether it is worth writing at all. "changed == false" means the
// caller must not call SetClientHealth: there is nothing to heal.
struct HealStep {
	int health = 0;
	bool changed = false;
};

// One "player_hurt" heal: the damage times the percentage, divided by 100, on top of the current
// health, clamped to the attacker's max health.
//
// The original's round() wrapped an already-integer expression (dmg_health and iVamp are both
// int, so the division truncated towards zero first): the port keeps the integer division, so
// 10 damage at 35 % heals 3, not 4.
//
// Four guards are the port's, and none of them changes what a real heal does:
//   * percentage <= 0 - nothing to heal by. The plugin already refuses a non-positive feature value
//     (the original's `iVamp > 0` gate); the core answers -1 when there is no VIP record or no such
//     feature (an expired VIP, VIP_RemoveClientVIP, a group change, a core reload) and 0 for a
//     group value of "0", and neither may ever be multiplied into the health;
//   * damage <= 0 - a "player_hurt" carrying no damage has nothing to heal by, and a negative
//     value must not drain health;
//   * a step that does not raise the health is skipped. The original wrote the clamped value
//     unconditionally, which *lowered* the health of a pawn that was already above its max health
//     (a health bonus, a revive): a heal never hurts;
//   * maxHealth <= 0 means the engine had no pawn to answer for (GetClientMaxHealth returns 0
//     then); clamping to it would turn the heal into a kill, so only a positive max health clamps
//     and the result is capped at INT_MAX instead.
inline HealStep Heal(int currentHealth, int maxHealth, int damage, int percentage) {
	if (percentage <= 0 || damage <= 0) {
		return {currentHealth, false};
	}
	// 64-bit: a nonsensical dmg_health must not overflow the multiply that precedes the division.
	const long long heal = static_cast<long long>(damage) * percentage / 100;
	long long health = static_cast<long long>(currentHealth) + heal;
	const long long limit = maxHealth > 0 ? static_cast<long long>(maxHealth)
										  : static_cast<long long>(std::numeric_limits<int>::max());
	if (health > limit) {
		health = limit;
	}
	if (health <= currentHealth) {
		return {currentHealth, false};
	}
	return {static_cast<int>(health), true};
}

} // namespace vip_vampirism
