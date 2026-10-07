#pragma once
// ---------------------------------------------------------------------------
// GDMenu core: replay / session serialization + file-name sanitising.
//
// This header deliberately has NO Geode dependency (only the standard library
// and the header-only GDReplayFormat library), so every byte that enters or
// leaves this mod can be unit-tested and fuzzed on a plain desktop compiler.
// See tests/test_replay_io.cpp.
//
// Safety rule for everything in here: NEVER trust a count read from disk.
// A 100-byte file can claim 4 billion entries; reserving that many crashes
// the game before any validation runs. Every loop below is bounded by the
// number of bytes that are actually left in the source.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <istream>
#include <new>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gdr/gdr.hpp>

// ---------------------------------------------------------------- input/state PODs
// Plain data, shared with the Geode side through state.hpp.
struct BotInput {
	int frame = 0;   // physics tick (m_gameState.m_currentProgress) - same frame base Eclipse/xdBot use for GDR2
	int button = 1;  // 1 = jump, 2 = left, 3 = right
	bool down = false;
	bool player2 = false; // true ONLY for the second player in 2-player levels (GDR2 convention)
	// "Phys" extension (GDR2 PhysicsInput): player state right before the input.
	// Playback snaps the player to this, so tiny drift (e.g. from practice respawns) can't build up.
	bool phys = false;
	float x = 0.f, y = 0.f, rot = 0.f;
	double xVel = 0.0, yVel = 0.0;
};

// Per-tick player state recorded while recording; playback snaps to it every tick so the
// run follows the exact recorded path (kills practice-mode drift completely).
struct PlayerFix {
	float x = 0.f, y = 0.f, rot = 0.f;
	double xVel = 0.0, yVel = 0.0;
};
struct FrameFix {
	int frame = 0;
	PlayerFix p1, p2;
	bool hasP2 = false;
};

namespace gdm {

struct Session {
	int resumeFrame = 0;
	float percent = 0.f;
	std::vector<BotInput> inputs;
};

// ---------------------------------------------------------------- istream helpers
namespace detail {
	template <class T>
	bool readPod(std::istream& f, T& v) {
		f.read(reinterpret_cast<char*>(&v), sizeof(T));
		return (bool)f;
	}
	template <class T>
	void writePod(std::ostream& f, T const& v) {
		f.write(reinterpret_cast<char const*>(&v), sizeof(v));
	}
	// bytes left from the current read position; used to bound untrusted counts
	inline uint64_t remaining(std::istream& f) {
		auto cur = f.tellg();
		if (cur < 0) return 0;
		f.clear(f.rdstate() & ~std::ios::failbit);
		f.seekg(0, std::ios::end);
		auto end = f.tellg();
		f.seekg(cur);
		if (end < cur) return 0;
		return (uint64_t)(end - cur);
	}

