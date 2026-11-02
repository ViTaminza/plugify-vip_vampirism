// vip_vampirism unit tests — the engine-independent part of the plugin (src/vampirism_core.hpp):
// the slot range, the self-damage test and the healing arithmetic of the original VIP_Vampirism
// (health + dmg_health * percentage / 100, integer division, clamped to the attacker's max health)
// with every guard the port adds around it.
//
// No pisexcompat, no s2sdk binding and no plugify symbol is linked here: the test only includes
// the header, so it can run anywhere (./test.sh runs it inside the Steam Runtime image,
// cwd = build/tests). The plugin's own s2sdk calls (the map gate, IsAlive, the event-field reads)
// are in src/plugin.cpp, which needs the engine; the arithmetic they feed is what this test pins.

#include "vampirism_core.hpp"

#include <cstdio>
#include <limits>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void CheckEq(int actual, int expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected, actual);
	}
}

void CheckEq(bool actual, bool expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected ? 1 : 0, actual ? 1 : 0);
	}
}

// --- slots and self-damage (PITFALLS C2) ---------------------------------------------------------

void TestSlots() {
	using vip_vampirism::IsSlotInRange;
	using vip_vampirism::kMaxSlots;

	CheckEq(kMaxSlots, 64, "CS2 has 64 player slots");
	CheckEq(IsSlotInRange(0), true, "slot 0 is valid");
	CheckEq(IsSlotInRange(63), true, "slot 63 is valid");
	CheckEq(IsSlotInRange(64), false, "slot 64 is out of range");
	CheckEq(IsSlotInRange(-1), false, "slot -1 is out of range");
	CheckEq(IsSlotInRange(std::numeric_limits<int>::min()), false, "INT_MIN is out of range");
	CheckEq(IsSlotInRange(std::numeric_limits<int>::max()), false, "INT_MAX is out of range");
}

void TestSelfDamage() {
	using vip_vampirism::IsSelfDamage;
	using vip_vampirism::IsSlotInRange;

	CheckEq(IsSelfDamage(3, 3), true, "the attacker damaging himself is self-damage");
	CheckEq(IsSelfDamage(3, 4), false, "another victim is not self-damage");
	CheckEq(IsSelfDamage(0, 0), true, "slot 0 hurting itself is self-damage");
	// The original compared the attacker slot with the raw user id: for a victim the engine
	// reports an id (e.g. 5), never a slot, so the check almost never matched. The resolved slots
	// compare equal, which is what the line meant (both are validated before this is asked).
	CheckEq(IsSelfDamage(5, 5), true, "two equal resolved slots are self-damage");
	// Nothing sensible compares equal when the slot was rejected first: the caller never reaches
	// IsSelfDamage with an out-of-range attacker.
	CheckEq(IsSlotInRange(-1) && IsSelfDamage(-1, -1), false, "slot -1 is rejected before the comparison");
}

// --- the round policy ---------------------------------------------------------------------------

