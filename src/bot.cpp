// Bot: recording, playback, resume sessions and GDR2 (.gdr2 / .gdbot) replay files.
// All byte-level serialization lives in src/core/replay_io.hpp (unit-tested there).
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <span>

BotData g_bot;

void notify(std::string const& msg, NotificationIcon icon) {
	Notification::create(msg, icon, 1.4f)->show();
}

// ---------------------------------------------------------------- helpers
namespace {
	// Click Between Frames makes inputs land *between* physics ticks, so a replay (which is
	// tick-based) can't reproduce them exactly -> random desyncs/deaths. Eclipse soft-disables
	// it while the bot is active; we do the same and restore it afterwards.
	constexpr auto CBF_ID = "syzzi.click_between_frames";
	bool s_cbfTouched = false;
	bool s_cbfPrev = false;

	void updateCBF(bool botActive) {
		auto cbf = Loader::get()->getLoadedMod(CBF_ID);
		if (!cbf) return;
		if (botActive && !s_cbfTouched) {
			s_cbfPrev = cbf->getSettingValue<bool>("soft-toggle");
			cbf->setSettingValue<bool>("soft-toggle", true);
			s_cbfTouched = true;
		}
		else if (!botActive && s_cbfTouched) {
			cbf->setSettingValue<bool>("soft-toggle", s_cbfPrev);
			s_cbfTouched = false;
		}
	}

	void setState(BotState st) {
		g_bot.state = st;
		updateCBF(st != BotState::Idle);
	}

	bool isTwoPlayer(GJBaseGameLayer* gl) {
		return gl->m_levelSettings && gl->m_levelSettings->m_twoPlayerMode;
	}

	// GDR2 / Eclipse convention: handleButton's 3rd arg is "isPlayer1"; an input is player 2
	// only in a 2-player level while dual mode is active.
	void pressRaw(GJBaseGameLayer* gl, bool down, int button, bool player2) {
		g_bot.botInput = true;
		gl->handleButton(down, button, !player2);
		g_bot.botInput = false;
	}

	void releaseAll(GJBaseGameLayer* gl) {
		for (int b = 1; b <= 3; b++) {
			pressRaw(gl, false, b, false);
			pressRaw(gl, false, b, true);
		}
		std::memset(g_bot.held, 0, sizeof(g_bot.held));
	}

	PlayerObject* playerFor(GJBaseGameLayer* gl, bool player2) {
		return (player2 && gl->m_player2) ? gl->m_player2 : gl->m_player1;
	}

	BotInput makeInput(GJBaseGameLayer* gl, int frame, int button, bool down, bool player2) {
		BotInput in{ frame, button, down, player2 };
		if (auto pl = playerFor(gl, player2)) {
			in.phys = true;
			in.x = pl->m_position.x;
			in.y = pl->m_position.y;
			in.rot = pl->getRotation();
			in.xVel = pl->m_platformerXVelocity;
			in.yVel = pl->m_yVelocity;
		}
		return in;
	}

	bool inputFixEnabled() {
		return Mod::get()->getSettingValue<bool>("input-fix");
	}

	void applyPhys(GJBaseGameLayer* gl, BotInput const& in) {
		if (!in.phys || !inputFixEnabled()) return;
		auto pl = playerFor(gl, in.player2);
		if (!pl || pl->m_isDead) return;
		pl->m_position = CCPoint(in.x, in.y);
		pl->setPosition(pl->m_position);
		pl->setRotation(in.rot);
		pl->m_yVelocity = in.yVel;
		if (gl->m_isPlatformer) pl->m_platformerXVelocity = in.xVel;
	}

	PlayerFix capture(PlayerObject* p) {
		return { p->m_position.x, p->m_position.y, p->getRotation(), p->m_platformerXVelocity, p->m_yVelocity };
	}
	void restore(GJBaseGameLayer* gl, PlayerObject* p, PlayerFix const& f) {
		if (!p || p->m_isDead) return;
		p->m_position = CCPoint(f.x, f.y);
		p->setPosition(p->m_position);
		p->setRotation(f.rot);
		p->m_yVelocity = f.yVel;
		if (gl->m_isPlatformer) p->m_platformerXVelocity = f.xVel;
	}

	std::filesystem::path fixesPath(int levelID) {
		return Mod::get()->getSaveDir() / "sessions" / fmt::format("{}.gdmf", levelID);
	}

