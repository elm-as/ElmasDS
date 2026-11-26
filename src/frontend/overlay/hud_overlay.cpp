#include "frontend/overlay/hud_overlay.hpp"

#include "frontend/overlay/menu_bar.hpp"
#include "frontend/overlay/pixel_font.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace elmasds::frontend {
namespace {

[[nodiscard]] constexpr u32 rgba8(u8 r, u8 g, u8 b, u8 a) noexcept {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8U) |
           (static_cast<u32>(b) << 16U) | (static_cast<u32>(a) << 24U);
}

constexpr u32 kColDim = rgba8(6, 9, 14, 155);
constexpr u32 kColPanelBg = rgba8(12, 16, 22, 240);
constexpr u32 kColHeaderBg = rgba8(18, 24, 33, 255);
constexpr u32 kColBorder = rgba8(44, 54, 67, 255);
constexpr u32 kColSelected = rgba8(32, 41, 56, 255);
constexpr u32 kColAmber = rgba8(245, 158, 11, 255);
constexpr u32 kColDarkText = rgba8(12, 16, 22, 255);
constexpr u32 kColText = rgba8(229, 231, 235, 255);
constexpr u32 kColMuted = rgba8(148, 163, 184, 255);
constexpr u32 kColStripBg = rgba8(10, 14, 20, 210);
constexpr u32 kColToastBg = rgba8(15, 21, 29, 235);

[[nodiscard]] constexpr HudOverlay::UiRect to_ui(MenuBarRect rect) noexcept {
    return {rect.x, rect.y, rect.w, rect.h};
}

void blit_glyph_5x7(std::span<u32> buf, HudOverlay::UiRect pos, u32 color, char ch) noexcept {
    const auto cols = glyph_5x7(ch);
    for (i32 cx = 0; cx < 5; ++cx) {
        const i32 px = pos.x + cx;
        if (px < 0 || px >= HudOverlay::kWidth) { continue; }
        const u8 col_bits = cols[static_cast<std::size_t>(cx)];
        for (i32 cy = 0; cy < 7; ++cy) {
            const i32 py = pos.y + cy;
            if (py >= 0 && py < HudOverlay::kHeight && ((col_bits >> cy) & 1U) != 0U) {
                buf[static_cast<std::size_t>(py * HudOverlay::kWidth + px)] = color;
            }
        }
    }
}

[[nodiscard]] const char* layout_name(ScreenLayoutMode mode) noexcept {
    switch (mode) {
    case ScreenLayoutMode::SideBySide: return "COTE A COTE";
    case ScreenLayoutMode::HybridTop: return "HYBRIDE HAUT";
    case ScreenLayoutMode::HybridBottom: return "HYBRIDE BAS";
    case ScreenLayoutMode::Vertical:
    default: return "VERTICAL";
    }
}

[[nodiscard]] const char* filter_name(ShaderFilterMode mode) noexcept {
    switch (mode) {
    case ShaderFilterMode::LcdGrid: return "GRILLE LCD";
    case ShaderFilterMode::ColorCorrect: return "COULEURS DS";
    case ShaderFilterMode::LcdColorCorrect: return "LCD + COUL";
    case ShaderFilterMode::SharpNearest:
    default: return "PIXEL SHARP";
    }
}