// The exact helper src/plugin.cpp's RoundEffectsAllowed() calls before the health write, with the
// production scenarios of the reported bug (the heal used to run on pistol rounds).
void TestRoundPolicy() {
	using vip_vampirism::kDefaultRoundMin;
	using vip_vampirism::kRoundMinFeature;
	using vip_vampirism::RoundAllowed;

	// The exact per-group key the runtime reads from vip/groups.ini.
	CheckEq(std::string(kRoundMinFeature) == "Vampirism_round_min", true, "the groups.ini key is exact");
	CheckEq(kDefaultRoundMin, 1, "the missing-key fallback is 1 (fail closed)");

	// First map before round_prestart: the core's pistol flag has not been latched yet (false) but
	// no round has completed. The missing setting (-> 1) must not heal here.
	CheckEq(RoundAllowed(-1, false, 0), false, "missing key: first map before round_prestart is refused");
	CheckEq(RoundAllowed(kDefaultRoundMin, false, 0), false, "fallback 1: total 0 is refused even when not pistol");

	// The first pistol round.
	CheckEq(RoundAllowed(-1, true, 0), false, "missing key: first pistol round is refused");

	// Second regular round (exactly one round completed).
	CheckEq(RoundAllowed(-1, false, 1), true, "missing key: second regular round (total 1) is allowed");

	// Second-half pistol: 12 completed rounds, but VIP_PistolRound is set for the switch.
	CheckEq(RoundAllowed(-1, true, 12), false, "missing key: second-half pistol is refused");

	// Ordinary later rounds.
	CheckEq(RoundAllowed(-1, false, 5), true, "missing key: an ordinary later round is allowed");
	CheckEq(RoundAllowed(1, false, 23), true, "min 1: a later non-pistol round is allowed");

	// 0 explicitly allows every round, the pistol ones and the pre-first-round state included.
	CheckEq(RoundAllowed(0, false, 0), true, "0: the first map is allowed");
	CheckEq(RoundAllowed(0, true, 0), true, "0: the first pistol round is allowed");
	CheckEq(RoundAllowed(0, true, 12), true, "0: the second-half pistol is allowed");

	// A negative total is the SDK's "round state unavailable" answer: no effect may run, not even
	// for an explicit 0 (the check runs before the roundMin == 0 branch).
	CheckEq(RoundAllowed(0, false, -1), false, "unavailable round state (total -1) refuses an explicit 0");
	CheckEq(RoundAllowed(-1, false, -1), false, "unavailable round state refuses the missing-key fallback");

	// A higher minimum counts COMPLETED rounds and never allows a pistol round.
	CheckEq(RoundAllowed(2, false, 1), false, "min 2 waits for the second completed round");
	CheckEq(RoundAllowed(2, false, 2), true, "min 2 allows once two rounds completed");
	CheckEq(RoundAllowed(2, true, 5), false, "min 2 refuses a pistol round");
	CheckEq(RoundAllowed(3, false, 2), false, "min 3 still waits at two completed rounds");
	CheckEq(RoundAllowed(3, false, 3), true, "min 3 allows at three completed rounds");
	CheckEq(RoundAllowed(5, false, 4), false, "min 5 waits at four completed rounds");
	CheckEq(RoundAllowed(5, false, 9), true, "min 5 allows later");

	// Any negative value is treated like the core's missing -1 (the fallback is 1).
	CheckEq(RoundAllowed(-7, false, 0), false, "a negative setting falls back to 1: total 0 refused");
	CheckEq(RoundAllowed(-7, false, 1), true, "a negative setting falls back to 1: total 1 allowed");

	// A huge value merely keeps waiting; no wrap turns it into an early allow.
	CheckEq(RoundAllowed(std::numeric_limits<int>::max(), false, 100), false, "INT_MAX does not wrap");
}

// --- the original's heal -------------------------------------------------------------------------

void TestHealShares() {
	using vip_vampirism::Heal;

	// The shipped groups.ini sets "Vampirism" to a small percentage per group; 25 % of 28 damage
	// is 7 HP.
	const auto quarter = Heal(50, 100, 28, 25);
	CheckEq(quarter.health, 57, "25 % of 28 damage heals 7");
	CheckEq(quarter.changed, true, "a real heal is written");

	// Integer division, exactly like the original: dmg_health and iVamp are int, so
	// `dmg_health * iVamp / 100` truncates before the original's round() sees it.
	const auto truncated = Heal(50, 100, 39, 25); // 9.75 -> 9
	CheckEq(truncated.health, 59, "a fractional share truncates");
	const auto rounded = Heal(50, 100, 10, 35); // 3.5 -> 3, not 4
	CheckEq(rounded.health, 53, "the original's round() did not round: integer division stays");
	const auto half = Heal(50, 100, 10, 5); // 0.5 -> 0
	CheckEq(half.health, 50, "a share below one HP heals nothing");
	CheckEq(half.changed, false, "a share below one HP is not written");

	// A 100 % feature heals the full damage.
	const auto full = Heal(40, 100, 60, 100);
	CheckEq(full.health, 100, "100 % of 60 damage reaches exactly max health");
	CheckEq(full.changed, true, "the full share is written");

	// No VIP record / feature missing: the core answers -1, and a group value of "0" answers 0.
	CheckEq(Heal(50, 100, 40, 0).changed, false, "a zero percentage heals nothing");
	CheckEq(Heal(50, 100, 40, 0).health, 50, "a zero percentage keeps the health");
	CheckEq(Heal(50, 100, 40, -1).changed, false, "the core's -1 heals nothing");
	CheckEq(Heal(50, 100, 40, -1).health, 50, "the core's -1 does not drain health");
}

