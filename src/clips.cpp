// Always-on attempt clips ("watch your last run"): a bounded ring of recent attempts,
// recorded passively while you play. Input events only - zero per-tick cost, so this can
// stay on all the time. Ring logic lives in src/core/clips.hpp (unit-tested).
//
// Hook ownership: this file owns NO hooks. The existing hooks call into it:
//   BotGameLayer::handleButton   -> clips::onInput        (bot.cpp)
//   BotPlayLayer::init/resetLevel/levelComplete/onQuit -> lifecycle (bot.cpp)
//   ExtrasPlayLayer::destroyPlayer -> clips::onDeath      (extras.cpp)
//   setState() -> clips::onBotActive                      (bot.cpp)
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJAccountManager.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>

namespace {
	gdm::ClipRing s_ring;
	bool s_openValid = false;      // an attempt is currently being captured
	gdm::Clip s_open;
	bool s_held[2][4] = {};        // dedupe key-repeat, mirrors g_bot.held semantics
	int s_levelID = -1;

	// CBS paused for the open clip (only with the clips-pause-cbs setting on)
	bool s_cbsPaused = false;
	bool s_cbsPrev = false;
	GJBaseGameLayer* s_cbsLayer = nullptr;

	uint64_t now() { return (uint64_t)std::time(nullptr); }

	bool cbfLoaded() { return Loader::get()->isModLoaded("syzzi.click_between_frames"); }

	void restoreClipCBS(PlayLayer* pl) {
		if (!s_cbsPaused) return;
		if (pl && static_cast<GJBaseGameLayer*>(pl) == s_cbsLayer) pl->m_clickBetweenSteps = s_cbsPrev;
		s_cbsPaused = false;
		s_cbsLayer = nullptr;
	}

	void finalizeClip(PlayLayer* pl, float percent, bool completed) {
		if (!s_openValid) return;
		restoreClipCBS(pl);
		s_openValid = false;
		s_open.percent = completed ? 100.f : std::max(s_open.percent, percent);
		s_open.completed = completed;
		s_open.endedAt = now();
		if (pl) s_open.frames = (uint32_t)pl->m_gameState.m_currentProgress;
		// attempts without a single input are noise (you died standing still / auto section)
		if (!s_open.inputs.empty()) {
			s_ring.setLimits(clips::keep(), 32ull << 20);
			s_ring.add(std::move(s_open));
		}
		s_open = {};
	}
}

bool clips::enabled() { return keep() > 0; }

size_t clips::keep() {
	return (size_t)std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("clips-count"), 0, 50);
}

size_t clips::count() { return s_ring.size(); }

gdm::Clip const* clips::newest(size_t back) { return s_ring.newest(back); }

void clips::onLevelEnter(PlayLayer* pl) {
	if (!pl) return;
	int id = pl->m_level->m_levelID.value();
	if (id != s_levelID) {
		// clips are only meaningful inside the level they were recorded in
		restoreClipCBS(nullptr); // old layer is gone: drop bookkeeping, don't touch memory
		s_openValid = false;
		s_open = {};
		s_ring.clear();
		s_levelID = id;
	}
	s_ring.setLimits(keep(), 32ull << 20);
}

