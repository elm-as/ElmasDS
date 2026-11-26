#include "cpu/arm9/cp15_system_control.hpp"
#include "cpu/arm9/tcm_bus_router.hpp"
#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_instructions/arm_executor.hpp"
#include "mock_memory_bus.hpp"

#include <gtest/gtest.h>

#include <initializer_list>

namespace elmasds::cpu {
namespace {

class SpyExternalBus final : public CpuBusInterface {
public:
    u8 read8(Address address) override {
        ++read_count;
        return inner_.read8(address);
    }
    u16 read16(Address address) override {
        ++read_count;
        return inner_.read16(address);
    }
    u32 read32(Address address) override {
        ++read_count;
        return inner_.read32(address);
    }
    u16 fetch16(Address address) override {
        ++fetch16_count;
        return inner_.read16(address);
    }
    u32 fetch32(Address address) override {
        ++fetch32_count;
        return inner_.read32(address);
    }
    void write8(Address address, u8 value) override {
        ++write_count;
        inner_.write8(address, value);
    }
    void write16(Address address, u16 value) override {
        ++write_count;
        inner_.write16(address, value);
    }
    void write32(Address address, u32 value) override {
        ++write_count;
        inner_.write32(address, value);
    }

    u32 read_count = 0;
    u32 write_count = 0;
    u32 fetch16_count = 0;
    u32 fetch32_count = 0;

private:
    test::MockMemoryBus inner_;
};

struct TcmWindow {
    Address base = 0U;
    u32 size_field = 5U;
    bool enabled = true;
    bool load_mode = false;
};

void configure_tcm_region(Cp15SystemControl& cp15, bool is_itcm, const TcmWindow& window) {
    const u32 region_reg = (window.base & 0xFFFFF000U) | ((window.size_field & 0x1FU) << 1U);
    const CoprocessorTransfer c9{15U, 0U, 9U, 1U, is_itcm ? u8{1U} : u8{0U}, true};
    ASSERT_TRUE(cp15.write_register(c9, region_reg));

    const CoprocessorTransfer c1{15U, 0U, 1U, 0U, 0U, true};
    u32 ctrl = cp15.read_register(c1).value_or(0U);
    const u32 enable_bit =
        is_itcm ? Cp15SystemControl::kBitItcmEnable : Cp15SystemControl::kBitDtcmEnable;
    const u32 load_bit =
        is_itcm ? Cp15SystemControl::kBitItcmLoadMode : Cp15SystemControl::kBitDtcmLoadMode;
    ctrl = bits::write_bit(ctrl, enable_bit, window.enabled);
    ctrl = bits::write_bit(ctrl, load_bit, window.load_mode);
    ASSERT_TRUE(cp15.write_register(c1, ctrl));
}

TEST(Cp15SystemControlTest, ReadsIdentificationCacheAndTcmSizeRegisters) {
    Cp15SystemControl cp15;
    cp15.reset(false);

    EXPECT_EQ(cp15.read_register({15U, 0U, 0U, 0U, 0U, true}), Cp15SystemControl::kMainId);
    EXPECT_EQ(cp15.read_register({15U, 0U, 0U, 0U, 1U, true}), Cp15SystemControl::kCacheType);
    EXPECT_EQ(cp15.read_register({15U, 0U, 0U, 0U, 2U, true}), Cp15SystemControl::kTcmSize);

    // CRn=0 is read-only and rejects unsupported CRm/op2 values.
    EXPECT_FALSE(cp15.write_register({15U, 0U, 0U, 0U, 0U, true}, 0U));
    EXPECT_FALSE(cp15.read_register({15U, 0U, 0U, 0U, 3U, true}).has_value());
    EXPECT_FALSE(cp15.read_register({15U, 0U, 0U, 1U, 0U, true}).has_value());
}

TEST(Cp15SystemControlTest, RoundTripsControlAndTcmRegionRegistersAndHivects) {
    Cp15SystemControl cp15;
    cp15.reset(true);
    EXPECT_TRUE(cp15.high_vectors());
    EXPECT_EQ(cp15.read_register({15U, 0U, 1U, 0U, 0U, true}),
              Cp15SystemControl::kControlFixedOneMask | (1U << Cp15SystemControl::kBitHivects));

    cp15.reset(false);
    EXPECT_FALSE(cp15.high_vectors());

    // Fixed SBO bits [6:3] stay 1 and reserved SBZ bits are masked off.
    const u32 target_ctrl = 0xFFFFFFFFU;
    ASSERT_TRUE(cp15.write_register({15U, 0U, 1U, 0U, 0U, true}, target_ctrl));
    EXPECT_EQ(cp15.read_register({15U, 0U, 1U, 0U, 0U, true}),
              Cp15SystemControl::kControlWritableMask | Cp15SystemControl::kControlFixedOneMask);
    EXPECT_TRUE(cp15.high_vectors());

    // Disable load mode, keep HIVECS + DTCM + ITCM enabled.
    const u32 c1_enabled = (1U << 13U) | (1U << 16U) | (1U << 18U);
    ASSERT_TRUE(cp15.write_register({15U, 0U, 1U, 0U, 0U, true}, c1_enabled));
    ASSERT_TRUE(cp15.write_register({15U, 0U, 9U, 1U, 0U, true}, 0x027C0FFFU));
    ASSERT_TRUE(cp15.write_register({15U, 0U, 9U, 1U, 1U, true}, 0x0100000DU));

    EXPECT_EQ(cp15.read_register({15U, 0U, 9U, 1U, 0U, true}), 0x027C003EU);
    EXPECT_EQ(cp15.read_register({15U, 0U, 9U, 1U, 1U, true}), 0x0100000CU);

    const TcmRegionConfig dtcm = cp15.dtcm_config();
    EXPECT_TRUE(dtcm.enabled);
    EXPECT_FALSE(dtcm.load_mode);
    EXPECT_EQ(dtcm.base_address, 0x027C0000U);
    EXPECT_EQ(dtcm.virtual_size_bytes, 0ULL);  // size_field=31 out of range [3,23]

    const TcmRegionConfig itcm = cp15.itcm_config();
    EXPECT_TRUE(itcm.enabled);
    EXPECT_FALSE(itcm.load_mode);
    EXPECT_EQ(itcm.base_address, 0x01000000U);
    EXPECT_EQ(itcm.virtual_size_bytes, 32768ULL);
}

TEST(Cp15SystemControlTest, EnforcesPrivilegeAndRejectsUnsupportedTransfers) {
    Cp15SystemControl cp15;
    cp15.reset(false);

    const std::initializer_list<CoprocessorTransfer> rejected = {
        {15U, 0U, 0U, 0U, 0U, false},  // User-mode MRC c0
        {15U, 0U, 1U, 0U, 0U, false},  // User-mode c1
        {15U, 0U, 7U, 0U, 4U, false},  // User-mode WFI
        {15U, 0U, 9U, 1U, 0U, false},  // User-mode c9
        {14U, 0U, 0U, 0U, 0U, true},   // Wrong coprocessor
        {15U, 1U, 1U, 0U, 0U, true},   // Unsupported op1 != 0
        {15U, 0U, 11U, 0U, 0U, true},  // Unsupported CRn=11
        {15U, 0U, 7U, 0U, 1U, true},   // Unsupported CRn=7 subop
        {15U, 0U, 9U, 1U, 2U, true},   // Unsupported CRn=9 op2
    };
    for (const auto& tx : rejected) {
        EXPECT_FALSE(cp15.write_register(tx, 0U));
        EXPECT_FALSE(cp15.read_register(tx).has_value());
    }

    // End-to-end User-mode MRC p15 traps to Undefined vector (0xFFFF0004 when HIVECS=1).
    test::MockMemoryBus bus;
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    ArmCoreContext ctx{regs, bus, true, false, &cp15};
    regs.reset(0x02000000U);
    regs.switch_mode(CpuMode::User);
    bus.write32(0x02000000U, 0xEE100F10U);  // MRC p15, 0, R0, c0, c0, 0
    EXPECT_EQ(step_arm_instruction(ctx).cycles, 3U);
    EXPECT_EQ(regs.current_mode(), CpuMode::Undefined);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0xFFFF0004U);
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x02000004U);
}

