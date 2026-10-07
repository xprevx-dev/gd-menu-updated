// Feature-drop hacks that need their own hook targets: visual cleanup (particles,
// pulse, wave trail), pass-through collision, infinite jumps and icon unlocks.
// Gameplay-flag caching lives in HackData (hacks::reloadSettings) so per-tick /
// per-collision code never re-reads settings.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/ParticleGameObject.hpp>
#include <Geode/modify/GJEffectManager.hpp>
#include <Geode/modify/GameManager.hpp>

// ---------------------------------------------------------------- no particles
// Hides level-spawned particle objects as they are created. Only affects particles
// spawned after the setting flips on (existing ones live until the next attempt);
// player death effects are a separate system and stay untouched.
class $modify(NoParticleObject, ParticleGameObject) {
	bool init() {
		if (!ParticleGameObject::init()) return false;
		if (Mod::get()->getSettingValue<bool>("no-particles")) this->setVisible(false);
		return true;
	}
};

// ---------------------------------------------------------------- no pulse
// Pulse triggers ask the effect manager for the pulsed colour; hand back the
// untouched colour and every pulse (colour mode) becomes a no-op.
class $modify(NoPulseEffects, GJEffectManager) {
	ccColor3B colorForPulseEffect(ccColor3B const& color, PulseEffectAction* action) {
		if (Mod::get()->getSettingValue<bool>("no-pulse")) return color;
		return GJEffectManager::colorForPulseEffect(color, action);
	}
};

// ---------------------------------------------------------------- all passable
// PlayerObject::collidedWithObject(dt, obj) is inline and forwards to this 4-arg
// overload, so hooking it covers every solid-collision path: returning false means
// "no collision happened" and the player walks/falls straight through blocks.
// Hazards keep their own kill path (pair with noclip for full ghost mode).
class $modify(PassablePlayer, PlayerObject) {
	bool collidedWithObject(float dt, GameObject* object, CCRect rect, bool skipCheck) {
		if (g_hacks.allPassable) return false;
		return PlayerObject::collidedWithObject(dt, object, rect, skipCheck);
	}
};

// ---------------------------------------------------------------- wave trail + jump hack
class $modify(GameHackPlayLayer, PlayLayer) {
	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		bool noTrail = Mod::get()->getSettingValue<bool>("no-wave-trail");
		bool jumpHack = g_hacks.jumpHack;
		if (!noTrail && !jumpHack) return;
		for (int i = 0; i < 2; i++) {
			auto p = i == 0 ? m_player1 : m_player2;
			if (!p) continue;
			if (p->m_waveTrail) p->m_waveTrail->setVisible(!noTrail); // also restores on toggle-off
			// infinite jumps: while jump is held in mid-air, pretend we're on the ground
			// so the game's own hold-to-jump logic keeps re-firing (hover upward, MH style)
			if (jumpHack && !p->m_isDead && g_bot.viewHeld[i][1] && !p->m_isOnGround)
				p->m_isOnGround = true;
		}
	}
};

// ---------------------------------------------------------------- unlock icons
// Purely client-side cosmetics: the garage asks GameManager whether an icon is
// unlocked. Server-side items (chests/shop) go through GameStatsManager and are
// deliberately NOT touched.
class $modify(UnlockAllIcons, GameManager) {
	bool isIconUnlocked(int id, IconType type) {
		if (Mod::get()->getSettingValue<bool>("unlock-icons")) return true;
		return GameManager::isIconUnlocked(id, type);
	}
};
