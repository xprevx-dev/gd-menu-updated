// Host-side tests for GDMenu's byte-level core (src/core/replay_io.hpp).
// Build & run:  bash tests/run_tests.sh
// These exist because a corrupt or malicious .gdr2 / session file used to be able to
// crash Geometry Dash (unbounded vector reserves from counts read straight from disk).
#include "replay_io.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static int g_checks = 0;
static int g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fails; } } while (0)

static std::vector<uint8_t> bytes(std::stringstream const& ss) {
	auto s = ss.str();
	return std::vector<uint8_t>(s.begin(), s.end());
}

// gdr writes integrals as little-endian base-128 varints
static void varint(std::stringstream& ss, uint64_t v) {
	if (v == 0) { ss.put('\0'); return; }
	while (v > 0) {
		uint8_t b = v & 0x7F;
		v >>= 7;
		if (v > 0) b |= 0x80;
		ss.put((char)b);
	}
}
template <class T> static void pod(std::stringstream& ss, T v) { ss.write(reinterpret_cast<char const*>(&v), sizeof(v)); }

// ---------------------------------------------------------------- sessions
static void testSessionRoundTrip() {
	std::printf("session: round trip\n");
	std::vector<BotInput> in;
	for (int i = 0; i < 50; i++) {
		BotInput b;
		b.frame = i * 3; b.button = 1 + (i % 3); b.down = (i % 2) == 0; b.player2 = (i % 5) == 0;
		b.phys = true; b.x = i * 1.5f; b.y = -i * 0.25f; b.rot = i * 7.5f; b.xVel = i * 0.5; b.yVel = -i * 2.0;
		in.push_back(b);
	}
	std::stringstream ss;
	ss.write("GDMS", 4);
	gdm::writeSession(ss, 1234, 56.5f, in);

	gdm::Session out;
	CHECK(gdm::readSession(ss, out));
	CHECK(out.resumeFrame == 1234);
	CHECK(out.percent == 56.5f);
	CHECK(out.inputs.size() == in.size());
	bool same = out.inputs.size() == in.size();
	for (size_t i = 0; same && i < in.size(); i++) {
		same = in[i].frame == out.inputs[i].frame && in[i].button == out.inputs[i].button &&
		       in[i].down == out.inputs[i].down && in[i].player2 == out.inputs[i].player2 &&
		       in[i].phys == out.inputs[i].phys && in[i].x == out.inputs[i].x && in[i].y == out.inputs[i].y &&
		       in[i].rot == out.inputs[i].rot && in[i].xVel == out.inputs[i].xVel && in[i].yVel == out.inputs[i].yVel;
	}
	CHECK(same);
}

static void testSessionForgedCount() {
	std::printf("session: forged entry count must not allocate/crash\n");
	std::stringstream ss;
	ss.write("GDMS", 4);
	pod<uint32_t>(ss, 3);          // version
	pod<int32_t>(ss, 10);          // resumeFrame
	pod<float>(ss, 1.f);           // percent
	pod<uint32_t>(ss, 0xFFFFFFFF); // 4 billion inputs... but only 2 records follow
	for (int i = 0; i < 2; i++) {
		pod<int32_t>(ss, i); pod<uint8_t>(ss, 1); pod<uint8_t>(ss, 1); pod<uint8_t>(ss, 0);
		pod<uint8_t>(ss, 0); pod<float>(ss, 0.f); pod<float>(ss, 0.f); pod<float>(ss, 0.f); pod<double>(ss, 0.0); pod<double>(ss, 0.0);
	}
	gdm::Session out;
	CHECK(gdm::readSession(ss, out));
	CHECK(out.inputs.size() == 2);
}

static void testSessionTruncated() {
	std::printf("session: truncated mid-record\n");
	std::stringstream ss;
	ss.write("GDMS", 4);
	pod<uint32_t>(ss, 3); pod<int32_t>(ss, 5); pod<float>(ss, 2.f); pod<uint32_t>(ss, 10);
	pod<int32_t>(ss, 0); pod<uint8_t>(ss, 1); pod<uint8_t>(ss, 1); // record cut off here
	gdm::Session out;
	CHECK(gdm::readSession(ss, out));
	CHECK(out.inputs.empty());

	std::stringstream bad;
	bad << "NOPE";
	CHECK(!gdm::readSession(bad, out));
}

static void testSessionV2Compat() {
	std::printf("session: pre-2.4 (v2) files still load\n");
	std::stringstream ss;
	ss.write("GDMS", 4);
	pod<uint32_t>(ss, 2); pod<int32_t>(ss, 7); pod<float>(ss, 3.f); pod<uint32_t>(ss, 2);
	for (int i = 0; i < 2; i++) { pod<int32_t>(ss, i); pod<uint8_t>(ss, 1); pod<uint8_t>(ss, 1); pod<uint8_t>(ss, 0); }
	gdm::Session out;
	CHECK(gdm::readSession(ss, out));
	CHECK(out.inputs.size() == 2);
	CHECK(!out.inputs[0].phys);
}

