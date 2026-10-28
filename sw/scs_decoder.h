// SCS bus (BTicino / Legrand) decoder for edge-timed captures, e.g. from the ESP32 RMT peripheral.
//
// Physical layer, as seen on the RX pin of a passive tap (comparator output, *no* pulse stretcher):
//   - idle and '1' bits: line inactive (high on the tap's RO output)
//   - '0' bit: an active (low) pulse of ~34 us at the start of the 104.17 us bit cell (9600 baud)
//   - UART-like framing: start bit ('0'), 8 data bits LSB first, stop bit ('1')
// Frame layer:
//   A8 <payload...> <chk> A3, 7 or 11 bytes total, chk = XOR of the payload,
//   every byte between the delimiters is <= 0x7F.
//
// Only the falling edge (start) of each pulse carries timing; its width is only used to reject
// glitches and stuck-low lines. Bits are placed by rounding the edge offset from the start bit to the
// nearest bit cell, so sender clock error of a few percent is tolerated (see Timing::phase_tol_ns).
//
// Enable the MCU pull-up on the RX pin: the tap pulls low hard but releases through 100 k only, and the
// slow rise stretches every low, glitches included (see the pull-up tests).
//
// Everything is constexpr so the decoder can be tested at compile time (see scs_decoder_test.cpp).
// Requires C++20.
//
// Sink interface (all four are required):
//   void on_byte(uint8_t b);              // every decoded byte, before framing (debug / logging)
//   void on_stray(uint8_t b);             // byte received outside an A8...A3 frame
//   void on_frame(const scs::Frame &f);   // complete frame with valid length and checksum
//   void on_error(scs::Error e);
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace scs {

inline constexpr uint8_t kFrameStart = 0xA8;
inline constexpr uint8_t kFrameEnd = 0xA3;
inline constexpr std::size_t kShortFrameSize = 7;
inline constexpr std::size_t kLongFrameSize = 11;
inline constexpr std::size_t kMaxFrameSize = kLongFrameSize;

struct Timing {
  uint32_t bit_ns = 104'167;            // 9600 baud
  uint32_t min_low_ns = 8'000;          // shorter active pulses are glitches and are ignored
  uint32_t max_low_ns = 80'000;         // nominal 34 us; longer is not an SCS bit
  uint32_t phase_tol_ns = 36'000;       // max distance of an edge from its bit cell start
  uint32_t frame_gap_ns = 1'500'000;    // idle after a byte that abandons an unterminated frame
};

enum class Error : uint8_t {
  LongLow,      // active pulse longer than max_low_ns (bus fault, wrong polarity, stretched signal)
  BitPhase,     // edge too far from any bit cell boundary; byte dropped, edge taken as a new start bit
  StopBit,      // active pulse in the stop bit
  NonDataByte,  // byte > 0x7F other than A3 inside a frame
  Length,       // frame is not 7 or 11 bytes long
  Checksum,     // XOR of the payload does not match
  Truncated,    // frame not closed by A3 (idle, end of capture, or a new A8)
};

constexpr const char *to_string(Error e) {
  switch (e) {
    case Error::LongLow: return "long low";
    case Error::BitPhase: return "bit phase";
    case Error::StopBit: return "stop bit";
    case Error::NonDataByte: return "non-data byte in frame";
    case Error::Length: return "bad frame length";
    case Error::Checksum: return "checksum";
    case Error::Truncated: return "truncated frame";
  }
  return "?";
}

struct Frame {
  std::array<uint8_t, kMaxFrameSize> data{};
  uint8_t size = 0;

  constexpr uint8_t operator[](std::size_t i) const { return data[i]; }
  constexpr const uint8_t *begin() const { return data.data(); }
  constexpr const uint8_t *end() const { return data.data() + size; }
};

constexpr uint8_t checksum(const uint8_t *payload, std::size_t n) {
  uint8_t x = 0;
  for (std::size_t i = 0; i < n; ++i)
    x ^= payload[i];
  return x;
}

template <class Sink>
class Decoder {
 public:
  constexpr explicit Decoder(Sink sink = Sink{}, Timing timing = Timing{}) : sink_(sink), t_(timing) {}

  constexpr Sink &sink() { return sink_; }
  constexpr const Sink &sink() const { return sink_; }
  constexpr uint32_t glitches() const { return glitches_; }

  // One stretch of constant level. `active` is true for the pulse level (low on the tap output).
  // Consecutive segments of the same level are merged, so captures may be split anywhere.
  constexpr void segment(bool active, uint32_t ns) {
    if (ns == 0)
      return;
    if (active) {
      if (!in_low_) {
        in_low_ = true;
        low_start_ = now_;
      }
      now_ += ns;
      return;
    }
    if (in_low_) {
      in_low_ = false;
      pulse_(low_start_, now_ - low_start_);
    }
    now_ += ns;
    advance_(now_);
  }

  // The capture ended after a long idle (RMT idle threshold, ESPHome `idle:`). Time does not continue
  // into the next capture, so everything pending is resolved now.
  constexpr void end_of_capture() {
    if (in_low_) {
      in_low_ = false;
      pulse_(low_start_, now_ - low_start_);
    }
    if (in_byte_)
      finish_byte_();
    if (frame_.size != 0) {
      frame_.size = 0;
      sink_.on_error(Error::Truncated);
    }
    now_ = 0;
  }

  // ESP-IDF rmt_symbol_word_t (or anything with duration0/level0/duration1/level1).
  // A zero duration is the RMT end-of-capture marker. Pass last=false when feeding a capture in parts.
  template <class Symbol>
  constexpr void feed_rmt(const Symbol *symbols, std::size_t count, uint32_t tick_ns, bool last = true,
                          uint32_t active_level = 0) {
    for (std::size_t i = 0; i < count; ++i) {
      if (!rmt_half_(symbols[i].duration0, symbols[i].level0, tick_ns, active_level))
        return;
      if (!rmt_half_(symbols[i].duration1, symbols[i].level1, tick_ns, active_level))
        return;
    }
    if (last)
      end_of_capture();
  }

  // Signed durations in microseconds, positive = active pulse ("mark"), negative = idle ("space"),
  // the format of ESPHome's remote_receiver raw data (std::vector<int>; int32_t is long on ESP-IDF).
  template <class Int>
  constexpr void feed_marks_spaces(const Int *durations_us, std::size_t count, bool last = true) {
    for (std::size_t i = 0; i < count; ++i) {
      const auto d = durations_us[i];
      if (d > 0)
        segment(true, static_cast<uint32_t>(d) * 1000u);
      else if (d < 0)
        segment(false, static_cast<uint32_t>(-d) * 1000u);
    }
    if (last)
      end_of_capture();
  }

 private:
  constexpr bool rmt_half_(uint32_t duration, uint32_t level, uint32_t tick_ns, uint32_t active_level) {
    if (duration == 0) {
      end_of_capture();
      return false;
    }
    segment(level == active_level, duration * tick_ns);
    return true;
  }

  // Resolve everything that is decided by time `t` having passed without a new edge.
  constexpr void advance_(uint64_t t) {
    if (in_byte_ && t >= byte_start_ + uint64_t{t_.bit_ns} * 19 / 2)  // middle of the stop bit
      finish_byte_();
    if (frame_.size != 0 && !in_byte_ && t >= byte_end_ + t_.frame_gap_ns) {
      frame_.size = 0;
      sink_.on_error(Error::Truncated);
    }
  }

  constexpr void start_byte_(uint64_t start) {
    in_byte_ = true;
    byte_start_ = start;
    value_ = 0xFF;
  }

  constexpr void pulse_(uint64_t start, uint64_t width) {
    advance_(start);
    if (width < t_.min_low_ns) {
      ++glitches_;
      return;
    }
    if (width > t_.max_low_ns) {
      in_byte_ = false;
      frame_.size = 0;
      sink_.on_error(Error::LongLow);
      return;
    }
    if (!in_byte_) {
      start_byte_(start);
      return;
    }
    const uint64_t bit = t_.bit_ns;
    const uint64_t rel = start - byte_start_;
    const uint64_t idx = (rel + bit / 2) / bit;
    const uint64_t cell = idx * bit;
    const uint64_t phase = rel > cell ? rel - cell : cell - rel;
    if (phase > t_.phase_tol_ns) {
      frame_.size = 0;
      sink_.on_error(Error::BitPhase);
      start_byte_(start);
      return;
    }
    if (idx == 0) {  // second edge inside the start bit
      ++glitches_;
      return;
    }
    if (idx <= 8) {
      value_ = static_cast<uint8_t>(value_ & ~(1u << (idx - 1)));
      return;
    }
    // idx == 9; later edges are impossible because advance_() closes the byte mid stop bit
    in_byte_ = false;
    frame_.size = 0;
    sink_.on_error(Error::StopBit);
  }

  constexpr void finish_byte_() {
    in_byte_ = false;
    byte_end_ = byte_start_ + uint64_t{t_.bit_ns} * 10;
    sink_.on_byte(value_);
    frame_byte_(value_);
  }

  constexpr void frame_byte_(uint8_t b) {
    if (b == kFrameStart) {
      if (frame_.size != 0)
        sink_.on_error(Error::Truncated);
      frame_.data[0] = b;
      frame_.size = 1;
      return;
    }
    if (frame_.size == 0) {
      sink_.on_stray(b);
      return;
    }
    frame_.data[frame_.size++] = b;
    if (b == kFrameEnd) {
      complete_frame_();
      frame_.size = 0;
      return;
    }
    if (b & 0x80) {
      frame_.size = 0;
      sink_.on_error(Error::NonDataByte);
      return;
    }
    if (frame_.size == kMaxFrameSize) {  // no room left for A3
      frame_.size = 0;
      sink_.on_error(Error::Length);
    }
  }

  constexpr void complete_frame_() {
    const std::size_t n = frame_.size;
    if (n != kShortFrameSize && n != kLongFrameSize) {
      sink_.on_error(Error::Length);
      return;
    }
    if (checksum(frame_.data.data() + 1, n - 3) != frame_.data[n - 2]) {
      sink_.on_error(Error::Checksum);
      return;
    }
    sink_.on_frame(frame_);
  }

  Sink sink_;
  Timing t_;
  uint64_t now_ = 0;
  bool in_low_ = false;
  uint64_t low_start_ = 0;
  bool in_byte_ = false;
  uint64_t byte_start_ = 0;
  uint64_t byte_end_ = 0;
  uint8_t value_ = 0xFF;
  Frame frame_{};
  uint32_t glitches_ = 0;
};

}  // namespace scs
