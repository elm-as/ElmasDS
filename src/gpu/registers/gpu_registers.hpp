#pragma once

#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>

namespace elmasds::gpu {

inline constexpr Address kEngineABase = 0x04000000U;
inline constexpr Address kEngineAEnd = 0x0400006FU;
inline constexpr Address kEngineBBase = 0x04001000U;
inline constexpr Address kEngineBEnd = 0x0400106FU;

/**
 * @brief Affine background transformation parameters (`BG2`/`BG3` PA..PD, X, Y, and internal latch).
 */
struct AffineBgParams {
    i16 pa = 0x0100;
    i16 pb = 0;
    i16 pc = 0;
    i16 pd = 0x0100;
    u32 ref_x_raw = 0U;
    u32 ref_y_raw = 0U;
    i32 internal_x = 0;
    i32 internal_y = 0;

    void latch_reference() noexcept;
    void step_scanline() noexcept;
};

/**
 * @brief Per-engine 2D graphics control registers (`DISPCNT`, `BGxCNT`, scroll, affine, windows, blend).
 */
class GpuRegisters final : public memory::MmioDevice {
public:
    explicit GpuRegisters(Address base_address, bool is_engine_a) noexcept;

    void reset() noexcept;
    void reload_affine_latches() noexcept;
    void step_affine_latches() noexcept;

    [[nodiscard]] u32 dispcnt() const noexcept { return dispcnt_; }
    [[nodiscard]] u16 bg_cnt(std::size_t bg) const noexcept { return bg_cnt_[bg & 3U]; }
    [[nodiscard]] u16 bg_hofs(std::size_t bg) const noexcept { return bg_hofs_[bg & 3U]; }
    [[nodiscard]] u16 bg_vofs(std::size_t bg) const noexcept { return bg_vofs_[bg & 3U]; }
    [[nodiscard]] const AffineBgParams& affine(std::size_t idx) const noexcept {
        return affine_[idx & 1U];
    }

    [[nodiscard]] u16 win0_h() const noexcept { return win_h_[0U]; }
    [[nodiscard]] u16 win1_h() const noexcept { return win_h_[1U]; }
    [[nodiscard]] u16 win0_v() const noexcept { return win_v_[0U]; }
    [[nodiscard]] u16 win1_v() const noexcept { return win_v_[1U]; }
    [[nodiscard]] u16 win_in() const noexcept { return win_in_; }
    [[nodiscard]] u16 win_out() const noexcept { return win_out_; }
    [[nodiscard]] u16 mosaic() const noexcept { return mosaic_; }
    [[nodiscard]] u16 bldcnt() const noexcept { return bldcnt_; }
    [[nodiscard]] u16 bldalpha() const noexcept { return bldalpha_; }
    [[nodiscard]] u16 bldy() const noexcept { return bldy_; }
    [[nodiscard]] u16 master_bright() const noexcept { return master_bright_; }
    [[nodiscard]] bool is_engine_a() const noexcept { return is_engine_a_; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    void write_affine_reg(std::size_t idx, Address sub_offset, u16 value) noexcept;

    Address base_address_;
    bool is_engine_a_;
    u32 dispcnt_ = 0U;
    std::array<u16, 4U> bg_cnt_{};
    std::array<u16, 4U> bg_hofs_{};
    std::array<u16, 4U> bg_vofs_{};
    std::array<AffineBgParams, 2U> affine_{};
    std::array<u16, 2U> win_h_{};
    std::array<u16, 2U> win_v_{};
    u16 win_in_ = 0U;
    u16 win_out_ = 0U;
    u16 mosaic_ = 0U;
    u16 bldcnt_ = 0U;
    u16 bldalpha_ = 0U;
    u16 bldy_ = 0U;
    u16 master_bright_ = 0U;
};

}  // namespace elmasds::gpu
