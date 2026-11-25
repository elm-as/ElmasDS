#pragma once

#include "input/keypad_mmio.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>

namespace elmasds::input {

/**
 * @brief Platform-independent keyboard key identifiers used by `InputKeyMap`.
 */
enum class HostKey : u16 {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Enter,
    RightShift,
    LeftShift,
    Space,
    Escape,
    F4,
    F5,
    F7,
    F11,
};

/**
 * @brief Modifier flags accompanying a `HostKey` press.
 */
struct KeyModifiers {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;

    [[nodiscard]] constexpr bool none() const noexcept { return !ctrl && !alt && !shift; }
};

/**
 * @brief Emulator-level UI/system shortcut actions defined in Section 13.4.
 */
enum class EmulatorShortcut : u8 {
    OpenRom = 0,
    TogglePause,
    ToggleFullscreen,
    ResetConsole,
    SaveState,
    LoadState,
    MenuOrBack,
    QuitApplication,
};

inline constexpr std::size_t kMappableButtonCount = 12U;

/**
 * @brief Configurable keyboard-to-DS button mapping with conflict protection and persistence.
 *
 * Responsibility:
 *   Maintains the mapping between `HostKey` and the 12 user-controllable Nintendo DS buttons
 *   (`Up`, `Down`, `Left`, `Right`, `X`, `Y`, `A`, `B`, `L`, `R`, `Start`, `Select`), prevents
 *   duplicate assignments across buttons, supports resetting to Section 13.1 defaults, evaluates
 *   Section 13.4 shortcuts, and loads/saves bindings to a key-value configuration file.
 */
class InputKeyMap final {
public:
    InputKeyMap() noexcept;

    /**
     * @brief Resets all 12 Nintendo DS button bindings to Section 13.1 default keys.
     */
    void reset_to_defaults() noexcept;

    /**
     * @brief Rebinds `button` to `key`; fails with `InvalidArgument` if `key` is already bound.
     */
    [[nodiscard]] Status bind_button(DsButton button, HostKey key) noexcept;

    [[nodiscard]] HostKey key_for_button(DsButton button) const noexcept;
    [[nodiscard]] std::optional<DsButton> button_for_key(HostKey key) const noexcept;

    /**
     * @brief Resolves one Section 13.4 system shortcut from `(key, modifiers)`.
     */
    [[nodiscard]] static std::optional<EmulatorShortcut> match_shortcut(
        HostKey key, KeyModifiers modifiers) noexcept;

    [[nodiscard]] Status save_to_file(const std::filesystem::path& config_path) const;
    [[nodiscard]] Status load_from_file(const std::filesystem::path& config_path);

    [[nodiscard]] static std::string_view button_name(DsButton button) noexcept;
    [[nodiscard]] static std::string_view key_name(HostKey key) noexcept;
    [[nodiscard]] static std::optional<DsButton> parse_button_name(std::string_view name) noexcept;
    [[nodiscard]] static std::optional<HostKey> parse_key_name(std::string_view name) noexcept;

private:
    std::array<HostKey, kMappableButtonCount> bindings_{};
};

/**
 * @brief Converts clamped bottom-screen pixel coordinates (`0..255`, `0..191`) into 12-bit TSC2046
 *        ADC raw coordinates compatible with the default firmware calibration profile.
 */
struct TouchAdcSample {
    u16 raw_x = 0U;
    u16 raw_y = 0U;
};

[[nodiscard]] TouchAdcSample pixel_to_touch_adc(u16 pixel_x, u16 pixel_y) noexcept;

}  // namespace elmasds::input