void format_dropdown_value(MenuAction action, const HudState& state, std::span<char> out) noexcept {
    switch (action) {
    case MenuAction::OpenRom: std::snprintf(out.data(), out.size(), "CTRL+O"); break;
    case MenuAction::SaveState: std::snprintf(out.data(), out.size(), "F5"); break;
    case MenuAction::LoadState: std::snprintf(out.data(), out.size(), "F7"); break;
    case MenuAction::CycleSlot: std::snprintf(out.data(), out.size(), "SLOT %u", state.active_slot); break;
    case MenuAction::Quit: std::snprintf(out.data(), out.size(), "ALT+F4"); break;
    case MenuAction::TogglePause: std::snprintf(out.data(), out.size(), state.paused ? "EN PAUSE" : "ESPACE"); break;
    case MenuAction::ToggleSpeed2x: std::snprintf(out.data(), out.size(), state.locked_speed_mult == 2U ? "ACTIF (2X)" : "OFF"); break;
    case MenuAction::ToggleSpeed4x: std::snprintf(out.data(), out.size(), state.locked_speed_mult == 4U ? "ACTIF (4X)" : "OFF"); break;
    case MenuAction::ResetConsole: std::snprintf(out.data(), out.size(), "CTRL+R"); break;
    case MenuAction::ToggleRewind: std::snprintf(out.data(), out.size(), state.rewind_enabled ? "ON (F9)" : "OFF (F9)"); break;
    case MenuAction::ToggleMute: std::snprintf(out.data(), out.size(), state.audio_muted ? "MUET (M)" : "ACTIF (M)"); break;
    case MenuAction::CycleLayout: std::snprintf(out.data(), out.size(), "%s", layout_name(state.layout_mode)); break;
    case MenuAction::ToggleSwapScreens: std::snprintf(out.data(), out.size(), state.swap_screens ? "OUI" : "NON"); break;
    case MenuAction::CycleGap: std::snprintf(out.data(), out.size(), "%u PX", state.native_gap); break;
    case MenuAction::CycleFilter: std::snprintf(out.data(), out.size(), "%s", filter_name(state.filter_mode)); break;
    case MenuAction::ToggleFullscreen: std::snprintf(out.data(), out.size(), state.fullscreen ? "OUI" : "F11"); break;
    case MenuAction::CaptureScreenshot: std::snprintf(out.data(), out.size(), "F12"); break;
    case MenuAction::TogglePerfHud: std::snprintf(out.data(), out.size(), state.perf_hud_open ? "ON (F3)" : "OFF (F3)"); break;
    case MenuAction::FlushSave: std::snprintf(out.data(), out.size(), ".SAV"); break;
    case MenuAction::OpenQuickMenu: std::snprintf(out.data(), out.size(), "ECHAP"); break;
    case MenuAction::ToggleSpeedMult: std::snprintf(out.data(), out.size(), "%uX", state.speed_mult); break;
    case MenuAction::ToggleControlsHelp: std::snprintf(out.data(), out.size(), "CLAVIER"); break;
    default: out[0] = '\0'; break;
    }
}

}  // namespace

HudOverlay::HudOverlay() : buffer_(static_cast<std::size_t>(kWidth * kHeight), 0U) {}

void HudOverlay::fill_rect(UiRect rect, u32 color) noexcept {
    const i32 x0 = std::max(0, rect.x);
    const i32 y0 = std::max(0, rect.y);
    const i32 x1 = std::min(kWidth, rect.x + rect.w);
    const i32 y1 = std::min(kHeight, rect.y + rect.h);
    for (i32 y = y0; y < y1; ++y) {
        u32* const row = buffer_.data() + static_cast<std::size_t>(y * kWidth);
        std::fill(row + x0, row + x1, color);
    }
}

void HudOverlay::stroke_rect(UiRect rect, u32 color) noexcept {
    fill_rect({rect.x, rect.y, rect.w, 1}, color);
    fill_rect({rect.x, rect.y + rect.h - 1, rect.w, 1}, color);
    fill_rect({rect.x, rect.y, 1, rect.h}, color);
    fill_rect({rect.x + rect.w - 1, rect.y, 1, rect.h}, color);
}

void HudOverlay::draw_text(std::string_view text, UiRect anchor, u32 color) noexcept {
    i32 cur_x = anchor.x;
    for (const char ch : text) {
        if (cur_x + 5 > kWidth) { break; }
        blit_glyph_5x7(buffer_, {cur_x, anchor.y, 0, 0}, color, ch);
        cur_x += 6;
    }
}

void HudOverlay::draw_perf_strip(const HudState& state) noexcept {
    char line[96]{};
    const u32 eff_speed =
        state.fast_forwarding ? std::max(state.speed_mult, state.locked_speed_mult) : state.locked_speed_mult;
    const int mult = state.rewinding ? -1 : static_cast<int>(eff_speed);
    std::snprintf(line, sizeof(line), "FPS %5.1f | %4.1fMS | 3D %4zu | %dX | RW %2zu | S%u",
                  state.fps, state.frame_ms, state.polys_3d, mult, state.rewind_count, state.active_slot);
    const i32 top_y = state.fullscreen ? 8 : (kMenuBarHeight + 6);
    fill_rect({8, top_y, 356, 15}, kColStripBg);
    stroke_rect({8, top_y, 356, 15}, kColBorder);
    draw_text(line, {14, top_y + 4, 0, 0}, (eff_speed > 1U) ? kColAmber : kColText);
}

