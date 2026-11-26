#pragma once

#include "gpu/engine_3d/gx_types.hpp"
#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/performance_profiler.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::gpu {

/**
 * @brief Nintendo DS 3D Geometry Engine (`GXFIFO`, matrix stacks, vertex pipeline) and Scanline
 *        Software Rasterizer.
 *
 * Responsibility:
 *   Handles ARM9 3D MMIO registers (`DISP3DCNT` `0x04000060`, `GXFIFO` `0x04000400..0x0400043F`,
 *   direct command ports `0x04000440..0x040005DF`, `GXSTAT` `0x04000600`, `RAM_COUNT` `0x04000604`,
 *   `CLIPMTX_RESULT` `0x04000640..0x0400067F`, `VECMTX_RESULT` `0x04000680..0x040006A3`),
 *   executes 1.19.12 fixed-point matrix stack operations and vertex assembly, triggers `GXFIFO`
 *   DMA/IRQ when the FIFO is less than half full, swaps polygon buffers on VBlank after
 *   `SWAP_BUFFERS`, and rasterizes 256×192 depth-tested `ARGB8888` scanlines for Engine A `BG0`.
 */
class Engine3D final : public memory::MmioDevice {
public:
    static constexpr std::size_t kMaxPolygons = 2048U;
    static constexpr std::size_t kMaxVertices = 6144U;

    Engine3D(const memory::MemorySystem& memory,
             hardware::interrupts::InterruptController& arm9_irq,
             hardware::dma::DmaController& arm9_dma) noexcept;

    void reset() noexcept;

    [[nodiscard]] u8 read8(Address address) noexcept override;
    [[nodiscard]] u16 read16(Address address) noexcept override;
    [[nodiscard]] u32 read32(Address address) noexcept override;
    void write8(Address address, u8 value) noexcept override;
    void write16(Address address, u16 value) noexcept override;
    void write32(Address address, u32 value) noexcept override;

    /**
     * @brief Commits any pending `SWAP_BUFFERS` at VBlank start and rasterizes the 3D frame.
     */
    void on_vblank() noexcept;

    /**
     * @brief Evaluates `GXFIFO` level-triggered IRQ/DMA conditions (`GXSTAT` bits 30..31).
     */
    void check_fifo_irq_and_dma() noexcept;
    void set_profiler(primitives::PerformanceProfiler* profiler) noexcept { profiler_ = profiler; }

    /**
     * @brief Returns the 256-pixel 3D scanline (`ARGB8888`, alpha > 0 = opaque) for `line`.
     */
    [[nodiscard]] std::span<const u32> scanline(u16 line) const noexcept;

    [[nodiscard]] u16 disp3dcnt() const noexcept { return disp3dcnt_; }
    [[nodiscard]] u32 gxstat() const noexcept;
    [[nodiscard]] const Matrix4x4& clip_matrix() const noexcept { return clip_mtx_; }
    [[nodiscard]] const Matrix4x4& vector_matrix() const noexcept { return vec_mtx_; }
    [[nodiscard]] std::size_t rendered_polygon_count() const noexcept {
        return render_polygons_.size();
    }

private:
    struct PendingCommand {
        u8 command = 0U;
        u32 param = 0U;
    };

    struct PolygonTexInfo {
        u32 format = 0U;
        i32 width = 8;
        i32 height = 8;
        u32 width_blocks = 2U;
        bool wrap_s = false;
        bool wrap_t = false;
        bool color0_transparent = false;
        bool tex_enabled = false;
        Address base = 0U;
        Address slot1_base = 0U;
        Address pal_base4 = 0U;
        Address pal_base3 = 0U;
        const u8* single_tex_bank = nullptr;
        Address single_tex_off = 0U;
    };

    static constexpr u32 kCmdRingSize = 1024U;
    static constexpr u32 kCmdRingMask = kCmdRingSize - 1U;

    void push_cmd(u8 cmd) noexcept {
        if (cmd_count_ < kCmdRingSize) {
            cmd_ring_[cmd_tail_] = cmd;
            cmd_tail_ = (cmd_tail_ + 1U) & kCmdRingMask;
            ++cmd_count_;
        }
    }
    [[nodiscard]] u8 pop_cmd() noexcept {
        const u8 cmd = cmd_ring_[cmd_head_];
        cmd_head_ = (cmd_head_ + 1U) & kCmdRingMask;
        --cmd_count_;
        return cmd;
    }
    void clear_cmds() noexcept { cmd_head_ = 0U; cmd_tail_ = 0U; cmd_count_ = 0U; }
    [[nodiscard]] bool has_cmds() const noexcept { return cmd_count_ > 0U; }

