// Compile-time tests for scs_protocol.h. If this file compiles, the tests passed.
#include "scs_protocol.h"

namespace {

using scs::Kind;

constexpr scs::Frame frame(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
  scs::Frame f;
  f.data = {scs::kFrameStart, p0, p1, p2, p3, static_cast<uint8_t>(p0 ^ p1 ^ p2 ^ p3), scs::kFrameEnd};
  f.size = scs::kShortFrameSize;
  return f;
}

constexpr bool equal(const char *a, const char *b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

constexpr bool says(const scs::Frame &f, const char *text) { return equal(scs::describe(scs::parse(f)).c_str(), text); }

// Published examples (Pagani, scsgate): A8 33 00 12 00 21 A3 is "33 on".
static_assert(frame(0x33, 0x00, 0x12, 0x00).data[5] == 0x21);
static_assert(frame(0x33, 0x00, 0x12, 0x01).data[5] == 0x20);

constexpr auto kOn = scs::parse(frame(0x33, 0x00, 0x12, 0x00));
static_assert(kOn.kind == Kind::Command && kOn.dest == 0x33 && kOn.src == 0x00 && kOn.value == 0x00);
static_assert(says(frame(0x33, 0x00, 0x12, 0x00), "command 33 on (from 00)"));
static_assert(says(frame(0x33, 0x00, 0x12, 0x01), "command 33 off (from 00)"));
static_assert(says(frame(0xB1, 0x00, 0x12, 0x01), "command all off (from 00)"));
static_assert(says(frame(0x33, 0x00, 0x12, 0x4D), "command 33 level 50% (from 00)"));
static_assert(says(frame(0x33, 0x00, 0x12, 0x9D), "command 33 level 100% (from 00)"));
static_assert(says(frame(0x33, 0x00, 0x12, 0x03), "command 33 dim up (from 00)"));
static_assert(says(frame(0x41, 0x00, 0x12, 0x0A), "command 41 shutter stop (from 00)"));
static_assert(says(frame(0x33, 0x00, 0x12, 0x26), "command 33 timed on 26 (from 00)"));
static_assert(says(frame(0x33, 0x00, 0x12, 0x7F), "command 33 action 7F (from 00)"));

// State broadcast after a command: A8 B8 33 12 01 98 A3.
static_assert(frame(0xB8, 0x33, 0x12, 0x01).data[5] == 0x98);
static_assert(scs::parse(frame(0xB8, 0x33, 0x12, 0x01)).kind == Kind::State);
static_assert(says(frame(0xB8, 0x33, 0x12, 0x01), "state 33 off"));

// Status request: A8 33 00 15 00 26 A3.
static_assert(frame(0x33, 0x00, 0x15, 0x00).data[5] == 0x26);
static_assert(says(frame(0x33, 0x00, 0x15, 0x00), "status request 33 (from 00)"));

static_assert(says(frame(0x21, 0x00, 0x14, 0x05), "scenario 05 (from 21)"));

// Door entry: ring from the outdoor panel to flat 03, door lock open / release on panel 20.
constexpr auto kRing = scs::parse(frame(0x91, 0x03, 0x60, 0x08));
static_assert(kRing.kind == Kind::DoorRing && kRing.dest == 0x03);
static_assert(says(frame(0x91, 0x03, 0x60, 0x08), "ring flat 03"));
static_assert(says(frame(0x96, 0xA0, 0x6F, 0xA4), "door lock 20 open"));
static_assert(says(frame(0x96, 0xA1, 0x6F, 0xA0), "door lock 21 release"));

// Seen on buses, meaning unknown; and extended frames.
static_assert(says(frame(0x98, 0xA0, 0x6F, 0xA3), "unknown"));
static_assert(says(frame(0x96, 0xA0, 0x6F, 0x01), "unknown"));
static_assert(says(frame(0x96, 0x20, 0x6F, 0xA4), "unknown"));  // panel byte below 0x80
constexpr scs::Frame kExtended = [] {
  scs::Frame f;
  f.data = {0xA8, 0xD1, 0x53, 0x01, 0x42, 0x0D, 0x00, 0x00, 0x02, 0xCE, 0xA3};
  f.size = scs::kLongFrameSize;
  return f;
}();
static_assert(scs::parse(kExtended).kind == Kind::Unknown);

}  // namespace

int main() { return 0; }
