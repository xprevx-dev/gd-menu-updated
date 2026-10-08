// Layout Mode: renders the level like an editor's layout view - every decorated
// object sprite is hidden and replaced with a flat, colour-coded square:
//   white = solids/slopes/breakables   red = hazards        purple = portals
//   yellow = pads                      cyan = rings/orbs    gold = coins
//   dark grey = decorations/modifiers/specials (drawn faint so blocks read first)
// Purely visual: physics and collision are untouched, so it is NOT a cheat.
//
// Implementation notes:
// - It does NOT rely on the engine's visible-object bookkeeping: the bindings'
//   updateVisibility is an address-less inline stub and m_visibleObjects is
//   undocumented native state. Instead every object is collected once at load
//   through the addObject hook (the same proven pattern the start-pos switcher
//   uses), kept sorted by X, and each frame a camera-rect range query runs over
//   it. convertToNodeSpace handles zoom, mirror mode and platformer cameras.
// - Objects are re-hidden every frame in postUpdate, which runs AFTER the
//   engine's own visibility pass, so section culling can never flicker.
// - Toggling off (or restarting) bulk-restores visibility; the engine re-culls
//   off-screen sections on the next frame. Hide-trigger'd objects may flash for
//   a moment mid-attempt; a restart fully normalises them.
// - Objects spawned mid-level by 2.2 spawn triggers are not in the load-time
//   list and stay decorated (documented limitation).
#include "state.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	bool layoutEnabled() {
		return Mod::get()->getSettingValue<bool>("layout-mode");
	}

	ccColor4F layoutColor(GameObject* o) {
		using T = GameObjectType;
		switch (o->getType()) {
			case T::Solid: case T::Slope: case T::Breakable: case T::CollisionObject:
				return ccc4f(0.92f, 0.92f, 0.96f, 0.85f);
			case T::Hazard: case T::AnimatedHazard:
				return ccc4f(1.f, 0.31f, 0.27f, 0.85f);
			case T::InverseGravityPortal: case T::NormalGravityPortal: case T::ShipPortal:
			case T::CubePortal: case T::BallPortal: case T::UfoPortal: case T::WavePortal:
			case T::RobotPortal: case T::SpiderPortal: case T::SwingPortal: case T::DualPortal:
			case T::SoloPortal: case T::TeleportPortal: case T::RegularSizePortal:
			case T::MiniSizePortal: case T::InverseMirrorPortal: case T::NormalMirrorPortal:
			case T::GravityTogglePortal:
				return ccc4f(0.75f, 0.35f, 1.f, 0.8f);
			case T::YellowJumpPad: case T::PinkJumpPad: case T::RedJumpPad:
			case T::GravityPad: case T::SpiderPad:
				return ccc4f(1.f, 0.82f, 0.24f, 0.9f);
			case T::YellowJumpRing: case T::PinkJumpRing: case T::RedJumpRing:
			case T::GravityRing: case T::GreenRing: case T::DropRing: case T::CustomRing:
			case T::DashRing: case T::GravityDashRing: case T::SpiderOrb: case T::TeleportOrb:
				return ccc4f(0.35f, 0.86f, 1.f, 0.9f);
			case T::SecretCoin: case T::UserCoin: case T::Collectible:
				return ccc4f(1.f, 0.78f, 0.16f, 0.95f);
			default: // Decoration, Modifier, Special, EnterEffectObject, ...
				return ccc4f(0.47f, 0.49f, 0.56f, 0.35f);
		}
	}

	// corners of an object in object-layer space, honouring position, anchor,
	// scale and rotation (rotated blocks and move/rotate triggers stay correct)
	void objectQuad(GameObject* o, CCPoint out[4]) {
		CCSize sz = o->getContentSize();
		CCPoint anc = o->getAnchorPoint();
		float w = sz.width * o->getScaleX();
		float h = sz.height * o->getScaleY();
		float x0 = -anc.x * w, y0 = -anc.y * h;
		CCPoint local[4] = { { x0, y0 }, { x0 + w, y0 }, { x0 + w, y0 + h }, { x0, y0 + h } };
		float rot = -o->getRotation() * (3.14159265358979323846f / 180.f); // cocos rotation is clockwise (Geode's cocos has no CCDegreesToRadians)
		float cs = std::cos(rot), sn = std::sin(rot);
		CCPoint pos = o->getPosition();
		for (int i = 0; i < 4; i++)
			out[i] = CCPoint(pos.x + local[i].x * cs - local[i].y * sn,
			                 pos.y + local[i].x * sn + local[i].y * cs); // explicit ctor: brace-assign is ambiguous (CCPoint/CCSize operator=)
	}
}

