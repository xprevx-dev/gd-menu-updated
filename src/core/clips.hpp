#pragma once
// Always-on attempt clips: a bounded ring of recent attempts.
// A clip stores INPUTS ONLY (a few bytes per key press, no per-tick cost), so GDMenu can
// "record the entire time" without touching performance. Watching a clip = feeding it to
// the bot, which replays the attempt live - that is the "video" of the run.
// Geode-free on purpose: the ring logic is unit-tested on a desktop compiler (tests/).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gdm {

struct ClipInput {
	uint32_t frame = 0;    // physics tick (240/s), same clock as BotInput
	uint8_t button = 1;    // 1 jump, 2 left, 3 right
	bool down = false;
	bool player2 = false;
};

struct Clip {
	int levelID = 0;
	std::string levelName;
	int attempt = 0;          // GD's attempt counter when the clip started
	uint64_t startedAt = 0;   // unix seconds
	uint64_t endedAt = 0;     // unix seconds (set when finalized)
	uint32_t frames = 0;      // duration in physics ticks
	float percent = 0.f;      // % reached
	bool completed = false;   // finished the level
	bool practice = false;    // recorded in practice mode
	bool subframe = false;    // CBS/CBF was active: replay timing can drift by <1 tick
	std::vector<ClipInput> inputs;

	size_t approxBytes() const {
		return sizeof(Clip) + levelName.size() + inputs.size() * sizeof(ClipInput);
	}
};

// Oldest-first vector with count AND memory caps; oldest clips are evicted first.
class ClipRing {
public:
	void setLimits(size_t maxClips, size_t maxBytes) {
		m_maxClips = maxClips;
		m_maxBytes = maxBytes ? maxBytes : 1;
		evict();
	}
	size_t maxClips() const { return m_maxClips; }
	size_t maxBytes() const { return m_maxBytes; }

	// Returns false when nothing was stored (ring disabled or clip evicted itself).
	bool add(Clip c) {
		if (m_maxClips == 0) return false;
		m_clips.push_back(std::move(c));
		evict();
		return !m_clips.empty();
	}

	size_t size() const { return m_clips.size(); }
	bool empty() const { return m_clips.empty(); }

	Clip const* at(size_t i) const { return i < m_clips.size() ? &m_clips[i] : nullptr; }
	Clip* at(size_t i) { return i < m_clips.size() ? &m_clips[i] : nullptr; }

	// "newest(back)": 0 = most recent clip, 1 = the one before it, ...
	Clip const* newest(size_t back) const { return back < m_clips.size() ? &m_clips[m_clips.size() - 1 - back] : nullptr; }
	Clip* newest(size_t back) { return back < m_clips.size() ? &m_clips[m_clips.size() - 1 - back] : nullptr; }
	void removeNewest(size_t back) {
		if (back < m_clips.size()) m_clips.erase(m_clips.begin() + (m_clips.size() - 1 - back));
	}

	void clear() { m_clips.clear(); }

	size_t totalBytes() const {
		size_t t = 0;
		for (auto& c : m_clips) t += c.approxBytes();
		return t;
	}

private:
	void evict() {
		// Never evict the clip that was just added, even if it alone exceeds the byte cap:
		// a pathological 20 MB attempt still gets kept (size > 1 guard), then the NEXT add
		// evicts it. Keeps add() from silently dropping what the caller just handed us.
		while (!m_clips.empty() &&
		       (m_clips.size() > m_maxClips || (totalBytes() > m_maxBytes && m_clips.size() > 1)))
			m_clips.erase(m_clips.begin());
	}

	std::vector<Clip> m_clips; // oldest first
	size_t m_maxClips = 5;
	size_t m_maxBytes = 32ull << 20; // 32 MB hard ceiling (hours of holding keys)
};

} // namespace gdm