	std::filesystem::path sessionPath(int levelID) {
		return Mod::get()->getSaveDir() / "sessions" / fmt::format("{}.gdm", levelID);
	}

	bool readSessionFile(int levelID, gdm::Session& out) {
		std::ifstream f(sessionPath(levelID), std::ios::binary);
		if (!f) return false;
		return gdm::readSession(f, out);
	}

	std::string lower(std::string s) {
		for (auto& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	bool readFile(std::filesystem::path const& p, std::vector<uint8_t>& out) {
		std::ifstream f(p, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}
}

int bot::frame() {
	auto pl = PlayLayer::get();
	return pl ? (int)pl->m_gameState.m_currentProgress : 0;
}

char const* bot::stateName() {
	switch (g_bot.state) {
		case BotState::Recording: return "RECORDING";
		case BotState::Playing:   return "PLAYING";
		case BotState::Resuming:  return "RESUMING";
		default:                  return "IDLE";
	}
}

ccColor3B bot::stateColor() {
	switch (g_bot.state) {
		case BotState::Recording: return { 255, 90, 90 };
		case BotState::Playing:   return { 90, 255, 120 };
		case BotState::Resuming:  return { 255, 210, 80 };
		default:                  return { 200, 200, 200 };
	}
}

void bot::setTimeScale(float s) {
	CCDirector::get()->getScheduler()->setTimeScale(s);
}

void bot::startRecording() {
	auto pl = PlayLayer::get();
	if (!pl) return;
	g_bot.inputs.clear();
	g_bot.fixes.clear();
	g_bot.loadedName.clear();
	setState(BotState::Recording);
	std::memset(g_bot.held, 0, sizeof(g_bot.held));
	pl->resetLevel();
	notify("Recording started", NotificationIcon::Success);
}

void bot::stop() {
	auto was = g_bot.state;
	setState(BotState::Idle);
	bot::setTimeScale(1.f);
	g_bot.pendingSteps = 0;
	if (auto pl = PlayLayer::get()) releaseAll(pl);
	if (was == BotState::Recording) notify(fmt::format("Recording stopped ({} inputs)", g_bot.inputs.size()));
	else if (was != BotState::Idle) notify("Playback stopped");
}

bool bot::startPlayback() {
	auto pl = PlayLayer::get();
	if (!pl) return false;
	if (g_bot.inputs.empty()) { notify("No bot loaded - open the Bots tab", NotificationIcon::Warning); return false; }
	std::stable_sort(g_bot.inputs.begin(), g_bot.inputs.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
	setState(BotState::Playing);
	g_bot.playIndex = 0;
	pl->resetLevel();
	notify("Playing bot", NotificationIcon::Success);
	return true;
}

void bot::clear() {
	bot::stop();
	g_bot.inputs.clear();
	g_bot.fixes.clear();
	g_bot.loadedName.clear();
}

// ---------------------------------------------------------------- sessions
void bot::saveSession() {
	auto pl = PlayLayer::get();
	if (!pl || g_bot.inputs.empty()) return;
	auto path = sessionPath(g_bot.levelID);
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) return;
	gdm::writeSession(f, bot::frame(), (float)pl->getCurrentPercent(), g_bot.inputs);
	std::ofstream ff(fixesPath(g_bot.levelID), std::ios::binary | std::ios::trunc);
	if (ff) gdm::writeFixes(ff, g_bot.fixes);
}

bool bot::hasSession(int levelID) {
	return std::filesystem::exists(sessionPath(levelID));
}

bool bot::sessionInfo(int levelID, float& percent, size_t& inputs) {
	gdm::Session s;
	if (!readSessionFile(levelID, s)) return false;
	percent = s.percent;
	inputs = s.inputs.size();
	return true;
}

void bot::deleteSession(int levelID) {
	std::error_code ec;
	std::filesystem::remove(fixesPath(levelID), ec);
	std::filesystem::remove(sessionPath(levelID), ec);
}

std::vector<bot::SessionInfo> bot::listSessions() {
	std::vector<SessionInfo> out;
	auto dir = Mod::get()->getSaveDir() / "sessions";
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
		if (!e.is_regular_file() || lower(e.path().extension().string()) != ".gdm") continue;
		int id = 0;
		try { id = std::stoi(e.path().stem().string()); }
		catch (...) { continue; }
		gdm::Session s;
		{
			std::ifstream f(e.path(), std::ios::binary);
			if (!f || !gdm::readSession(f, s)) continue; // corrupt session: skip, never crash
		}
		SessionInfo info;
		info.levelID = id;
		info.path = e.path();
		info.percent = s.percent;
		info.inputs = s.inputs.size();
		info.written = (uint64_t)e.last_write_time(ec).time_since_epoch().count();
		out.push_back(std::move(info));
	}
	std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.written > b.written; });
	return out;
}

