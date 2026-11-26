#include "frontend/overlay/menu_bar.hpp"

#include <algorithm>
#include <array>

namespace elmasds::frontend {
namespace {

struct DropdownItemSpec {
    std::string_view label;
    MenuAction action = MenuAction::None;
};

struct TopMenuSpec {
    std::string_view label;
    MenuBarRect tab_rect;
    const DropdownItemSpec* items = nullptr;
    i32 item_count = 0;
};

constexpr i32 kDropdownWidth = 236;
constexpr i32 kDropdownPadY = 5;

constexpr std::array<DropdownItemSpec, 5U> kFileItems = {{
    {"OUVRIR UNE ROM...", MenuAction::OpenRom},
    {"SAUVEGARDER ETAT", MenuAction::SaveState},
    {"CHARGER ETAT", MenuAction::LoadState},
    {"SLOT ACTIF", MenuAction::CycleSlot},
    {"QUITTER", MenuAction::Quit},
}};

constexpr std::array<DropdownItemSpec, 6U> kEmulationItems = {{
    {"PAUSE / REPRENDRE", MenuAction::TogglePause},
    {"VITESSE X2", MenuAction::ToggleSpeed2x},
    {"VITESSE X4", MenuAction::ToggleSpeed4x},
    {"REINITIALISER", MenuAction::ResetConsole},
    {"REMBOBINAGE", MenuAction::ToggleRewind},
    {"MUET AUDIO", MenuAction::ToggleMute},
}};

constexpr std::array<DropdownItemSpec, 5U> kDisplayItems = {{
    {"DISPOSITION", MenuAction::CycleLayout},
    {"INVERSER ECRANS", MenuAction::ToggleSwapScreens},
    {"ESPACEMENT", MenuAction::CycleGap},
    {"FILTRE VISUEL", MenuAction::CycleFilter},
    {"PLEIN ECRAN", MenuAction::ToggleFullscreen},
}};

constexpr std::array<DropdownItemSpec, 4U> kToolsItems = {{
    {"CAPTURE BMP", MenuAction::CaptureScreenshot},
    {"HUD PERF / 3D", MenuAction::TogglePerfHud},
    {"SAUVEGARDER .SAV", MenuAction::FlushSave},
    {"MENU RAPIDE", MenuAction::OpenQuickMenu},
}};

constexpr std::array<DropdownItemSpec, 4U> kSettingsItems = {{
    {"VITESSE TAB", MenuAction::ToggleSpeedMult},
    {"MUET AUDIO", MenuAction::ToggleMute},
    {"REMBOBINAGE", MenuAction::ToggleRewind},
    {"AIDE CONTROLES", MenuAction::ToggleControlsHelp},
}};

constexpr std::array<TopMenuSpec, static_cast<std::size_t>(kTopMenuCount)> kMenus = {{
    {"FICHIER", {6, 0, 56, kMenuBarHeight}, kFileItems.data(), static_cast<i32>(kFileItems.size())},
    {"EMULATION", {64, 0, 68, kMenuBarHeight}, kEmulationItems.data(), static_cast<i32>(kEmulationItems.size())},
    {"AFFICHAGE", {134, 0, 68, kMenuBarHeight}, kDisplayItems.data(), static_cast<i32>(kDisplayItems.size())},
    {"OUTILS", {204, 0, 50, kMenuBarHeight}, kToolsItems.data(), static_cast<i32>(kToolsItems.size())},
    {"PARAMETRES", {256, 0, 74, kMenuBarHeight}, kSettingsItems.data(), static_cast<i32>(kSettingsItems.size())},
}};

[[nodiscard]] bool is_valid_menu(i32 menu_index) noexcept {
    return menu_index >= 0 && menu_index < kTopMenuCount;
}

[[nodiscard]] bool is_valid_item(i32 menu_index, i32 item_index) noexcept {
    if (!is_valid_menu(menu_index)) {
        return false;
    }
    return item_index >= 0 && item_index < kMenus[static_cast<std::size_t>(menu_index)].item_count;
}

}  // namespace

bool point_in_rect(MenuBarRect rect, i32 x, i32 y) noexcept {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

std::string_view top_menu_label(i32 menu_index) noexcept {
    if (!is_valid_menu(menu_index)) {
        return "";
    }
    return kMenus[static_cast<std::size_t>(menu_index)].label;
}

MenuBarRect top_menu_rect(i32 menu_index) noexcept {
    if (!is_valid_menu(menu_index)) {
        return {};
    }
    return kMenus[static_cast<std::size_t>(menu_index)].tab_rect;
}

i32 dropdown_item_count(i32 menu_index) noexcept {
    if (!is_valid_menu(menu_index)) {
        return 0;
    }
    return kMenus[static_cast<std::size_t>(menu_index)].item_count;
}

MenuBarRect dropdown_panel_rect(i32 menu_index, i32 canvas_w) noexcept {
    if (!is_valid_menu(menu_index)) {
        return {};
    }
    const auto& menu = kMenus[static_cast<std::size_t>(menu_index)];
    const i32 max_x = std::max(4, canvas_w - kDropdownWidth - 4);
    const i32 px = std::clamp(menu.tab_rect.x, 4, max_x);
    return MenuBarRect{px, kMenuBarHeight, kDropdownWidth,
                       (menu.item_count * kDropdownItemHeight) + (kDropdownPadY * 2)};
}

MenuBarRect welcome_card_rect(i32 canvas_w, i32 canvas_h) noexcept {
    const i32 avail_h = std::max(240, canvas_h - kMenuBarHeight);
    const i32 w = std::clamp(canvas_w - 32, 320, 440);
    const i32 h = std::clamp(avail_h - 32, 240, 336);
    const i32 x = std::max(8, (canvas_w - w) / 2);
    const i32 y = kMenuBarHeight + std::max(8, (avail_h - h) / 2);
    return MenuBarRect{x, y, w, h};
}

MenuBarRect welcome_open_button_rect(i32 canvas_w, i32 canvas_h) noexcept {
    const MenuBarRect card = welcome_card_rect(canvas_w, canvas_h);
    const i32 bw = std::min(264, card.w - 32);
    return MenuBarRect{card.x + (card.w - bw) / 2, card.y + 82, bw, 32};
}

MenuBarRect controls_modal_rect(i32 canvas_w, i32 canvas_h) noexcept {
    const i32 avail_h = std::max(240, canvas_h - kMenuBarHeight);
    const i32 w = std::clamp(canvas_w - 32, 320, 444);
    const i32 h = std::clamp(avail_h - 32, 240, 312);
    return MenuBarRect{std::max(8, (canvas_w - w) / 2),
                       kMenuBarHeight + std::max(8, (avail_h - h) / 2), w, h};
}

MenuBarRect controls_close_button_rect(i32 canvas_w, i32 canvas_h) noexcept {
    const MenuBarRect modal = controls_modal_rect(canvas_w, canvas_h);
    return MenuBarRect{modal.x + (modal.w - 110) / 2, modal.y + modal.h - 30, 110, 22};
}

std::string_view dropdown_item_label(i32 menu_index, i32 item_index) noexcept {
    if (!is_valid_item(menu_index, item_index)) {
        return "";
    }
    return kMenus[static_cast<std::size_t>(menu_index)].items[item_index].label;
}

MenuAction dropdown_item_action(i32 menu_index, i32 item_index) noexcept {
    if (!is_valid_item(menu_index, item_index)) {
        return MenuAction::None;
    }
    return kMenus[static_cast<std::size_t>(menu_index)].items[item_index].action;
}

MenuBarHit hit_test_menu_bar_canvas(OverlayPointer ptr, i32 open_menu) noexcept {
    if (ptr.y >= 0 && ptr.y < kMenuBarHeight) {
        for (i32 i = 0; i < kTopMenuCount; ++i) {
            if (point_in_rect(top_menu_rect(i), ptr.x, ptr.y)) {
                return MenuBarHit{i, -1, MenuAction::None, true};
            }
        }
        return MenuBarHit{-1, -1, MenuAction::CloseOverlays, true};
    }

    if (!is_valid_menu(open_menu)) {
        return MenuBarHit{};
    }

    const MenuBarRect panel = dropdown_panel_rect(open_menu, ptr.canvas_w);
    if (!point_in_rect(panel, ptr.x, ptr.y)) {
        return MenuBarHit{-1, -1, MenuAction::CloseOverlays, true};
    }

    const i32 rel_y = ptr.y - (panel.y + kDropdownPadY);
    const i32 item_count = dropdown_item_count(open_menu);
    if (rel_y < 0 || rel_y >= (item_count * kDropdownItemHeight)) {
        return MenuBarHit{open_menu, -1, MenuAction::None, true};
    }
    const i32 item_idx = rel_y / kDropdownItemHeight;
    return MenuBarHit{open_menu, item_idx, dropdown_item_action(open_menu, item_idx), true};
}

MenuBarHit hit_test_menu_bar(i32 ox, i32 oy, i32 open_menu) noexcept {
    return hit_test_menu_bar_canvas({ox, oy, 512}, open_menu);
}

}  // namespace elmasds::frontend