    struct TriStepInfo;
    struct ScanlineCursor;

    [[nodiscard]] static u8 param_count_for_command(u8 cmd) noexcept;
    void enqueue_packed_commands(u32 packed_cmds) noexcept;
    void submit_parameter(u32 param) noexcept;
    void execute_command(u8 cmd, std::span<const u32> params) noexcept;
    void execute_matrix_command(u8 cmd, std::span<const u32> params) noexcept;
    void execute_vertex_command(u8 cmd, std::span<const u32> params) noexcept;
    void emit_vertex(i16 x, i16 y, i16 z) noexcept;
    void assemble_polygon() noexcept;
    void update_clip_matrix() noexcept;
    void refresh_vram_texture_cache() noexcept;
    void rasterize_frame() noexcept;
    void rasterize_polygon(const Polygon3D& poly) noexcept;
    void rasterize_triangle(const Polygon3D& poly, const PolygonTexInfo& tex,
                            const std::array<const Vertex3D*, 3U>& tri) noexcept;
    void rasterize_span_textured(const TriStepInfo& step, ScanlineCursor& cur) noexcept;
    void rasterize_span_solid(const TriStepInfo& step, ScanlineCursor& cur) noexcept;
    [[nodiscard]] u32 sample_texture(const PolygonTexInfo& tex, i32 s, i32 t) const noexcept;
    [[nodiscard]] u32 sample_compressed_4x4(const PolygonTexInfo& tex, i32 u, i32 v) const noexcept;

    const memory::MemorySystem& memory_;
    hardware::interrupts::InterruptController& arm9_irq_;
    hardware::dma::DmaController& arm9_dma_;

    u16 disp3dcnt_ = 0U;
    u32 gxstat_latch_ = 0x04000000U;
    u32 clear_color_ = 0x001F0000U;
    u16 clear_depth_ = 0x7FFFU;

    u8 mtx_mode_ = 0U;
    Matrix4x4 proj_mtx_ = Matrix4x4::identity();
    Matrix4x4 proj_stack_ = Matrix4x4::identity();
    u8 proj_sp_ = 0U;

    Matrix4x4 pos_mtx_ = Matrix4x4::identity();
    Matrix4x4 vec_mtx_ = Matrix4x4::identity();
    std::array<Matrix4x4, 32U> pos_stack_{};
    std::array<Matrix4x4, 32U> vec_stack_{};
    u8 pos_sp_ = 0U;

    Matrix4x4 tex_mtx_ = Matrix4x4::identity();
    Matrix4x4 tex_stack_ = Matrix4x4::identity();
    Matrix4x4 clip_mtx_ = Matrix4x4::identity();

    std::array<u8, kCmdRingSize> cmd_ring_{};
    u32 cmd_head_ = 0U;
    u32 cmd_tail_ = 0U;
    u32 cmd_count_ = 0U;
    u8 active_cmd_ = 0U;
    u8 active_needed_params_ = 0U;
    std::array<u32, 32U> active_params_{};
    u8 active_param_index_ = 0U;

    PrimitiveType primitive_type_ = PrimitiveType::Triangles;
    u32 poly_attr_ = 0x001F00C0U;
    u32 latched_poly_attr_ = 0x001F00C0U;
    u32 teximage_param_ = 0U;
    u16 pltt_base_ = 0U;
    Viewport3D viewport_{};

    i16 cur_vtx_x_ = 0;
    i16 cur_vtx_y_ = 0;
    i16 cur_vtx_z_ = 0;
    u8 cur_r_ = 31U;
    u8 cur_g_ = 31U;
    u8 cur_b_ = 31U;
    i16 cur_s_ = 0;
    i16 cur_t_ = 0;

    std::array<Vertex3D, 4U> vtx_ring_{};
    u32 primitive_vtx_counter_ = 0U;
    bool swap_pending_ = false;
    u32 swap_flags_ = 0U;

    std::vector<Polygon3D> submit_polygons_;
    std::vector<Polygon3D> render_polygons_;
    u32 submitted_vertex_count_ = 0U;
    u32 rendered_vertex_count_ = 0U;

    std::vector<u32> color_buffer_;
    std::vector<i32> depth_buffer_;
    std::array<const u8*, 4U> tex_slots_{};
    std::array<const u8*, 6U> tex_pal_slots_{};
    primitives::PerformanceProfiler* profiler_ = nullptr;
};

}  // namespace elmasds::gpu
