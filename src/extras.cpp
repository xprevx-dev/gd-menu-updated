// 2.4.1 extras: autoclicker, safe mode, noclip accuracy, menu themes, preset profiles.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

// ---------------------------------------------------------------- persistence
bool extras::cheatsActive() {
	return g_hacks.noclip || (g_hacks.speedhack && g_hacks.speed != 1.f) || g_hacks.autoclick || g_bot.stepper
		|| g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming || g_hacks.startPosIndex >= 0;
}

void extras::saveHackState() {
	auto m = Mod::get();
	m->setSavedValue("autoclick-cps", (double)g_hacks.cps);
	m->setSavedValue("safe-mode", g_hacks.safeMode);
	m->setSavedValue("accuracy", g_hacks.accuracy);
}

void extras::loadHackState() {
	auto m = Mod::get();
	g_hacks.cps = (float)std::clamp(m->getSavedValue<double>("autoclick-cps", 10.0), 1.0, 60.0);
	g_hacks.safeMode = m->getSavedValue<bool>("safe-mode", true);
	g_hacks.accuracy = m->getSavedValue<bool>("accuracy", false);
}

$on_mod(Loaded) { extras::loadHackState(); }

// ---------------------------------------------------------------- stats
void extras::bumpStat(char const* key) {
	Mod::get()->setSavedValue<int64_t>(fmt::format("stat-{}", key), stat(key) + 1);
}
int64_t extras::stat(char const* key) {
	return Mod::get()->getSavedValue<int64_t>(fmt::format("stat-{}", key), 0);
}

// ---------------------------------------------------------------- themes
namespace {
	struct Theme { char const* name; ccColor3B color; };
	constexpr Theme THEMES[] = {
		{ "Sky",    { 120, 200, 255 } },
		{ "Purple", { 190, 130, 255 } },
		{ "Mint",   { 110, 240, 170 } },
		{ "Ruby",   { 255, 100, 110 } },
		{ "Gold",   { 255, 205,  80 } },
		{ "Pink",   { 255, 140, 210 } },
		{ "Mono",   { 225, 225, 235 } },
		{ "Custom", { 120, 200, 255 } }, // colour comes from the custom-accent setting
	};
	constexpr int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
}
int extras::themeCount() { return THEME_COUNT; }
int extras::themeIndex() { return std::clamp((int)Mod::get()->getSavedValue<int64_t>("theme", 0), 0, THEME_COUNT - 1); }
void extras::setTheme(int i) { Mod::get()->setSavedValue<int64_t>("theme", ((i % THEME_COUNT) + THEME_COUNT) % THEME_COUNT); }
char const* extras::themeName(int i) { return THEMES[std::clamp(i, 0, THEME_COUNT - 1)].name; }
ccColor3B extras::accent() {
	if (themeIndex() == THEME_COUNT - 1)
		return Mod::get()->getSettingValue<ccColor3B>("custom-accent");
	return THEMES[themeIndex()].color;
}
float extras::bubbleOpacity() { return (float)std::clamp(Mod::get()->getSavedValue<double>("bubble-opacity", 1.0), 0.2, 1.0); }
void extras::setBubbleOpacity(float v) { Mod::get()->setSavedValue<double>("bubble-opacity", std::clamp(v, 0.2f, 1.f)); }
float extras::bubbleSize() { return (float)std::clamp(Mod::get()->getSavedValue<double>("bubble-size", 1.0), 0.6, 1.8); }
void extras::setBubbleSize(float v) { Mod::get()->setSavedValue<double>("bubble-size", std::clamp(v, 0.6f, 1.8f)); }

// ---------------------------------------------------------------- profiles
static std::string profileKey(int slot) { return fmt::format("profile-{}", slot); }
static char const* DEFAULT_NAMES[] = { "Practice", "Showcase", "Custom" };

std::string extras::profileName(int slot) { return DEFAULT_NAMES[std::clamp(slot, 0, 2)]; }
bool extras::profileExists(int slot) { return Mod::get()->hasSavedValue(profileKey(slot)); }

