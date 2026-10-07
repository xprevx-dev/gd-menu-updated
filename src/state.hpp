#pragma once
// Shared state + API used by every part of GDMenu.
#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

// Plain-data replay types + all serialization live in the Geode-free core so they
// can be unit-tested on a desktop compiler (see tests/).
#include "core/replay_io.hpp"
#include "core/clips.hpp"

using namespace geode::prelude;

// binding classes referenced in the API below (full definitions arrive through
// <Geode/Geode.hpp> / the per-TU modify includes)
class PlayLayer;
class GJBaseGameLayer;
class GJGameLevel;

// ---------------------------------------------------------------- bot
enum class BotState { Idle, Recording, Playing, Resuming };

struct BotData {
	BotState state = BotState::Idle;
	std::vector<BotInput> inputs;
	std::vector<FrameFix> fixes;
	size_t playIndex = 0;
	size_t fixIndex = 0;
	int levelID = 0;
	int resumeFrame = 0;     // frame to fast-forward to when resuming a session
	float lastPercent = 0.f;
	bool botInput = false;   // true while the bot itself is calling handleButton
	bool held[2][4] = {};    // [player][button] held according to the macro
	bool realHeld[2][4] = {};// [player][button] what the real player is physically holding
	bool viewHeld[2][4] = {};// [player][button] what the game is actually applying (HUD input viewer)
	std::string loadedName;  // replay file currently loaded (for UI)

	bool stepper = false;
	int pendingSteps = 0;
};
extern BotData g_bot;

// ---------------------------------------------------------------- hacks
struct HackData {
	bool noclip = false;
	bool speedhack = false;
	bool hitboxes = false;
	float speed = 1.f;
	// 2.4.1
	bool autoclick = false;
	float cps = 10.f;             // clicks per second
	bool safeMode = true;         // block progress/completions after a cheat was used this attempt
	bool cheatedAttempt = false;
	bool accuracy = false;        // noclip accuracy + deaths label (only shown if enabled)
	int accTicks = 0, accDeadTicks = 0, accDeaths = 0;
	bool accHitThisTick = false, accWasHit = false;
	// 2.7.0: cached setting flags for per-tick / per-collision code paths
	bool allPassable = false;     // fall through every solid
	bool jumpHack = false;        // infinite jumps (hold jump in mid-air)
	bool physicsBypass = false;   // fixed 240 tps regardless of render FPS
	bool forcePlatformer = false; // any level plays as platformer
	std::string keyConflict;      // "" or a warning shown in the Keys tab when two actions share a key
	std::vector<Keybind> kToggleStep, kStep, kNoclip, kHitbox, kSpeed, kSpPrev, kSpNext;
	std::vector<Ref<StartPosObject>> startPositions; // sorted by X
	int startPosIndex = -1;                           // -1 = level start
};
extern HackData g_hacks;

// how many GDMenu panels/popups are open (pause-menu auto-resume must not fire
// while the user is in the menu - defined in ui.cpp)
extern int g_menuOpenCount;

// ---------------------------------------------------------------- helpers
void notify(std::string const& msg, NotificationIcon icon = NotificationIcon::Info);

namespace bot {
	int frame();                    // current physics tick, 0 outside a level
	char const* stateName();
	ccColor3B stateColor();

	void startRecording();          // fresh recording from the start
	void stop();                    // stop recording / playback
	bool startPlayback();           // play the loaded inputs (restarts the level)
	void clear();

	// resume sessions (auto-saved when you quit while recording)
	bool hasSession(int levelID);
	bool sessionInfo(int levelID, float& percent, size_t& inputs);
	void saveSession();
	bool resumeSession();           // load session + fast-forward to where you left off
	void deleteSession(int levelID);

	void setTimeScale(float s);

	// every saved resume session on disk (Tools tab manager)
	struct SessionInfo {
		int levelID = 0;
		float percent = 0.f;
		size_t inputs = 0;
		uint64_t written = 0; // unix timestamp of last write
		std::filesystem::path path;
	};
	std::vector<SessionInfo> listSessions();
	void clearAllSessions();
}