TEST(Cp15SystemControlTest, HandlesWfiCacheNoOpsAndTcmSizeBounds) {
    Cp15SystemControl cp15;
    cp15.reset(false);

    // Valid ARM946E-S cache maintenance MCRs are no-ops and do not raise WFI.
    for (const auto& op : {CoprocessorTransfer{15U, 0U, 7U, 5U, 0U, true},
                           CoprocessorTransfer{15U, 0U, 7U, 10U, 4U, true},
                           CoprocessorTransfer{15U, 0U, 7U, 14U, 2U, true}}) {
        EXPECT_TRUE(cp15.write_register(op, 0U));
        EXPECT_FALSE(cp15.consume_wfi_request());
        EXPECT_FALSE(cp15.read_register(op).has_value());
    }

    // WFI sets a single-shot request.
    EXPECT_TRUE(cp15.write_register({15U, 0U, 7U, 0U, 4U, true}, 0U));
    EXPECT_TRUE(cp15.consume_wfi_request());
    EXPECT_FALSE(cp15.consume_wfi_request());

    // MPU CRn=2/3/5/6 round-trip and D-cache test-clean returns bit 30 (Z=1).
    EXPECT_TRUE(cp15.write_register({15U, 0U, 2U, 0U, 0U, true}, 0xA5U));
    EXPECT_TRUE(cp15.write_register({15U, 0U, 2U, 0U, 1U, true}, 0x5AU));
    EXPECT_TRUE(cp15.write_register({15U, 0U, 3U, 0U, 0U, true}, 0x0FU));
    EXPECT_TRUE(cp15.write_register({15U, 0U, 5U, 0U, 2U, true}, 0x12345678U));
    EXPECT_TRUE(cp15.write_register({15U, 0U, 6U, 4U, 0U, true}, 0x0200002DU));
    EXPECT_EQ(cp15.read_register({15U, 0U, 2U, 0U, 0U, true}), 0xA5U);
    EXPECT_EQ(cp15.read_register({15U, 0U, 2U, 0U, 1U, true}), 0x5AU);
    EXPECT_EQ(cp15.read_register({15U, 0U, 3U, 0U, 0U, true}), 0x0FU);
    EXPECT_EQ(cp15.read_register({15U, 0U, 5U, 0U, 2U, true}), 0x12345678U);
    EXPECT_EQ(cp15.read_register({15U, 0U, 6U, 4U, 0U, true}), 0x0200002DU);
    EXPECT_EQ(cp15.read_register({15U, 0U, 7U, 14U, 3U, true}), 0x40000000U);

    // Virtual TCM size bounds: valid [3, 23] -> 4 KiB..4 GiB, out-of-range -> 0.
    configure_tcm_region(cp15, true, TcmWindow{0x00000000U, 3U, true, false});
    EXPECT_EQ(cp15.itcm_config().virtual_size_bytes, 4096ULL);
    configure_tcm_region(cp15, true, TcmWindow{0x00000000U, 23U, true, false});
    EXPECT_EQ(cp15.itcm_config().virtual_size_bytes, 0x100000000ULL);
    configure_tcm_region(cp15, true, TcmWindow{0x00000000U, 2U, true, false});
    EXPECT_EQ(cp15.itcm_config().virtual_size_bytes, 0ULL);
    configure_tcm_region(cp15, true, TcmWindow{0x00000000U, 24U, true, false});
    EXPECT_EQ(cp15.itcm_config().virtual_size_bytes, 0ULL);
}

