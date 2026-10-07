// Host-side tests for the always-on attempt clip ring (src/core/clips.hpp).
// Build & run:  bash tests/run_tests.sh
#include "clips.hpp"

#include <cstdio>

static int g_checks = 0;
static int g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fails; } } while (0)

static gdm::Clip makeClip(int attempt, size_t inputs, float percent = 50.f, bool completed = false) {
	gdm::Clip c;
	c.levelID = 1;
	c.levelName = "Test Level";
	c.attempt = attempt;
	c.startedAt = 1000 + attempt;
	c.endedAt = 1000 + attempt + 1;
	c.frames = 2400;
	c.percent = percent;
	c.completed = completed;
	c.inputs.resize(inputs);
	for (size_t i = 0; i < inputs; i++) c.inputs[i] = { (uint32_t)(i * 3), 1, (i % 2) == 0, false };
	return c;
}

static void testBasics() {
	std::printf("clips: add / size / at / newest\n");
	gdm::ClipRing ring;
	CHECK(ring.empty());
	CHECK(ring.size() == 0);
	CHECK(ring.at(0) == nullptr);
	CHECK(ring.newest(0) == nullptr);

	CHECK(ring.add(makeClip(1, 10)));
	CHECK(ring.size() == 1);
	CHECK(ring.at(0) && ring.at(0)->attempt == 1);
	CHECK(ring.at(1) == nullptr);
	CHECK(ring.newest(0) && ring.newest(0)->attempt == 1);
	CHECK(ring.newest(1) == nullptr);

	ring.add(makeClip(2, 20));
	ring.add(makeClip(3, 30));
	CHECK(ring.size() == 3);
	// storage is oldest-first; newest(0) is the most recent
	CHECK(ring.at(0)->attempt == 1);
	CHECK(ring.at(2)->attempt == 3);
	CHECK(ring.newest(0)->attempt == 3);
	CHECK(ring.newest(2)->attempt == 1);
	CHECK(ring.totalBytes() > 0);
}

static void testCountEviction() {
	std::printf("clips: count cap evicts oldest\n");
	gdm::ClipRing ring;
	ring.setLimits(5, 32ull << 20);
	for (int i = 1; i <= 8; i++) ring.add(makeClip(i, 5));
	CHECK(ring.size() == 5);
	CHECK(ring.newest(0)->attempt == 8);
	CHECK(ring.newest(4)->attempt == 4); // 1..3 evicted
}

static void testDisabled() {
	std::printf("clips: capacity 0 stores nothing\n");
	gdm::ClipRing ring;
	ring.setLimits(0, 1 << 20);
	CHECK(!ring.add(makeClip(1, 5)));
	CHECK(ring.empty());
}

static void testByteEviction() {
	std::printf("clips: byte cap evicts oldest but never the fresh clip alone\n");
	gdm::ClipRing ring;
	// ClipInput is 8 bytes, so a 10k-input clip is ~80 KB; cap 200 KB -> exactly 2 fit
	ring.setLimits(50, 200ull << 10);
	for (int i = 1; i <= 5; i++) ring.add(makeClip(i, 10000));
	CHECK(ring.size() == 2);
	CHECK(ring.newest(0)->attempt == 5); // the newest is always kept
	CHECK(ring.newest(1)->attempt == 4);
	CHECK(ring.totalBytes() <= ring.maxBytes());

	// a single clip bigger than the whole cap still gets stored (not dropped silently)
	gdm::ClipRing big;
	big.setLimits(50, 1024);
	CHECK(big.add(makeClip(9, 100000)));
	CHECK(big.size() == 1);
	// ...and the next add evicts it
	big.add(makeClip(10, 10));
	CHECK(big.size() == 1);
	CHECK(big.newest(0)->attempt == 10);
}

static void testRemoveAndClear() {
	std::printf("clips: removeNewest / clear / setLimits shrink\n");
	gdm::ClipRing ring;
	ring.setLimits(10, 32ull << 20);
	for (int i = 1; i <= 4; i++) ring.add(makeClip(i, 5));
	ring.removeNewest(0); // drop attempt 4
	CHECK(ring.size() == 3);
	CHECK(ring.newest(0)->attempt == 3);
	ring.removeNewest(1); // drop attempt 2 (middle)
	CHECK(ring.size() == 2);
	CHECK(ring.newest(0)->attempt == 3);
	CHECK(ring.newest(1)->attempt == 1);
	ring.removeNewest(99); // out of range: no-op, no crash
	CHECK(ring.size() == 2);

	// shrinking the limit evicts immediately
	ring.setLimits(1, 32ull << 20);
	CHECK(ring.size() == 1);
	CHECK(ring.newest(0)->attempt == 3);

	ring.clear();
	CHECK(ring.empty());
	CHECK(ring.totalBytes() == 0);
}

static void testClipBytes() {
	std::printf("clips: approxBytes grows with inputs\n");
	gdm::Clip a = makeClip(1, 0);
	gdm::Clip b = makeClip(1, 1000);
	CHECK(b.approxBytes() > a.approxBytes());
	CHECK(b.approxBytes() >= 1000 * sizeof(gdm::ClipInput));
}

int main() {
	std::printf("== ClipRing tests ==\n");
	testBasics();
	testCountEviction();
	testDisabled();
	testByteEviction();
	testRemoveAndClear();
	testClipBytes();
	std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
