// vip_vampirism — Plugify port of the Pisex VIP module "VIP_Vampirism" / "[VIP] Vampirism"
// (the original's vip_vampirism.cpp, 154 lines).
//
// Behaviour kept identical: on "player_hurt", a VIP attacker whose "Vampirism" feature value is
// positive heals for value% of the damage dealt (integer arithmetic: the damage times the
// percentage, divided by 100), clamped to the attacker's max health. Self-damage is skipped.
//
// Pure s2sdk (docs/PLUGIN_STANDARD.md §7): there is no pisexcompat and no s2guard. Every safety
// rule the plugin needs lives in this file and in the engine-free vampirism_core.hpp:
//   * PITFALLS B1/B2 - the map gate g_mapReady. The OnMapStart/OnMapEnd listeners are registered
//     in OnPluginStart, readiness is latched by OnMapStart and by the first OnPluginUpdate of a
//     plugin loaded into a running map (when no OnMapEnd was seen since OnPluginStart), and
//     dropped by OnMapEnd. No client, entity or schema call is made while the gate is closed:
//     s2sdk dereferences a torn-down client list in the OnMapEnd..OnMapStart window and a null
//     serializer map before the first map.
//   * PITFALLS C12/C13 - "player_hurt" is hooked with s2sdk::HookEvent(HookMode::Pre) right in
//     OnPluginStart and unhooked in OnPluginEnd; the handler answers Continue on every path and
//     reads the fields with GetEventPlayerSlot/GetEventInt when the event arrives (nothing about
//     an entity is cached across a changelevel).
//   * PITFALLS C2 - IsAlive() layers the gate, the slot range, IsClientConnected, IsClientInGame,
//     !IsClientSourceTV, a non-null controller pointer and IsClientAlive (which needs the pawn, so
//     it is asked last). It is what the original's CCSPlayerController::FromSlot + m_hPlayerPawn +
//     m_lifeState chain was, and it covers pisexcompat's GetControllerEntity(i) != 0 and
//     g_pPlayers->IsAlive(i).
//   * PITFALLS A1/A7 - OnPluginStart never returns Failed (one Failed stops every Plugify plugin)
//     and OnPluginEnd removes every hook/listener it registered, so `plg reload` is safe.
//
// Port notes:
//   * The core (IVIPApi) is the Plugify "vip" plugin; the module calls it through the consumer
//     wrapper of plugins/vip/include/vip_api.hpp.
//   * The heal is the engine's own CBaseEntity::SetHealth (s2sdk::SetClientHealth): it writes
//     m_iHealth and sends the change to the clients - the original's field write plus its
//     SetStateChanged("CBaseEntity", "m_iHealth"). s2sdk::GetClientHealth / GetClientMaxHealth
//     read the same pawn fields the original read.
//   * The original compared the attacker's slot against the raw "userid" event field. In CS2 a
//     user id is not a player slot, so that test almost never matched and self-damage healed the
//     attacker. The port compares the two resolved slots (attacker vs. the "userid" key through
//     GetEventPlayerSlot), which is what the line was meant to do - the same kind of fix the admin
//     modules document for "jointeam".
//   * The original hooked "player_hurt" from VIP_OnVIPLoaded, i.e. only once the core's database
//     was up. The port hooks it in OnPluginStart like the other vip modules: the handler asks the
//     core per event, and the core answers "not a VIP" / -1 while its records are not loaded, so
//     the heal cannot run early, while a hook registered late would miss every hurt in between.
//   * The original's g_bvampirism[64] table and its OnClientLoaded/OnClientDisconnect/OnTogglable
//     callbacks only ever wrote that table and never read it back (the toggle callback always
//     returned false, i.e. it never vetoed a menu toggle); they are dropped as dead state, which
//     leaves the observable behaviour - menu toggle and healing - untouched. Dropping the toggle
//     callback also leaves the core no pointer into this library, so OnPluginEnd has no vip
//     callback to unregister (the vip API has no feature unregister either).
//   * The arithmetic itself (the integer division, the clamp, which guards a heal) is
//     vampirism_core.hpp, shared with tests/vip_vampirism_tests.cpp. It keeps the original's
//     integer division and max-health clamp, but a step that would not raise the health is not
//     written at all: the original's unconditional clamp *lowered* a pawn that was already above
//     its max health (a health bonus, a revive), and it also wrote on a "player_hurt" that carried
//     no damage at all, where there is simply nothing to heal.
//   * Round policy: the heal used to run on every round, including pistol rounds (the reported
//     bug). The optional per-group key "Vampirism_round_min" (vip/groups.ini, read through
//     VIP_GetClientFeatureInt) is evaluated on every hurt event - right before the write - by
//     vampirism_core.hpp's RoundAllowed(): missing/negative means 1, 0 allows every round, and
//     N >= 1 needs a non-pistol round with at least N completed rounds. The current round is
//     rechecked on every event, so neither a menu toggle nor any cached value can bypass it. If
//     the core does not expose both round calls the heal stays disabled (fail closed).

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include <vip_api.hpp>