void bot::clearAllSessions() {
	auto dir = Mod::get()->getSaveDir() / "sessions";
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
		auto ext = lower(e.path().extension().string());
		if (ext == ".gdm" || ext == ".gdmf") std::filesystem::remove(e.path(), ec);
	}
}

bool bot::resumeSession() {
	auto pl = PlayLayer::get();
	if (!pl) return false;
	gdm::Session s;
	if (!readSessionFile(g_bot.levelID, s) || s.resumeFrame <= 0) {
		notify("No session to resume", NotificationIcon::Warning);
		return false;
	}
	g_bot.resumeFrame = s.resumeFrame;
	g_bot.lastPercent = s.percent;
	g_bot.inputs = std::move(s.inputs);
	std::erase_if(g_bot.inputs, [](BotInput const& i) { return i.frame > g_bot.resumeFrame; });
	{
		std::ifstream ff(fixesPath(g_bot.levelID), std::ios::binary);
		if (ff) gdm::readFixes(ff, g_bot.fixes);
		else g_bot.fixes.clear();
		std::erase_if(g_bot.fixes, [](FrameFix const& x) { return x.frame > g_bot.resumeFrame; });
	}
	g_bot.fixIndex = 0;
	setState(BotState::Resuming);
	g_bot.playIndex = 0;
	bot::setTimeScale((float)Mod::get()->getSettingValue<double>("resume-speed"));
	pl->resetLevel();
	notify(fmt::format("Resuming to {:.1f}%...", g_bot.lastPercent));
	return true;
}

// ---------------------------------------------------------------- replay files
std::filesystem::path replays::dir() {
	auto d = Mod::get()->getSaveDir() / "replays";
	std::error_code ec;
	std::filesystem::create_directories(d, ec);
	return d;
}

std::vector<replays::Info> replays::list() {
	// Parsing every replay on each open is slow once the library grows, so parsed results
	// are cached per file and only re-read when the file's size or write time changes.
	static std::map<std::filesystem::path, Info> s_cache;
	std::vector<Info> out;
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir(), ec)) {
		if (!e.is_regular_file()) continue;
		auto ext = lower(e.path().extension().string());
		if (ext != ".gdr2" && ext != ".gdbot" && ext != ".gdr") continue;
		Info info;
		info.path = e.path();
		info.name = e.path().filename().string();
		info.size = (uint64_t)e.file_size(ec);
		info.written = (uint64_t)e.last_write_time(ec).time_since_epoch().count();
		auto it = s_cache.find(info.path);
		if (it != s_cache.end() && it->second.size == info.size && it->second.written == info.written) {
			info.levelName = it->second.levelName;
			info.author = it->second.author;
			info.inputs = it->second.inputs;
			info.duration = it->second.duration;
			info.valid = it->second.valid;
			info.hasPhys = it->second.hasPhys;
		}
		else {
			std::vector<uint8_t> bytes;
			if (readFile(e.path(), bytes)) {
				// safeImport: a corrupt/truncated file becomes "invalid" instead of a crash
				auto res = gdm::safeImport(std::span<uint8_t>(bytes));
				if (res.isOk()) {
					auto& r = res.unwrap();
					info.valid = true;
					info.inputs = r.inputs.size();
					info.levelName = r.levelInfo.name;
					info.author = r.author;
					info.duration = r.duration;
					info.hasPhys = !r.inputs.empty() && !std::isnan(r.inputs[0].xPosition);
				}
			}
			s_cache[info.path] = info;
		}
		out.push_back(std::move(info));
	}
	for (auto it = s_cache.begin(); it != s_cache.end();) {
		bool gone = std::none_of(out.begin(), out.end(), [&](auto& i) { return i.path == it->first; });
		if (gone) it = s_cache.erase(it);
		else ++it;
	}
	std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return lower(a.name) < lower(b.name); });
	return out;
}

bool replays::exists(std::string const& name, std::string const& ext) {
	return std::filesystem::exists(dir() / (gdm::sanitizeFileName(name) + ext));
}

