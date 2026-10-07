// Hacks: noclip, speedhack, hitboxes, start-pos switcher, frame stepper + PC keybinds.
#include "state.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>

HackData g_hacks;

// ---------------------------------------------------------------- settings
// Keybinds are native Geode keybind settings since v2.5.0: the Settings popup gives a
// proper capture UI (any key, modifiers, mouse buttons) instead of the old single
// letter strings, and Geode fires press events for us (no keyboard hook needed).
static std::vector<Keybind> binds(std::string_view id) {
	return Mod::get()->getSettingValue<std::vector<Keybind>>(id);
}

void hacks::reloadSettings() {
	g_hacks.speed = (float)Mod::get()->getSettingValue<double>("speedhack");
	// cached flags for hot paths (per-tick / per-collision): refreshed on every
	// settings change by the listener at the bottom of this file
	g_hacks.allPassable    = Mod::get()->getSettingValue<bool>("all-passable");
	g_hacks.jumpHack       = Mod::get()->getSettingValue<bool>("jump-hack");
	g_hacks.physicsBypass  = Mod::get()->getSettingValue<bool>("physics-bypass");
	g_hacks.forcePlatformer = Mod::get()->getSettingValue<bool>("force-platformer");
	struct KeySlot { char const* id; std::vector<Keybind>* slot; char const* label; };
	KeySlot keys[] = {
		{ "toggle-stepper-key", &g_hacks.kToggleStep, "Toggle stepper" },
		{ "step-key",           &g_hacks.kStep,       "Step one frame" },
		{ "noclip-key",         &g_hacks.kNoclip,     "Noclip" },
		{ "hitbox-key",         &g_hacks.kHitbox,     "Hitboxes" },
		{ "speed-key",          &g_hacks.kSpeed,      "Speedhack" },
		{ "startpos-prev-key",  &g_hacks.kSpPrev,     "Previous start pos" },
		{ "startpos-next-key",  &g_hacks.kSpNext,     "Next start pos" },
	};
	for (auto& k : keys) *k.slot = binds(k.id);
	// the same combo on two actions would fire both; keep the first, disable the rest, say so
	g_hacks.keyConflict.clear();
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		for (size_t j = i + 1; j < sizeof(keys) / sizeof(keys[0]); j++) {
			bool clash = false;
			for (auto& a : *keys[i].slot)
				for (auto& b : *keys[j].slot)
					if (a.key != KEY_None && a == b) clash = true;
			if (clash) {
				keys[j].slot->clear();
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

// ---------------------------------------------------------------- force platformer
// GJGameLevel::isPlatformer() is literally "m_levelLength == 5", so forcing platformer
// mode = temporarily writing that field. The GJGameLevel object outlives the level
// (cached lists / search results), so the original value MUST be restored on quit -
// bot.cpp's onQuit hook calls restoreForcedPlatformer().
namespace {
	GJGameLevel* s_forcedLevel = nullptr;
	int s_origLevelLength = 0;
}

void hacks::applyForcedPlatformer(GJGameLevel* level) {
	hacks::restoreForcedPlatformer(); // never leave an old mutation behind
	if (level && g_hacks.forcePlatformer && level->m_levelLength != 5) {
		s_forcedLevel = level;
		s_origLevelLength = level->m_levelLength;
		level->m_levelLength = 5;
	}
}

void hacks::restoreForcedPlatformer() {
	if (s_forcedLevel) {
		s_forcedLevel->m_levelLength = s_origLevelLength;
		s_forcedLevel = nullptr;
	}
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
namespace {
	// "Sync Music With Speedhack" (Eclipse / xdBot style): pitch the song with the
	// scheduler speed so fast/slow motion doesn't drift away from the music. The base
	// frequency is captured once per channel and restored when the hack ends or you
	// leave the level (want=false path), so nothing stays detuned.
	float s_audioBase[2] = { 0.f, 0.f };
	bool s_audioApplied[2] = { false, false };

	void syncMusicAudio(float speed) {
		// factor = (speedhack pitch if sync is on) * (standalone pitch shift)
		bool syncOn = speed != 1.f && Mod::get()->getSettingValue<bool>("speedhack-audio");
		float pitch = (float)std::clamp(Mod::get()->getSettingValue<double>("audio-pitch"), 0.25, 4.0);
		float factor = (syncOn ? speed : 1.f) * pitch;
		bool want = factor != 1.f;
		if (!want && !s_audioApplied[0] && !s_audioApplied[1]) return; // idle fast path
		auto eng = FMODAudioEngine::get();
		if (!eng) return;
		for (int id = 0; id < 2; id++) {
			auto ch = eng->getActiveMusicChannel(id);
			if (!ch) { s_audioApplied[id] = false; s_audioBase[id] = 0.f; continue; }
			if (want) {
				if (!s_audioApplied[id]) {
					float f = 0.f;
					ch->getFrequency(&f);
					s_audioBase[id] = f;
					s_audioApplied[id] = true;
				}
				if (s_audioBase[id] > 1.f) ch->setFrequency(s_audioBase[id] * factor);
			}
			else if (s_audioApplied[id]) {
				ch->setFrequency(s_audioBase[id]);
				s_audioApplied[id] = false;
				s_audioBase[id] = 0.f;
			}
		}
	}
}

class $modify(HackScheduler, CCScheduler) {
	void update(float dt) {
		bool hack = g_hacks.speedhack && g_bot.state != BotState::Resuming && PlayLayer::get();
		if (hack) dt *= g_hacks.speed;
		syncMusicAudio(hack ? g_hacks.speed : 1.f);
		CCScheduler::update(dt);
	}
};

class $modify(HackGameLayer, GJBaseGameLayer) {
	// physics-bypass accumulator: leftover real time that hasn't been stepped yet
	struct Fields { float physAcc = 0.f; };

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
		// physics bypass (CBF/xdBot style): step physics at a fixed 240 tps no matter
		// the display FPS. dt already carries speedhack/resume scaling, so those keep
		// working. Substeps are capped so a tab-out spike drops time instead of
		// spiralling into a freeze.
		if (g_hacks.physicsBypass && PlayLayer::get()
			&& static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
			constexpr float STEP = 1.f / 240.f;
			constexpr int MAX_SUBSTEPS = 16; // = keeps up down to ~15 render FPS
			m_fields->physAcc += dt;
			if (m_fields->physAcc > STEP * MAX_SUBSTEPS) m_fields->physAcc = STEP * MAX_SUBSTEPS;
			int n = 0;
			while (m_fields->physAcc >= STEP && n < MAX_SUBSTEPS) {
				GJBaseGameLayer::update(STEP);
				m_fields->physAcc -= STEP;
				n++;
			}
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
		hacks::applyForcedPlatformer(level); // must happen BEFORE the real init reads it
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

		std::sort(g_hacks.startPositions.begin(), g_hacks.startPositions.end(),
			[](auto& a, auto& b) { return a->getPositionX() < b->getPositionX(); });
		for (int i = 0; i < (int)g_hacks.startPositions.size(); i++)
			if (g_hacks.startPositions[i].data() == m_startPosObject) g_hacks.startPosIndex = i;
		// the touch step bar lives on m_uiLayer, which is recreated per attempt: re-add it
		// when restarting while the stepper is still on (it used to vanish until re-toggled)
		hacks::updateStepperControls();
		// auto practice mode: flip on next frame (same path as the pause-menu button)
		if (Mod::get()->getSettingValue<bool>("auto-practice") && !m_isPracticeMode)
			this->scheduleOnce(schedule_selector(HackPlayLayer::autoPractice), 0.f);
		return true;
	}

	void autoPractice(float) {
		if (!m_isPracticeMode) this->togglePracticeMode(true);
	}

	// practice music sync bypass: vanilla restarts/resyncs the song on every checkpoint
	// respawn; skipping the call lets the music keep flowing uninterrupted
	void startMusic() {
		if (Mod::get()->getSettingValue<bool>("practice-music-bypass") && m_isPracticeMode &&
		    m_checkpointArray && m_checkpointArray->count() > 0)
			return;
		PlayLayer::startMusic();
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

// ---------------------------------------------------------------- keybind presses
// Settings used to be read once per level start, so editing a key did nothing until you
// re-entered the level. Now every settings change applies instantly, and one listener
// receives every GDMenu keybind press (Geode skips text inputs and gives us down/repeat,
// so holding the step key keeps stepping).
$on_mod(Loaded) {
	geode::listenForAllSettingChanges([](std::string_view, std::shared_ptr<geode::SettingV3>) {
		hacks::reloadSettings();
		hacks::updateStepperControls();
	});
	geode::listenForAllKeybindSettingPresses(
		[](std::string_view key, geode::Keybind const&, bool down, bool repeat, double) {
			auto pl = PlayLayer::get();
			if (!pl || pl->m_isPaused) return;
			if (key == "step-key") {
				if (down && g_bot.stepper) hacks::stepFrames(1);
				return;
			}
			if (!down || repeat) return;
			if (key == "toggle-stepper-key") hacks::toggleStepper();
			else if (key == "noclip-key")    hacks::toggleNoclip();
			else if (key == "hitbox-key")    hacks::toggleHitboxes();
			else if (key == "speed-key")     hacks::toggleSpeed();
			else if (key == "startpos-prev-key") hacks::switchStartPos(-1);
			else if (key == "startpos-next-key") hacks::switchStartPos(1);
		});
}
