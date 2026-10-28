// Compile-time tests for scs_decoder.h. If this file compiles, the tests passed:
//   g++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only scs_decoder_test.cpp
//
// A small constexpr encoder turns bytes into the pulse train a tap would see, with optional
// sender clock skew, edge jitter, pulse width variation and glitches.
#include "scs_decoder.h"

#include <initializer_list>

namespace {

using scs::Error;
using scs::Frame;
using scs::Timing;

// ---------------------------------------------------------------- recording sink

template <class T, std::size_t N>
struct Log {
  std::array<T, N> items{};
  std::size_t count = 0;  // keeps counting past N, stores only the first N
  constexpr void push(T v) {
    if (count < N)
      items[count] = v;
    ++count;
  }
  constexpr T operator[](std::size_t i) const { return items[i]; }
};

struct Rec {
  Log<Frame, 8> frames;
  Log<Error, 8> errors;
  Log<uint8_t, 300> bytes;
  Log<uint8_t, 8> stray;
  constexpr void on_frame(const Frame &f) { frames.push(f); }
  constexpr void on_error(Error e) { errors.push(e); }
  constexpr void on_byte(uint8_t b) { bytes.push(b); }
  constexpr void on_stray(uint8_t b) { stray.push(b); }
};

struct Result {
  Rec rec;
  uint32_t glitches;
};

// ---------------------------------------------------------------- signal generation

struct Seg {
  bool active;
  uint32_t ns;
};

template <std::size_t N>
struct Signal {
  std::array<Seg, N> seg{};
  std::size_t n = 0;
  uint64_t t = 0;  // end of the last segment

  constexpr void add(bool active, uint64_t ns) {
    if (ns == 0)
      return;
    if (n != 0 && seg[n - 1].active == active)
      seg[n - 1].ns += static_cast<uint32_t>(ns);
    else
      seg[n++] = {active, static_cast<uint32_t>(ns)};
    t += ns;
  }
  constexpr void pulse(uint64_t start, uint64_t width) {
    add(false, start - t);
    add(true, width);
  }
};

struct Tx {
  uint32_t bit_ns = 104'167;
  uint32_t pulse_ns = 34'000;
  int32_t skew_ppm = 0;      // positive = slower sender (longer bits)
  uint32_t jitter_ns = 0;    // each edge moved by a pseudo-random amount in [-j, +j]
  uint32_t seed = 1;
  uint32_t byte_gap_ns = 0;  // extra idle between bytes
  uint32_t glitch_ns = 0;    // if set, a glitch of this width follows every pulse
  uint32_t rise_ns = 0;      // every low (glitches too) lasts this much longer, see pullup_delay()
};

template <std::size_t N>
struct Encoder {
  Signal<N> sig;
  Tx tx;
  uint32_t rng;
  uint64_t cursor = 1'000'000;  // 1 ms idle lead-in

  constexpr explicit Encoder(Tx p = Tx{}) : tx(p), rng(p.seed) {}