void clips::onAttemptStart(PlayLayer* pl) {
	if (!pl) return;

	// practice checkpoint respawn: the attempt CONTINUES - trim everything at/after the
	// respawn frame (same rule as bot recording) instead of finalizing
	if (s_openValid && s_open.practice && pl->m_isPracticeMode &&
	    pl->m_checkpointArray && pl->m_checkpointArray->count() > 0) {
		int frame = (int)pl->m_gameState.m_currentProgress;
		std::erase_if(s_open.inputs, [&](gdm::ClipInput const& i) { return (int)i.frame >= frame; });
		// vanilla re-applies what you're physically holding after a respawn; record any
		// change once here so the dedupe below doesn't eat the natural re-press
		for (int p = 0; p < 2; p++)
			for (int b = 1; b <= 3; b++) {
				bool want = g_bot.realHeld[p][b];
				if (s_held[p][b] == want) continue;
				s_held[p][b] = want;
				if (b == 1 || pl->m_isPlatformer)
					s_open.inputs.push_back({ (uint32_t)frame, (uint8_t)b, want, p == 1 });
			}
		s_open.frames = (uint32_t)std::max(0, frame);
		return;
	}

	// any other reset while a clip is open = the previous attempt ended without a death
	// (retry from the pause menu, start-pos switch...): keep it, then start fresh
	if (s_openValid) finalizeClip(pl, pl->getCurrentPercent(), false);
	if (!enabled()) return;

	s_open = gdm::Clip{};
	s_open.levelID = pl->m_level->m_levelID.value();
	s_open.levelName = std::string(pl->m_level->m_levelName);
	s_open.attempt = pl->m_attempts;
	s_open.startedAt = now();
	s_open.practice = pl->m_isPracticeMode;
	s_openValid = true;
	std::memset(s_held, 0, sizeof(s_held));

	// Optional: force vanilla CBS off while clips record so replays are frame-perfect.
	// Default OFF - this is the user's vanilla setting and we don't hijack it silently.
	restoreClipCBS(pl);
	if (Mod::get()->getSettingValue<bool>("clips-pause-cbs") && pl->m_clickBetweenSteps) {
		s_cbsPrev = pl->m_clickBetweenSteps;
		s_cbsLayer = pl;
		pl->m_clickBetweenSteps = false;
		s_cbsPaused = true;
	}
	// subframe input was possible during this attempt when CBS is on (unless we just
	// paused it) or CBF is installed - watching such a clip can drift by <1 tick
	s_open.subframe = (s_cbsPaused ? false : pl->m_clickBetweenSteps) || cbfLoaded();
}

void clips::onInput(GJBaseGameLayer* gl, int frame, int button, bool down, bool player2) {
	if (g_bot.state != BotState::Idle || g_bot.botInput) return;
	if (button < 1 || button > 3) return;
	auto pl = PlayLayer::get();
	if (!pl || static_cast<GJBaseGameLayer*>(pl) != gl) return;
	if (!enabled() && !s_openValid) return;
	if (!s_openValid) clips::onAttemptStart(pl); // lazily start (setting flipped mid-level)
	if (!s_openValid) return;
	if (button != 1 && !pl->m_isPlatformer) return; // GDR2 can't express it; matches bot recording
	if (!pl->m_player1 || pl->m_player1->m_isDead) return;

	// CBS can be flipped on between attempts (options menu): flag, never unflag
	if (!s_open.subframe && (pl->m_clickBetweenSteps || cbfLoaded())) s_open.subframe = true;

	bool& h = s_held[player2 ? 1 : 0][button];
	if (h == down) return; // key-repeat duplicate
	h = down;
	s_open.inputs.push_back({ (uint32_t)std::max(0, frame), (uint8_t)button, down, player2 });
	s_open.frames = (uint32_t)std::max(0, frame);
}

void clips::onDeath(PlayLayer* pl, float percent) {
	if (!s_openValid || !pl) return;
	if (pl->m_isPracticeMode) {
		// practice deaths respawn at a checkpoint: the attempt continues
		s_open.percent = std::max(s_open.percent, percent);
		return;
	}
	finalizeClip(pl, percent, false);
}

void clips::onComplete(PlayLayer* pl) { finalizeClip(pl, 100.f, true); }

void clips::onQuit(PlayLayer* pl) {
	if (!s_openValid) return;
	finalizeClip(pl, pl ? pl->getCurrentPercent() : s_open.percent, false);
}

void clips::onBotActive(bool active) {
	// A bot taking over ends the passive clip (the bot's own macro is not "your attempt").
	// Called BEFORE the bot's CBS management so the two never stack paused states.
	if (active) finalizeClip(PlayLayer::get(), s_open.percent, false);
}

// ---------------------------------------------------------------- UI actions
bool clips::watch(size_t back) {
	auto* c = s_ring.newest(back);
	if (!c) return false;
	if (c->inputs.empty()) { notify("That clip has no inputs", NotificationIcon::Warning); return false; }
	if (g_bot.state != BotState::Idle) { notify("Stop the bot first", NotificationIcon::Warning); return false; }
	if (!PlayLayer::get()) { notify("Open that level first", NotificationIcon::Warning); return false; }

	bot::stop(); // idle: cheap safety
	g_bot.inputs.clear();
	g_bot.fixes.clear();
	g_bot.inputs.reserve(c->inputs.size());
	for (auto& i : c->inputs) {
		BotInput in{ (int)i.frame, (int)i.button, i.down, i.player2 };
		g_bot.inputs.push_back(in);
	}
	g_bot.loadedName = fmt::format("Clip: attempt {} ({:.0f}%){}", c->attempt, c->percent,
		c->completed ? " COMPLETE" : "");
	if (c->subframe)
		notify("This attempt used CBS/CBF - replay timing may drift", NotificationIcon::Warning);
	return bot::startPlayback();
}

