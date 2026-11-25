#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/rtc/rtc_controller.hpp"
#include "hardware/spi/spi_controller.hpp"

#include <array>
#include <gtest/gtest.h>
#include <vector>

namespace elmasds::hardware {
namespace {

using interrupts::CpuKind;
using interrupts::InterruptController;
using interrupts::IrqSource;

u8 spi_xfer(spi::SpiController& spi_ctrl, u16 device, u8 byte, bool hold_cs) {
    u16 cnt = static_cast<u16>((1U << 15U) | ((device & 3U) << 8U));
    if (hold_cs) {
        cnt |= (1U << 11U);
    }
    spi_ctrl.write16(spi::kRegSpiCnt, cnt);
    spi_ctrl.write16(spi::kRegSpiData, byte);
    return static_cast<u8>(spi_ctrl.read16(spi::kRegSpiData) & 0xFFU);
}

void rtc_write_byte_msb(rtc::RtcController& rtc_ctrl, u8 byte) {
    for (int bit = 7; bit >= 0; --bit) {
        const u8 data_bit = static_cast<u8>((byte >> bit) & 1U);
        rtc_ctrl.write8(rtc::kRtcMmioStart, static_cast<u8>(0x74U | data_bit));
        rtc_ctrl.write8(rtc::kRtcMmioStart, static_cast<u8>(0x76U | data_bit));
    }
}

u8 rtc_read_byte_lsb(rtc::RtcController& rtc_ctrl) {
    u8 value = 0U;
    for (u8 bit = 0U; bit < 8U; ++bit) {
        rtc_ctrl.write8(rtc::kRtcMmioStart, 0x66U);  // CS=1, SCK=1, SIO=read
        rtc_ctrl.write8(rtc::kRtcMmioStart, 0x64U);  // CS=1, SCK=0 (falling edge drives bit)
        const u8 sampled = static_cast<u8>(rtc_ctrl.read8(rtc::kRtcMmioStart) & 1U);
        value = static_cast<u8>(value | (sampled << bit));
    }
    return value;
}

TEST(SpiControllerTest, FirmwareReadAndPowermanAndTouchscreen) {
    InterruptController arm7_irq(CpuKind::Arm7);
    spi::SpiController spi_ctrl(&arm7_irq);

    std::vector<u8> fw(1024U, u8{0xFF});
    fw[0x100U] = 0x42U;
    fw[0x101U] = 0x99U;
    spi_ctrl.firmware().load_image(fw);

    // Read 2 bytes at 0x000100 from Firmware Flash (device 1).
    static_cast<void>(spi_xfer(spi_ctrl, 1U, 0x03U, true));
    static_cast<void>(spi_xfer(spi_ctrl, 1U, 0x00U, true));
    static_cast<void>(spi_xfer(spi_ctrl, 1U, 0x01U, true));
    static_cast<void>(spi_xfer(spi_ctrl, 1U, 0x00U, true));
    EXPECT_EQ(spi_xfer(spi_ctrl, 1U, 0x00U, true), 0x42U);
    EXPECT_EQ(spi_xfer(spi_ctrl, 1U, 0x00U, false), 0x99U);

    // Powerman write/read register 2 (device 0).
    static_cast<void>(spi_xfer(spi_ctrl, 0U, 0x02U, true));
    static_cast<void>(spi_xfer(spi_ctrl, 0U, 0x7AU, false));
    static_cast<void>(spi_xfer(spi_ctrl, 0U, 0x82U, true));
    EXPECT_EQ(spi_xfer(spi_ctrl, 0U, 0x00U, false), 0x7AU);

    // Touchscreen X sample (device 2, channel 5 -> control 0xD0).
    spi_ctrl.touchscreen().set_touch_state(true, 0x0ABC, 0x0345);
    static_cast<void>(spi_xfer(spi_ctrl, 2U, 0xD0U, true));
    const u8 hi = spi_xfer(spi_ctrl, 2U, 0x00U, true);
    const u8 lo = spi_xfer(spi_ctrl, 2U, 0x00U, false);
    const u16 decoded_x = static_cast<u16>((static_cast<u16>(hi) << 5U) | (lo >> 3U));
    EXPECT_EQ(decoded_x, 0x0ABCU);

    // Verify SPI IRQ assertion when bit 14 is set.
    spi_ctrl.write16(spi::kRegSpiCnt, static_cast<u16>((1U << 15U) | (1U << 14U)));
    spi_ctrl.write16(spi::kRegSpiData, 0x00U);
    EXPECT_NE(arm7_irq.if_flags() & interrupts::irq_bit_mask(IrqSource::Spi), 0U);
}

TEST(RtcControllerTest, ReadsDeterministicBcdDateTimeOverSerialProtocol) {
    rtc::RtcController rtc_ctrl;
    rtc_ctrl.set_date_time(rtc::RtcDateTime{
        .year = 26U,
        .month = 11U,
        .day = 25U,
        .weekday = 3U,
        .hour = 14U,
        .minute = 37U,
        .second = 52U,
    });

    // Deassert CS, then assert CS and clock command 0x65 (reg 2 = Date/Time, read = 1).
    rtc_ctrl.write8(rtc::kRtcMmioStart, 0x00U);
    rtc_write_byte_msb(rtc_ctrl, 0x65U);

    std::array<u8, 7U> bytes{};
    for (u8& byte : bytes) {
        byte = rtc_read_byte_lsb(rtc_ctrl);
    }
    rtc_ctrl.write8(rtc::kRtcMmioStart, 0x00U);

    EXPECT_EQ(rtc::from_bcd(bytes[0U]), 26U);
    EXPECT_EQ(rtc::from_bcd(bytes[1U]), 11U);
    EXPECT_EQ(rtc::from_bcd(bytes[2U]), 25U);
    EXPECT_EQ(rtc::from_bcd(bytes[3U]), 3U);
    EXPECT_EQ(rtc::from_bcd(bytes[4U]), 14U);
    EXPECT_EQ(rtc::from_bcd(bytes[5U]), 37U);
    EXPECT_EQ(rtc::from_bcd(bytes[6U]), 52U);
}

}  // namespace
}  // namespace elmasds::hardware