std::filesystem::path replays::eclipseDir() {
	return dirs::getModsSaveDir() / "eclipse.eclipse-menu" / "replays";
}

bool replays::eclipseInstalled() {
	return Loader::get()->isModLoaded("eclipse.eclipse-menu");
}

bool replays::save(std::string name, std::string const& ext, bool copyToEclipse) {
	if (g_bot.inputs.empty()) { notify("Nothing to save - record first", NotificationIcon::Warning); return false; }

	gdm::GDMReplay r;
	r.author = std::string(GJAccountManager::get()->m_username);
	r.description = "Recorded with GDMenu";
	r.gameVersion = GEODE_COMP_GD_VERSION;
	r.framerate = 240.0;
	if (auto pl = PlayLayer::get()) {
		r.levelInfo.id = pl->m_level->m_levelID.value();
		r.levelInfo.name = std::string(pl->m_level->m_levelName);
		r.platformer = pl->m_level->isPlatformer();
		r.ldm = pl->m_level->m_lowDetailModeToggled;
	}
	uint64_t last = 0;
	for (auto& i : g_bot.inputs) {
		if (!r.platformer && i.button != 1) continue; // can't be represented in non-platformer GDR2
		// inputs without physics (old recordings) get NaN so readers know to ignore them
		r.inputs.emplace_back((uint64_t)std::max(0, i.frame), (uint8_t)i.button, i.player2, i.down,
			i.phys ? i.x : NAN, i.phys ? i.y : NAN, i.rot, i.xVel, i.yVel);
		last = std::max<uint64_t>(last, (uint64_t)std::max(0, i.frame));
	}
	r.duration = (float)(last / r.framerate);
	r.fixes = g_bot.fixes;
	r.sortInputs();

	auto data = r.exportData();
	if (data.isErr()) { notify("Save failed: " + data.unwrapErr(), NotificationIcon::Error); return false; }
	auto path = dir() / (gdm::sanitizeFileName(name) + ext);
	auto& bytes = data.unwrap();
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) { notify("Couldn't write file", NotificationIcon::Error); return false; }
	f.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
	f.close();
	g_bot.loadedName = path.filename().string();

	if (copyToEclipse) {
		// Eclipse only lists .gdr2/.gdr files in ITS OWN folder, so drop a .gdr2 copy there
		std::error_code ec;
		std::filesystem::create_directories(eclipseDir(), ec);
		auto epath = eclipseDir() / (gdm::sanitizeFileName(name) + ".gdr2");
		std::ofstream ef(epath, std::ios::binary | std::ios::trunc);
		if (ef) {
			ef.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
			notify("Saved " + g_bot.loadedName + " + copied to Eclipse", NotificationIcon::Success);
			return true;
		}
		notify("Saved, but couldn't copy to Eclipse's folder", NotificationIcon::Warning);
		return true;
	}
	notify("Saved " + g_bot.loadedName, NotificationIcon::Success);
	return true;
}

bool replays::load(std::filesystem::path const& path) {
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) { notify("Couldn't open file", NotificationIcon::Error); return false; }
	auto res = gdm::safeImport(std::span<uint8_t>(bytes));
	if (res.isErr()) { notify("Not a GDR2 replay: " + res.unwrapErr(), NotificationIcon::Error); return false; }
	auto& r = res.unwrap();

	bot::stop();
	g_bot.inputs.clear();
	g_bot.fixes.clear();
	g_bot.inputs.reserve(r.inputs.size());
	for (auto& i : r.inputs)
	{
		BotInput in{ (int)i.frame, i.button == 0 ? 1 : (int)i.button, i.down, i.player2 };
		if (!std::isnan(i.xPosition) && !std::isnan(i.yPosition)) {
			in.phys = true;
			in.x = i.xPosition; in.y = i.yPosition; in.rot = i.rotation;
			in.xVel = i.xVelocity; in.yVel = i.yVelocity;
		}
		g_bot.inputs.push_back(in);
	}
	std::stable_sort(g_bot.inputs.begin(), g_bot.inputs.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
	g_bot.fixes = r.fixes;
	g_bot.loadedName = path.filename().string();
	notify(fmt::format("Loaded {} ({} inputs)", g_bot.loadedName, g_bot.inputs.size()), NotificationIcon::Success);
	return true;
}

bool replays::remove(std::filesystem::path const& path) {
	std::error_code ec;
	bool ok = std::filesystem::remove(path, ec);
	if (ok && path.filename().string() == g_bot.loadedName) g_bot.loadedName.clear();
	return ok;
}

