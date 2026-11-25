#include "input/input_mapping.hpp"

#include <algorithm>
#include <fstream>
#include <string>

namespace elmasds::input {

namespace {

struct ButtonNameEntry {
    DsButton button;
    std::string_view name;
};

struct KeyNameEntry {
    HostKey key;
    std::string_view name;
};

constexpr std::array<ButtonNameEntry, kMappableButtonCount> kButtonNames = {{
    {DsButton::A, "A"},
    {DsButton::B, "B"},
    {DsButton::Select, "Select"},
    {DsButton::Start, "Start"},
    {DsButton::Right, "Right"},
    {DsButton::Left, "Left"},
    {DsButton::Up, "Up"},
    {DsButton::Down, "Down"},
    {DsButton::R, "R"},
    {DsButton::L, "L"},
    {DsButton::X, "X"},
    {DsButton::Y, "Y"},
}};

constexpr std::array<KeyNameEntry, 40U> kKeyNames = {{
    {HostKey::None, "None"},           {HostKey::Up, "Up"},
    {HostKey::Down, "Down"},           {HostKey::Left, "Left"},
    {HostKey::Right, "Right"},         {HostKey::A, "A"},
    {HostKey::B, "B"},                 {HostKey::C, "C"},
    {HostKey::D, "D"},                 {HostKey::E, "E"},
    {HostKey::F, "F"},                 {HostKey::G, "G"},
    {HostKey::H, "H"},                 {HostKey::I, "I"},
    {HostKey::J, "J"},                 {HostKey::K, "K"},
    {HostKey::L, "L"},                 {HostKey::M, "M"},
    {HostKey::N, "N"},                 {HostKey::O, "O"},
    {HostKey::P, "P"},                 {HostKey::Q, "Q"},
    {HostKey::R, "R"},                 {HostKey::S, "S"},
    {HostKey::T, "T"},                 {HostKey::U, "U"},
    {HostKey::V, "V"},                 {HostKey::W, "W"},
    {HostKey::X, "X"},                 {HostKey::Y, "Y"},
    {HostKey::Z, "Z"},                 {HostKey::Enter, "Enter"},
    {HostKey::RightShift, "RightShift"},
    {HostKey::LeftShift, "LeftShift"}, {HostKey::Space, "Space"},
    {HostKey::Escape, "Escape"},       {HostKey::F4, "F4"},
    {HostKey::F5, "F5"},               {HostKey::F7, "F7"},
    {HostKey::F11, "F11"},
}};

[[nodiscard]] std::string_view trim_whitespace(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

}  // namespace

InputKeyMap::InputKeyMap() noexcept {
    reset_to_defaults();
}

void InputKeyMap::reset_to_defaults() noexcept {
    // Section 13.1 default keyboard mapping:
    // D-Pad: Up/Down/Left/Right; Face buttons: X=I, Y=J, A=L, B=K;
    // Shoulders: L=Q, R=E; Start=Enter, Select=RightShift.
    bindings_[static_cast<std::size_t>(DsButton::A)] = HostKey::L;
    bindings_[static_cast<std::size_t>(DsButton::B)] = HostKey::K;
    bindings_[static_cast<std::size_t>(DsButton::Select)] = HostKey::RightShift;
    bindings_[static_cast<std::size_t>(DsButton::Start)] = HostKey::Enter;
    bindings_[static_cast<std::size_t>(DsButton::Right)] = HostKey::Right;
    bindings_[static_cast<std::size_t>(DsButton::Left)] = HostKey::Left;
    bindings_[static_cast<std::size_t>(DsButton::Up)] = HostKey::Up;
    bindings_[static_cast<std::size_t>(DsButton::Down)] = HostKey::Down;
    bindings_[static_cast<std::size_t>(DsButton::R)] = HostKey::E;
    bindings_[static_cast<std::size_t>(DsButton::L)] = HostKey::Q;
    bindings_[static_cast<std::size_t>(DsButton::X)] = HostKey::I;
    bindings_[static_cast<std::size_t>(DsButton::Y)] = HostKey::J;
}

Status InputKeyMap::bind_button(DsButton button, HostKey key) noexcept {
    const auto idx = static_cast<std::size_t>(button);
    if (idx >= kMappableButtonCount || key == HostKey::None) {
        return Status::err(ErrorCode::InvalidArgument, "Invalid DS button or host key.");
    }

    for (std::size_t i = 0U; i < kMappableButtonCount; ++i) {
        if (i != idx && bindings_[i] == key) {
            return Status::err(ErrorCode::InvalidArgument,
                               "Host key is already bound to another DS button.");
        }
    }
    bindings_[idx] = key;
    return Status::ok();
}

HostKey InputKeyMap::key_for_button(DsButton button) const noexcept {
    const auto idx = static_cast<std::size_t>(button);
    return (idx < kMappableButtonCount) ? bindings_[idx] : HostKey::None;
}

std::optional<DsButton> InputKeyMap::button_for_key(HostKey key) const noexcept {
    if (key == HostKey::None) {
        return std::nullopt;
    }
    for (std::size_t i = 0U; i < kMappableButtonCount; ++i) {
        if (bindings_[i] == key) {
            return static_cast<DsButton>(i);
        }
    }
    return std::nullopt;
}

std::optional<EmulatorShortcut> InputKeyMap::match_shortcut(HostKey key,
                                                            KeyModifiers modifiers) noexcept {
    if (modifiers.ctrl && !modifiers.alt && key == HostKey::O) {
        return EmulatorShortcut::OpenRom;
    }
    if (modifiers.ctrl && !modifiers.alt && key == HostKey::R) {
        return EmulatorShortcut::ResetConsole;
    }
    if (modifiers.alt && !modifiers.ctrl && key == HostKey::F4) {
        return EmulatorShortcut::QuitApplication;
    }
    if (!modifiers.none()) {
        return std::nullopt;
    }
    switch (key) {
    case HostKey::Space:
        return EmulatorShortcut::TogglePause;
    case HostKey::F11:
        return EmulatorShortcut::ToggleFullscreen;
    case HostKey::F5:
        return EmulatorShortcut::SaveState;
    case HostKey::F7:
        return EmulatorShortcut::LoadState;
    case HostKey::Escape:
        return EmulatorShortcut::MenuOrBack;
    default:
        return std::nullopt;
    }
}

Status InputKeyMap::save_to_file(const std::filesystem::path& config_path) const {
    std::ofstream out(config_path, std::ios::trunc);
    if (!out.is_open()) {
        return Status::err(ErrorCode::IoFailure, "Failed to open input config for writing.");
    }
    out << "# ElmasDS Input Configuration\n";
    for (const auto& entry : kButtonNames) {
        out << entry.name << "=" << key_name(key_for_button(entry.button)) << "\n";
    }
    return out.good() ? Status::ok()
                      : Status::err(ErrorCode::IoFailure, "Failed writing input config.");
}

Status InputKeyMap::load_from_file(const std::filesystem::path& config_path) {
    std::ifstream in(config_path);
    if (!in.is_open()) {
        return Status::err(ErrorCode::FileNotFound, "Input configuration file not found.");
    }

    InputKeyMap parsed = *this;
    std::fill(parsed.bindings_.begin(), parsed.bindings_.end(), HostKey::None);
    std::string line;
    while (std::getline(in, line)) {
        const std::string_view sv = trim_whitespace(line);
        if (sv.empty() || sv.front() == '#') {
            continue;
        }
        const std::size_t eq = sv.find('=');
        if (eq == std::string_view::npos) {
            return Status::err(ErrorCode::CorruptData, "Malformed line in input config.");
        }
        const auto button = parse_button_name(trim_whitespace(sv.substr(0U, eq)));
        const auto key = parse_key_name(trim_whitespace(sv.substr(eq + 1U)));
        if (!button.has_value() || !key.has_value()) {
            return Status::err(ErrorCode::CorruptData, "Unknown button or key in input config.");
        }
        const Status status = parsed.bind_button(*button, *key);
        if (status.is_err()) {
            return status;
        }
    }

    for (std::size_t i = 0U; i < kMappableButtonCount; ++i) {
        if (parsed.bindings_[i] == HostKey::None) {
            parsed.bindings_[i] = bindings_[i];
        }
    }
    *this = parsed;
    return Status::ok();
}

std::string_view InputKeyMap::button_name(DsButton button) noexcept {
    for (const auto& entry : kButtonNames) {
        if (entry.button == button) {
            return entry.name;
        }
    }
    return "Unknown";
}

std::string_view InputKeyMap::key_name(HostKey key) noexcept {
    for (const auto& entry : kKeyNames) {
        if (entry.key == key) {
            return entry.name;
        }
    }
    return "None";
}

std::optional<DsButton> InputKeyMap::parse_button_name(std::string_view name) noexcept {
    for (const auto& entry : kButtonNames) {
        if (entry.name == name) {
            return entry.button;
        }
    }
    return std::nullopt;
}

std::optional<HostKey> InputKeyMap::parse_key_name(std::string_view name) noexcept {
    for (const auto& entry : kKeyNames) {
        if (entry.name == name) {
            return entry.key;
        }
    }
    return std::nullopt;
}

TouchAdcSample pixel_to_touch_adc(u16 pixel_x, u16 pixel_y) noexcept {
    const u16 clamped_x = std::min<u16>(pixel_x, 255U);
    const u16 clamped_y = std::min<u16>(pixel_y, 191U);
    // Matches create_default_firmware_image() calibration:
    // ADC X in [0x0100..0x0ED0] for pixel X in [0..255],
    // ADC Y in [0x00B0..0x0F20] for pixel Y in [0..191].
    const u32 raw_x = 0x0100U + ((static_cast<u32>(clamped_x) * (0x0ED0U - 0x0100U)) / 255U);
    const u32 raw_y = 0x00B0U + ((static_cast<u32>(clamped_y) * (0x0F20U - 0x00B0U)) / 191U);
    return TouchAdcSample{static_cast<u16>(raw_x & 0x0FFFU), static_cast<u16>(raw_y & 0x0FFFU)};
}

}  // namespace elmasds::input
