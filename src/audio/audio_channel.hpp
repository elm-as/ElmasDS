#pragma once

#include "primitives/fixed_width_types.hpp"

#include <cstddef>

namespace elmasds::audio {

inline constexpr Address kAudioMmioStart = 0x04000400U;
inline constexpr Address kAudioMmioEnd = 0x0400051FU;
inline constexpr Address kRegSoundCnt = 0x04000500U;
inline constexpr Address kRegSoundBias = 0x04000504U;
inline constexpr Address kRegSndCap0Cnt = 0x04000508U;
inline constexpr Address kRegSndCap1Cnt = 0x04000509U;

inline constexpr std::size_t kAudioChannelCount = 16U;
inline constexpr u32 kAudioSampleRateHz = 32768U;
/// ARM9 ticks per 32,768 Hz audio frame (`67,027,964 / 32,768 ≈ 2045`).
inline constexpr TickCount kTicksPerAudioSample = 2045ULL;

/**
 * @brief Audio channel sample format encoded in `SOUNDxCNT` bits `29..30`.
 */
enum class AudioFormat : u8 {
    Pcm8 = 0,
    Pcm16 = 1,
    ImaAdpcm = 2,
    PsgOrNoise = 3,
};

/**
 * @brief Loop repeat mode encoded in `SOUNDxCNT` bits `27..28`.
 */
enum class AudioRepeatMode : u8 {
    Manual = 0,
    LoopInfinite = 1,
    OneShot = 2,
    Prohibited = 3,
};

/**
 * @brief Per-channel state and registers for one of the 16 Nintendo DS hardware sound channels.
 */
struct AudioChannelState {
    u32 control = 0U;
    Address source_address = 0U;
    u16 timer_reload = 0U;
    u16 loop_start_words = 0U;
    u32 length_words = 0U;

    bool active = false;
    u32 current_byte_offset = 0U;
    u32 phase_accumulator = 0U;
    i32 current_sample = 0;

    i32 adpcm_value = 0;
    i32 adpcm_step_index = 0;
    i32 adpcm_loop_value = 0;
    i32 adpcm_loop_step_index = 0;
    bool adpcm_header_loaded = false;
    bool adpcm_high_nibble = false;

    u8 psg_duty_step = 0U;
    u16 noise_lfsr = 0x7FFFU;

    [[nodiscard]] u8 volume_mul() const noexcept {
        return static_cast<u8>(control & 0x7FU);
    }

    [[nodiscard]] u8 volume_div_shift() const noexcept {
        const u8 code = static_cast<u8>((control >> 8U) & 0x3U);
        if (code == 3U) {
            return 4U;
        }
        return code;
    }

    [[nodiscard]] u8 panning() const noexcept {
        return static_cast<u8>((control >> 16U) & 0x7FU);
    }

    [[nodiscard]] u8 wave_duty() const noexcept {
        return static_cast<u8>((control >> 24U) & 0x7U);
    }

    [[nodiscard]] AudioRepeatMode repeat_mode() const noexcept {
        return static_cast<AudioRepeatMode>((control >> 27U) & 0x3U);
    }

    [[nodiscard]] AudioFormat format() const noexcept {
        return static_cast<AudioFormat>((control >> 29U) & 0x3U);
    }
};

}  // namespace elmasds::audio
