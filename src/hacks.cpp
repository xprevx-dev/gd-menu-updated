// Hacks: noclip, speedhack, hitboxes, start-pos switcher, frame stepper + PC keybinds.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#ifndef GEODE_IS_IOS
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif

HackData g_hacks;

// ---------------------------------------------------------------- settings
static enumKeyCodes keyFromSetting(char const* id) {
	auto s = Mod::get()->getSettingValue<std::string>(id);
	if (s.empty()) return KEY_None;
	char c = (char)std::toupper((unsigned char)s[0]);
	if (c >= 'A' && c <= 'Z') return (enumKeyCodes)(KEY_A + (c - 'A'));
	if (c >= '0' && c <= '9') return (enumKeyCodes)(KEY_Zero + (c - '0'));
	return KEY_None;
}

void hacks::reloadSettings() {
	g_hacks.speed       = (float)Mod::get()->getSettingValue<double>("speedhack");
	struct KeySlot { char const* id; enumKeyCodes* slot; char const* label; };
	KeySlot keys[] = {
		{ "toggle-stepper-key", &g_hacks.kToggleStep, "Toggle stepper" },
		{ "step-key",           &g_hacks.kStep,       "Step one frame" },
		{ "noclip-key",         &g_hacks.kNoclip,     "Noclip" },
		{ "hitbox-key",         &g_hacks.kHitbox,     "Hitboxes" },
		{ "speed-key",          &g_hacks.kSpeed,      "Speedhack" },
		{ "startpos-prev-key",  &g_hacks.kSpPrev,     "Previous start pos" },
		{ "startpos-next-key",  &g_hacks.kSpNext,     "Next start pos" },
	};
	for (auto& k : keys) *k.slot = keyFromSetting(k.id);
	// two actions on one key would both fire; keep the first and disable the rest, loudly
	g_hacks.keyConflict.clear();
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		for (size_t j = i + 1; j < sizeof(keys) / sizeof(keys[0]); j++) {
			if (*keys[i].slot != KEY_None && *keys[i].slot == *keys[j].slot) {
				*keys[j].slot = KEY_None;
				g_hacks.keyConflict = fmt::format("\"{}\" and \"{}\" use the same key - {} was disabled. Fix it in Settings.",
					keys[i].label, keys[j].label, keys[j].label);
			}
		}
	}
}

// ---------------------------------------------------------------- actions
void hacks::setStepper(bool on) {
	g_bot.stepper = on;
	g_bot.pendingSteps = 0;
	hacks::updateStepperControls();
}
void hacks::toggleStepper() {
	setStepper(!g_bot.stepper);
	notify(g_bot.stepper ? "Frame stepper ON" : "Frame stepper OFF");
}
void hacks::stepFrames(int n) {
	if (g_bot.stepper) g_bot.pendingSteps += n;
}
void hacks::toggleNoclip() {
	g_hacks.noclip = !g_hacks.noclip;
	notify(g_hacks.noclip ? "Noclip ON" : "Noclip OFF");
}
void hacks::toggleSpeed() {
	g_hacks.speedhack = !g_hacks.speedhack;
	notify(g_hacks.speedhack ? fmt::format("Speedhack {:.2f}x", g_hacks.speed) : "Speedhack OFF");
}
void hacks::setSpeed(float v) {
	g_hacks.speed = std::clamp(std::round(v * 100.f) / 100.f, 0.1f, 5.f);
	Mod::get()->setSettingValue<double>("speedhack", g_hacks.speed);
}
void hacks::toggleHitboxes() {
	g_hacks.hitboxes = !g_hacks.hitboxes;
	if (!g_hacks.hitboxes)
		if (auto pl = PlayLayer::get(); pl && pl->m_debugDrawNode && !pl->m_isPracticeMode)
			pl->m_debugDrawNode->clear();
	notify(g_hacks.hitboxes ? "Hitboxes ON" : "Hitboxes OFF");
}

std::string hacks::startPosLabel() {
	int count = (int)g_hacks.startPositions.size();
	if (count == 0) return "No start positions";
	if (g_hacks.startPosIndex < 0) return fmt::format("Level start  (0/{})", count);
	return fmt::format("Start pos {}/{}", g_hacks.startPosIndex + 1, count);
}

void hacks::switchStartPos(int dir) {
	auto pl = PlayLayer::get();
	if (!pl) return;
	if (g_bot.state != BotState::Idle) { notify("Stop the bot before switching start pos", NotificationIcon::Warning); return; }
	int count = (int)g_hacks.startPositions.size();
	if (count == 0) { notify("No start positions in this level", NotificationIcon::Warning); return; }
	g_hacks.startPosIndex += dir;
	if (g_hacks.startPosIndex < -1) g_hacks.startPosIndex = count - 1;
	if (g_hacks.startPosIndex >= count) g_hacks.startPosIndex = -1;

	pl->m_currentCheckpoint = nullptr;
	pl->setStartPosObject(g_hacks.startPosIndex < 0 ? nullptr : g_hacks.startPositions[g_hacks.startPosIndex].data());
	if (pl->m_isPracticeMode) pl->resetLevelFromStart();
	pl->resetLevel();
	pl->startMusic();
	notify(hacks::startPosLabel());
}

// ---------------------------------------------------------------- stepper touch bar
// Only appears while the frame stepper is ON (you can't step on a phone otherwise).
// Normal gameplay shows nothing - everything else lives in the pause menu.
static bool stepperTouchEnabled() {
	auto mode = Mod::get()->getSettingValue<std::string>("stepper-touch-controls");
	if (mode == "always") return true;
	if (mode == "never") return false;
#ifdef GEODE_IS_MOBILE
	return true;
#else
	return false;
#endif
}

