// ESPHome glue: feeds remote_receiver raw captures (on_raw) into scs_decoder.h.
//
// remote_receiver must use an inverted pin, so that the tap's active low is a "mark", and an `idle`
// longer than any gap inside a frame (2 ms). Each capture is decoded on its own.
//
// Every valid frame is handed to the callback as hex ("A8 33 00 12 00 21 A3") together with its
// meaning ("command 33 on (from 00)", see scs_protocol.h). A5 acknowledgements are logged at DEBUG.
// A capture with a decoding error, any other byte outside a frame or a glitch is logged at WARN: a
// summary, the decoded bytes and the raw capture (mark/space durations in us), ready to be replayed
// with feed_marks_spaces().
#pragma once

#include <array>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "esphome/core/log.h"

#include "scs_decoder.h"
#include "scs_protocol.h"

namespace scs_esphome {

static const char *const TAG = "scs";

inline std::string hex(const uint8_t *p, std::size_t n) {
  std::string s;
  char b[4];
  for (std::size_t i = 0; i < n; ++i) {
    std::snprintf(b, sizeof b, i == 0 ? "%02X" : " %02X", p[i]);
    s += b;
  }
  return s;
}

// Collects what happened during one capture.
struct Sink {
  using FrameCallback = std::function<void(const std::string &frame, const std::string &message)>;
  FrameCallback on_frame_cb;
  std::array<uint8_t, 64> bytes{};
  std::size_t n_bytes = 0;
  std::array<scs::Error, 8> errors{};
  std::size_t n_errors = 0;
  std::size_t n_frames = 0;
  std::size_t n_stray = 0;

  void reset() { n_bytes = n_errors = n_frames = n_stray = 0; }

  void on_byte(uint8_t b) {
    if (n_bytes < bytes.size())
      bytes[n_bytes] = b;
    ++n_bytes;
  }
  void on_stray(uint8_t b) {
    if (b == scs::kAck)
      ESP_LOGD(TAG, "ack");
    else
      ++n_stray;
  }
  void on_error(scs::Error e) {
    if (n_errors < errors.size())
      errors[n_errors] = e;
    ++n_errors;
  }
  void on_frame(const scs::Frame &f) {
    ++n_frames;
    const std::string s = hex(f.begin(), f.size);
    const scs::Text m = scs::describe(scs::parse(f));
    ESP_LOGD(TAG, "frame %s: %s", s.c_str(), m.c_str());
    if (on_frame_cb)
      on_frame_cb(s, m.c_str());
  }
};

inline void dump_raw(const std::vector<int> &raw) {
  static const std::size_t PER_LINE = 24;  // "-32767" at most: fits the line buffer
  char line[256];
  for (std::size_t i = 0; i < raw.size(); i += PER_LINE) {
    int len = std::snprintf(line, sizeof line, "raw[%u]:", static_cast<unsigned>(i));
    for (std::size_t j = i; j < raw.size() && j < i + PER_LINE; ++j)
      len += std::snprintf(line + len, sizeof line - len, " %d", raw[j]);
    ESP_LOGW(TAG, "%s", line);
  }
}

inline void feed(const std::vector<int> &raw, Sink::FrameCallback on_frame) {
  static scs::Decoder<Sink> decoder;
  Sink &s = decoder.sink();
  s.on_frame_cb = std::move(on_frame);
  s.reset();
  const uint32_t glitches_before = decoder.glitches();
  decoder.feed_marks_spaces(raw.data(), raw.size());
  const uint32_t glitches = decoder.glitches() - glitches_before;
  if (s.n_errors == 0 && s.n_stray == 0 && glitches == 0)
    return;

  std::string errors;
  for (std::size_t i = 0; i < s.n_errors && i < s.errors.size(); ++i) {
    errors += i == 0 ? "" : ", ";
    errors += scs::to_string(s.errors[i]);
  }
  if (s.n_errors > s.errors.size())
    errors += ", ...";
  ESP_LOGW(TAG, "capture: %u frames, %u errors [%s], %u stray bytes, %u glitches, %u durations",
           static_cast<unsigned>(s.n_frames), static_cast<unsigned>(s.n_errors), errors.c_str(),
           static_cast<unsigned>(s.n_stray), static_cast<unsigned>(glitches), static_cast<unsigned>(raw.size()));
  ESP_LOGW(TAG, "bytes: %s%s", hex(s.bytes.data(), s.n_bytes < s.bytes.size() ? s.n_bytes : s.bytes.size()).c_str(),
           s.n_bytes > s.bytes.size() ? " ..." : "");
  dump_raw(raw);
}

}  // namespace scs_esphome
