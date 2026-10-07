// Optional in-game HUD (Settings > In-Game HUD, OFF by default).
// GDMenu is deliberately invisible while you play; this is the opt-in exception:
// a tiny corner overlay with bot state, frame, percent, speed, inputs, noclip accuracy
// and Mega Hack style counters (FPS / attempts / jumps / level time).
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

bool hud::enabled()      { return Mod::get()->getSettingValue<bool>("hud-enabled"); }
bool hud::showState()    { return Mod::get()->getSettingValue<bool>("hud-show-state"); }
bool hud::showFrame()    { return Mod::get()->getSettingValue<bool>("hud-show-frame"); }
bool hud::showPercent()  { return Mod::get()->getSettingValue<bool>("hud-show-percent"); }
bool hud::showSpeed()    { return Mod::get()->getSettingValue<bool>("hud-show-speed"); }

namespace {
	// run tracking for Best Run / Run From (session-scoped; resets on level change)
	float s_runStart = 0.f;
	float s_bestPct = 0.f;
	int s_bestLevelID = -1;
}
void hud::noteRunStart(float percent) { s_runStart = percent; }
void hud::noteRunEnd(float percent)   { s_bestPct = std::max(s_bestPct, percent); }

namespace {
	constexpr int MAX_LINES = 6; // state, info, inputs, accuracy, counters, best/cps/from

	struct Line {
		std::string text;
		ccColor3B color{ 255, 255, 255 };
	};

	// snapshot of the settings that affect layout, so we only rebuild when they change
	struct HudConfig {
		bool enabled = false;
		std::string anchor;
		float size = 1.f;
		bool operator==(HudConfig const& o) const {
			return enabled == o.enabled && anchor == o.anchor && size == o.size;
		}
	};

	HudConfig currentConfig() {
		HudConfig c;
		c.enabled = hud::enabled();
		c.anchor = Mod::get()->getSettingValue<std::string>("hud-anchor");
		c.size = (float)Mod::get()->getSettingValue<double>("hud-size");
		return c;
	}
}

