#pragma once

#include "audio/audio_channel.hpp"
#include "core/scheduler/event_scheduler.hpp"
#include "cpu/arm_architecture/bus_interface.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/performance_profiler.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::audio {

/**
 * @brief Nintendo DS 16-channel ARM7 sound controller and stereo 32,768 Hz PCM mixer.
 *
 * Responsibility:
 *   Exposes ARM7 MMIO registers `0x04000400..0x0400051F` (`SOUND0CNT..SOUND15LEN`, `SOUNDCNT`,
 *   `SOUNDBIAS`, `SNDCAP0/1`), steps active PCM8, PCM16, IMA-ADPCM, PSG square-wave, and LFSR
 *   white-noise channels, mixes stereo 16-bit PCM frames at 32,768 Hz via `EventScheduler`, and
 *   buffers stereo sample pairs `(L, R)` for consumption by the frontend or headless tests.
 */
class AudioEngine final : public memory::MmioDevice {
public:
    /// Maximum buffered stereo sample values (`i16` elements = 4096 stereo frames).
    static constexpr std::size_t kMaxBufferedSamples = 8192U;

    AudioEngine(core::EventScheduler& scheduler, cpu::CpuBusInterface& arm7_bus) noexcept;

    void reset() noexcept;

    [[nodiscard]] u8 read8(Address address) noexcept override;
    [[nodiscard]] u16 read16(Address address) noexcept override;
    [[nodiscard]] u32 read32(Address address) noexcept override;
    void write8(Address address, u8 value) noexcept override;
    void write16(Address address, u16 value) noexcept override;
    void write32(Address address, u32 value) noexcept override;

    /**
     * @brief Drains up to `max_samples` interleaved `i16` values `(L, R, L, R...)` from buffer.
     */
    [[nodiscard]] std::vector<i16> drain_samples(std::size_t max_samples = kMaxBufferedSamples);
    void drain_all_into(std::vector<i16>& out) noexcept;

    [[nodiscard]] std::span<const i16> buffered_samples() const noexcept { return sample_buffer_; }
    [[nodiscard]] u16 soundcnt() const noexcept { return soundcnt_; }
    [[nodiscard]] u16 soundbias() const noexcept { return soundbias_; }
    [[nodiscard]] const std::array<AudioChannelState, kAudioChannelCount>& channels()
        const noexcept {
        return channels_;
    }

    void restore_state(u16 soundcnt,
                       u16 soundbias,
                       const std::array<AudioChannelState, kAudioChannelCount>& channels) noexcept;
    void set_profiler(primitives::PerformanceProfiler* profiler) noexcept { profiler_ = profiler; }

private:
    void on_sample_tick(TickCount late_ticks);
    void schedule_next_sample(TickCount late_ticks) noexcept;
    void write_channel_reg32(std::size_t channel_index, u32 reg_offset, u32 value) noexcept;
    void start_channel(std::size_t channel_index) noexcept;
    void step_channel(std::size_t channel_index) noexcept;
    void advance_sample_stream(std::size_t channel_index, AudioChannelState& ch) noexcept;
    void decode_next_adpcm_nibble(AudioChannelState& ch, u32 loop_bytes, u32 total_bytes) noexcept;

    core::EventScheduler& scheduler_;
    cpu::CpuBusInterface& arm7_bus_;
    std::array<AudioChannelState, kAudioChannelCount> channels_{};
    u16 soundcnt_ = 0U;
    u16 soundbias_ = 0x0200U;
    u8 sndcap0_cnt_ = 0U;
    u8 sndcap1_cnt_ = 0U;
    std::vector<i16> sample_buffer_;
    primitives::PerformanceProfiler* profiler_ = nullptr;
};

}  // namespace elmasds::audio