// ---------------------------------------------------------------- fixes
static void testFixes() {
	std::printf("fixes: round trip / forged / truncated\n");
	std::vector<FrameFix> in;
	for (int i = 0; i < 20; i++) {
		FrameFix x;
		x.frame = i; x.hasP2 = (i % 2) == 0;
		x.p1 = { (float)i, (float)-i, (float)i * 2, (double)i, (double)-i };
		x.p2 = { (float)i / 2, (float)i / 3, (float)i / 4, (double)i / 5, (double)i / 6 };
		in.push_back(x);
	}
	std::stringstream ss;
	gdm::writeFixes(ss, in);
	std::vector<FrameFix> out;
	CHECK(gdm::readFixes(ss, out));
	CHECK(out.size() == in.size());
	CHECK(out.size() == 20 && out[19].frame == 19 && out[19].p2.yVel == in[19].p2.yVel);

	std::stringstream forged;
	pod<uint32_t>(forged, 0xFFFFFFFE); // claims ~4 billion fixes, contains none
	std::vector<FrameFix> none;
	CHECK(gdm::readFixes(forged, none));
	CHECK(none.empty());

	std::stringstream cut;
	gdm::writeFixes(cut, in);
	auto b = bytes(cut);
	b.resize(b.size() - 7); // chop the tail off mid-record
	std::stringstream tr(std::string(b.begin(), b.end()));
	std::vector<FrameFix> part;
	CHECK(gdm::readFixes(tr, part));
	CHECK(part.size() == in.size() - 1);
}

// ---------------------------------------------------------------- file names
static void testSanitize() {
	std::printf("sanitize: windows rules\n");
	CHECK(gdm::sanitizeFileName("my run") == "my run");
	CHECK(gdm::sanitizeFileName("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij");
	CHECK(gdm::sanitizeFileName("trailing...") == "trailing");
	CHECK(gdm::sanitizeFileName("   ") == "replay");
	CHECK(gdm::sanitizeFileName("") == "replay");
	CHECK(gdm::sanitizeFileName("CON") == "gd-CON");       // reserved device name
	CHECK(gdm::sanitizeFileName("nul.gdr2") == "gd-nul.gdr2");
	CHECK(gdm::sanitizeFileName(std::string(200, 'x')).size() == 64);
	CHECK(gdm::sanitizeFileName("bell\x01ring") == "bellring");
}

// ---------------------------------------------------------------- GDR2 replays
static gdm::GDMReplay makeReplay() {
	gdm::GDMReplay r;
	r.author = "Prevx";
	r.description = "test";
	r.gameVersion = 22081;
	r.framerate = 240.0;
	r.duration = 2.f;
	r.levelInfo.id = 42;
	r.levelInfo.name = "Test Level";
	r.platformer = false;
	r.ldm = false;
	for (int i = 0; i < 30; i++)
		r.inputs.emplace_back((uint64_t)(i * 4), 1, false, (i % 2) == 0, i * 10.f, i * 5.f, i * 90.f, 1.5, -2.5);
	for (int i = 0; i < 12; i++) {
		FrameFix x;
		x.frame = i * 4; x.hasP2 = false;
		x.p1 = { (float)i, (float)i + 0.5f, (float)i * 3, 0.25, -0.5 };
		r.fixes.push_back(x);
	}
	return r;
}

static void testReplayRoundTrip() {
	std::printf("gdr2: export -> safeImport round trip (with Phys + fixes)\n");
	auto r = makeReplay();
	auto data = r.exportData();
	CHECK(data.isOk());
	if (!data.isOk()) return;
	auto owned = data.unwrap(); // local copy: safeImport takes a mutable span
	auto res = gdm::safeImport(std::span<uint8_t>(owned));
	CHECK(res.isOk());
	if (!res.isOk()) { std::printf("    import error: %s\n", res.unwrapErr().c_str()); return; }
	auto& back = res.unwrap();
	CHECK(back.author == "Prevx");
	CHECK(back.levelInfo.name == "Test Level");
	CHECK(back.inputs.size() == r.inputs.size());
	CHECK(back.fixes.size() == r.fixes.size());
	CHECK(back.inputs.size() > 0 && back.inputs[3].xPosition == 30.f);
	CHECK(back.fixes.size() > 0 && back.fixes[5].p1.y == 5.5f);
}

static void testReplayForgedFixCount() {
	std::printf("gdr2: forged fix count in extension is bounded by real bytes\n");
	// varint(huge) followed by junk bytes: at most bytes/58 entries can physically exist
	std::stringstream ss;
	varint(ss, 0xFFFFFFFFFFFFFFFFull);
	for (int i = 0; i < 40; i++) ss.put((char)0xAB);
	auto b = bytes(ss);
	std::span<uint8_t> view(b);
	binary_reader reader(view);
	gdm::GDMReplay r;
	r.parseExtension(reader);
	CHECK(r.fixes.size() <= 1); // ~0 entries, definitely not billions
	CHECK(r.fixes.empty());
}

static void testReplayTruncated() {
	std::printf("gdr2: truncated file is an error, not a crash\n");
	auto r = makeReplay();
	auto data = r.exportData();
	CHECK(data.isOk());
	auto b = data.unwrap();
	for (size_t cut = b.size(); cut > 4; cut -= std::max<size_t>(1, cut / 7)) {
		std::vector<uint8_t> part(b.begin(), b.begin() + (long)cut);
		auto res = gdm::safeImport(std::span<uint8_t>(part));
		(void)res; // either Ok-with-fewer-inputs or Err; the point is we got here alive
	}
	CHECK(true);
}

// deterministic fuzz: random junk with a valid magic must never throw or hang
static void testFuzz() {
	std::printf("gdr2: fuzz 4000 random buffers\n");
	uint64_t s = 0x2545F4914F6CDD1Dull;
	auto rnd = [&] { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint8_t)(s >> 33); };
	size_t ok = 0, err = 0;
	for (int iter = 0; iter < 4000; iter++) {
		std::vector<uint8_t> buf = { 'G', 'D', 'R' };
		size_t n = rnd() % 200;
		for (size_t i = 0; i < n; i++) buf.push_back(rnd());
		auto res = gdm::safeImport(std::span<uint8_t>(buf));
		if (res.isOk()) ok++; else err++;
	}
	std::printf("    (ok=%zu err=%zu - both are acceptable, throwing is not)\n", ok, err);
	CHECK(true);
}