// ---------------------------------------------------------------- hooks
class $modify(BotGameLayer, GJBaseGameLayer) {
	// Timing matches Eclipse exactly so files are interchangeable:
	//   record:   in handleButton, frame = m_currentProgress
	//   playback: right AFTER processCommands, fire every input with frame <= m_currentProgress
	void handleButton(bool down, int button, bool isPlayer1) {
		auto pl = PlayLayer::get();
		bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;
		if (mine && !g_bot.botInput && (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming))
			return; // the bot is driving: ignore the real player

		bool player2 = isTwoPlayer(this) && m_gameState.m_isDualMode && !isPlayer1;
		BotInput captured = makeInput(this, (int)m_gameState.m_currentProgress, button, down, player2);

		GJBaseGameLayer::handleButton(down, button, isPlayer1);

		if (!mine || g_bot.botInput || button < 1 || button > 3) return;
		g_bot.realHeld[player2][button] = down;

		if (g_bot.state != BotState::Recording) return;
		// GDR2 doesn't store the button in non-platformer levels (everything reads back as JUMP),
		// so recording left/right there turns into phantom jumps in Eclipse and on reload.
		if (button != 1 && !m_isPlatformer) return;
		if (!m_player1 || m_player1->m_isDead) return;

		bool& held = g_bot.held[player2][button];
		if (held == down) return; // skip key-repeat duplicates
		held = down;
		g_bot.inputs.push_back(captured);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

		auto pl = PlayLayer::get();
		if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return;
		int frame = (int)m_gameState.m_currentProgress;

		// recording: remember exactly where the player is after every tick
		if (g_bot.state == BotState::Recording) {
			if (!m_player1 || m_player1->m_isDead) return;
			FrameFix x;
			x.frame = frame;
			x.p1 = capture(m_player1);
			x.hasP2 = m_gameState.m_isDualMode && m_player2;
			if (x.hasP2) x.p2 = capture(m_player2);
			if (!g_bot.fixes.empty() && g_bot.fixes.back().frame >= frame) {
				// shouldn't happen, but keep the list strictly increasing
				std::erase_if(g_bot.fixes, [&](FrameFix const& f) { return f.frame >= frame; });
			}
			g_bot.fixes.push_back(x);
			return;
		}

		if (g_bot.state != BotState::Playing && g_bot.state != BotState::Resuming) return;

		// playback: put the player exactly on the recorded path for this tick
		if (inputFixEnabled()) {
			while (g_bot.fixIndex < g_bot.fixes.size() && g_bot.fixes[g_bot.fixIndex].frame < frame) g_bot.fixIndex++;
			if (g_bot.fixIndex < g_bot.fixes.size() && g_bot.fixes[g_bot.fixIndex].frame == frame) {
				auto& x = g_bot.fixes[g_bot.fixIndex];
				restore(this, m_player1, x.p1);
				if (x.hasP2 && m_gameState.m_isDualMode) restore(this, m_player2, x.p2);
			}
		}

		bool twoP = isTwoPlayer(this);
		while (g_bot.playIndex < g_bot.inputs.size() && g_bot.inputs[g_bot.playIndex].frame <= frame) {
			auto& in = g_bot.inputs[g_bot.playIndex++];
			if (in.player2 && !twoP) continue;
			applyPhys(this, in);
			pressRaw(this, in.down, in.button, in.player2);
		}

		// "Stop Playback At %": bail out automatically, handy for drilling one section
		if (g_bot.state == BotState::Playing) {
			float stopPct = (float)Mod::get()->getSettingValue<double>("stop-percent");
			if (stopPct > 0.f && pl->getCurrentPercent() >= stopPct) {
				bot::stop();
				notify(fmt::format("Playback stopped at {:.1f}%", stopPct));
				return;
			}
		}

		if (g_bot.state == BotState::Resuming && frame >= g_bot.resumeFrame) {
			// reached where you left off: hand control back and keep recording
			setState(BotState::Recording);
			bot::setTimeScale(1.f);
			releaseAll(this);
			// re-sync "held" with what the macro was holding so the next input is recorded correctly
			for (auto& in : g_bot.inputs) g_bot.held[in.player2][in.button] = in.down;
			for (int p = 0; p < 2; p++)
				for (int b = 1; b <= 3; b++)
					if (g_bot.held[p][b]) { g_bot.inputs.push_back(makeInput(this, frame, b, false, p == 1)); g_bot.held[p][b] = false; }
			hacks::setStepper(true); // freeze on the exact frame so you're not caught off guard
			notify(fmt::format("Resumed at {:.1f}% - step or turn off the stepper to continue", g_bot.lastPercent),
				NotificationIcon::Success);
		}
	}
};