void HudOverlay::draw_toast(std::string_view text) noexcept {
    const i32 box_w = std::clamp(static_cast<i32>(text.size()) * 6 + 16, 120, kWidth - 24);
    const i32 x = (kWidth - box_w) / 2;
    const i32 y = kHeight - 28;
    fill_rect({x, y, box_w, 17}, kColToastBg);
    stroke_rect({x, y, box_w, 17}, kColAmber);
    draw_text(text, {x + 8, y + 5, 0, 0}, kColText);
}

void HudOverlay::draw_menu(const HudState& state) noexcept {
    fill_rect({0, 0, kWidth, kHeight}, kColDim);
    constexpr i32 kPanelX = 46;
    constexpr i32 kPanelY = 44;
    constexpr i32 kPanelW = 420;
    constexpr i32 kPanelH = 296;
    fill_rect({kPanelX, kPanelY, kPanelW, kPanelH}, kColPanelBg);
    fill_rect({kPanelX, kPanelY, kPanelW, 30}, kColHeaderBg);
    stroke_rect({kPanelX, kPanelY, kPanelW, kPanelH}, kColBorder);
    fill_rect({kPanelX, kPanelY, kPanelW, 2}, kColAmber);

    const std::string_view title = state.rom_title.empty() ? "AUCUNE ROM" : state.rom_title;
    draw_text("ELMASDS // MENU RAPIDE", {kPanelX + 12, kPanelY + 8, 0, 0}, kColAmber);
    draw_text(title.substr(0U, 24U), {kPanelX + 190, kPanelY + 8, 0, 0}, kColMuted);

    char val_gap[24]{};
    char val_ff[24]{};
    char val_slot[24]{};
    std::snprintf(val_gap, sizeof(val_gap), "%u PX", state.native_gap);
    std::snprintf(val_ff, sizeof(val_ff), "%uX", state.locked_speed_mult);
    std::snprintf(val_slot, sizeof(val_slot), "SLOT %u", state.active_slot);

    struct MenuRow { const char* label; const char* value; };
    const std::array<MenuRow, kMenuItemCount> rows = {{
        {"REPRENDRE", "ECHAP"},
        {"OUVRIR ROM", "CTRL+O"},
        {"DISPOSITION", layout_name(state.layout_mode)},
        {"INVERSER ECRANS", state.swap_screens ? "OUI" : "NON"},
        {"ESPACEMENT", val_gap},
        {"FILTRE VISUEL", filter_name(state.filter_mode)},
        {"VITESSE JEU", val_ff},
        {"SLOT SAVE STATE", val_slot},
        {"OVERLAY PERF/3D", state.perf_hud_open ? "ON" : "OFF"},
        {"CAPTURE ECRAN BMP", "F12"},
        {"REINITIALISER", "CTRL+R"},
        {"QUITTER", "ALT+F4"},
    }};

    for (i32 i = 0; i < kMenuItemCount; ++i) {
        const i32 ry = kPanelY + 38 + (i * 20);
        const bool sel = (i == state.menu_index);
        if (sel) {
            fill_rect({kPanelX + 8, ry - 2, kPanelW - 16, 17}, kColSelected);
            fill_rect({kPanelX + 8, ry - 2, 3, 17}, kColAmber);
        }
        draw_text(rows[static_cast<std::size_t>(i)].label, {kPanelX + 18, ry + 3, 0, 0},
                  sel ? kColText : kColMuted);
        draw_text(rows[static_cast<std::size_t>(i)].value, {kPanelX + 260, ry + 3, 0, 0},
                  sel ? kColAmber : kColText);
    }

    draw_text("HAUT/BAS: CHOIX | GAUCHE/DROITE: REGLER | ENTREE: ACTIVER",
              {kPanelX + 14, kPanelY + kPanelH - 16, 0, 0}, kColMuted);
}

