// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "sound_player.hpp"

#include "assets.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

bool ensure_audio()
{
    if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        return true;
    }
    return SDL_InitSubSystem(SDL_INIT_AUDIO);
}

std::string normalize_name(std::string text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        if (ch == '.' || ch == '-' || ch == '_' || ch == ' ') {
            continue;
        }
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<unsigned char>(ch - 'A' + 'a');
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

struct Clip {
    int rate = 22050;
    std::vector<std::int16_t> samples;
};

bool parse_wav(const std::string &path, Clip &clip)
{
    std::error_code ec;
    const auto kind = std::filesystem::symlink_status(path, ec).type();
    if (ec || kind != std::filesystem::file_type::regular) {
        return false;
    }
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes < 44 || bytes > kMaxWavBytes) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<unsigned char> data(static_cast<std::size_t>(bytes));
    in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (static_cast<std::size_t>(in.gcount()) != data.size()) {
        return false;
    }
    if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    int format = 0;
    int channels = 0;
    int rate = 0;
    int bits = 0;
    const unsigned char *pcm = nullptr;
    std::size_t pcm_bytes = 0;

    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const unsigned char *chunk = data.data() + pos;
        const std::uint32_t size = static_cast<std::uint32_t>(chunk[4]) |
                                   (static_cast<std::uint32_t>(chunk[5]) << 8) |
                                   (static_cast<std::uint32_t>(chunk[6]) << 16) |
                                   (static_cast<std::uint32_t>(chunk[7]) << 24);
        const unsigned char *body = chunk + 8;
        if (pos + 8 + size > data.size()) {
            break;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            format = body[0] | (body[1] << 8);
            channels = body[2] | (body[3] << 8);
            rate = static_cast<int>(body[4] | (body[5] << 8) | (body[6] << 16) | (body[7] << 24));
            bits = body[14] | (body[15] << 8);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            pcm = body;
            pcm_bytes = size;
        }
        pos += 8 + size + (size & 1u);
    }

    // Format 1 is linear PCM. μ-law files are kept on disk but not played.
    if (format != 1 || pcm == nullptr || channels < 1 || channels > 2 || rate < 1000) {
        return false;
    }
    if (bits != 8 && bits != 16) {
        return false;
    }

    clip.rate = rate;
    clip.samples.clear();
    if (bits == 16) {
        const std::size_t frames = pcm_bytes / (static_cast<std::size_t>(channels) * 2);
        clip.samples.reserve(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            int mixed = 0;
            for (int c = 0; c < channels; ++c) {
                const std::size_t off = (i * static_cast<std::size_t>(channels) + c) * 2;
                int sample = pcm[off] | (pcm[off + 1] << 8);
                if (sample & 0x8000) {
                    sample -= 0x10000;
                }
                mixed += sample;
            }
            clip.samples.push_back(static_cast<std::int16_t>(mixed / channels));
        }
    } else {
        const std::size_t frames = pcm_bytes / static_cast<std::size_t>(channels);
        clip.samples.reserve(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            int mixed = 0;
            for (int c = 0; c < channels; ++c) {
                const int sample = static_cast<int>(pcm[i * static_cast<std::size_t>(channels) + c]) - 128;
                mixed += sample << 8;
            }
            clip.samples.push_back(static_cast<std::int16_t>(mixed / channels));
        }
    }
    return !clip.samples.empty();
}

constexpr int kMixRate = 22050;

std::vector<std::int16_t> resample(const std::vector<std::int16_t> &in, int from_rate, int to_rate)
{
    if (in.empty() || from_rate <= 0 || to_rate <= 0 || from_rate == to_rate) {
        return in;
    }
    const double step = static_cast<double>(from_rate) / static_cast<double>(to_rate);
    const auto out_n = static_cast<std::size_t>(static_cast<double>(in.size()) / step);
    std::vector<std::int16_t> out;
    out.reserve(out_n);
    for (std::size_t i = 0; i < out_n; ++i) {
        const double pos = static_cast<double>(i) * step;
        const auto i0 = static_cast<std::size_t>(pos);
        if (i0 >= in.size()) {
            break;
        }
        const auto i1 = std::min(i0 + 1, in.size() - 1);
        const double frac = pos - static_cast<double>(i0);
        const double mixed = static_cast<double>(in[i0]) * (1.0 - frac) + static_cast<double>(in[i1]) * frac;
        int sample = static_cast<int>(mixed);
        if (sample > 32767) {
            sample = 32767;
        }
        if (sample < -32768) {
            sample = -32768;
        }
        out.push_back(static_cast<std::int16_t>(sample));
    }
    return out;
}