class $modify(BotPlayLayer, PlayLayer) {
	struct Fields { float deadTime = 0.f; };

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		setState(BotState::Idle);
		g_bot.playIndex = 0;
		g_bot.stepper = false;
		g_bot.pendingSteps = 0;
		std::memset(g_bot.realHeld, 0, sizeof(g_bot.realHeld));
		std::memset(g_bot.held, 0, sizeof(g_bot.held));
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		int id = level->m_levelID.value();
		if (id != g_bot.levelID) { // keep a loaded replay when re-entering the same level
			g_bot.inputs.clear();
			g_bot.fixes.clear();
			g_bot.loadedName.clear();
		}
		g_bot.levelID = id;
		if (bot::hasSession(id))
			notify("Saved bot session found - Pause > GDMenu > Resume");
		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		practice::applyPending(this); // exact physics at the checkpoint (practice fix)
		int frame = (int)m_gameState.m_currentProgress;

		if (g_bot.state == BotState::Recording) {
			if (m_player1) m_player1->m_isDashing = false; // dash orbs otherwise carry over (Eclipse does this too)
			if (m_player2) m_player2->m_isDashing = false;

			// drop everything at/after the respawn point
			std::erase_if(g_bot.inputs, [&](BotInput const& i) { return i.frame >= frame; });
			std::erase_if(g_bot.fixes, [&](FrameFix const& x) { return x.frame > frame; });

			// What was the macro holding at this frame? In a straight playback run that's the state
			// the bot will be in here, so make the recording continue from exactly that state and
			// only add a press/release if what you're physically holding now is different.
			bool macroHeld[2][4] = {};
			for (auto& in : g_bot.inputs) macroHeld[in.player2][in.button] = in.down;
			bool twoP = isTwoPlayer(this);
			for (int p = 0; p < 2; p++) {
				if (p == 1 && !twoP) continue;
				for (int b = 1; b <= 3; b++) {
					if (b != 1 && !m_isPlatformer) continue;
					bool want = g_bot.realHeld[p][b];
					if (macroHeld[p][b] != want) {
						g_bot.inputs.push_back(makeInput(this, frame, b, want, p == 1));
						pressRaw(this, want, b, p == 1);
					}
					g_bot.held[p][b] = want;
				}
			}
		}
		else if (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming) {
			releaseAll(this);
			g_bot.fixIndex = 0;
			g_bot.playIndex = 0;
			while (g_bot.playIndex < g_bot.inputs.size() && g_bot.inputs[g_bot.playIndex].frame < frame)
				g_bot.playIndex++;
		}
	}

	// "Loop Playback": when the bot dies, restart the attempt automatically so you can
	// watch it run over and over. Resets before GD's death popup appears; if the popup
	// wins the race (m_isPaused), the normal retry path still resumes playback correctly.
	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		bool looping = g_bot.state == BotState::Playing && !m_isPaused &&
		               Mod::get()->getSettingValue<bool>("loop-playback");
		if (looping && m_player1 && m_player1->m_isDead) {
			m_fields->deadTime += dt;
			if (m_fields->deadTime > 0.8f) {
				m_fields->deadTime = 0.f;
				this->resetLevel();
			}
		}
		else m_fields->deadTime = 0.f;
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		if (g_bot.state == BotState::Recording) {
			bot::saveSession();
			setState(BotState::Idle);
			notify("Level complete! Pause > GDMenu > Save Bot to keep it", NotificationIcon::Success);
		}
		else if (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming) {
			setState(BotState::Idle);
			bot::setTimeScale(1.f);
		}
	}

	void onQuit() {
		// save where you were so you can come back and continue
		if (g_bot.state == BotState::Recording) bot::saveSession();
		setState(BotState::Idle);
		g_bot.stepper = false;
		bot::setTimeScale(1.f);
		PlayLayer::onQuit();
	}
};

// autosave the session every time you pause while recording (protects against crashes)
class $modify(BotAutosavePause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		if (g_bot.state == BotState::Recording) bot::saveSession();
	}
};
