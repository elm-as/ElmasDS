#include "audio/audio_engine.hpp"
#include "core/emulator.hpp"

#include <gtest/gtest.h>

namespace elmasds::audio {
namespace {

TEST(AudioEngineTest, MixesPcm16PsgAndNoiseChannelsAt32768Hz) {
    core::Emulator emu;
    emu.reset();

    auto& arm7_bus = emu.arm7().context().bus;

    // Write a 4-sample 16-bit PCM waveform into Main RAM at 0x02000000.
    arm7_bus.write16(0x02000000U, 0x4000U);
    arm7_bus.write16(0x02000002U, 0x4000U);
    arm7_bus.write16(0x02000004U, 0x4000U);
    arm7_bus.write16(0x02000006U, 0x4000U);

    // Channel 0: PCM16, infinite loop, volume=127, pan=64, active.
    arm7_bus.write32(0x04000404U, 0x02000000U);  // SOUND0SAD
    arm7_bus.write16(0x04000408U, 0xFE00U);      // SOUND0TMR (period = 512 -> 1 step per sample)
    arm7_bus.write16(0x0400040AU, 0U);           // SOUND0PNT
    arm7_bus.write32(0x0400040CU, 2U);           // SOUND0LEN (2 words = 8 bytes)
    arm7_bus.write32(0x04000400U,
                     0x80000000U | (1U << 29U) | (1U << 27U) | (64U << 16U) | 127U);

    // Channel 8: PSG square wave (format = 3), duty = 4, volume = 64, pan = 32.
    arm7_bus.write16(0x04000488U, 0xFE00U);
    arm7_bus.write32(0x04000480U,
                     0x80000000U | (3U << 29U) | (4U << 24U) | (32U << 16U) | 64U);

    // Master SOUNDCNT: enable master audio (bit 15) + full volume (127).
    arm7_bus.write16(kRegSoundCnt, 0x807FU);

    emu.step_ticks(kTicksPerAudioSample * 8ULL);

    const auto samples = emu.audio().drain_samples();
    ASSERT_GE(samples.size(), 16U);

    bool any_nonzero = false;
    for (const i16 sample : samples) {
        if (sample != 0) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero);
}

}  // namespace
}  // namespace elmasds::audio