bool wav_file_ok(const std::string &path)
{
    Clip clip;
    return parse_wav(path, clip);
}

} // namespace

bool preview_wav_file(const std::string &path)
{
    return wav_file_ok(path);
}

std::unordered_map<std::string, std::string> index_sound_directory(const std::string &dir)
{
    std::unordered_map<std::string, std::string> map;
    if (dir.empty()) {
        return map;
    }
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        // symlink_status does not follow a link to a huge file.
        if (entry.symlink_status(ec).type() != std::filesystem::file_type::regular) {
            continue;
        }
        const auto name = entry.path().filename().string();
        if (name.size() < 5 || name.substr(name.size() - 4) != ".wav") {
            continue;
        }
        const auto bytes = entry.file_size(ec);
        if (ec || bytes > kMaxWavBytes) {
            continue;
        }
        const std::string key = normalize_name(name.substr(0, name.size() - 4));
        // The first regular file under the cap wins. A larger collision
        // used to replace the shipped clip.
        if (map.find(key) == map.end()) {
            map.emplace(key, entry.path().string());
        }
    }
    return map;
}

namespace {

// Paths only. The PCM itself is decoded once, on the mixer thread.
const std::unordered_map<std::string, std::string> &sound_index()
{
    static const std::unordered_map<std::string, std::string> index = [] {
        const std::string dir = asset_root().empty() ? std::string() : asset_root() + "/res/sounds";
        return index_sound_directory(dir);
    }();
    return index;
}

bool open_device(int rate, SDL_AudioStream *&stream)
{
    stream = nullptr;
    if (!ensure_audio()) {
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 1;
    spec.freq = rate;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
        return false;
    }
    return true;
}

} // namespace

struct SoundPlayer::Mixer {
    struct Voice {
        std::shared_ptr<const std::vector<std::int16_t>> samples;
        std::size_t pos = 0;
    };

    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::string> pending;
    std::mutex clip_mu;
    std::unordered_map<std::string, std::shared_ptr<const std::vector<std::int16_t>>> clips;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<float> gain{1.0f};
    bool muted = false;
    bool started = false;

    ~Mixer()
    {
        stop.store(true);
        cv.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::shared_ptr<const std::vector<std::int16_t>> clip_for(const std::string &path)
    {
        {
            std::lock_guard<std::mutex> lock(clip_mu);
            const auto found = clips.find(path);
            if (found != clips.end()) {
                return found->second;
            }
        }
        Clip parsed;
        if (!parse_wav(path, parsed)) {
            return {};
        }
        auto samples = std::make_shared<const std::vector<std::int16_t>>(resample(parsed.samples, parsed.rate, kMixRate));
        if (samples->empty()) {
            return {};
        }
        std::lock_guard<std::mutex> lock(clip_mu);
        const auto found = clips.find(path);
        if (found != clips.end()) {
            return found->second;
        }
        clips.emplace(path, samples);
        return samples;
    }

    void request(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mu);
        if (muted || stop.load()) {
            return;
        }
        if (pending.size() >= kMaxQueuedSounds) {
            pending.erase(pending.begin());
        }
        pending.push_back(path);
        cv.notify_all();
    }

    void set_muted(bool on)
    {
        std::lock_guard<std::mutex> lock(mu);
        muted = on;
        if (on) {
            pending.clear();
        }
        cv.notify_all();
    }