TEST(TcmBusRouterTest, RoutesItcmReadsWritesAndFetchesOverExternalBus) {
    SpyExternalBus external;
    external.write32(0x01000000U, 0xDEADBEEFU);
    external.write_count = 0U;

    Cp15SystemControl cp15;
    cp15.reset(false);
    TcmBusRouter router(external, cp15);
    configure_tcm_region(cp15, true, TcmWindow{0x01000000U, 6U, true, false});

    router.write32(0x01000000U, 0x11223344U);
    router.write16(0x01000004U, 0x5566U);
    router.write8(0x01000006U, 0x77U);
    EXPECT_EQ(external.write_count, 0U);

    EXPECT_EQ(router.read32(0x01000000U), 0x11223344U);
    EXPECT_EQ(router.read16(0x01000004U), 0x5566U);
    EXPECT_EQ(router.read8(0x01000006U), 0x77U);
    EXPECT_EQ(router.fetch32(0x01000000U), 0x11223344U);
    EXPECT_EQ(router.fetch16(0x01000004U), 0x5566U);
    EXPECT_EQ(external.read_count, 0U);
    EXPECT_EQ(external.fetch16_count, 0U);
    EXPECT_EQ(external.fetch32_count, 0U);

    // Disabling ITCM restores external bus visibility while external memory stayed untouched.
    configure_tcm_region(cp15, true, TcmWindow{0x01000000U, 6U, false, false});
    EXPECT_EQ(router.fetch32(0x01000000U), 0xDEADBEEFU);
    EXPECT_EQ(router.read32(0x01000000U), 0xDEADBEEFU);
    EXPECT_EQ(external.fetch32_count, 1U);
    EXPECT_EQ(external.read_count, 1U);
}