  constexpr uint64_t bit() const {
    return static_cast<uint64_t>(int64_t{tx.bit_ns} * (1'000'000 + tx.skew_ppm) / 1'000'000);
  }
  constexpr int64_t jitter() {
    if (tx.jitter_ns == 0)
      return 0;
    rng = rng * 1664525u + 1013904223u;
    return int64_t{(rng >> 8) % (2 * tx.jitter_ns + 1)} - tx.jitter_ns;
  }
  constexpr void pulse(uint64_t start, uint64_t width) {
    sig.pulse(start, width + tx.rise_ns);
    if (tx.glitch_ns)
      sig.pulse(start + 60'000, tx.glitch_ns + tx.rise_ns);
  }
  // Active pulses at the given bit cells of one character (0 = start bit, 9 = stop bit).
  constexpr void cells(std::initializer_list<int> zero_cells) {
    for (int i : zero_cells)
      pulse(static_cast<uint64_t>(static_cast<int64_t>(cursor + i * bit()) + jitter()), tx.pulse_ns);
    cursor += 10 * bit() + tx.byte_gap_ns;
  }
  constexpr void byte(uint8_t b) {
    for (int i = 0; i < 9; ++i)
      if (i == 0 || !((b >> (i - 1)) & 1))
        pulse(static_cast<uint64_t>(static_cast<int64_t>(cursor + i * bit()) + jitter()), tx.pulse_ns);
    cursor += 10 * bit() + tx.byte_gap_ns;
  }
  template <class Bytes>
  constexpr void bytes(const Bytes &bs) {
    for (uint8_t b : bs)
      byte(b);
  }
  constexpr void bytes(std::initializer_list<uint8_t> bs) {
    for (uint8_t b : bs)
      byte(b);
  }
  constexpr void idle(uint64_t ns) { cursor += ns; }
  constexpr void raw_pulse(uint64_t offset, uint64_t width) { sig.pulse(cursor + offset, width); }
  constexpr const Signal<N> &finish(uint64_t trailing_ns = 3'000'000) {
    sig.add(false, cursor + trailing_ns - sig.t);
    return sig;
  }
};

template <std::size_t N>
constexpr Result decode(const Signal<N> &s, Timing timing = Timing{}) {
  scs::Decoder<Rec> d{Rec{}, timing};
  for (std::size_t i = 0; i < s.n; ++i)
    d.segment(s.seg[i].active, s.seg[i].ns);
  d.end_of_capture();
  return {d.sink(), d.glitches()};
}

// Same signal, every segment chopped into pieces of at most `piece_ns` (shorter than a glitch, so
// this only works if the decoder joins them back together).
template <std::size_t N>
constexpr Result decode_chopped(const Signal<N> &s, uint32_t piece_ns) {
  scs::Decoder<Rec> d{};
  for (std::size_t i = 0; i < s.n; ++i)
    for (uint32_t left = s.seg[i].ns; left != 0;) {
      const uint32_t p = left < piece_ns ? left : piece_ns;
      d.segment(s.seg[i].active, p);
      left -= p;
    }
  d.end_of_capture();
  return {d.sink(), d.glitches()};
}

// ---------------------------------------------------------------- RMT packing

// Same layout as ESP-IDF's rmt_symbol_word_t.
struct RmtWord {
  uint16_t duration0 : 15;
  uint16_t level0 : 1;
  uint16_t duration1 : 15;
  uint16_t level1 : 1;
};

template <std::size_t W>
struct RmtCapture {
  std::array<RmtWord, W> words{};
  std::size_t n = 0;
};

// What the RMT would record: starts at the first edge, edges quantised to the tick, durations
// limited to 15 bits, closed by a zero-duration marker. Active level is GPIO low.
template <std::size_t W, std::size_t N>
constexpr RmtCapture<W> to_rmt(const Signal<N> &s, uint32_t tick_ns) {
  std::array<uint16_t, 2 * W> dur{};
  std::array<uint16_t, 2 * W> lvl{};
  std::size_t h = 0;
  uint64_t t = s.seg[0].ns;  // drop the idle lead-in
  uint64_t q_prev = (t + tick_ns / 2) / tick_ns;
  for (std::size_t i = 1; i + 1 < s.n; ++i) {  // and the trailing idle
    t += s.seg[i].ns;
    const uint64_t q = (t + tick_ns / 2) / tick_ns;
    for (uint64_t left = q - q_prev; left != 0;) {
      const uint64_t d = left > 32767 ? 32767 : left;
      dur[h] = static_cast<uint16_t>(d);
      lvl[h] = s.seg[i].active ? 0 : 1;
      ++h;
      left -= d;
    }
    q_prev = q;
  }
  dur[h] = 0;  // end marker, on the idle level
  lvl[h] = 1;
  ++h;
  RmtCapture<W> c{};
  for (std::size_t i = 0; i < h; i += 2) {
    c.words[c.n].duration0 = dur[i];
    c.words[c.n].level0 = lvl[i];
    c.words[c.n].duration1 = i + 1 < h ? dur[i + 1] : 0;
    c.words[c.n].level1 = i + 1 < h ? lvl[i + 1] : 1;
    ++c.n;
  }
  return c;
}

// ---------------------------------------------------------------- helpers

constexpr std::array<uint8_t, 7> frame7(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return {scs::kFrameStart, a, b, c, d, static_cast<uint8_t>(a ^ b ^ c ^ d), scs::kFrameEnd};
}

constexpr std::array<uint8_t, 11> frame11(std::array<uint8_t, 8> p) {
  std::array<uint8_t, 11> f{scs::kFrameStart};
  for (std::size_t i = 0; i < 8; ++i)
    f[i + 1] = p[i];
  f[9] = scs::checksum(p.data(), p.size());
  f[10] = scs::kFrameEnd;
  return f;
}

template <std::size_t N>
constexpr bool same(const Frame &f, const std::array<uint8_t, N> &want) {
  if (f.size != N)
    return false;
  for (std::size_t i = 0; i < N; ++i)
    if (f[i] != want[i])
      return false;
  return true;
}

constexpr auto kF1 = frame7(0x31, 0x00, 0x12, 0x01);
constexpr auto kF2 = frame7(0x7F, 0x00, 0x55, 0x2A);
constexpr auto kF11 = frame11({0x01, 0x7F, 0x00, 0x40, 0x13, 0x37, 0x2A, 0x05});

static_assert(kF1[5] == 0x22);
static_assert(kF11[9] == (0x01 ^ 0x7F ^ 0x00 ^ 0x40 ^ 0x13 ^ 0x37 ^ 0x2A ^ 0x05));

// ---------------------------------------------------------------- tests

// One clean short frame.
constexpr Result kBasic = [] {
  Encoder<256> e;
  e.bytes(kF1);
  return decode(e.finish());
}();
static_assert(kBasic.rec.frames.count == 1);
static_assert(same(kBasic.rec.frames[0], kF1));
static_assert(kBasic.rec.errors.count == 0);
static_assert(kBasic.rec.stray.count == 0);
static_assert(kBasic.rec.bytes.count == 7);
static_assert(kBasic.glitches == 0);

// Every byte value survives the bit layer (framing errors are expected and ignored here).
constexpr bool all_byte_values() {
  Encoder<5000> e;
  for (int b = 0; b < 256; ++b)
    e.byte(static_cast<uint8_t>(b));
  const Result r = decode(e.finish());
  if (r.rec.bytes.count != 256)
    return false;
  for (int b = 0; b < 256; ++b)
    if (r.rec.bytes[b] != b)
      return false;
  return true;
}
static_assert(all_byte_values());

// Long frame, back-to-back frames, a stray byte between frames.
constexpr Result kSequence = [] {
  Encoder<1024> e;
  e.bytes(kF1);
  e.bytes(kF11);  // no gap at all
  e.byte(0xA5);
  e.idle(500'000);
  e.bytes(kF2);
  e.idle(5'000'000);
  e.bytes(kF1);  // repeated frame after a long pause
  return decode(e.finish());
}();
static_assert(kSequence.rec.frames.count == 4);
static_assert(same(kSequence.rec.frames[0], kF1));
static_assert(same(kSequence.rec.frames[1], kF11));
static_assert(same(kSequence.rec.frames[2], kF2));
static_assert(same(kSequence.rec.frames[3], kF1));
static_assert(kSequence.rec.stray.count == 1 && kSequence.rec.stray[0] == 0xA5);
static_assert(kSequence.rec.errors.count == 0);

// Pulse widths 20..50 us, small byte gaps, and either sender clock error up to +-3.5 % with clean
// edges, or up to +-2 % with +-8 us jitter on every edge (the start bit edge included, so the
// position of a late bit can be off by twice that).
// One static_assert per skew keeps each evaluation under the compilers' constexpr step limits.
constexpr bool robust_timing(int32_t skew) {
  for (uint32_t width : {20'000u, 34'000u, 50'000u})
    for (uint32_t jit : {0u, 8'000u})
      for (uint32_t gap : {0u, 20'000u})
        for (uint32_t seed : {1u, 2u, 3u}) {
          if (jit != 0 && (skew > 20'000 || skew < -20'000))
            continue;
          Encoder<256> e{{.pulse_ns = width, .skew_ppm = skew, .jitter_ns = jit, .seed = seed,
                          .byte_gap_ns = gap}};
          e.bytes(kF1);
          e.bytes(kF11);
          const Result r = decode(e.finish());
          if (r.rec.frames.count != 2 || r.rec.errors.count != 0 || !same(r.rec.frames[0], kF1) ||
              !same(r.rec.frames[1], kF11))
            return false;
        }
  return true;
}
static_assert(robust_timing(-35'000));
static_assert(robust_timing(-20'000));
static_assert(robust_timing(-10'000));
static_assert(robust_timing(0));
static_assert(robust_timing(10'000));
static_assert(robust_timing(20'000));
static_assert(robust_timing(35'000));

// ...and the tolerance is real: a sender 6 % slow is rejected rather than misread.
constexpr Result kTooSlow = [] {
  Encoder<256> e{{.skew_ppm = 60'000}};
  e.bytes(kF1);
  return decode(e.finish());
}();
static_assert(kTooSlow.rec.frames.count == 0);
static_assert(kTooSlow.rec.errors.count > 0 && kTooSlow.rec.errors[0] == Error::BitPhase);

// Short glitches after every pulse and in the idle gap are ignored.
constexpr Result kGlitchy = [] {
  Encoder<512> e{{.glitch_ns = 2'000}};
  e.bytes(kF1);
  e.raw_pulse(400'000, 3'000);
  e.idle(800'000);
  e.bytes(kF2);
  return decode(e.finish());
}();
static_assert(kGlitchy.rec.frames.count == 2);
static_assert(kGlitchy.rec.errors.count == 0);
static_assert(kGlitchy.glitches > 40);

// ---------------------------------------------------------------- pull-up on the RX pin

// On the tap, Q2 pulls the RX line low fast, but the release is an RC rise through the pull-up:
// on-board R11 (100 k) alone, or in parallel with the MCU's internal one (~45 k on the ESP32).
// The MCU reads high once the line crosses VIH = 0.75 * VDD, ln(4) * R * C after the release,
// so every low pulse arrives stretched by that much.
constexpr uint32_t pullup_delay(uint32_t r_ohm, uint32_t c_pf) {
  return static_cast<uint32_t>(uint64_t{r_ohm} * c_pf * 1386 / 1'000'000);
}
constexpr uint32_t kOnBoard = 100'000;  // R11 only
constexpr uint32_t kWithMcu = 31'000;   // R11 || ~45 k internal pull-up

static_assert(pullup_delay(kWithMcu, 100) == 4'296);
static_assert(pullup_delay(kOnBoard, 100) == 13'860);

template <std::size_t N = 512>
constexpr Result decode_with_rise(uint32_t rise, uint32_t glitch_ns) {
  Encoder<N> e{{.jitter_ns = 4'000, .glitch_ns = glitch_ns, .rise_ns = rise}};
  e.bytes(kF1);
  e.bytes(kF11);
  return decode(e.finish());
}

// MCU pull-up enabled, 100 pF of wiring: data and glitch filtering both unaffected.
constexpr Result kPullupOn = decode_with_rise(pullup_delay(kWithMcu, 100), 2'000);
static_assert(kPullupOn.rec.frames.count == 2 && kPullupOn.rec.errors.count == 0);
static_assert(kPullupOn.glitches > 40);

// R11 alone: the data itself still decodes (34 us pulses arrive as ~48 us)...
constexpr Result kOnBoardClean = decode_with_rise(pullup_delay(kOnBoard, 100), 0);
static_assert(kOnBoardClean.rec.frames.count == 2 && kOnBoardClean.rec.errors.count == 0);

// ...but a 2 us glitch is stretched past min_low_ns and taken for a bit edge: frames are lost.
constexpr Result kOnBoardGlitchy = decode_with_rise(pullup_delay(kOnBoard, 100), 2'000);
static_assert(kOnBoardGlitchy.rec.frames.count < 2 && kOnBoardGlitchy.rec.errors.count > 0);

// R11 alone on a long wire (500 pF): every pulse exceeds max_low_ns and is rejected, not misread.
constexpr Result kOnBoardLongWire = decode_with_rise(pullup_delay(kOnBoard, 500), 0);
static_assert(kOnBoardLongWire.rec.frames.count == 0);
static_assert(kOnBoardLongWire.rec.errors.count > 0 && kOnBoardLongWire.rec.errors[0] == Error::LongLow);

// Segment boundaries do not matter, even when every piece is shorter than a glitch.
constexpr Result kChopped = [] {
  Encoder<256> e{{.jitter_ns = 5'000}};
  e.bytes(kF1);
  e.bytes(kF2);
  return decode_chopped(e.finish(), 5'000);
}();
static_assert(kChopped.rec.frames.count == 2 && kChopped.rec.errors.count == 0);
static_assert(kChopped.glitches == 0);

// Bad checksum.
constexpr Result kBadChecksum = [] {
  auto f = kF1;
  f[5] ^= 0x01;
  Encoder<256> e;
  e.bytes(f);
  e.bytes(kF2);
  return decode(e.finish());
}();
static_assert(kBadChecksum.rec.frames.count == 1 && same(kBadChecksum.rec.frames[0], kF2));
static_assert(kBadChecksum.rec.errors.count == 1 && kBadChecksum.rec.errors[0] == Error::Checksum);

// Frame cut short by idle, by a new A8, and by the end of the capture. The idle case continues with
// the rest of kF1: glued together they would form a valid frame, so this checks the gap is honoured.
constexpr Result kTruncated = [] {
  Encoder<512> e;
  e.bytes({0xA8, 0x31, 0x00});
  e.idle(2'000'000);                                     // > frame_gap_ns
  e.bytes({0x12, 0x01, 0x22, 0xA3});
  e.bytes({0xA8, 0x31});
  e.bytes(kF1);                                          // A8 restarts the frame
  e.bytes({0xA8, 0x31, 0x00, 0x12});                     // capture ends here
  return decode(e.finish(100'000));                      // shorter than frame_gap_ns
}();
static_assert(kTruncated.rec.frames.count == 1 && same(kTruncated.rec.frames[0], kF1));
static_assert(kTruncated.rec.stray.count == 4);
static_assert(kTruncated.rec.errors.count == 3);
static_assert(kTruncated.rec.errors[0] == Error::Truncated);
static_assert(kTruncated.rec.errors[1] == Error::Truncated);
static_assert(kTruncated.rec.errors[2] == Error::Truncated);

// The RMT end marker on its own closes the capture (no last=true needed).
constexpr bool rmt_end_marker() {
  Encoder<128> e;
  e.bytes({0xA8, 0x31, 0x00});
  const auto cap = to_rmt<64>(e.finish(), 1'000);
  scs::Decoder<Rec> d{};
  d.feed_rmt(cap.words.data(), cap.n, 1'000, /*last=*/false);
  const Rec &r = d.sink();
  return r.bytes.count == 3 && r.errors.count == 1 && r.errors[0] == Error::Truncated;
}
static_assert(rmt_end_marker());

// Byte with the high bit set inside a frame, and wrong frame lengths.
constexpr Result kFraming = [] {
  Encoder<1024> e;
  e.bytes({0xA8, 0x31, 0x85, 0x12, 0x01, 0xA7, 0xA3});           // 0x85 not allowed
  e.bytes({0xA8, 0x31, 0x00, 0x31, 0xA3});                       // 5 bytes, checksum fine
  e.bytes({0xA8, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});            // no A3 within 11 bytes
  e.idle(2'000'000);
  e.bytes(kF2);
  return decode(e.finish());
}();
static_assert(kFraming.rec.frames.count == 1 && same(kFraming.rec.frames[0], kF2));
static_assert(kFraming.rec.errors.count == 3);
static_assert(kFraming.rec.errors[0] == Error::NonDataByte);
static_assert(kFraming.rec.errors[1] == Error::Length);
static_assert(kFraming.rec.errors[2] == Error::Length);

// Bit-level faults: pulse in the stop bit, pulse between bit cells, stuck-low line.
constexpr Result kBitFaults = [] {
  Encoder<512> e;
  e.cells({0, 3, 9});
  e.idle(300'000);
  e.raw_pulse(0, 34'000);                    // start bit...
  e.raw_pulse(4 * 104'167 + 52'000, 34'000);  // ...then an edge half-way between cells
  e.idle(2'000'000);
  e.raw_pulse(0, 200'000);                   // 200 us low
  e.idle(1'000'000);
  e.bytes(kF1);
  return decode(e.finish());
}();
static_assert(kBitFaults.rec.frames.count == 1 && same(kBitFaults.rec.frames[0], kF1));
static_assert(kBitFaults.rec.errors.count == 3);
static_assert(kBitFaults.rec.errors[0] == Error::StopBit);
static_assert(kBitFaults.rec.errors[1] == Error::BitPhase);
static_assert(kBitFaults.rec.errors[2] == Error::LongLow);

// RMT path: 1 us ticks, quantised edges, end marker, idle longer than 15 bits split in two halves.
constexpr bool rmt_round_trip() {
  Encoder<512> e{{.jitter_ns = 4'000}};
  e.bytes(kF1);
  e.idle(40'000'000);  // 40 ms > 32767 ticks
  e.bytes(kF11);
  const auto cap = to_rmt<256>(e.finish(), 1'000);
  scs::Decoder<Rec> d{};
  d.feed_rmt(cap.words.data(), cap.n, 1'000);
  const Rec &r = d.sink();
  return r.frames.count == 2 && same(r.frames[0], kF1) && same(r.frames[1], kF11) && r.errors.count == 0;
}
static_assert(rmt_round_trip());

// RMT capture handed over in arbitrary chunks (partial receive).
constexpr bool rmt_chunked() {
  Encoder<512> e;
  e.bytes(kF2);
  e.bytes(kF1);
  const auto cap = to_rmt<256>(e.finish(), 1'000);
  scs::Decoder<Rec> d{};
  for (std::size_t i = 0; i < cap.n; i += 5) {
    const std::size_t n = cap.n - i < 5 ? cap.n - i : 5;
    d.feed_rmt(cap.words.data() + i, n, 1'000, /*last=*/i + n == cap.n);
  }
  const Rec &r = d.sink();
  return r.frames.count == 2 && same(r.frames[0], kF2) && same(r.frames[1], kF1) && r.errors.count == 0;
}
static_assert(rmt_chunked());

// ESPHome remote_receiver style raw data, written out by hand: the byte 0xA3 on its own.
// 0xA3 LSB first = 1,1,0,0,0,1,0,1 -> pulses in cells 0 (start), 3, 4, 5, 7.
constexpr Result kHandWritten = [] {
  constexpr int raw[] = {34, -279, 34, -70, 34, -70, 34, -174, 34, -2000};  // as std::vector<int>
  scs::Decoder<Rec> d{};
  d.feed_marks_spaces(raw, sizeof raw / sizeof raw[0]);
  return Result{d.sink(), d.glitches()};
}();
static_assert(kHandWritten.rec.bytes.count == 1 && kHandWritten.rec.bytes[0] == 0xA3);
static_assert(kHandWritten.rec.stray.count == 1 && kHandWritten.rec.stray[0] == 0xA3);
static_assert(kHandWritten.rec.errors.count == 0);

}  // namespace

int main() { return 0; }