    void loop()
    {
        if (!ensure_audio()) {
            return;
        }
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = 1;
        spec.freq = kMixRate;
        SDL_AudioDeviceID device = 0;
        struct Playing {
            Voice voice;
            SDL_AudioStream *stream = nullptr;
        };
        std::vector<Playing> playing;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_start;
        const int limit = audio_queue_limit_bytes(kMixRate, 1, static_cast<int>(sizeof(std::int16_t)), kAudioLatencyMs);
        auto drop_playing = [&]() {
            for (Playing &item : playing) {
                if (item.stream != nullptr) {
                    SDL_UnbindAudioStream(item.stream);
                    SDL_DestroyAudioStream(item.stream);
                    item.stream = nullptr;
                }
            }
            playing.clear();
        };
        auto close_device = [&]() {
            drop_playing();
            if (device != 0) {
                SDL_CloseAudioDevice(device);
                device = 0;
            }
        };
        auto ensure_device = [&]() {
            if (device != 0) {
                return true;
            }
            device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
            if (device == 0) {
                return false;
            }
            if (!SDL_ResumeAudioDevice(device)) {
                SDL_CloseAudioDevice(device);
                device = 0;
                return false;
            }
            return true;
        };
        std::chrono::steady_clock::time_point quiet_since = std::chrono::steady_clock::now();
        while (!stop.load()) {
            std::vector<std::string> todo;
            bool quiet = false;
            {
                std::unique_lock<std::mutex> lock(mu);
                if (playing.empty() && pending.empty()) {
                    if (device != 0) {
                        cv.wait_for(lock, std::chrono::milliseconds(kAudioIdleCloseMs),
                                    [&] { return stop.load() || !pending.empty() || muted; });
                    } else if (muted) {
                        cv.wait(lock, [&] { return stop.load() || !muted; });
                    } else {
                        cv.wait(lock, [&] { return stop.load() || !pending.empty() || muted; });
                    }
                }
                if (stop.load()) {
                    break;
                }
                if (muted) {
                    pending.clear();
                    quiet = true;
                } else {
                    todo.swap(pending);
                }
            }
            if (quiet) {
                drop_playing();
                const auto now = std::chrono::steady_clock::now();
                if (now - quiet_since >= std::chrono::milliseconds(kAudioIdleCloseMs)) {
                    close_device();
                }
                continue;
            }
            if (!ensure_device()) {
                continue;
            }
            for (const auto &path : todo) {
                if (stop.load()) {
                    break;
                }
                const auto now = std::chrono::steady_clock::now();
                const auto prior = last_start.find(path);
                if (prior != last_start.end() &&
                    now - prior->second < std::chrono::milliseconds(kSoundCooldownMs)) {
                    continue;
                }
                auto clip = clip_for(path);
                if (!clip) {
                    continue;
                }
                while (static_cast<int>(playing.size()) >= kMaxVoices) {
                    Playing &oldest = playing.front();
                    if (oldest.stream != nullptr) {
                        SDL_UnbindAudioStream(oldest.stream);
                        SDL_DestroyAudioStream(oldest.stream);
                    }
                    playing.erase(playing.begin());
                }
                last_start[path] = now;
                SDL_AudioStream *stream = SDL_CreateAudioStream(&spec, &spec);
                if (stream == nullptr || !SDL_BindAudioStream(device, stream)) {
                    if (stream != nullptr) {
                        SDL_DestroyAudioStream(stream);
                    }
                    continue;
                }
                SDL_SetAudioStreamGain(stream, gain.load());
                playing.push_back(Playing{Voice{std::move(clip), 0}, stream});
            }
            bool fed = false;
            for (auto it = playing.begin(); it != playing.end();) {
                if (stop.load()) {
                    break;
                }
                SDL_SetAudioStreamGain(it->stream, gain.load());
                const int queued = SDL_GetAudioStreamQueued(it->stream);
                const auto &samples = *it->voice.samples;
                const int available =
                    static_cast<int>((samples.size() - it->voice.pos) * sizeof(std::int16_t));
                int bytes = audio_bytes_to_write(queued, limit, available);
                bytes -= bytes % static_cast<int>(sizeof(std::int16_t));
                if (bytes > 0) {
                    if (!SDL_PutAudioStreamData(it->stream, samples.data() + it->voice.pos, bytes)) {
                        SDL_UnbindAudioStream(it->stream);
                        SDL_DestroyAudioStream(it->stream);
                        it = playing.erase(it);
                        continue;
                    }
                    it->voice.pos += static_cast<std::size_t>(bytes / static_cast<int>(sizeof(std::int16_t)));
                    fed = true;
                }
                const bool finished = it->voice.pos >= samples.size() && SDL_GetAudioStreamQueued(it->stream) <= 0;
                if (finished) {
                    SDL_UnbindAudioStream(it->stream);
                    SDL_DestroyAudioStream(it->stream);
                    it = playing.erase(it);
                } else {
                    ++it;
                }
            }
            if (stop.load()) {
                break;
            }
            if (!fed) {
                std::unique_lock<std::mutex> lock(mu);
                if (pending.empty() && !muted) {
                    cv.wait_for(lock, std::chrono::milliseconds(10), [&] { return stop.load() || !pending.empty() || muted; });
                }
            }
            if (playing.empty()) {
                const auto now = std::chrono::steady_clock::now();
                if (now - quiet_since >= std::chrono::milliseconds(kAudioIdleCloseMs)) {
                    close_device();
                }
            } else {
                quiet_since = std::chrono::steady_clock::now();
            }
        }
        close_device();
    }
};