void TestClamp() {
	using vip_vampirism::Heal;

	// The original's clamp: iHealth > m_iMaxHealth -> m_iMaxHealth.
	const auto clamped = Heal(99, 100, 100, 50);
	CheckEq(clamped.health, 100, "the over-heal is clamped to max health");
	CheckEq(clamped.changed, true, "the clamped step still heals");

	const auto exact = Heal(90, 100, 10, 100);
	CheckEq(exact.health, 100, "a heal that lands exactly on max health is kept");
	CheckEq(exact.changed, true, "landing on max health is a change");

	// Already at max: nothing to heal, nothing to write.
	const auto atMax = Heal(100, 100, 50, 50);
	CheckEq(atMax.health, 100, "at max health nothing is added");
	CheckEq(atMax.changed, false, "at max health nothing is written");

	// Above max (a health bonus, a revive): the original's unconditional clamp *lowered* the
	// health there; a heal never hurts, so the step is skipped instead.
	const auto aboveMax = Heal(120, 100, 20, 25);
	CheckEq(aboveMax.health, 120, "a pawn above max health is not lowered by a heal");
	CheckEq(aboveMax.changed, false, "a pawn above max health is not written");

	// A max health the engine could not answer (no pawn) is 0: clamping to it would be a kill.
	const auto noMax = Heal(50, 0, 20, 25);
	CheckEq(noMax.health, 55, "a missing max health does not clamp the heal to 0");
	CheckEq(noMax.changed, true, "a missing max health still heals");
	const auto negativeMax = Heal(50, -10, 20, 25);
	CheckEq(negativeMax.health, 55, "a negative max health does not clamp the heal");
	CheckEq(negativeMax.changed, true, "a negative max health still heals");
}

void TestGuardValues() {
	using vip_vampirism::Heal;

	// A "player_hurt" with no damage has nothing to heal by.
	CheckEq(Heal(50, 100, 0, 25).changed, false, "zero damage heals nothing");
	CheckEq(Heal(50, 100, 0, 25).health, 50, "zero damage keeps the health");
	// A negative damage must not drain health (the original would have subtracted it).
	CheckEq(Heal(50, 100, -40, 25).changed, false, "negative damage heals nothing");
	CheckEq(Heal(50, 100, -40, 25).health, 50, "negative damage does not drain health");
	// The guards fire before the multiply, so these cannot wrap.
	CheckEq(Heal(50, 100, std::numeric_limits<int>::min(), std::numeric_limits<int>::min()).changed, false,
			"INT_MIN damage and percentage heal nothing");
}

void TestOverflow() {
	using vip_vampirism::Heal;

	// A nonsensical dmg_health must not overflow the multiply: with a max health the clamp answers
	// it, without one the result is capped instead of wrapping negative (a kill).
	const int huge = std::numeric_limits<int>::max();
	const auto clamped = Heal(50, 100, huge, 100);
	CheckEq(clamped.health, 100, "a huge damage is clamped to max health");
	CheckEq(clamped.changed, true, "a huge damage is a change");

	const auto capped = Heal(50, 0, huge, 100);
	CheckEq(capped.health, std::numeric_limits<int>::max(), "a huge damage without a max health caps at INT_MAX");
	CheckEq(capped.changed, true, "the capped value is a change");
}

// --- invariants over the whole input space -----------------------------------------------------

void TestInvariants() {
	using vip_vampirism::Heal;

	// The heal may only ever raise the health, and never above a positive max health; an unchanged
	// step always returns the health it was given. Sweep the realistic ranges plus the edges.
	int changed = 0;
	for (int current = 0; current <= 200; ++current) {
		for (int maxHealth : {-1, 0, 1, 50, 100, 150, 200}) {
			for (int damage : {-5, 0, 1, 2, 10, 100, 500}) {
				for (int percentage : {-1, 0, 1, 5, 25, 50, 100, 1000}) {
					const auto step = Heal(current, maxHealth, damage, percentage);
					if (step.changed) {
						++changed;
					} else if (step.health != current) {
						CheckEq(step.health, current, "an unchanged step returns the input health");
					}
					if (step.changed && step.health <= current) {
						CheckEq(step.health, current + 1, "a changed step must raise the health");
					}
					if (step.changed && maxHealth > 0 && step.health > maxHealth) {
						CheckEq(step.health, maxHealth, "a changed step must stay within max health");
					}
				}
			}
		}
	}
	CheckEq(changed > 0, true, "the sweep exercises the healing path");
}

} // namespace

int main() {
	std::printf("vip_vampirism tests\n");
	TestSlots();
	TestSelfDamage();
	TestRoundPolicy();
	TestHealShares();
	TestClamp();
	TestGuardValues();
	TestOverflow();
	TestInvariants();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