void extras::saveProfile(int slot) {
	matjson::Value v = matjson::Value::object();
	v["noclip"] = g_hacks.noclip;
	v["speedhack"] = g_hacks.speedhack;
	v["speed"] = (double)g_hacks.speed;
	v["hitboxes"] = g_hacks.hitboxes;
	v["autoclick"] = g_hacks.autoclick;
	v["cps"] = (double)g_hacks.cps;
	v["safe"] = g_hacks.safeMode;
	v["accuracy"] = g_hacks.accuracy;
	Mod::get()->setSavedValue(profileKey(slot), v);
	notify(fmt::format("Saved profile \"{}\"", profileName(slot)), NotificationIcon::Success);
}

bool extras::loadProfile(int slot) {
	if (!profileExists(slot)) { notify("That profile is empty - save it first", NotificationIcon::Warning); return false; }
	auto v = Mod::get()->getSavedValue<matjson::Value>(profileKey(slot));
	g_hacks.noclip    = v["noclip"].asBool().unwrapOr(false);
	g_hacks.speedhack = v["speedhack"].asBool().unwrapOr(false);
	hacks::setSpeed((float)v["speed"].asDouble().unwrapOr(1.0));
	g_hacks.hitboxes  = v["hitboxes"].asBool().unwrapOr(false);
	g_hacks.autoclick = v["autoclick"].asBool().unwrapOr(false);
	g_hacks.cps       = (float)std::clamp(v["cps"].asDouble().unwrapOr(10.0), 1.0, 60.0);
	g_hacks.safeMode  = v["safe"].asBool().unwrapOr(true);
	g_hacks.accuracy  = v["accuracy"].asBool().unwrapOr(false);
	saveHackState();
	notify(fmt::format("Loaded profile \"{}\"", profileName(slot)), NotificationIcon::Success);
	return true;
}

// ---------------------------------------------------------------- gameplay
static bool s_autoDown = false;
static bool s_limitHit = false; // "noclip limit reached" notification, once per attempt

class $modify(ExtrasGameLayer, GJBaseGameLayer) {
	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		auto pl = PlayLayer::get();
		bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;

		// autoclicker: presses through handleButton, so it gets recorded by the bot like a real click
		if (mine && g_hacks.autoclick && g_bot.state != BotState::Playing && g_bot.state != BotState::Resuming
			&& !m_player1->m_isDead) {
			int period = std::max(2, (int)std::lround(240.f / g_hacks.cps));
			int phase = (int)m_gameState.m_currentProgress % period;
			bool want = phase < period / 2;
			if (want != s_autoDown) {
				s_autoDown = want;
				this->handleButton(want, 1, true);
			}
		}
		else if (mine && s_autoDown && !g_hacks.autoclick) {
			s_autoDown = false;
			this->handleButton(false, 1, true);
		}

		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

		if (!mine) return;
		if (extras::cheatsActive()) g_hacks.cheatedAttempt = true;

		// noclip accuracy: count ticks where noclip saved you
		if (g_hacks.noclip && !m_player1->m_isDead) {
			g_hacks.accTicks++;
			if (g_hacks.accHitThisTick) {
				g_hacks.accDeadTicks++;
				if (!g_hacks.accWasHit) g_hacks.accDeaths++;
			}
			g_hacks.accWasHit = g_hacks.accHitThisTick;
			g_hacks.accHitThisTick = false;
		}
	}
};

