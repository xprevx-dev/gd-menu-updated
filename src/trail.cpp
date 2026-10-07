// Player trail: draws the path your icon travels, in your theme colour, with a
// configurable length (Settings / Hacks tab: Trail Length in seconds).
// Rolling trail: points are drawn into chunk draw-nodes and whole chunks are dropped
// once they age out - the trail keeps a steady length with no flicker and no
// full redraws. Opt-in setting, off by default.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <vector>

namespace {
	bool trailEnabled() {
		return Mod::get()->getSettingValue<bool>("player-trail");
	}
	int trailLengthSeconds() {
		return (int)std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("trail-length"), 1, 60);
	}
	constexpr int CHUNK_COUNT = 4; // visible length ~= 75%-100% of the setting
}

class $modify(TrailPlayLayer, PlayLayer) {
	struct Fields {
		std::vector<CCDrawNode*> chunks; // draw order, oldest first (children of this layer)
		CCDrawNode* current = nullptr;
		int segInChunk = 0;
		int chunkSize = 120;
		CCPoint last{ 0.f, 0.f };
		bool hasLast = false;
		bool wasEnabled = false;
	};

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		m_fields->wasEnabled = trailEnabled();
		return true;
	}

	void clearTrail() {
		for (auto*& c : m_fields->chunks) // reference: null the slots as we drop them
			if (c) { c->removeFromParent(); c = nullptr; }
		m_fields->chunks.clear();
		m_fields->current = nullptr;
		m_fields->segInChunk = 0;
		m_fields->hasLast = false;
	}

	void ensureChunk() {
		int cap = trailLengthSeconds() * 240; // segments ~= one per physics tick
		m_fields->chunkSize = std::max(60, cap / CHUNK_COUNT);
		auto d = CCDrawNode::create();
		d->setZOrder(999); // above level art, below the HUD labels
		this->addChild(d);
		m_fields->chunks.push_back(d);
		m_fields->current = d;
		m_fields->segInChunk = 0;
		while ((int)m_fields->chunks.size() > CHUNK_COUNT) {
			auto* old = m_fields->chunks.front();
			if (old) old->removeFromParent();
			m_fields->chunks.erase(m_fields->chunks.begin());
		}
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		clearTrail();
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		bool on = trailEnabled();
		if (on != m_fields->wasEnabled) {
			m_fields->wasEnabled = on;
			clearTrail();
		}
		if (!on || !m_player1 || m_player1->m_isDead) {
			m_fields->hasLast = false; // don't draw a line from the death spot to the respawn
			return;
		}
		auto p = m_player1->getPosition();
		if (m_fields->hasLast && (p - m_fields->last).getLengthSquared() > 0.001f) {
			if (!m_fields->current || m_fields->segInChunk >= m_fields->chunkSize) ensureChunk();
			auto c = extras::accent();
			m_fields->current->drawSegment(m_fields->last, p, 1.f,
				ccc4f(c.r / 255.f, c.g / 255.f, c.b / 255.f, 0.55f));
			m_fields->segInChunk++;
		}
		m_fields->last = p;
		m_fields->hasLast = true;
	}
};