SoundPlayer::SoundPlayer() = default;

SoundPlayer::~SoundPlayer() = default;

void SoundPlayer::set_muted(bool muted)
{
    muted_.store(muted);
    if (mixer_) {
        mixer_->set_muted(muted);
    }
}

void SoundPlayer::set_volume(float volume)
{
    if (volume < 0.0f) {
        volume = 0.0f;
    }
    if (volume > 1.0f) {
        volume = 1.0f;
    }
    volume_.store(volume);
    if (mixer_) {
        mixer_->gain.store(volume);
        mixer_->cv.notify_all();
    }
}

void SoundPlayer::shutdown()
{
    if (!mixer_) {
        return;
    }
    mixer_->stop.store(true);
    mixer_->cv.notify_all();
    if (mixer_->thread.joinable()) {
        mixer_->thread.join();
    }
    mixer_.reset();
}

bool SoundPlayer::probe()
{
    if (probed_) {
        return device_ok_;
    }
    probed_ = true;
    SDL_AudioStream *stream = nullptr;
    device_ok_ = open_device(kMixRate, stream);
    if (stream != nullptr) {
        SDL_DestroyAudioStream(stream);
    }
    return device_ok_;
}

void SoundPlayer::ensure_mixer()
{
    if (!mixer_) {
        mixer_ = std::make_unique<Mixer>();
    }
    if (mixer_->started) {
        return;
    }
    mixer_->started = true;
    mixer_->muted = muted_.load();
    mixer_->gain.store(volume_.load());
    Mixer *mixer = mixer_.get();
    mixer_->thread = std::thread([mixer] { mixer->loop(); });
}

bool SoundPlayer::play(const std::string &engine_name)
{
    if (muted_.load() || engine_name.empty()) {
        return false;
    }
    // The dummy driver used by headless tests frees the simplified stream
    // twice if a mixer thread is pumping it. Real devices are unaffected.
    if (const char *driver = SDL_GetCurrentAudioDriver()) {
        if (std::strcmp(driver, "dummy") == 0) {
            return false;
        }
    }
    std::string key = normalize_name(engine_name);
    // Upstream res/sounds has no fog-horn sample. Skip it rather than substitute.
    if (key == "foghornlow") {
        return false;
    }
    const auto &index = sound_index();
    const auto found = index.find(key);
    if (found == index.end()) {
        return false;
    }
    if (!probe() || !device_ok_) {
        return false;
    }
    ensure_mixer();
    mixer_->request(found->second);
    return true;
}