	// session input record sizes (fixed layout, see writeSession)
	constexpr uint64_t SESSION_INPUT_V2 = 4 + 1 + 1 + 1;
	constexpr uint64_t SESSION_INPUT_V3 = SESSION_INPUT_V2 + 1 + 4 + 4 + 4 + 8 + 8;
	// fix record: i32 frame + u8 hasP2 + 2 * (3 floats + 2 doubles)
	constexpr uint64_t FIX_RECORD = 4 + 1 + 2 * (4 + 4 + 4 + 8 + 8);
}

// ---------------------------------------------------------------- sessions
// Binary layout (little-endian, unchanged since v2.4 so old sessions still load):
//   "GDMS" u32 version, i32 resumeFrame, f32 percent, u32 count,
//   then count * { i32 frame, u8 button, u8 down, u8 player2,
//                  [v3:] u8 phys, f32 x, f32 y, f32 rot, f64 xVel, f64 yVel }
inline void writeSession(std::ostream& f, int resumeFrame, float percent, std::vector<BotInput> const& inputs) {
	detail::writePod(f, uint32_t(3));
	detail::writePod(f, int32_t(resumeFrame));
	detail::writePod(f, float(percent));
	detail::writePod(f, uint32_t(inputs.size()));
	for (auto& i : inputs) {
		detail::writePod(f, int32_t(i.frame));
		detail::writePod(f, uint8_t(i.button));
		detail::writePod(f, uint8_t(i.down));
		detail::writePod(f, uint8_t(i.player2));
		detail::writePod(f, uint8_t(i.phys));
		detail::writePod(f, i.x);
		detail::writePod(f, i.y);
		detail::writePod(f, i.rot);
		detail::writePod(f, i.xVel);
		detail::writePod(f, i.yVel);
	}
}

// Returns false only if the file isn't a session at all. A truncated/forged file
// yields as many complete records as physically fit - never a crash, never a hang.
inline bool readSession(std::istream& f, Session& out) {
	out = Session{};
	char magic[4];
	f.read(magic, 4);
	if (!f || std::string_view(magic, 4) != "GDMS") return false;
	uint32_t ver = 0, n = 0;
	int32_t rf = 0;
	float pc = 0.f;
	if (!detail::readPod(f, ver) || !detail::readPod(f, rf) || !detail::readPod(f, pc) || !detail::readPod(f, n))
		return false;
	out.resumeFrame = rf;
	out.percent = pc;
	uint64_t per = (ver >= 3) ? detail::SESSION_INPUT_V3 : detail::SESSION_INPUT_V2;
	uint64_t count = std::min<uint64_t>(n, detail::remaining(f) / per);
	out.inputs.reserve((size_t)count);
	for (uint64_t k = 0; k < count; k++) {
		BotInput in;
		int32_t fr;
		uint8_t b, d, p;
		if (!detail::readPod(f, fr) || !detail::readPod(f, b) || !detail::readPod(f, d) || !detail::readPod(f, p)) break;
		in.frame = fr;
		in.button = b;
		in.down = d != 0;
		in.player2 = p != 0;
		if (ver >= 3) {
			uint8_t ph;
			if (!detail::readPod(f, ph) || !detail::readPod(f, in.x) || !detail::readPod(f, in.y) ||
				!detail::readPod(f, in.rot) || !detail::readPod(f, in.xVel) || !detail::readPod(f, in.yVel))
				break;
			in.phys = ph != 0;
		}
		out.inputs.push_back(in);
	}
	return true;
}

// ---------------------------------------------------------------- per-tick fixes
inline void writeFixes(std::ostream& f, std::vector<FrameFix> const& fixes) {
	detail::writePod(f, uint32_t(fixes.size()));
	for (auto& x : fixes) {
		detail::writePod(f, int32_t(x.frame));
		detail::writePod(f, uint8_t(x.hasP2));
		for (auto* p : { &x.p1, &x.p2 }) {
			detail::writePod(f, p->x);
			detail::writePod(f, p->y);
			detail::writePod(f, p->rot);
			detail::writePod(f, p->xVel);
			detail::writePod(f, p->yVel);
		}
	}
}

inline bool readFixes(std::istream& f, std::vector<FrameFix>& fixes) {
	fixes.clear();
	uint32_t n = 0;
	if (!detail::readPod(f, n)) return false;
	uint64_t count = std::min<uint64_t>(n, detail::remaining(f) / detail::FIX_RECORD);
	fixes.reserve((size_t)count);
	for (uint64_t k = 0; k < count; k++) {
		FrameFix x;
		int32_t fr;
		uint8_t h;
		if (!detail::readPod(f, fr) || !detail::readPod(f, h)) break;
		x.frame = fr;
		x.hasP2 = h != 0;
		bool ok = true;
		for (auto* p : { &x.p1, &x.p2 })
			ok = ok && detail::readPod(f, p->x) && detail::readPod(f, p->y) && detail::readPod(f, p->rot) &&
			     detail::readPod(f, p->xVel) && detail::readPod(f, p->yVel);
		if (!ok) break;
		fixes.push_back(x);
	}
	return true;
}

// ---------------------------------------------------------------- file names
// Users type these, and downloaded bots bring their own, so this has to survive
// Windows' rules: no \ / : * ? " < > |, no control chars, no trailing dot/space,
// no reserved device names (CON, NUL, COM1...) and a sane length cap.
inline std::string sanitizeFileName(std::string name, size_t maxLen = 64) {
	std::erase_if(name, [](char c) {
		return std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos || (unsigned char)c < 32;
	});
	while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
	while (!name.empty() && name.front() == ' ') name.erase(name.begin());

	auto upper = name;
	for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
	auto base = upper.substr(0, upper.find('.'));
	static char const* RESERVED[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
		                              "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
		                              "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
	if (std::any_of(std::begin(RESERVED), std::end(RESERVED), [&](char const* r) { return base == r; }))
		name = "gd-" + name;

	if (name.size() > maxLen) name = name.substr(0, maxLen);
	while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
	if (name.empty()) name = "replay";
	return name;
}

// ---------------------------------------------------------------- GDR2 replay types
// Same tag + byte layout as gdr's standard PhysicsInput ("Phys"), but fields default to
// NaN so we can tell whether a loaded file actually had physics data.
struct GDMInput : gdr::Input<"Phys"> {
	float xPosition = NAN, yPosition = NAN, rotation = 0.f;
	double xVelocity = 0.0, yVelocity = 0.0;
	GDMInput() = default;
	GDMInput(uint64_t frame, uint8_t button, bool player2, bool down, float x, float y, float rot, double xv, double yv)
		: Input(frame, button, player2, down), xPosition(x), yPosition(y), rotation(rot), xVelocity(xv), yVelocity(yv) {}
	void parseExtension(binary_reader& reader) override {
		reader >> xPosition >> yPosition >> rotation >> xVelocity >> yVelocity;
	}
	void saveExtension(binary_writer& writer) const override {
		writer << xPosition << yPosition << rotation << xVelocity << yVelocity;
	}
};

struct GDMReplay : gdr::Replay<GDMReplay, GDMInput> {
	std::vector<FrameFix> fixes;
	GDMReplay() : Replay("GDMenu", 4) {}

	void saveExtension(binary_writer& w) const override {
		w << (uint64_t)fixes.size();
		for (auto& x : fixes) {
			w << (uint64_t)x.frame << x.hasP2;
			for (auto* p : { &x.p1, &x.p2 }) w << p->x << p->y << p->rot << p->xVel << p->yVel;
		}
	}
	void parseExtension(binary_reader& r) override {
		uint64_t n = 0;
		r >> n;
		fixes.clear();
		// frame is a varint (>=1 byte) + hasP2 (1 byte) + 2 * 28 bytes of floats/doubles;
		// using the smallest possible entry keeps this an upper bound on the real count.
		constexpr uint64_t MIN_ENTRY = 1 + 1 + 2 * (4 + 4 + 4 + 8 + 8);
		n = std::min<uint64_t>(n, r.size() / MIN_ENTRY);
		fixes.reserve((size_t)n);
		for (uint64_t k = 0; k < n; k++) {
			FrameFix x;
			uint64_t fr = 0;
			uint8_t h = 0;
			r >> fr >> h;
			x.frame = (int)fr;
			x.hasP2 = h != 0;
			for (auto* p : { &x.p1, &x.p2 }) r >> p->x >> p->y >> p->rot >> p->xVel >> p->yVel;
			fixes.push_back(x);
		}
	}
	bool shouldParseExtension() const override {
		return botInfo.name == "GDMenu" && botInfo.version >= 4;
	}
};

// gdr's own importData reserves vectors using counts straight from the file
// (deaths / inputs), which throws std::length_error or std::bad_alloc on a forged
// file - i.e. it crashes the game. Worse, its reader silently *stalls* on a truncated
// varint instead of failing, which turns its counted loops into infinite ones that eat
// all memory (no exception, so try/catch alone cannot stop it). Bots arrive from
// Discord/YouTube downloads, so every import first dry-runs the header here:
//   (1) every counted loop's count must fit in the bytes that actually remain, and
//   (2) every single read must advance the stream.
// If this returns true, gdr::Replay::importData provably cannot allocate unboundedly
// or hang on these bytes.
inline bool prevalidateGdr(std::span<uint8_t> data) {
	binary_reader r(data);
	std::array<char, 3> magic{};
	r.read(magic);
	if (std::string_view(magic.data(), magic.size()) != "GDR") return false;

	auto rd = [&](auto& v) {
		size_t before = r.size();
		r >> v;
		return r.size() != before;
	};

	int version = 0, gameVersion = 0, seed = 0, coins = 0, botVersion = 0;
	float duration = 0.f;
	double framerate = 0.0;
	bool ldm = false, platformer = false;
	uint32_t levelId = 0;
	std::string inputTag, author, description, botName, levelName;
	if (!rd(version)) return false;
	if (!rd(inputTag)) return false;
	if (!rd(author)) return false;
	if (!rd(description)) return false;
	if (!rd(duration)) return false;
	if (!rd(gameVersion)) return false;
	if (!rd(framerate)) return false;
	if (!rd(seed)) return false;
	if (!rd(coins)) return false;
	if (!rd(ldm)) return false;
	if (!rd(platformer)) return false;
	if (!rd(botName)) return false;
	if (!rd(botVersion)) return false;
	if (!rd(levelId)) return false;
	if (!rd(levelName)) return false;

	size_t extSize = 0;
	if (!rd(extSize)) return false;
	if (extSize > r.size()) return false;
	r.skip(extSize);

	uint64_t deaths = 0;
	if (!rd(deaths)) return false;
	if (deaths > r.size()) return false; // each death is a varint of >= 1 byte
	for (uint64_t i = 0; i < deaths; i++) {
		uint64_t d = 0;
		if (!rd(d)) return false;
	}

	uint64_t inputs = 0;
	if (!rd(inputs)) return false;
	if (inputs > r.size()) return false; // reserve() would otherwise trust the file
	size_t p1Inputs = 0;
	if (!rd(p1Inputs)) return false;

	bool hasInputExt = !inputTag.empty();
	while (!r.empty()) {
		uint64_t packed = 0;
		if (!rd(packed)) return false;
		if (hasInputExt) {
			size_t ies = 0;
			if (!rd(ies)) return false;
			if (ies > r.size()) return false;
			r.skip(ies);
		}
	}
	return true;
}

// The only entry point the mod uses for .gdr2 / .gdbot / .gdr files.
// A corrupt or forged file becomes a friendly "unsupported" label, never a crash.
inline gdr::Result<GDMReplay> safeImport(std::span<uint8_t> bytes) {
	if (!prevalidateGdr(bytes))
		return gdr::Err<GDMReplay>(std::string("File is corrupt or not a GDR2 replay"));
	try {
		return GDMReplay::importData(bytes);
	}
	catch (std::exception const&) {
		return gdr::Err<GDMReplay>(std::string("File is corrupt or not a GDR2 replay"));
	}
}

} // namespace gdm
