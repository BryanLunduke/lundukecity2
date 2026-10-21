// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

// A couple of megabytes covers the shipped clips. Larger files, and
// symlinks to them, are not decoded.
inline constexpr std::size_t kMaxWavBytes = 2u * 1024u * 1024u;
// The mixer drops the oldest request once the queue reaches this length.
inline constexpr std::size_t kMaxQueuedSounds = 8;
// Keep this much audio queued on a voice stream. More than that makes a
// new sound wait behind audio that was already pushed.
inline constexpr int kAudioLatencyMs = 50;

inline int audio_queue_limit_bytes(int rate, int channels, int bytes_per_sample, int latency_ms)
{
    if (rate < 1) {
        rate = 1;
    }
    if (channels < 1) {
        channels = 1;
    }
    if (bytes_per_sample < 1) {
        bytes_per_sample = 1;
    }
    if (latency_ms < 1) {
        latency_ms = 1;
    }
    const long long bytes =
        static_cast<long long>(rate) * channels * bytes_per_sample * latency_ms / 1000;
    if (bytes < bytes_per_sample) {
        return bytes_per_sample;
    }
    if (bytes > 1024 * 1024) {
        return 1024 * 1024;
    }
    return static_cast<int>(bytes);
}

inline int audio_bytes_to_write(int queued, int limit, int available)
{
    if (available < 1 || limit < 1) {
        return 0;
    }
    const int room = limit - (queued > 0 ? queued : 0);
    if (room < 1) {
        return 0;
    }
    return available < room ? available : room;
}

// Regular files only, first name wins, nothing over kMaxWavBytes.
std::unordered_map<std::string, std::string> index_sound_directory(const std::string &dir);
// False when the file is missing, not a regular file, too large, or not PCM.
bool preview_wav_file(const std::string &path);

// Plays Micropolis wav assets when the engine asks for a sound by name.
// Each clip is decoded once. A worker gives every voice its own SDL audio
// stream on one device, and keeps at most about 50 ms queued on each
// stream so later sounds mix instead of waiting. Mute and shutdown stop
// that worker before SDL_Quit. If no device can be opened, play() returns
// without throwing. The dummy SDL audio driver is skipped.
class SoundPlayer {
public:
    SoundPlayer();
    ~SoundPlayer();

    SoundPlayer(const SoundPlayer &) = delete;
    SoundPlayer &operator=(const SoundPlayer &) = delete;

    void set_muted(bool muted);
    bool muted() const { return muted_.load(); }
    // Linear gain, 0..1. Applied to each voice stream.
    void set_volume(float volume);
    float volume() const { return volume_.load(); }
    // Join the mixer before SDL_Quit. Safe to call more than once.
    void shutdown();

    // Opens the sound library and tries one silent stream. Safe when no
    // audio device exists. Returns whether a device accepted the stream.
    bool probe();

    // engine_name is the Micropolis sound id, for example "Siren".
    // Returns false when muted, unknown, or no device. Never throws.
    bool play(const std::string &engine_name);

private:
    struct Mixer;

    void ensure_mixer();

    std::atomic<bool> muted_{false};
    std::atomic<float> volume_{1.0f};
    bool probed_ = false;
    bool device_ok_ = false;
    std::unique_ptr<Mixer> mixer_;
};