void HudOverlay::draw_top_menu_bar(const HudState& state) noexcept {
    fill_rect({0, 0, kWidth, kMenuBarHeight}, kColHeaderBg);
    fill_rect({0, kMenuBarHeight - 1, kWidth, 1}, kColBorder);
    for (i32 i = 0; i < kTopMenuCount; ++i) {
        const MenuBarRect tab = top_menu_rect(i);
        const bool active = (i == state.open_dropdown);
        const bool hovered = (i == state.hover_menu);
        if (active || hovered) {
            fill_rect({tab.x, tab.y, tab.w, tab.h - 1}, kColSelected);
        }
        if (active) {
            fill_rect({tab.x, 0, tab.w, 2}, kColAmber);
        }
        draw_text(top_menu_label(i), {tab.x + 6, 5, 0, 0}, (active || hovered) ? kColAmber : kColText);
    }
    if (state.audio_muted) {
        draw_text("[MUET]", {324, 5, 0, 0}, kColAmber);
    } else if (state.paused && state.rom_loaded) {
        draw_text("[PAUSE]", {324, 5, 0, 0}, kColAmber);
    }
    const u32 eff_speed =
        state.fast_forwarding ? std::max(state.speed_mult, state.locked_speed_mult) : state.locked_speed_mult;
    if (eff_speed > 1U) {
        char speed_badge[8]{};
        std::snprintf(speed_badge, sizeof(speed_badge), "[%uX]", eff_speed);
        draw_text(speed_badge, {368, 5, 0, 0}, kColAmber);
    }
    const std::string_view title = state.rom_title.empty() ? "AUCUNE ROM"
                                                           : std::string_view(state.rom_title).substr(0U, 18U);
    const i32 title_x = std::max(396, kWidth - 8 - static_cast<i32>(title.size()) * 6);
    draw_text(title, {title_x, 5, 0, 0}, kColMuted);
}

void HudOverlay::draw_dropdown(const HudState& state) noexcept {
    const i32 count = dropdown_item_count(state.open_dropdown);
    if (count <= 0) { return; }
    const MenuBarRect panel = dropdown_panel_rect(state.open_dropdown);
    fill_rect(to_ui(panel), kColPanelBg);
    stroke_rect(to_ui(panel), kColBorder);
    fill_rect({panel.x, panel.y, panel.w, 2}, kColAmber);

    char val[24]{};
    for (i32 i = 0; i < count; ++i) {
        const i32 ry = panel.y + 4 + (i * kDropdownItemHeight);
        const bool hovered = (i == state.hover_item);
        if (hovered) {
            fill_rect({panel.x + 4, ry, panel.w - 8, kDropdownItemHeight - 2}, kColSelected);
            fill_rect({panel.x + 4, ry, 2, kDropdownItemHeight - 2}, kColAmber);
        }
        draw_text(dropdown_item_label(state.open_dropdown, i), {panel.x + 12, ry + 5, 0, 0},
                  hovered ? kColText : kColMuted);
        format_dropdown_value(dropdown_item_action(state.open_dropdown, i), state, val);
        const std::string_view val_view(val);
        const i32 vx = panel.x + panel.w - 10 - static_cast<i32>(val_view.size()) * 6;
        draw_text(val_view, {vx, ry + 5, 0, 0}, hovered ? kColAmber : kColText);
    }
}