class $modify(ExtrasPlayLayer, PlayLayer) {
	struct Fields { CCLabelBMFont* acc = nullptr; };

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		s_autoDown = false;
		// Mega Hack's "Show Hitboxes on Death": force the vanilla option on (only when
		// our setting is on - the user's vanilla choice is never touched otherwise)
		if (Mod::get()->getSettingValue<bool>("hitboxes-on-death")) m_hitboxesOnDeath = true;
		g_hacks.cheatedAttempt = extras::cheatsActive();
		resetAccuracy();
		auto win = CCDirector::get()->getWinSize();
		auto l = CCLabelBMFont::create("", "bigFont.fnt");
		l->setScale(0.35f);
		l->setOpacity(170);
		l->setAnchorPoint({ 0.f, 1.f });
		l->setPosition({ 6.f, win.height - 6.f });
		l->setZOrder(1000);
		l->setVisible(false);
		this->addChild(l);
		m_fields->acc = l;
		return true;
	}

	void resetAccuracy() {
		g_hacks.accTicks = g_hacks.accDeadTicks = g_hacks.accDeaths = 0;
		g_hacks.accHitThisTick = g_hacks.accWasHit = false;
		s_limitHit = false;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		s_autoDown = false;
		if (Mod::get()->getSettingValue<bool>("hitboxes-on-death")) m_hitboxesOnDeath = true;
		g_hacks.cheatedAttempt = extras::cheatsActive();
		// accuracy is per run: only reset when restarting from the beginning (not on practice checkpoints)
		if (!m_isPracticeMode || m_checkpointArray->count() == 0) resetAccuracy();
	}

	// The ONLY destroyPlayer hook in the mod (the old twin hook in hacks.cpp ran in an
	// unspecified order relative to this one and could swallow the call before accuracy
	// was counted - or, worse, let a noclip player die).
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		if (g_hacks.noclip && obj != m_anticheatSpike) {
			// per-player noclip (Mega Hack style): protect each side separately
			bool sideOk = (player == m_player1 && Mod::get()->getSettingValue<bool>("noclip-p1"))
			           || (player == m_player2 && Mod::get()->getSettingValue<bool>("noclip-p2"));
			// noclip limits (Mega Hack / Eclipse): after leaning on noclip too hard,
			// hits become lethal again for the rest of the attempt
			int hitLimit = (int)Mod::get()->getSettingValue<int64_t>("noclip-hit-limit");
			float accLimit = (float)Mod::get()->getSettingValue<double>("noclip-acc-limit");
			float accNow = g_hacks.accTicks
				? 100.f * (1.f - (float)(g_hacks.accDeadTicks + 1) / (float)(g_hacks.accTicks + 1))
				: 100.f;
			bool overLimit = (hitLimit > 0 && g_hacks.accDeaths >= hitLimit)
			              || (accLimit > 0.f && accNow < accLimit);
			if (sideOk && !overLimit) {
				g_hacks.accHitThisTick = true; // noclip saved you this tick
				return;
			}
			if (sideOk && overLimit && !s_limitHit) {
				s_limitHit = true;
				notify("Noclip limit reached - hits are lethal again", NotificationIcon::Warning);
			}
		}
		clips::onDeath(this, this->getCurrentPercent()); // a REAL death: the clip ends here
		if (g_hacks.safeMode && g_hacks.cheatedAttempt) {
			bool old = m_isTestMode;
			m_isTestMode = true;  // test mode = GD won't save a new best %
			PlayLayer::destroyPlayer(player, obj);
			m_isTestMode = old;
			return;
		}
		PlayLayer::destroyPlayer(player, obj);
	}

	void levelComplete() {
		if (g_hacks.safeMode && g_hacks.cheatedAttempt) {
			bool old = m_isTestMode;
			m_isTestMode = true;  // completion won't be saved / submitted
			PlayLayer::levelComplete();
			m_isTestMode = old;
			notify("Safe Mode: completion not saved (cheats were used)", NotificationIcon::Info);
			return;
		}
		PlayLayer::levelComplete();
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		auto l = m_fields->acc;
		if (!l) return;
		// when the in-game HUD is on it draws accuracy itself (as part of the stack)
		bool show = g_hacks.accuracy && g_hacks.noclip && !hud::enabled();
		l->setVisible(show);
		if (!show) return;
		float acc = g_hacks.accTicks ? 100.f * (1.f - (float)g_hacks.accDeadTicks / g_hacks.accTicks) : 100.f;
		l->setString(fmt::format("{:.2f}%  {} deaths", acc, g_hacks.accDeaths).c_str());
		l->setColor(acc >= 100.f ? ccColor3B{ 140, 255, 140 } : acc >= 90.f ? ccColor3B{ 255, 230, 120 } : ccColor3B{ 255, 120, 120 });
	}
};