class StepperBar : public CCMenu {
public:
	static StepperBar* create() {
		auto ret = new StepperBar();
		if (ret->init()) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
	bool init() override {
		if (!CCMenu::init()) return false;
		this->setID("stepper-bar"_spr);
		auto win = CCDirector::get()->getWinSize();
		float s = (float)Mod::get()->getSettingValue<double>("hud-scale");
		this->setPosition({ win.width / 2, 28.f * s });
		this->setTouchPriority(-500);
		auto add = [&](const char* text, const char* bg, SEL_MenuHandler sel, float x) {
			auto spr = ButtonSprite::create(text, 50, true, "bigFont.fnt", bg, 26.f, 0.6f);
			spr->setScale(0.8f * s);
			spr->setOpacity(190);
			spr->setCascadeOpacityEnabled(true);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
			btn->setPosition({ x * s, 0 });
			this->addChild(btn);
		};
		add("+1",   "GJ_button_01.png", menu_selector(StepperBar::onStep1), -55.f);
		add("+10",  "GJ_button_05.png", menu_selector(StepperBar::onStep10), 0.f);
		add("Play", "GJ_button_06.png", menu_selector(StepperBar::onOff), 55.f);
		return true;
	}
	void onStep1(CCObject*)  { hacks::stepFrames(1); }
	void onStep10(CCObject*) { hacks::stepFrames(10); }
	void onOff(CCObject*)    { hacks::setStepper(false); }
};

void hacks::updateStepperControls() {
	auto pl = PlayLayer::get();
	if (!pl || !pl->m_uiLayer) return;
	auto bar = pl->m_uiLayer->getChildByID("stepper-bar"_spr);
	bool want = g_bot.stepper && stepperTouchEnabled();
	if (want && !bar) pl->m_uiLayer->addChild(StepperBar::create(), 100);
	else if (!want && bar) bar->removeFromParent();
}

// ---------------------------------------------------------------- hooks
class $modify(HackScheduler, CCScheduler) {
	void update(float dt) {
		if (g_hacks.speedhack && g_bot.state != BotState::Resuming && PlayLayer::get()) dt *= g_hacks.speed;
		CCScheduler::update(dt);
	}
};

class $modify(HackGameLayer, GJBaseGameLayer) {
	void updateDebugDraw() {
		bool old = m_isDebugDrawEnabled;
		if (g_hacks.hitboxes) m_isDebugDrawEnabled = true;
		GJBaseGameLayer::updateDebugDraw();
		m_isDebugDrawEnabled = old;
	}

	void update(float dt) {
		// frame stepper: freeze unless a step was requested
		if (g_bot.stepper && g_bot.state != BotState::Resuming && PlayLayer::get()
			&& static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
			if (g_bot.pendingSteps <= 0) return;
			g_bot.pendingSteps--;
			GJBaseGameLayer::update(1.f / 240.f);
			return;
		}
		GJBaseGameLayer::update(dt);
	}
};

class $modify(HackPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		g_hacks.startPositions.clear();
		g_hacks.startPosIndex = -1;
		hacks::reloadSettings();
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

		std::sort(g_hacks.startPositions.begin(), g_hacks.startPositions.end(),
			[](auto& a, auto& b) { return a->getPositionX() < b->getPositionX(); });
		for (int i = 0; i < (int)g_hacks.startPositions.size(); i++)
			if (g_hacks.startPositions[i].data() == m_startPosObject) g_hacks.startPosIndex = i;
		return true;
	}

	void addObject(GameObject* obj) {
		PlayLayer::addObject(obj);
		if (obj->m_objectID == 31) g_hacks.startPositions.push_back(static_cast<StartPosObject*>(obj));
	}

	// NOTE: destroyPlayer used to be hooked here AND in extras.cpp. Two hooks on the same
	// function in one mod run in an order Geode doesn't promise, and the noclip hook here
	// swallowed the call before extras' hook could count accuracy. It now lives in exactly
	// one place: ExtrasPlayLayer::destroyPlayer in extras.cpp.

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (g_hacks.hitboxes && !m_isPracticeMode && m_debugDrawNode) {
			m_debugDrawNode->setVisible(true);
			updateDebugDraw();
		}
	}
};

// ---------------------------------------------------------------- PC keybinds (hidden, no UI during gameplay)
// Settings used to be read once per level start, so editing a keybind in Settings did
// nothing until you re-entered the level. Now every settings change applies instantly.
$on_mod(Loaded) {
	geode::listenForAllSettingChanges([](std::string_view, std::shared_ptr<geode::SettingV3>) {
		hacks::reloadSettings();
		hacks::updateStepperControls();
	});
}

#ifndef GEODE_IS_IOS
class $modify(CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time) {
		auto pl = PlayLayer::get();
		if (down && key != KEY_None && pl && !pl->m_isPaused) {
			if (key == g_hacks.kStep && g_bot.stepper) { hacks::stepFrames(1); return true; } // hold = keep stepping
			if (!repeat) {
				if (key == g_hacks.kToggleStep) { hacks::toggleStepper();     return true; }
				if (key == g_hacks.kNoclip)     { hacks::toggleNoclip();      return true; }
				if (key == g_hacks.kHitbox)     { hacks::toggleHitboxes();    return true; }
				if (key == g_hacks.kSpeed)      { hacks::toggleSpeed();       return true; }
				if (key == g_hacks.kSpPrev)     { hacks::switchStartPos(-1);  return true; }
				if (key == g_hacks.kSpNext)     { hacks::switchStartPos(1);   return true; }
			}
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
	}
};
#endif