class $modify(HudPlayLayer, PlayLayer) {
	struct Fields {
		HudConfig config{};
		CCLabelBMFont* lines[MAX_LINES] = {};
		// fps meter: wall clock between postUpdate calls (the game's dt is scaled by
		// speedhack / resume fast-forward, so it can't be used to count real frames)
		bool clockInit = false;
		std::chrono::steady_clock::time_point lastClock{};
		double fpsAcc = 0.0;
		int fpsCount = 0;
		int fps = 0;
		// cps meter: jump clicks per wall-clock second (rising edges of viewHeld)
		int cpsClicks = 0;
		double cpsAcc = 0.0;
		int cps = 0;
		bool jumpPrev[2] = { false, false };
	};

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		rebuildHud();
		return true;
	}

	void rebuildHud() {
		auto& cfg = m_fields->config;
		cfg = currentConfig();
		for (auto& l : m_fields->lines) { // reference: clear the slots, don't copy pointers
			if (l) { l->removeFromParent(); l = nullptr; }
		}
		if (!cfg.enabled) return;
		for (auto& l : m_fields->lines) {
			l = CCLabelBMFont::create("", "bigFont.fnt");
			l->setScale(0.35f * cfg.size);
			l->setOpacity(180);
			l->setZOrder(1000);
			l->setVisible(false);
			this->addChild(l);
		}
		layoutHud(0); // positions depend only on config; line count set per update
	}

	void layoutHud(int lineCount) {
		auto& cfg = m_fields->config;
		if (!cfg.enabled || lineCount <= 0) return;
		auto win = CCDirector::get()->getWinSize();
		bool top = cfg.anchor.starts_with("top");
		bool right = cfg.anchor.ends_with("right");
		float lh = 13.f * cfg.size;
		for (int i = 0; i < MAX_LINES; i++) {
			auto* l = m_fields->lines[i];
			if (!l) continue;
			l->setAnchorPoint({ right ? 1.f : 0.f, 0.5f });
			l->setPosition({ right ? win.width - 8.f : 8.f,
				top ? win.height - 8.f - lh * (i + 1) + lh / 2 : 8.f + lh * (lineCount - 1 - i) + lh / 2 });
		}
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (!(m_fields->config == currentConfig())) rebuildHud();
		if (!m_fields->config.enabled) return;

		// wall clock for the FPS + CPS windows (game dt is scaled by speedhack/resume,
		// so it can't measure real frames or real clicks-per-second)
		auto clockNow = std::chrono::steady_clock::now();
		double realDt = 0.0;
		if (m_fields->clockInit) realDt = std::chrono::duration<double>(clockNow - m_fields->lastClock).count();
		m_fields->clockInit = true;
		m_fields->lastClock = clockNow;
		if (realDt > 0.0) {
			m_fields->fpsAcc += realDt;
			m_fields->fpsCount++;
			if (m_fields->fpsAcc >= 0.5) {
				m_fields->fps = (int)std::lround((double)m_fields->fpsCount / m_fields->fpsAcc);
				m_fields->fpsAcc = 0.0;
				m_fields->fpsCount = 0;
			}
			m_fields->cpsAcc += realDt;
			if (m_fields->cpsAcc >= 1.0) {
				m_fields->cps = m_fields->cpsClicks;
				m_fields->cpsClicks = 0;
				m_fields->cpsAcc = 0.0;
			}
		}
		for (int p = 0; p < 2; p++) { // jump-click rising edges (you or the bot)
			bool j = g_bot.viewHeld[p][1];
			if (j && !m_fields->jumpPrev[p]) m_fields->cpsClicks++;
			m_fields->jumpPrev[p] = j;
		}
		// Best Run / Run From are per level: reset when a different level is open
		int levelID = m_level ? m_level->m_levelID.value() : -1;
		if (levelID != s_bestLevelID) { s_bestLevelID = levelID; s_bestPct = 0.f; s_runStart = 0.f; }

		Line ls[MAX_LINES];
		int n = 0;
		if (hud::showState() && g_bot.state != BotState::Idle)
			ls[n++] = { bot::stateName(), bot::stateColor() };
		std::string info;
		if (hud::showPercent()) {
			int dec = (int)std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("hud-percent-decimals"), 1, 3);
			info += fmt::format("{:.{}f}%", this->getCurrentPercent(), dec);
		}
		if (hud::showFrame())   info += (info.empty() ? "" : "   ") + fmt::format("f {}", bot::frame());
		float speed = 1.f;
		if (g_bot.state == BotState::Resuming) speed = (float)Mod::get()->getSettingValue<double>("resume-speed");
		else if (g_hacks.speedhack) speed = g_hacks.speed;
		if (hud::showSpeed() && speed != 1.f) info += fmt::format("   {:.2f}x", speed);
		if (!info.empty()) ls[n++] = { info, { 255, 255, 255 } };
		if (Mod::get()->getSettingValue<bool>("hud-show-inputs")) {
			// live input viewer: what the game is applying right now (you or the bot)
			auto held = [](int p) {
				std::string s;
				if (g_bot.viewHeld[p][1]) s += "JUMP ";
				if (g_bot.viewHeld[p][2]) s += "LEFT ";
				if (g_bot.viewHeld[p][3]) s += "RIGHT ";
				while (!s.empty() && s.back() == ' ') s.pop_back();
				return s;
			};
			std::string inputs = fmt::format("P1 [{}]", held(0));
			if (m_gameState.m_isDualMode) inputs += fmt::format("   P2 [{}]", held(1));
			ls[n++] = { inputs, { 200, 200, 220 } };
		}
		if (g_hacks.accuracy && g_hacks.noclip) {
			float acc = g_hacks.accTicks ? 100.f * (1.f - (float)g_hacks.accDeadTicks / g_hacks.accTicks) : 100.f;
			ls[n++] = { fmt::format("{:.2f}%  {} deaths", acc, g_hacks.accDeaths),
				acc >= 100.f ? ccColor3B{ 140, 255, 140 } : acc >= 90.f ? ccColor3B{ 255, 230, 120 } : ccColor3B{ 255, 120, 120 } };
		}
		// Mega Hack style counters (FPS / attempts / jumps / level time) on one compact line
		{
			bool fOn = Mod::get()->getSettingValue<bool>("hud-show-fps");
			bool aOn = Mod::get()->getSettingValue<bool>("hud-show-attempts");
			bool jOn = Mod::get()->getSettingValue<bool>("hud-show-jumps");
			bool tOn = Mod::get()->getSettingValue<bool>("hud-show-time");
			if (fOn || aOn || jOn || tOn) {
				std::string s;
				auto add = [&](std::string const& part) { if (!s.empty()) s += "   "; s += part; };
				if (fOn) add(fmt::format("FPS {}", m_fields->fps));
				if (aOn) add(fmt::format("ATT {}", m_attempts));
				if (jOn) add(fmt::format("JUMP {}", m_jumps));
				if (tOn) {
					int sec = (int)(m_gameState.m_currentProgress / 240u);
					add(fmt::format("TIME {}:{:02}", sec / 60, sec % 60));
				}
				ls[n++] = { s, { 190, 210, 255 } };
			}
		}
		// session line: best run % / clicks per second / where this run started
		{
			bool bOn = Mod::get()->getSettingValue<bool>("hud-show-best");
			bool cOn = Mod::get()->getSettingValue<bool>("hud-show-cps");
			bool rOn = Mod::get()->getSettingValue<bool>("hud-show-runfrom");
			if (bOn || cOn || rOn) {
				std::string s;
				auto add = [&](std::string const& part) { if (!s.empty()) s += "   "; s += part; };
				if (bOn) add(fmt::format("BEST {:.1f}%", s_bestPct));
				if (cOn) add(fmt::format("CPS {}", m_fields->cps));
				if (rOn && s_runStart > 0.5f) add(fmt::format("FROM {:.0f}%", s_runStart));
				if (!s.empty()) ls[n++] = { s, { 255, 220, 160 } };
			}
		}
		layoutHud(n);
		for (int i = 0; i < MAX_LINES; i++) {
			auto* l = m_fields->lines[i];
			if (!l) continue;
			bool on = i < n && !ls[i].text.empty();
			l->setVisible(on);
			if (!on) continue;
			l->setColor(ls[i].color);
			l->setString(ls[i].text.c_str());
		}
	}
};