// Export one clip as a standard .gdr2 into the replay library (+ Eclipse copy).
// Shared by Clips-tab Save (finished clips) and Bot-tab Save Attempt (live buffer).
static bool exportClip(PlayLayer* pl, gdm::Clip const& c) {
	gdm::GDMReplay r;
	r.author = std::string(GJAccountManager::get()->m_username);
	r.description = c.completed ? "Completed attempt (GDMenu clip)"
	                            : fmt::format("Attempt clip - reached {:.1f}%", c.percent);
	r.gameVersion = GEODE_COMP_GD_VERSION;
	r.framerate = 240.0;
	r.levelInfo.id = c.levelID;
	r.levelInfo.name = c.levelName;
	r.platformer = pl->m_level->isPlatformer();
	r.ldm = pl->m_level->m_lowDetailModeToggled;
	uint64_t last = 0;
	for (auto& i : c.inputs) {
		if (!r.platformer && i.button != 1) continue; // GDR2 can't express left/right here
		// no per-input physics in clips: NaN tells readers (and our own loader) to skip fixes
		r.inputs.emplace_back((uint64_t)i.frame, (uint8_t)i.button, i.player2, i.down, NAN, NAN, 0.f, 0.f, 0.f);
		last = std::max<uint64_t>(last, (uint64_t)i.frame);
	}
	if (r.inputs.empty()) { notify("Nothing exportable in that clip", NotificationIcon::Warning); return false; }
	r.duration = (float)(last / r.framerate);
	r.sortInputs();

	auto data = r.exportData();
	if (data.isErr()) { notify("Save failed: " + data.unwrapErr(), NotificationIcon::Error); return false; }
	auto& bytes = data.unwrap();

	std::string base = gdm::sanitizeFileName(fmt::format("{} a{} {:.0f}pct", c.levelName, c.attempt, c.percent));
	if (base.empty()) base = "clip";
	std::string name = base;
	for (int n = 2; replays::exists(name, ".gdr2"); n++) name = fmt::format("{} ({})", base, n);

	auto path = replays::dir() / (name + ".gdr2");
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) { notify("Couldn't write file", NotificationIcon::Error); return false; }
	f.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
	f.close();
	extras::bumpStat("saves");

	if (replays::eclipseInstalled()) {
		std::error_code ec;
		std::filesystem::create_directories(replays::eclipseDir(), ec);
		auto epath = replays::eclipseDir() / (name + ".gdr2");
		std::ofstream ef(epath, std::ios::binary | std::ios::trunc);
		if (ef) {
			ef.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
			notify("Saved " + name + ".gdr2 + copied to Eclipse", NotificationIcon::Success);
			return true;
		}
	}
	notify("Saved " + name + ".gdr2", NotificationIcon::Success);
	return true;
}

bool clips::save(size_t back) {
	auto* c = s_ring.newest(back);
	auto pl = PlayLayer::get();
	if (!c || !pl) return false;
	if (c->inputs.empty()) { notify("That clip has no inputs", NotificationIcon::Warning); return false; }
	return exportClip(pl, *c);
}

bool clips::saveCurrent() {
	auto pl = PlayLayer::get();
	if (!pl) { notify("Open a level first", NotificationIcon::Warning); return false; }
	// the live attempt buffer: works MID-RUN from the pause menu ("save my 63% so far").
	// Snapshot metadata only - the attempt itself keeps recording, nothing is finalized.
	if (s_openValid && !s_open.inputs.empty()) {
		gdm::Clip snap = s_open; // copies the input list (a few KB, once per button press)
		snap.percent = std::max(snap.percent, pl->getCurrentPercent());
		snap.frames = (uint32_t)pl->m_gameState.m_currentProgress;
		snap.endedAt = now();
		return exportClip(pl, snap);
	}
	if (auto* c = s_ring.newest(0)) return exportClip(pl, *c); // most recent finished attempt
	notify("No attempt recorded yet - play (and press something) first", NotificationIcon::Warning);
	return false;
}

void clips::remove(size_t back) { s_ring.removeNewest(back); }
void clips::clear() { s_ring.clear(); }