TEST(TcmBusRouterTest, RoutesDtcmDataAndBypassesInstructionFetchesToExternalBus) {
    SpyExternalBus external;
    external.write32(0x027C0000U, 0xE3A00042U);
    external.write_count = 0U;

    Cp15SystemControl cp15;
    cp15.reset(false);
    TcmBusRouter router(external, cp15);
    configure_tcm_region(cp15, false, TcmWindow{0x027C0000U, 5U, true, false});

    router.write32(0x027C0000U, 0xCAFEBABEU);
    EXPECT_EQ(external.write_count, 0U);
    EXPECT_EQ(router.read32(0x027C0000U), 0xCAFEBABEU);
    EXPECT_EQ(external.read_count, 0U);

    // DTCM never serves instruction fetches.
    EXPECT_EQ(router.fetch32(0x027C0000U), 0xE3A00042U);
    EXPECT_EQ(router.fetch16(0x027C0000U), 0x0042U);
    EXPECT_EQ(external.fetch32_count, 1U);
    EXPECT_EQ(external.fetch16_count, 1U);

    // Overlapping ITCM + DTCM: DTCM wins data reads/writes, ITCM wins fetches.
    configure_tcm_region(cp15, true, TcmWindow{0x027C0000U, 5U, true, false});
    router.write32(0x027C0000U, 0xAAAAAAAAU);
    EXPECT_EQ(router.read32(0x027C0000U), 0xAAAAAAAAU);
    EXPECT_EQ(router.fetch32(0x027C0000U), 0x00000000U);  // Zeroed ITCM, not DTCM
}

TEST(TcmBusRouterTest, HonorsLoadModeForDtcmAndItcm) {
    SpyExternalBus external;
    external.write32(0x027C0000U, 0x11111111U);
    external.write32(0x01000000U, 0x33333333U);
    external.write_count = 0U;

    Cp15SystemControl cp15;
    cp15.reset(false);
    TcmBusRouter router(external, cp15);

    configure_tcm_region(cp15, false, TcmWindow{0x027C0000U, 5U, true, true});
    router.write32(0x027C0000U, 0x22222222U);
    EXPECT_EQ(external.write_count, 0U);
    EXPECT_EQ(router.read32(0x027C0000U), 0x11111111U);
    configure_tcm_region(cp15, false, TcmWindow{0x027C0000U, 5U, true, false});
    EXPECT_EQ(router.read32(0x027C0000U), 0x22222222U);

    configure_tcm_region(cp15, true, TcmWindow{0x01000000U, 6U, true, true});
    router.write32(0x01000000U, 0x44444444U);
    EXPECT_EQ(external.write_count, 0U);
    EXPECT_EQ(router.read32(0x01000000U), 0x33333333U);
    EXPECT_EQ(router.fetch32(0x01000000U), 0x33333333U);
    configure_tcm_region(cp15, true, TcmWindow{0x01000000U, 6U, true, false});
    EXPECT_EQ(router.read32(0x01000000U), 0x44444444U);
    EXPECT_EQ(router.fetch32(0x01000000U), 0x44444444U);
}

