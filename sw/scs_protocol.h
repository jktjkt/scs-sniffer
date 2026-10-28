// SCS message layer: what a valid frame (see scs_decoder.h) means.
//
// There is no public specification; this follows what others reverse-engineered:
//   - lighting/automation commands, state broadcasts, status requests, A5 acknowledgements:
//     Guido Pagani (guidopic.altervista.org/eibscout/logticino.html), flavio/scsgate (messages.py)
//   - action byte table: r0bb10/ESPHome-SCS-Bridge (re/README.md), mined from F461 gateway firmware
//   - door entry ring: stapelberg/intercom-backpack (scsfilter.h), r0bb10
//   - door lock release: r0bb10, matching captures by Michael Stapelberg
// Anything else, including all extended (11-byte) frames, is reported as Kind::Unknown.
//
// Short frames are A8 P0 P1 P2 P3 chk A3. Devices commonly send each frame up to three times.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "scs_decoder.h"

namespace scs {

enum class Kind : uint8_t {
  Unknown,
  Command,        // P0 dest, P1 src, P2 = 12, P3 action
  State,          // P0 = B8, P1 src, P2 = 12, P3 action: a device broadcasting its new state
  StatusRequest,  // P0 dest, P1 src, P2 = 15
  Scenario,       // P0 src, P2 = 14, P3 scenario number
  DoorRing,       // P0 = 91 (outdoor panel), P1 flat, P2 = 60
  DoorLock,       // P0 = 96, P1 = 80 + panel, P2 = 6F, P3 = A4 (open) or A0 (release)
};

inline constexpr uint8_t kAck = 0xA5;          // single byte outside a frame
inline constexpr uint8_t kGeneralDest = 0xB1;  // Command addressed to all devices
inline constexpr uint8_t kStateDest = 0xB8;

struct Message {
  Kind kind = Kind::Unknown;
  uint8_t dest = 0;   // Command, StatusRequest; flat for DoorRing
  uint8_t src = 0;    // Command, State, StatusRequest, Scenario; panel for DoorLock
  uint8_t value = 0;  // action for Command/State, scenario for Scenario, P3 otherwise
};

constexpr Message parse(const Frame &f) {
  if (f.size != kShortFrameSize)
    return {};
  const uint8_t p0 = f[1], p1 = f[2], p2 = f[3], p3 = f[4];
  if (p0 == 0x91 && p2 == 0x60)
    return {Kind::DoorRing, p1, p0, p3};
  if (p0 == 0x96 && p1 >= 0x80 && p2 == 0x6F && (p3 == 0xA4 || p3 == 0xA0))
    return {Kind::DoorLock, 0, static_cast<uint8_t>(p1 - 0x80), p3};
  switch (p2) {
    case 0x12:
      return p0 == kStateDest ? Message{Kind::State, 0, p1, p3} : Message{Kind::Command, p0, p1, p3};
    case 0x14:
      return {Kind::Scenario, 0, p0, p3};
    case 0x15:
      return {Kind::StatusRequest, p0, p1, p3};
  }
  return {};
}

// Short human-readable text, e.g. "command 33 on (from 00)", "ring flat 03", "door lock 20 open".
struct Text {
  std::array<char, 64> s{};
  std::size_t n = 0;

  constexpr Text &operator<<(const char *t) {
    while (*t != '\0' && n + 1 < s.size())
      s[n++] = *t++;
    return *this;
  }
  constexpr Text &hex(uint8_t b) {
    const char digits[] = "0123456789ABCDEF";
    const char t[] = {digits[b >> 4], digits[b & 0x0F], '\0'};
    return *this << t;
  }
  constexpr Text &dec(unsigned v) {
    char t[11] = {};
    std::size_t i = sizeof t - 1;
    do {
      t[--i] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v != 0);
    return *this << t + i;
  }
  constexpr const char *c_str() const { return s.data(); }
};

// Lighting and shutter action byte (Command and State).
constexpr Text &action(Text &t, uint8_t a) {
  switch (a) {
    case 0x00: return t << "on";
    case 0x01: return t << "off";
    case 0x03: return t << "dim up";
    case 0x04: return t << "dim down";
    case 0x08: return t << "shutter up";
    case 0x09: return t << "shutter down";
    case 0x0A: return t << "shutter stop";
  }
  switch (a & 0x0F) {
    case 0x0D:  // preset level, 1D..9D = 20..100 %
      t << "level ";
      return t.dec(((a >> 4) + 1) * 10) << "%";
    case 0x06:  // timed on; duration mapping unknown
      t << "timed on ";
      return t.hex(a);
    case 0x0B:
      t << "blink ";
      return t.hex(a);
  }
  t << "action ";
  return t.hex(a);
}

constexpr Text describe(const Message &m) {
  Text t;
  switch (m.kind) {
    case Kind::Command:
      t << "command ";
      if (m.dest == kGeneralDest)
        t << "all";
      else
        t.hex(m.dest);
      t << " ";
      action(t, m.value) << " (from ";
      t.hex(m.src) << ")";
      break;
    case Kind::State:
      t << "state ";
      t.hex(m.src) << " ";
      action(t, m.value);
      break;
    case Kind::StatusRequest:
      t << "status request ";
      t.hex(m.dest) << " (from ";
      t.hex(m.src) << ")";
      break;
    case Kind::Scenario:
      t << "scenario ";
      t.hex(m.value) << " (from ";
      t.hex(m.src) << ")";
      break;
    case Kind::DoorRing:
      t << "ring flat ";
      t.hex(m.dest);
      break;
    case Kind::DoorLock:
      t << "door lock ";
      t.hex(m.src) << (m.value == 0xA4 ? " open" : " release");
      break;
    case Kind::Unknown:
      t << "unknown";
      break;
  }
  return t;
}

}  // namespace scs
