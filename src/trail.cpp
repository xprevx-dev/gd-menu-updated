// Player trail: draws the path your icon travels, in your theme colour.
// Handy for studying bot lines, wave corridors and ship timings. Opt-in setting.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>

namespace {
	constexpr int MAX_SEGMENTS = 20000; // ~80s of flight at 240tps; beyond that the trail resets

	bool trailEnabled() {
		return Mod::get()->getSettingValue<bool>("player-trail");
	}
}

class $modify(TrailPlayLayer, PlayLayer) {
	struct Fields {
		CCDrawNode* draw = nullptr;
		CCPoint last{ 0.f, 0.f };
		bool hasLast = false;
		int segments = 0;
		bool wasEnabled = false;
	};

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		m_fields->wasEnabled = trailEnabled();
		return true;
	}

	void ensureDraw() {
		if (m_fields->draw) return;
		m_fields->draw = CCDrawNode::create();
		m_fields->draw->setZOrder(999); // above level art, below the HUD labels
		this->addChild(m_fields->draw);
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		if (m_fields->draw) m_fields->draw->clear();
		m_fields->segments = 0;
		m_fields->hasLast = false;
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		bool on = trailEnabled();
		if (on != m_fields->wasEnabled) {
			m_fields->wasEnabled = on;
			if (m_fields->draw) m_fields->draw->clear();
			m_fields->segments = 0;
			m_fields->hasLast = false;
		}
		if (!on || !m_player1 || m_player1->m_isDead) {
			m_fields->hasLast = false; // don't draw a line from the death spot to the respawn
			return;
		}
		auto p = m_player1->getPosition();
		if (m_fields->hasLast && (p - m_fields->last).getLengthSquared() > 0.001f) {
			if (m_fields->segments >= MAX_SEGMENTS) {
				if (m_fields->draw) m_fields->draw->clear();
				m_fields->segments = 0;
			}
			ensureDraw();
			auto c = extras::accent();
			m_fields->draw->drawSegment(m_fields->last, p, 1.f,
				ccc4f(c.r / 255.f, c.g / 255.f, c.b / 255.f, 0.55f));
			m_fields->segments++;
		}
		m_fields->last = p;
		m_fields->hasLast = true;
	}
};