// a truncated varint makes gdr's reader stall; its loops would then spin forever.
// prevalidate must reject these before importData ever runs.
static void testStall() {
	std::printf("gdr2: stalled-varint buffers are rejected, not hung\n");
	std::vector<std::vector<uint8_t>> cases = {
		{ 'G', 'D', 'R', 0xFF },
		{ 'G', 'D', 'R', 0xFF, 0xFF, 0xFF },
		{ 'G', 'D', 'R', 0x02, 0x00, 0xFF },          // stall inside a header string/varint
	};
	for (auto& c : cases) {
		CHECK(!gdm::prevalidateGdr(std::span<uint8_t>(c)));
		auto res = gdm::safeImport(std::span<uint8_t>(c));
		CHECK(res.isErr());
	}
	// and a real replay still passes prevalidation
	auto owned = makeReplay().exportData().unwrap();
	CHECK(gdm::prevalidateGdr(std::span<uint8_t>(owned)));
}

// pre-v2.5 fix files had no magic: raw count then records. Must still load.
static void testFixesLegacy() {
	std::printf("fixes: legacy (no magic) files still load\n");
	std::stringstream ss;
	std::vector<FrameFix> in(3);
	for (int i = 0; i < 3; i++) { in[i].frame = i; in[i].p1.x = (float)i; }
	pod<uint32_t>(ss, (uint32_t)in.size());
	for (auto& x : in) {
		pod<int32_t>(ss, x.frame); pod<uint8_t>(ss, 0);
		for (auto* p : { &x.p1, &x.p2 }) {
			pod<float>(ss, p->x); pod<float>(ss, p->y); pod<float>(ss, p->rot);
			pod<double>(ss, p->xVel); pod<double>(ss, p->yVel);
		}
	}
	std::vector<FrameFix> out;
	CHECK(gdm::readFixes(ss, out));
	CHECK(out.size() == 3);
	CHECK(out.size() == 3 && out[2].p1.x == 2.f);
}

// sessions and fixes readers must survive arbitrary bytes without throwing/hanging
static void testFuzzSessions() {
	std::printf("sessions/fixes: fuzz 4000 random buffers\n");
	uint64_t s = 0x9E3779B97F4A7C15ull;
	auto rnd = [&] { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint8_t)(s >> 33); };
	for (int iter = 0; iter < 4000; iter++) {
		std::string buf = (iter % 2 == 0) ? "GDMS" : "GDMF";
		size_t n = rnd() % 160;
		for (size_t i = 0; i < n; i++) buf.push_back((char)rnd());
		std::stringstream ss(buf);
		gdm::Session ses;
		gdm::readSession(ss, ses);
		std::stringstream ss2(buf);
		std::vector<FrameFix> fx;
		gdm::readFixes(ss2, fx);
	}
	CHECK(true);
}

int main() {
	testSessionRoundTrip();
	testSessionForgedCount();
	testSessionTruncated();
	testSessionV2Compat();
	testFixes();
	testFixesLegacy();
	testSanitize();
	testReplayRoundTrip();
	testReplayForgedFixCount();
	testReplayTruncated();
	testStall();
	testFuzz();
	testFuzzSessions();
	std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