#include "vampirism_core.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

namespace vamp = vip_vampirism;

// The feature the module registers with the core and reads for every hurt.
constexpr char kFeature[] = "Vampirism";

// The object the ported module uses (the Pisex g_pVIPCore).
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

// --- PITFALLS B1/B2: the map gate --------------------------------------------------------------

bool g_mapReady = false;     // a map is up: s2sdk client/entity calls are safe
bool g_mapStartSeen = false; // OnMapStart arrived since start / the last OnMapEnd
bool g_mapEndSeen = false;   // OnMapEnd arrived since start
bool g_mapListeners = false; // OnMapStart/OnMapEnd are registered with s2sdk

// --- PITFALLS C12/C13: the game-event hook -----------------------------------------------------

bool g_playerHurtHooked = false; // "player_hurt" is hooked (and must be unhooked on unload)

void LogMessage(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(message, severity, plg::source_location::current());
	}
}

void LogInfo(std::string_view message) {
	LogMessage(message, plg::Severity::Info);
}

void LogError(std::string_view message) {
	LogMessage(message, plg::Severity::Error);
}

// The mid-map latch of the gate: a plugin loaded into a running map never saw OnMapStart, so its
// first update is the "a map is up" signal (the host pumps updates from the server frame). After
// an OnMapEnd only a new OnMapStart re-opens the gate: the host keeps pumping updates through a
// changelevel, so `!g_mapEndSeen` alone must not be enough once one was seen.
void PollMapGate() {
	if (!g_mapListeners) {
		return;
	}
	if (!g_mapReady && (g_mapStartSeen || !g_mapEndSeen)) {
		g_mapReady = true;
	}
}

// --- PITFALLS C2: the client checks ------------------------------------------------------------

// Alive means every layer s2sdk does not check itself: a live map, a slot in range, a connected
// client in game that is not the SourceTV proxy, a controller entity, and the engine's alive flag.
// s2sdk::IsClientAlive dereferences the pawn, so it is asked last. The missing-binding checks keep
// a null binding slot from being called (the s2sdk wrappers do not check them).
bool IsAlive(int iSlot) {
	if (!g_mapReady || !vamp::IsSlotInRange(iSlot)) {
		return false;
	}
	if (!__s2sdk_IsClientConnected || !__s2sdk_IsClientInGame || !__s2sdk_IsClientSourceTV ||
		!__s2sdk_PlayerSlotToEntPointer || !__s2sdk_IsClientAlive) {
		return false;
	}
	try {
		return s2sdk::IsClientConnected(iSlot) && s2sdk::IsClientInGame(iSlot) &&
			   !s2sdk::IsClientSourceTV(iSlot) &&
			   s2sdk::PlayerSlotToEntPointer(iSlot) != nullptr && s2sdk::IsClientAlive(iSlot);
	} catch (...) {
		return false;
	}
}

// The round gate of the module, evaluated on every hurt event (the heal is written from there,
// so this is the "actual effect" check, not a spawn-time one). The policy itself - a threshold of
// COMPLETED rounds, missing -> 1, 0 -> every round - is vampirism_core.hpp's RoundAllowed(),
// shared with the test. The core may be an older build without the round API: without both
// bindings no effect is enabled (fail closed). All engine-backed calls stay behind the map gate.
bool RoundEffectsAllowed(int iSlot) {
	if (!g_mapReady || !__vip_VIP_IsVIPLoaded || !__vip_VIP_PistolRound ||
		!__vip_VIP_GetTotalRounds) {
		return false;
	}
	try {
		if (!g_pVIPCore->VIP_IsVIPLoaded()) {
			return false;
		}
		return vamp::RoundAllowed(g_pVIPCore->VIP_GetClientFeatureInt(iSlot, vamp::kRoundMinFeature),
								  g_pVIPCore->VIP_PistolRound(), g_pVIPCore->VIP_GetTotalRounds());
	} catch (...) {
		return false;
	}
}

class VipVampirismPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// The "player_hurt" body (an s2sdk event callback is a plain function pointer that cannot be a
	// member function, so it forwards here).
	void OnPlayerHurt(void* event);

	// s2sdk map listeners are plain function pointers called on the game thread: they must not
	// throw, and OnMapEnd must not touch a client (the client list is torn down then).
	static void OnMapStartCallback();
	static void OnMapEndCallback();
};

VipVampirismPlugin g_plugin;

void VipVampirismPlugin::OnMapStartCallback() {
	// The map's first live frame: clients, entities and the serializer are in place.
	g_mapReady = true;
	g_mapStartSeen = true;
	g_mapEndSeen = false;
}

void VipVampirismPlugin::OnMapEndCallback() {
	// PITFALLS B2: from here until the next OnMapStart every s2sdk client query can crash (the
	// client list is being torn down), so this handler only drops state.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

// The single "player_hurt" hook. A Pre hook must answer Continue on every path: a result >=
// Handled frees the event for every other plugin on the server (the original's listener returned
// false for the same reason).
s2sdk::ResultType OnPlayerHurtEvent(const plg::string& /*name*/, void* event, bool /*dontBroadcast*/) {
	try {
		g_plugin.OnPlayerHurt(event);
	} catch (const std::exception& ex) {
		LogError(std::string("vip_vampirism: player_hurt failed: ") + ex.what());
	} catch (...) {
		LogError("vip_vampirism: player_hurt failed with an unknown exception");
	}
	return s2sdk::ResultType::Continue;
}

void VipVampirismPlugin::OnPlayerHurt(void* event) {
	// Events keep arriving during a changelevel (PITFALLS C13) and every client query before the
	// first map crashes (PITFALLS B1/B2): the gate turns both into a no-op first.
	if (event == nullptr || !g_mapReady) {
		return;
	}
	// The s2sdk wrappers call their binding slots unchecked: a missing binding would be a null
	// call from inside the game event hook.
	if (!__s2sdk_GetEventPlayerSlot || !__s2sdk_GetEventInt) {
		return;
	}

	// The attacker's slot from the event.
	const int iAttacker = s2sdk::GetEventPlayerSlot(event, "attacker");
	if (!vamp::IsSlotInRange(iAttacker)) {
		return;
	}
	// Self-damage is skipped against the victim's slot, see the file header.
	if (vamp::IsSelfDamage(iAttacker, s2sdk::GetEventPlayerSlot(event, "userid"))) {
		return;
	}
	// The original's controller -> pawn -> life-state chain (PITFALLS C2).
	if (!IsAlive(iAttacker)) {
		return;
	}

	// The attacker's "Vampirism" value, for a VIP only, and only when it is positive.
	const int iVamp = g_pVIPCore->VIP_GetClientFeatureInt(iAttacker, kFeature);
	if (!g_pVIPCore->VIP_IsClientVIP(iAttacker) || iVamp <= 0) {
		return;
	}
	// The round gate runs before the health read and the write: a heal must never be applied on
	// a pistol round or before the configured number of completed rounds, and the decision is
	// recomputed for this event (a menu toggle or a cached value cannot keep it on).
	if (!RoundEffectsAllowed(iAttacker)) {
		return;
	}

	// The engine health accessors; without them there is nothing to heal with.
	if (!__s2sdk_GetClientHealth || !__s2sdk_GetClientMaxHealth || !__s2sdk_SetClientHealth) {
		return;
	}
	// The original's new health (the damage share as integer division, clamped to max health), see
	// vampirism_core.hpp for the guards and for why a non-healing step is skipped.
	const vamp::HealStep step = vamp::Heal(s2sdk::GetClientHealth(iAttacker),
										   s2sdk::GetClientMaxHealth(iAttacker),
										   s2sdk::GetEventInt(event, "dmg_health"), iVamp);
	if (!step.changed) {
		return;
	}

	// The original's health write plus its explicit state change: the engine's SetHealth does both.
	s2sdk::SetClientHealth(iAttacker, step.health);
}

// --- lifecycle ---------------------------------------------------------------------------------

plg::PluginResult VipVampirismPlugin::OnPluginStart() {
	try {
		g_mapReady = false;
		g_mapStartSeen = false;
		g_mapEndSeen = false;

		// PITFALLS B1: registering the map listeners touches no client, so it is safe here.
		// Without them nothing could close the gate again, so a missing binding keeps it shut
		// (every client call stays refused) instead of opening it blindly.
		if (!g_mapListeners && __s2sdk_OnMapStart_Register && __s2sdk_OnMapEnd_Register) {
			s2sdk::OnMapStart_Register(&VipVampirismPlugin::OnMapStartCallback);
			s2sdk::OnMapEnd_Register(&VipVampirismPlugin::OnMapEndCallback);
			g_mapListeners = true;
		} else if (!__s2sdk_OnMapStart_Register || !__s2sdk_OnMapEnd_Register) {
			LogError("vip_vampirism: the s2sdk map listeners are unavailable - healing stays disabled");
		}

		// The feature registration of the original (it did it in AllPluginsLoaded; the core stores
		// the entry whether or not its database is up yet). The original's toggle callback is
		// dropped, see the file header. The vip module fills every binding slot or none of them:
		// __vip_VIP_IsVIPLoaded is the availability probe the consumer wrapper itself uses.
		if (__vip_VIP_IsVIPLoaded && __vip_VIP_RegisterFeature) {
			g_pVIPCore->VIP_RegisterFeature(kFeature, vip::VIP_BOOL, vip::TOGGLABLE);
		} else {
			LogError("vip_vampirism: the vip bindings are unavailable - 'Vampirism' is not registered");
		}

		// PITFALLS C12/C13: one Pre hook on "player_hurt"; s2sdk only accepts plain function
		// pointers, and the callback above answers Continue on every path.
		if (!g_playerHurtHooked && __s2sdk_HookEvent) {
			if (s2sdk::HookEvent("player_hurt", &OnPlayerHurtEvent, s2sdk::HookMode::Pre) ==
				s2sdk::EventHookError::Okay) {
				g_playerHurtHooked = true;
			} else {
				LogError("vip_vampirism: hooking 'player_hurt' failed - healing is disabled");
			}
		} else if (!__s2sdk_HookEvent) {
			LogError("vip_vampirism: the s2sdk event bindings are unavailable - 'player_hurt' is not hooked");
		}

		LogInfo("vip_vampirism: ready");
	} catch (const std::exception& ex) {
		LogError(std::string("vip_vampirism: OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogError("vip_vampirism: OnPluginStart failed with an unknown exception");
	}
	return {}; // never Failed (PITFALLS A1)
}

plg::PluginResult VipVampirismPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	// Only the gate is pumped here (PITFALLS B1/B2): the heal itself happens in the event hook.
	PollMapGate();
	return {};
}

plg::PluginResult VipVampirismPlugin::OnPluginEnd() {
	try {
		// PITFALLS A7/C12: every registration of this library is removed again, so a `plg reload`
		// leaves nothing that points into the unloaded .so.
		if (g_playerHurtHooked) {
			g_playerHurtHooked = false;
			if (__s2sdk_UnhookEvent) {
				s2sdk::UnhookEvent("player_hurt", &OnPlayerHurtEvent, s2sdk::HookMode::Pre);
			}
		}
		if (g_mapListeners) {
			g_mapListeners = false;
			if (__s2sdk_OnMapStart_Unregister) {
				s2sdk::OnMapStart_Unregister(&VipVampirismPlugin::OnMapStartCallback);
			}
			if (__s2sdk_OnMapEnd_Unregister) {
				s2sdk::OnMapEnd_Unregister(&VipVampirismPlugin::OnMapEndCallback);
			}
		}

		// This module registers no vip callback and no feature callback, so the core keeps no
		// pointer into this library (see the file header); only the gate is closed for good, so a
		// listener the engine may still hold for a moment cannot reach a client.
		g_mapReady = false;
		g_mapStartSeen = false;
		g_mapEndSeen = true;

		LogInfo("vip_vampirism: unloaded");
	} catch (const std::exception& ex) {
		LogError(std::string("vip_vampirism: OnPluginEnd failed: ") + ex.what());
	} catch (...) {
		LogError("vip_vampirism: OnPluginEnd failed with an unknown exception");
	}
	return {};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
