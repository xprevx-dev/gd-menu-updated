// Optional in-game HUD (Settings > In-Game HUD, OFF by default).
// GDMenu is deliberately invisible while you play; this is the opt-in exception:
// a tiny corner overlay with bot state, frame, percent, speed and noclip accuracy.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>

bool hud::enabled()      { return Mod::get()->getSettingValue<bool>("hud-enabled"); }
bool hud::showState()    { return Mod::get()->getSettingValue<bool>("hud-show-state"); }
bool hud::showFrame()    { return Mod::get()->getSettingValue<bool>("hud-show-frame"); }
bool hud::showPercent()  { return Mod::get()->getSettingValue<bool>("hud-show-percent"); }
bool hud::showSpeed()    { return Mod::get()->getSettingValue<bool>("hud-show-speed"); }

namespace {
	constexpr int MAX_LINES = 3;

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
		CCLabelBMFont* lines[MAX_LINES] = { nullptr, nullptr, nullptr };
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

		Line ls[MAX_LINES];
		int n = 0;
		if (hud::showState() && g_bot.state != BotState::Idle)
			ls[n++] = { bot::stateName(), bot::stateColor() };
		std::string info;
		if (hud::showPercent()) info += fmt::format("{:.1f}%", this->getCurrentPercent());
		if (hud::showFrame())   info += (info.empty() ? "" : "   ") + fmt::format("f {}", bot::frame());
		float speed = 1.f;
		if (g_bot.state == BotState::Resuming) speed = (float)Mod::get()->getSettingValue<double>("resume-speed");
		else if (g_hacks.speedhack) speed = g_hacks.speed;
		if (hud::showSpeed() && speed != 1.f) info += fmt::format("   {:.2f}x", speed);
		if (!info.empty()) ls[n++] = { info, { 255, 255, 255 } };
		if (g_hacks.accuracy && g_hacks.noclip) {
			float acc = g_hacks.accTicks ? 100.f * (1.f - (float)g_hacks.accDeadTicks / g_hacks.accTicks) : 100.f;
			ls[n++] = { fmt::format("{:.2f}%  {} deaths", acc, g_hacks.accDeaths),
				acc >= 100.f ? ccColor3B{ 140, 255, 140 } : acc >= 90.f ? ccColor3B{ 255, 230, 120 } : ccColor3B{ 255, 120, 120 } };
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