class $modify(LayoutPlayLayer, PlayLayer) {
	struct Fields {
		std::vector<Ref<GameObject>> objects; // everything addObject delivered
		bool sorted = false;
		bool wasOn = false;
		CCDrawNode* draw = nullptr;
	};

	void addObject(GameObject* object) {
		PlayLayer::addObject(object);
		if (object) {
			m_fields->objects.push_back(object);
			m_fields->sorted = false;
		}
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		if (m_fields->wasOn) restoreSprites(); // skip the bulk pass when layout was off
	}

	void restoreSprites() {
		for (auto& r : m_fields->objects)
			if (r) r->setVisible(true); // the engine re-culls off-screen sections next frame
		if (m_fields->draw) m_fields->draw->clear();
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		bool on = layoutEnabled();
		if (on != m_fields->wasOn) {
			if (m_fields->wasOn) restoreSprites(); // only the on->off edge needs the bulk restore
			m_fields->wasOn = on;
		}
		if (!on) return;

		if (!m_fields->sorted) {
			std::stable_sort(m_fields->objects.begin(), m_fields->objects.end(),
				[](Ref<GameObject> const& a, Ref<GameObject> const& b) {
					return a->getPositionX() < b->getPositionX();
				});
			m_fields->sorted = true;
		}
		if (m_fields->objects.empty()) return;

		if (!m_fields->draw) {
			m_fields->draw = CCDrawNode::create();
			// every object sprite is hidden while layout mode runs, so any Z above
			// them works; the player lives on its own layer and stays on top
			m_objectLayer->addChild(m_fields->draw, 10000);
		}
		auto* dn = m_fields->draw;
		dn->clear(); // redrawn every frame: move/rotate/scale triggers keep changing transforms

		// camera rect in object-layer space (zoom / mirror / rotation aware)
		auto vs = CCDirector::sharedDirector()->getVisibleSize();
		CCPoint a = m_objectLayer->convertToNodeSpace({ 0.f, 0.f });
		CCPoint b = m_objectLayer->convertToNodeSpace({ vs.width, vs.height });
		float x0 = std::min(a.x, b.x) - 200.f, x1 = std::max(a.x, b.x) + 200.f;
		float y0 = std::min(a.y, b.y) - 200.f, y1 = std::max(a.y, b.y) + 200.f;

		auto& objs = m_fields->objects;
		auto first = std::lower_bound(objs.begin(), objs.end(), x0,
			[](Ref<GameObject> const& o, float v) { return o->getPositionX() < v; });
		for (auto it = first; it != objs.end(); it++) {
			auto* o = it->data();
			if (!o) continue;
			float ox = o->getPositionX();
			if (ox > x1) break; // sorted by X: nothing further can be in range
			float oy = o->getPositionY();
			if (oy < y0 || oy > y1) continue;
			if (!o->isVisible()) continue; // respect the engine's culling and hide triggers
			o->setVisible(false);          // after the engine's pass: stable, no flicker
			CCPoint q[4];
			objectQuad(o, q);
			auto c = layoutColor(o);
			dn->drawPolygon(q, 4, c, 0.f, c);
		}
	}
};