TEST(TcmBusRouterTest, MirrorsPhysicalStorageAndPreventsHighAddressWraparound) {
    SpyExternalBus external;
    external.write32(0x027C1000U, 0x77777777U);
    external.write32(0x00000000U, 0x88888888U);
    external.write_count = 0U;

    Cp15SystemControl cp15;
    cp15.reset(false);
    TcmBusRouter router(external, cp15);

    // Sub-physical 4 KiB window (size_field=3): offset +0x1000 misses to external bus.
    configure_tcm_region(cp15, false, TcmWindow{0x027C0000U, 3U, true, false});
    EXPECT_EQ(router.read32(0x027C1000U), 0x77777777U);

    // 64 KiB virtual window (size_field=7) over 16 KiB DTCM / 32 KiB ITCM mirrors cleanly.
    configure_tcm_region(cp15, false, TcmWindow{0x027C0000U, 7U, true, false});
    router.write32(0x027C0010U, 0x12345678U);
    EXPECT_EQ(router.read32(0x027C4010U), 0x12345678U);

    configure_tcm_region(cp15, true, TcmWindow{0x01000000U, 7U, true, false});
    router.write32(0x01000020U, 0x9ABCDEF0U);
    EXPECT_EQ(router.fetch32(0x01008020U), 0x9ABCDEF0U);

    // High-address wraparound safety: 8 KiB DTCM/ITCM at 0xFFFFF000 stops at 4 GiB.
    configure_tcm_region(cp15, false, TcmWindow{0xFFFFF000U, 4U, true, false});
    configure_tcm_region(cp15, true, TcmWindow{0xFFFFF000U, 4U, true, false});
    external.read_count = 0U;
    external.write_count = 0U;
    external.fetch32_count = 0U;
    router.write32(0xFFFFFFFCU, 0x55AA55AAU);
    router.write8(0xFFFFFFFFU, 0xABU);
    EXPECT_EQ(router.read32(0xFFFFFFFCU), 0xABAA55AAU);
    EXPECT_EQ(router.read8(0xFFFFFFFFU), 0xABU);
    EXPECT_EQ(external.write_count, 0U);
    EXPECT_EQ(external.read_count, 0U);
    EXPECT_EQ(router.read32(0x00000000U), 0x88888888U);
    EXPECT_EQ(external.read_count, 1U);

    // Accesses straddling 4 GiB fall through to external bus without touching TCM.
    router.write32(0xFFFFFFFEU, 0x11223344U);
    EXPECT_EQ(external.write_count, 1U);
    EXPECT_EQ(router.read32(0xFFFFFFFEU), 0x11223344U);
    EXPECT_EQ(router.read16(0xFFFFFFFFU), 0x2233U);
    EXPECT_EQ(router.fetch32(0xFFFFFFFEU), 0x11223344U);
    EXPECT_EQ(external.read_count, 3U);
    EXPECT_EQ(external.fetch32_count, 1U);
    EXPECT_EQ(router.read32(0xFFFFFFFCU), 0xABAA55AAU);

    // Reset clears both TCM backing arrays.
    configure_tcm_region(cp15, true, TcmWindow{0x01000000U, 7U, true, false});
    router.reset();
    EXPECT_EQ(router.read32(0xFFFFFFFCU), 0U);
    EXPECT_EQ(router.fetch32(0x01008020U), 0U);
    EXPECT_EQ(external.read_count, 3U);
    EXPECT_EQ(external.fetch32_count, 1U);
}

}  // namespace
}  // namespace elmasds::cpu
