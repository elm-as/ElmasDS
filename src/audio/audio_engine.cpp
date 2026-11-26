#include "audio/audio_engine.hpp"

#include "primitives/bit_operations.hpp"

#include <algorithm>

namespace elmasds::audio {

namespace {

constexpr std::array<i32, 8U> kAdpcmIndexTable = {-1, -1, -1, -1, 2, 4, 6, 8};

constexpr std::array<i32, 89U> kAdpcmStepTable = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

[[nodiscard]] i16 clamp_s16(i32 value) noexcept { return static_cast<i16>(std::clamp(value, -32768, 32767)); }

}  // namespace

AudioEngine::AudioEngine(core::EventScheduler& scheduler, cpu::CpuBusInterface& arm7_bus) noexcept
    : scheduler_(scheduler), arm7_bus_(arm7_bus) {
    sample_buffer_.reserve(kMaxBufferedSamples);
    reset();
}

void AudioEngine::reset() noexcept {
    for (auto& ch : channels_) { ch = AudioChannelState{}; }
    soundcnt_ = 0U; soundbias_ = 0x0200U; sndcap0_cnt_ = 0U; sndcap1_cnt_ = 0U;
    sample_buffer_.clear();
    scheduler_.cancel_by_type(core::EventType::AudioSample);
    schedule_next_sample(0U);
}

void AudioEngine::schedule_next_sample(TickCount late_ticks) noexcept {
    const TickCount delay =
        (late_ticks < kTicksPerAudioSample) ? (kTicksPerAudioSample - late_ticks) : 1ULL;
    scheduler_.schedule(core::ScheduleRequest{
        core::EventType::AudioSample,
        delay,
        core::bind_event_handler<AudioEngine, &AudioEngine::on_sample_tick>(this),
    });
}

u8 AudioEngine::read8(Address address) noexcept {
    if (address == kRegSndCap0Cnt || address == kRegSndCap1Cnt) {
        return (address == kRegSndCap0Cnt) ? sndcap0_cnt_ : sndcap1_cnt_;
    }
    return static_cast<u8>((read32(address & ~3U) >> ((address & 3U) * 8U)) & 0xFFU);
}

u16 AudioEngine::read16(Address address) noexcept {
    const Address aligned = address & ~1U;
    if (aligned == kRegSoundCnt || aligned == kRegSoundBias) {
        return (aligned == kRegSoundCnt) ? soundcnt_ : soundbias_;
    }
    return static_cast<u16>((read32(aligned & ~3U) >> ((aligned & 2U) * 8U)) & 0xFFFFU);
}

u32 AudioEngine::read32(Address address) noexcept {
    const Address aligned = address & ~3U;
    if (aligned < 0x04000500U) {
        const std::size_t ch_idx = ((aligned - kAudioMmioStart) >> 4U) & 0xFU;
        return ((aligned & 0xFU) == 0x0U) ? channels_[ch_idx].control : 0U;
    }
    if (aligned == kRegSoundCnt) { return soundcnt_; }
    if (aligned == kRegSoundBias) { return soundbias_; }
    if (aligned == kRegSndCap0Cnt) {
        return static_cast<u32>(sndcap0_cnt_) | (static_cast<u32>(sndcap1_cnt_) << 8U);
    }
    return 0U;
}

void AudioEngine::write8(Address address, u8 value) noexcept {
    if (address == kRegSndCap0Cnt) { sndcap0_cnt_ = value; return; }
    if (address == kRegSndCap1Cnt) { sndcap1_cnt_ = value; return; }
    const Address aligned = address & ~3U;
    const u32 shift = (address & 3U) * 8U;
    write32(aligned, (read32(aligned) & ~(0xFFU << shift)) | (static_cast<u32>(value) << shift));
}

void AudioEngine::write16(Address address, u16 value) noexcept {
    const Address aligned = address & ~1U;
    if (aligned == kRegSoundCnt) { soundcnt_ = value & 0xBF7FU; return; }
    if (aligned == kRegSoundBias) { soundbias_ = value & 0x03FFU; return; }
    if (aligned == kRegSndCap0Cnt) {
        sndcap0_cnt_ = static_cast<u8>(value & 0xFFU);
        sndcap1_cnt_ = static_cast<u8>((value >> 8U) & 0xFFU);
        return;
    }
    if (aligned < 0x04000500U) {
        const std::size_t ch_idx = ((aligned - kAudioMmioStart) >> 4U) & 0xFU;
        const u32 offset = aligned & 0xFU;
        if (offset == 0x8U) { channels_[ch_idx].timer_reload = value; return; }
        if (offset == 0xAU) { channels_[ch_idx].loop_start_words = value; return; }
        const Address word_addr = aligned & ~3U;
        const u32 shift = (aligned & 2U) * 8U;
        write32(word_addr,
                (read32(word_addr) & ~(0xFFFFU << shift)) | (static_cast<u32>(value) << shift));
    }
}

void AudioEngine::write32(Address address, u32 value) noexcept {
    const Address aligned = address & ~3U;
    if (aligned < 0x04000500U) {
        const std::size_t ch_idx = ((aligned - kAudioMmioStart) >> 4U) & 0xFU;
        write_channel_reg32(ch_idx, aligned & 0xFU, value);
        return;
    }
    write16(aligned, static_cast<u16>(value & 0xFFFFU));
    write16(aligned + 2U, static_cast<u16>((value >> 16U) & 0xFFFFU));
}

void AudioEngine::write_channel_reg32(std::size_t channel_index,
                                      u32 reg_offset,
                                      u32 value) noexcept {
    auto& ch = channels_[channel_index];
    switch (reg_offset) {
    case 0x0U: {
        const bool was_active = ch.active;
        const bool now_active = bits::test_bit(value, 31U);
        ch.control = value & 0xFF7F837FU;
        if (now_active && !was_active) {
            start_channel(channel_index);
        } else if (!now_active) {
            ch.active = false;
        }
        break;
    }
    case 0x4U: ch.source_address = value & 0x07FFFFFCU; break;
    case 0x8U:
        ch.timer_reload = static_cast<u16>(value & 0xFFFFU);
        ch.loop_start_words = static_cast<u16>((value >> 16U) & 0xFFFFU);
        break;
    case 0xCU: ch.length_words = value & 0x003FFFFFU; break;
    default: break;
    }
}

void AudioEngine::start_channel(std::size_t channel_index) noexcept {
    auto& ch = channels_[channel_index];
    ch.active = true;
    ch.current_byte_offset = 0U;
    ch.phase_accumulator = 0U;
    ch.current_sample = 0;
    ch.adpcm_header_loaded = false;
    ch.adpcm_high_nibble = false;
    ch.psg_duty_step = 0U;
    ch.noise_lfsr = 0x7FFFU;
}

void AudioEngine::decode_next_adpcm_nibble(AudioChannelState& ch,
                                           u32 loop_bytes,
                                           u32 total_bytes) noexcept {
    if (!ch.adpcm_header_loaded) {
        const u32 header = arm7_bus_.read32(ch.source_address);
        ch.adpcm_value = static_cast<i16>(header & 0xFFFFU);
        ch.adpcm_step_index = std::clamp<i32>(static_cast<i32>((header >> 16U) & 0x7FU), 0, 88);
        ch.adpcm_loop_value = ch.adpcm_value;
        ch.adpcm_loop_step_index = ch.adpcm_step_index;
        ch.current_byte_offset = 4U;
        ch.adpcm_header_loaded = true;
        ch.adpcm_high_nibble = false;
        ch.current_sample = ch.adpcm_value;
        return;
    }

    if (ch.current_byte_offset >= total_bytes) {
        if (ch.repeat_mode() == AudioRepeatMode::LoopInfinite && total_bytes > 4U) {
            ch.current_byte_offset = std::max<u32>(4U, loop_bytes);
            ch.adpcm_value = ch.adpcm_loop_value;
            ch.adpcm_step_index = ch.adpcm_loop_step_index;
            ch.adpcm_high_nibble = false;
        } else {
            ch.active = false;
            ch.control &= ~0x80000000U;
            ch.current_sample = 0;
            return;
        }
    }

    if (ch.current_byte_offset == loop_bytes && !ch.adpcm_high_nibble) {
        ch.adpcm_loop_value = ch.adpcm_value;
        ch.adpcm_loop_step_index = ch.adpcm_step_index;
    }

    const u8 byte = arm7_bus_.read8(ch.source_address + ch.current_byte_offset);
    const u8 nibble = ch.adpcm_high_nibble ? static_cast<u8>((byte >> 4U) & 0xFU)
                                           : static_cast<u8>(byte & 0xFU);
    if (ch.adpcm_high_nibble) {
        ++ch.current_byte_offset;
    }
    ch.adpcm_high_nibble = !ch.adpcm_high_nibble;

    const i32 step = kAdpcmStepTable[static_cast<std::size_t>(ch.adpcm_step_index)];
    i32 diff = step >> 3;
    if ((nibble & 1U) != 0U) { diff += step >> 2; }
    if ((nibble & 2U) != 0U) { diff += step >> 1; }
    if ((nibble & 4U) != 0U) { diff += step; }
    ch.adpcm_value = ((nibble & 8U) != 0U) ? std::max(-32767, ch.adpcm_value - diff)
                                           : std::min(32767, ch.adpcm_value + diff);
    ch.adpcm_step_index = std::clamp(
        ch.adpcm_step_index + kAdpcmIndexTable[static_cast<std::size_t>(nibble & 7U)], 0, 88);
    ch.current_sample = ch.adpcm_value;
}

void AudioEngine::advance_sample_stream(std::size_t channel_index,
                                        AudioChannelState& ch) noexcept {
    const AudioFormat fmt = ch.format();
    if (fmt == AudioFormat::PsgOrNoise) {
        if (channel_index >= 8U && channel_index <= 13U) {
            const u8 duty = ch.wave_duty();
            ch.current_sample = (ch.psg_duty_step <= duty) ? 0x7FFF : -0x7FFF;
            ch.psg_duty_step = static_cast<u8>((ch.psg_duty_step + 1U) & 7U);
            return;
        }
        if (channel_index >= 14U) {
            const bool carry = (ch.noise_lfsr & 1U) != 0U;
            ch.noise_lfsr >>= 1U;
            if (carry) {
                ch.noise_lfsr ^= 0x6000U;
                ch.current_sample = -0x7FFF;
            } else {
                ch.current_sample = 0x7FFF;
            }
            return;
        }
        ch.current_sample = 0;
        return;
    }

    const u32 loop_bytes = static_cast<u32>(ch.loop_start_words) * 4U;
    const u32 total_bytes = loop_bytes + (ch.length_words * 4U);
    if (total_bytes == 0U) {
        ch.active = false;
        ch.control &= ~0x80000000U;
        return;
    }

    if (fmt == AudioFormat::ImaAdpcm) {
        decode_next_adpcm_nibble(ch, loop_bytes, total_bytes);
        return;
    }

    if (ch.current_byte_offset >= total_bytes) {
        if (ch.repeat_mode() == AudioRepeatMode::LoopInfinite) {
            ch.current_byte_offset = loop_bytes;
        } else {
            ch.active = false;
            ch.control &= ~0x80000000U;
            ch.current_sample = 0;
            return;
        }
    }

    if (fmt == AudioFormat::Pcm8) {
        const auto s = static_cast<i8>(arm7_bus_.read8(ch.source_address + ch.current_byte_offset));
        ch.current_sample = static_cast<i32>(s) << 8;
        ++ch.current_byte_offset;
        return;
    }

    const auto s = static_cast<i16>(arm7_bus_.read16(ch.source_address + ch.current_byte_offset));
    ch.current_sample = static_cast<i32>(s);
    ch.current_byte_offset += 2U;
}

void AudioEngine::step_channel(std::size_t channel_index) noexcept {
    auto& ch = channels_[channel_index];
    if (!ch.active) {
        return;
    }

    const u32 period = std::max<u32>(1U, 0x10000U - ch.timer_reload);
    // Hardware sample timer ticks at 16,756,991 Hz = 512 timer ticks per 32,768 Hz output sample.
    ch.phase_accumulator += 512U;
    while (ch.phase_accumulator >= period && ch.active) {
        ch.phase_accumulator -= period;
        advance_sample_stream(channel_index, ch);
    }
}

void AudioEngine::on_sample_tick(TickCount late_ticks) {
    primitives::ScopedProfile profile_scope(profiler_, primitives::ProfileCategory::Audio);
    schedule_next_sample(late_ticks);

    if (!bits::test_bit(soundcnt_, 15U)) {
        if (sample_buffer_.size() + 2U <= kMaxBufferedSamples) {
            sample_buffer_.push_back(0);
            sample_buffer_.push_back(0);
        }
        return;
    }

    i64 left_mix = 0;
    i64 right_mix = 0;
    for (std::size_t i = 0U; i < kAudioChannelCount; ++i) {
        step_channel(i);
        const auto& ch = channels_[i];
        if (!ch.active) {
            continue;
        }
        const i32 vol_scaled = (ch.current_sample * static_cast<i32>(ch.volume_mul())) >>
                               (7U + ch.volume_div_shift());
        const i32 pan = static_cast<i32>(ch.panning());
        left_mix += (vol_scaled * (128 - pan)) >> 7;
        right_mix += (vol_scaled * pan) >> 7;
    }

    const i32 master_vol = static_cast<i32>(soundcnt_ & 0x7FU);
    const i16 out_left = clamp_s16(static_cast<i32>((left_mix * master_vol) >> 7));
    const i16 out_right = clamp_s16(static_cast<i32>((right_mix * master_vol) >> 7));

    if (sample_buffer_.size() + 2U <= kMaxBufferedSamples) {
        sample_buffer_.push_back(out_left);
        sample_buffer_.push_back(out_right);
    }
}

std::vector<i16> AudioEngine::drain_samples(std::size_t max_samples) {
    primitives::ScopedProfile profile_scope(profiler_, primitives::ProfileCategory::Audio);
    const std::size_t count = std::min(max_samples, sample_buffer_.size()) & ~std::size_t{1U};
    std::vector<i16> drained(sample_buffer_.begin(), sample_buffer_.begin() + static_cast<std::ptrdiff_t>(count));
    sample_buffer_.erase(sample_buffer_.begin(), sample_buffer_.begin() + static_cast<std::ptrdiff_t>(count));
    return drained;
}

void AudioEngine::drain_all_into(std::vector<i16>& out) noexcept {
    primitives::ScopedProfile profile_scope(profiler_, primitives::ProfileCategory::Audio);
    if (out.capacity() < kMaxBufferedSamples) { out.reserve(kMaxBufferedSamples); }
    out.clear();
    out.swap(sample_buffer_);
    if ((out.size() & 1U) != 0U) {
        sample_buffer_.push_back(out.back());
        out.pop_back();
    }
}

void AudioEngine::restore_state(u16 soundcnt, u16 soundbias,
                                const std::array<AudioChannelState, kAudioChannelCount>& channels) noexcept {
    soundcnt_ = soundcnt; soundbias_ = soundbias; channels_ = channels; sample_buffer_.clear();
}

}  // namespace elmasds::audio