void HudOverlay::draw_welcome_screen(const HudState& state) noexcept {
    if (state.rom_loaded || state.menu_open || state.controls_help_open) { return; }
    constexpr UiRect kCard{56, 52, 400, 282};
    fill_rect(kCard, kColPanelBg);
    fill_rect({kCard.x, kCard.y, kCard.w, 34}, kColHeaderBg);
    stroke_rect(kCard, kColBorder);
    fill_rect({kCard.x, kCard.y, kCard.w, 3}, kColAmber);

    draw_text("ELMASDS // NINTENDO DS", {kCard.x + 18, kCard.y + 13, 0, 0}, kColText);
    draw_text("V1.0.0", {kCard.x + kCard.w - 54, kCard.y + 13, 0, 0}, kColAmber);
    draw_text("OUVREZ UNE ROM .NDS POUR DEMARRER", {154, kCard.y + 46, 0, 0}, kColMuted);

    const UiRect btn = to_ui(kWelcomeOpenButton);
    fill_rect(btn, state.hover_welcome_btn ? kColAmber : kColSelected);
    stroke_rect(btn, kColAmber);
    draw_text("OUVRIR UNE ROM (.NDS)", {btn.x + 67, btn.y + 11, 0, 0},
              state.hover_welcome_btn ? kColDarkText : kColAmber);

    draw_text("OU GLISSER-DEPOSER UN FICHIER .NDS SUR LA FENETRE", {109, 160, 0, 0}, kColMuted);
    fill_rect({kCard.x + 18, 178, kCard.w - 36, 1}, kColBorder);

    constexpr std::array<const char*, 6U> kHints = {{
        "CROIX : FLECHES    | A/B/X/Y : L/K/I/J",
        "GACHETTES : Q / E  | START/SEL : ENTREE / MAJ",
        "STYLET : CLIC GAUCHE SUR ECRAN DU BAS",
        "SAUVEGARDE ETAT : 1-8 + F5 / F7",
        "PAUSE : ESPACE     | MUET AUDIO : M",
        "MENU RAPIDE : ECHAP | PERF 3D : F3",
    }};
    for (std::size_t i = 0U; i < kHints.size(); ++i) {
        draw_text(kHints[i], {kCard.x + 24, 192 + static_cast<i32>(i) * 16, 0, 0}, kColMuted);
    }
}

void HudOverlay::draw_controls_help() noexcept {
    fill_rect({0, 0, kWidth, kHeight}, kColDim);
    constexpr UiRect kModal{46, 44, 420, 300};
    fill_rect(kModal, kColPanelBg);
    fill_rect({kModal.x, kModal.y, kModal.w, 30}, kColHeaderBg);
    stroke_rect(kModal, kColBorder);
    fill_rect({kModal.x, kModal.y, kModal.w, 2}, kColAmber);
    draw_text("AIDE CONTROLES // ELMASDS", {kModal.x + 14, kModal.y + 11, 0, 0}, kColAmber);

    constexpr std::array<const char*, 10U> kLines = {{
        "CROIX DIRECTIONNELLE : FLECHES CLAVIER",
        "BOUTONS A / B / X / Y : L / K / I / J",
        "GACHETTES L / R       : Q / E",
        "START / SELECT        : ENTREE / MAJ DROIT",
        "STYLET TACTILE        : SOURIS ECRAN DU BAS",
        "OUVRIR ROM / RESET    : CTRL+O / CTRL+R",
        "PAUSE / MUET AUDIO    : ESPACE / M",
        "AVANCE RAPIDE / REWIND: TAB / RETOUR ARR (F9)",
        "DISPOSITION / FILTRE  : F6 / F8 / F2 / F11",
        "SAVE STATE / CAPTURE  : 1-8 + F5 / F7 / F12",
    }};
    for (std::size_t i = 0U; i < kLines.size(); ++i) {
        draw_text(kLines[i], {kModal.x + 16, kModal.y + 44 + static_cast<i32>(i) * 20, 0, 0}, kColText);
    }
    const UiRect close_btn = to_ui(kControlsCloseButton);
    fill_rect(close_btn, kColSelected);
    stroke_rect(close_btn, kColAmber);
    draw_text("[ FERMER ]", {close_btn.x + 20, close_btn.y + 6, 0, 0}, kColAmber);
}

bool HudOverlay::render(const HudState& state) noexcept {
    const bool bar_visible = !state.fullscreen || state.open_dropdown >= 0;
    const bool welcome_visible = !state.rom_loaded;
    if (!bar_visible && !welcome_visible && !state.menu_open && !state.controls_help_open &&
        !state.perf_hud_open && !state.toast_visible) {
        return false;
    }
    std::fill(buffer_.begin(), buffer_.end(), 0U);
    draw_welcome_screen(state);
    if (state.perf_hud_open) { draw_perf_strip(state); }
    if (state.menu_open) { draw_menu(state); }
    if (state.controls_help_open) { draw_controls_help(); }
    if (bar_visible) { draw_top_menu_bar(state); }
    if (state.open_dropdown >= 0) { draw_dropdown(state); }
    if (state.toast_visible && !state.toast_text.empty()) { draw_toast(state.toast_text); }
    return true;
}

}  // namespace elmasds::frontend