namespace hud {
	bool enabled();          // in-game overlay setting
	bool showState();
	bool showFrame();
	bool showPercent();
	bool showSpeed();
	// run tracking for the Best Run / Run From counters (session-scoped, per level)
	void noteRunStart(float percent); // resetLevel: where this run began (start pos / checkpoint)
	void noteRunEnd(float percent);   // real death or completion: candidate for session best
}

// ---------------------------------------------------------------- clips
// Always-on attempt recording (inputs only -> no measurable cost). Implementation in
// clips.cpp; the ring buffer itself is core/clips.hpp (Geode-free, unit-tested).
namespace clips {
	bool enabled();                       // clips-count setting > 0
	size_t keep();                        // how many attempts the ring keeps (0..50)
	size_t count();                       // finalized clips for the current level
	gdm::Clip const* newest(size_t back); // 0 = most recent

	// capture lifecycle - called from the existing hooks, all no-ops when disabled
	void onLevelEnter(PlayLayer* pl);     // clears the ring when the level changes
	void onAttemptStart(PlayLayer* pl);   // resetLevel while idle (practice respawn = trim)
	void onInput(GJBaseGameLayer* gl, int frame, int button, bool down, bool player2);
	void onDeath(PlayLayer* pl, float percent);
	void onComplete(PlayLayer* pl);
	void onQuit(PlayLayer* pl);
	void onBotActive(bool active);        // finalize the open clip when a bot takes over

	// UI actions (Clips tab)
	bool watch(size_t back);              // load into the bot + play it back ("the video")
	bool save(size_t back);               // export as .gdr2 into the replay library
	bool saveCurrent();                   // export the LIVE attempt buffer (Bot tab button);
	                                      // falls back to the most recent finished clip
	void remove(size_t back);
	void clear();
}

namespace replays {
	struct Info {
		std::filesystem::path path;
		std::string name;
		std::string levelName;
		std::string author;
		size_t inputs = 0;
		float duration = 0.f;
		bool valid = false;
		uint64_t written = 0; // last-write time (sort key, same clock for all entries)
		uint64_t size = 0;    // bytes on disk
		bool hasPhys = false; // file carries per-input physics data
	};
	std::filesystem::path dir();    // save/geode/mods/cyber39dreamgd.gdmenu/replays
	std::vector<Info> list();
	bool exists(std::string const& name, std::string const& ext);
	bool save(std::string name, std::string const& ext, bool copyToEclipse); // ".gdr2" or ".gdbot" (identical layout)
	std::filesystem::path eclipseDir();  // save/geode/mods/eclipse.eclipse-menu/replays
	bool eclipseInstalled();
	bool load(std::filesystem::path const& path);
	bool remove(std::filesystem::path const& path);
}

namespace hacks {
	void reloadSettings();
	void toggleStepper();
	void setStepper(bool on);
	void stepFrames(int n);
	void toggleNoclip();
	void toggleSpeed();
	void setSpeed(float v);
	void toggleHitboxes();
	void switchStartPos(int dir);
	std::string startPosLabel();
	void updateStepperControls();   // show/hide touch step bar (only while stepper is ON)
	// force platformer mutates the shared GJGameLevel (m_levelLength = 5), so the
	// original value is tracked and MUST be restored when leaving the level
	void applyForcedPlatformer(GJGameLevel* level); // before PlayLayer::init
	void restoreForcedPlatformer();                 // from PlayLayer::onQuit (bot.cpp)
}

namespace extras {
	bool cheatsActive();          // noclip / speedhack / autoclick / stepper / bot playback
	void saveHackState();         // persist toggles
	void loadHackState();
	// themes
	int themeIndex();
	void setTheme(int i);
	int themeCount();
	char const* themeName(int i);
	ccColor3B accent();
	float bubbleOpacity();        // 0.2 - 1
	void setBubbleOpacity(float v);
	float bubbleSize();           // multiplier
	void setBubbleSize(float v);
	// lifetime usage counters (More tab)
	void bumpStat(char const* key);
	int64_t stat(char const* key);
	// profiles (3 slots)
	std::string profileName(int slot);
	bool profileExists(int slot);
	void saveProfile(int slot);
	bool loadProfile(int slot);
}

namespace practice {
	bool applyPending(PlayLayer* pl);
}
