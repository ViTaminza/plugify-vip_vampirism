// vip_vampirism — Plugify port of the Pisex VIP module "VIP_Vampirism" / "[VIP] Vampirism"
// (the original's vip_vampirism.cpp, 154 lines).
//
// Behaviour kept identical: on "player_hurt", a VIP attacker whose "Vampirism" feature value is
// positive heals for value% of the damage dealt (integer arithmetic: the damage times the
// percentage, divided by 100), clamped to the attacker's max health. Self-damage is skipped.
//
// Port notes:
//   * The core (IVIPApi) is the Plugify "vip" plugin; the module calls it through the consumer
//     wrapper of plugins/vip/include/vip_api.hpp.
//   * The original compared the attacker's slot against the raw "userid" event field. In CS2 a
//     user id is not a player slot, so that test almost never matched and self-damage healed the
//     attacker. The port compares the two resolved slots (attacker vs. the "userid" key through
//     IGameEvent::GetPlayerSlot), which is what the line was meant to do - the same kind of fix
//     the admin modules document for "jointeam".
//   * The pawn/controller checks of the original (CCSPlayerController::FromSlot +
//     m_hPlayerPawn + m_lifeState) become pisexcompat's GetControllerEntity / IsAlive.
//   * Health and max health are read with s2sdk's GetClientHealth / GetClientMaxHealth (the
//     engine's GetHealth/GetMaxHealth on the pawn) and written with SetClientHealth (the engine's
//     SetHealth, which performs the field write and the network update the original's health
//     write plus explicit state change did).
//   * The original's g_bvampirism[64] table and its OnClientLoaded/OnClientDisconnect/OnTogglable
//     callbacks only ever wrote that table and never read it back (the toggle callback always
//     returned false, i.e. it never vetoed a menu toggle); they are dropped as dead state, which
//     leaves the observable behaviour - menu toggle, cookies, healing - untouched. Dropping the
//     toggle callback also removes a core->module pointer that would otherwise outlive this
//     library (the vip API has no feature unregister).

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>

#include <s2sdk/s2sdk.hpp>

#include <vip_api.hpp>

#include <chrono>
#include <cstdint>
#include <exception>

namespace {

// The GetLogTag() of the original ("[VIP-Vampirism]").
constexpr char kLogTag[] = "VIP-Vampirism";

// The object the ported module uses (the Pisex g_pVIPCore).
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

class VipVampirismPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// VIP_OnVIPLoaded of the original: the core's database is up, the hook may run.
	void OnCoreReady();

private:
	void OnPlayerHurt(const char* szName, pisex::IGameEvent* pEvent, bool bDontBroadcast);

	bool m_coreReady = false;
	bool m_hookRegistered = false;
};

VipVampirismPlugin g_plugin;

void VipVampirismPlugin::OnPlayerHurt(const char* szName, pisex::IGameEvent* pEvent, bool bDontBroadcast) {
	(void)szName;
	(void)bDontBroadcast;
	if (!pEvent) {
		return;
	}

	// The attacker's slot from the event.
	const int iAttacker = pEvent->GetPlayerSlot("attacker");
	if (iAttacker < 0) {
		return;
	}
	// Self-damage is skipped against the victim's slot, see the file header.
	if (iAttacker == pEvent->GetPlayerSlot("userid")) {
		return;
	}

	// The original's controller -> pawn -> life-state chain.
	if (pisex::GetControllerEntity(iAttacker) == 0) {
		return;
	}
	if (!pisex::g_pPlayers->IsAlive(iAttacker)) {
		return;
	}

	// The attacker's "Vampirism" value, for a VIP only, and only when it is positive.
	const int iVamp = g_pVIPCore->VIP_GetClientFeatureInt(iAttacker, "Vampirism");
	if (!g_pVIPCore->VIP_IsClientVIP(iAttacker) || iVamp <= 0) {
		return;
	}

	// The original's new health: the current health plus the damage share, as integer division
	// (a round() call there changed nothing on ints).
	// The s2sdk wrappers call their binding slots unchecked: without the two guards a missing
	// binding would be a null call from inside the game event hook.
	if (!__s2sdk_GetClientMaxHealth || !__s2sdk_SetClientHealth) {
		return;
	}
	int iHealth = pisex::GetPlayerHealth(iAttacker) + (pEvent->GetInt("dmg_health") * iVamp) / 100;
	const int iMaxHealth = s2sdk::GetClientMaxHealth(iAttacker);
	// The original clamped against the pawn's m_iMaxHealth; GetClientMaxHealth answers 0 when the
	// pawn is gone (the alive check above makes that unreachable - the > 0 keeps a 0 from turning
	// into a kill).
	if (iMaxHealth > 0 && iHealth > iMaxHealth) {
		iHealth = iMaxHealth;
	}

	// The original's health write plus its explicit state change.
	s2sdk::SetClientHealth(iAttacker, iHealth);
}

// --- lifecycle ---------------------------------------------------------------------------------

plg::PluginResult VipVampirismPlugin::OnPluginStart() {
	try {
		pisex::Init("vip");

		// The original registered the VIP_OnVIPLoaded callback in AllPluginsLoaded.
		g_pVIPCore->VIP_OnVIPLoaded([] { g_plugin.OnCoreReady(); });
		if (g_pVIPCore->VIP_IsVIPLoaded()) {
			OnCoreReady();
		}

		// The original registered "Vampirism" with a toggle callback; the callback is dropped, see
		// the file header.
		g_pVIPCore->VIP_RegisterFeature("Vampirism", vip::VIP_BOOL, vip::TOGGLABLE);

		return {};
	} catch (const std::exception& ex) {
		pisex::ErrorLog("[%s] OnPluginStart failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("[%s] OnPluginStart failed with an unknown exception", kLogTag);
	}
	return {}; // never Failed: that stops every plugify plugin
}

plg::PluginResult VipVampirismPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	try {
		pisex::Poll();
	} catch (const std::exception& ex) {
		pisex::ErrorLog("[%s] OnPluginUpdate failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("[%s] OnPluginUpdate failed with an unknown exception", kLogTag);
	}
	return {};
}

plg::PluginResult VipVampirismPlugin::OnPluginEnd() {
	try {
		g_pVIPCore->VIP_OnVIPLoadedUnregister();
		pisex::Shutdown();
	} catch (...) {
		// nothing left to do at unload
	}
	return {};
}

void VipVampirismPlugin::OnCoreReady() {
	if (m_coreReady) {
		return;
	}
	m_coreReady = true;
	if (!m_hookRegistered) {
		m_hookRegistered = true;
		// The original hooked "player_hurt" in AllPluginsLoaded.
		pisex::g_pUtils->HookEvent("player_hurt", [](const char* szName, pisex::IGameEvent* pEvent,
													 bool bDontBroadcast) {
			g_plugin.OnPlayerHurt(szName, pEvent, bDontBroadcast);
		});
	}
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
