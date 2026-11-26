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

constexpr u32 kColDim = rgba8(6, 9, 14, 175);
constexpr u32 kColPanelBg = rgba8(17, 22, 31, 246);
constexpr u32 kColHeaderBg = rgba8(22, 29, 41, 255);
constexpr u32 kColBorder = rgba8(37, 48, 65, 255);
constexpr u32 kColAccentBorder = rgba8(51, 65, 85, 255);
constexpr u32 kColSelected = rgba8(30, 41, 59, 255);
constexpr u32 kColAmber = rgba8(245, 158, 11, 255);
constexpr u32 kColDarkText = rgba8(10, 14, 20, 255);
constexpr u32 kColText = rgba8(241, 245, 249, 255);
constexpr u32 kColMuted = rgba8(148, 163, 184, 255);
constexpr u32 kColStripBg = rgba8(10, 14, 20, 224);
constexpr u32 kColToastBg = rgba8(17, 22, 31, 244);

[[nodiscard]] constexpr HudOverlay::UiRect to_ui(MenuBarRect rect) noexcept {
    return {rect.x, rect.y, rect.w, rect.h};
}

void blit_glyph(std::span<u32> buf, HudOverlay::UiRect canvas_pos, u32 color, char ch) noexcept {
    const i32 cw = canvas_pos.w;
    const i32 ch_h = canvas_pos.h;
    const auto cols = glyph_5x7(ch);
    for (i32 cx = 0; cx < 5; ++cx) {
        const i32 px = canvas_pos.x + cx;
        if (px < 0 || px >= cw) { continue; }
        const u8 bits = cols[static_cast<std::size_t>(cx)];
        for (i32 cy = 0; cy < 7; ++cy) {
            const i32 py = canvas_pos.y + cy;
            if (py >= 0 && py < ch_h && ((bits >> cy) & 1U) != 0U) {
                buf[static_cast<std::size_t>(py * cw + px)] = color;
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

HudOverlay::HudOverlay() : buffer_(static_cast<std::size_t>(width_ * height_), 0U) {}

void HudOverlay::resize(i32 width, i32 height) noexcept {
    const i32 nw = std::max(320, width);
    const i32 nh = std::max(240, height);
    if (nw == width_ && nh == height_) { return; }
    width_ = nw;
    height_ = nh;
    buffer_.assign(static_cast<std::size_t>(width_ * height_), 0U);
}

void HudOverlay::fill_rect(UiRect rect, u32 color) noexcept {
    const i32 x0 = std::max(0, rect.x);
    const i32 y0 = std::max(0, rect.y);
    const i32 x1 = std::min(width_, rect.x + rect.w);
    const i32 y1 = std::min(height_, rect.y + rect.h);
    for (i32 y = y0; y < y1; ++y) {
        u32* const row = buffer_.data() + static_cast<std::size_t>(y * width_);
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
        if (cur_x + 5 > width_) { break; }
        blit_glyph(buffer_, {cur_x, anchor.y, width_, height_}, color, ch);
        cur_x += 6;
    }
}

void HudOverlay::draw_text_centered(std::string_view text, UiRect bounds, u32 color) noexcept {
    const i32 text_w = text.empty() ? 0 : (static_cast<i32>(text.size()) * 6 - 1);
    const i32 tx = bounds.x + std::max(0, (bounds.w - text_w) / 2);
    const i32 ty = bounds.y + std::max(0, (bounds.h - 7) / 2);
    draw_text(text, {tx, ty, 0, 0}, color);
}

void HudOverlay::draw_perf_strip(const HudState& state) noexcept {
    char line[96]{};
    const u32 eff = state.fast_forwarding ? std::max(state.speed_mult, state.locked_speed_mult) : state.locked_speed_mult;
    const int mult = state.rewinding ? -1 : static_cast<int>(eff);
    std::snprintf(line, sizeof(line), "FPS %5.1f | %4.1fMS | 3D %4zu | %dX | RW %2zu | S%u",
                  state.fps, state.frame_ms, state.polys_3d, mult, state.rewind_count, state.active_slot);
    const i32 top_y = state.fullscreen ? 8 : (kMenuBarHeight + 8);
    fill_rect({10, top_y, 356, 17}, kColStripBg);
    stroke_rect({10, top_y, 356, 17}, kColBorder);
    draw_text(line, {16, top_y + 5, 0, 0}, (eff > 1U) ? kColAmber : kColText);
}

void HudOverlay::draw_toast(std::string_view text) noexcept {
    const i32 box_w = std::clamp(static_cast<i32>(text.size()) * 6 + 20, 128, width_ - 24);
    const i32 x = (width_ - box_w) / 2;
    const i32 y = height_ - 32;
    fill_rect({x, y, box_w, 19}, kColToastBg);
    stroke_rect({x, y, box_w, 19}, kColAmber);
    draw_text_centered(text, {x, y, box_w, 19}, kColText);
}

void HudOverlay::draw_menu(const HudState& state) noexcept {
    fill_rect({0, kMenuBarHeight, width_, height_ - kMenuBarHeight}, kColDim);
    const i32 pw = std::min(428, width_ - 24);
    const i32 ph = std::min(300, height_ - kMenuBarHeight - 24);
    const i32 px = (width_ - pw) / 2;
    const i32 py = kMenuBarHeight + std::max(8, (height_ - kMenuBarHeight - ph) / 2);
    fill_rect({px, py, pw, ph}, kColPanelBg);
    fill_rect({px, py, pw, 30}, kColHeaderBg);
    stroke_rect({px, py, pw, ph}, kColAccentBorder);
    fill_rect({px, py, pw, 2}, kColAmber);

    const std::string_view title = state.rom_title.empty() ? "AUCUNE ROM" : state.rom_title;
    draw_text("ELMASDS // MENU RAPIDE", {px + 12, py + 11, 0, 0}, kColAmber);
    draw_text(title.substr(0U, 20U), {px + pw - 130, py + 11, 0, 0}, kColMuted);

    char val_gap[24]{}, val_ff[24]{}, val_slot[24]{};
    std::snprintf(val_gap, sizeof(val_gap), "%u PX", state.native_gap);
    std::snprintf(val_ff, sizeof(val_ff), "%uX", state.locked_speed_mult);
    std::snprintf(val_slot, sizeof(val_slot), "SLOT %u", state.active_slot);

    struct MenuRow { const char* label; const char* value; };
    const std::array<MenuRow, kMenuItemCount> rows = {{
        {"REPRENDRE", "ECHAP"}, {"OUVRIR ROM", "CTRL+O"},
        {"DISPOSITION", layout_name(state.layout_mode)}, {"INVERSER ECRANS", state.swap_screens ? "OUI" : "NON"},
        {"ESPACEMENT", val_gap}, {"FILTRE VISUEL", filter_name(state.filter_mode)},
        {"VITESSE JEU", val_ff}, {"SLOT SAVE STATE", val_slot},
        {"OVERLAY PERF/3D", state.perf_hud_open ? "ON" : "OFF"}, {"CAPTURE ECRAN BMP", "F12"},
        {"REINITIALISER", "CTRL+R"}, {"QUITTER", "ALT+F4"},
    }};

    for (i32 i = 0; i < kMenuItemCount; ++i) {
        const i32 ry = py + 38 + (i * 20);
        const bool sel = (i == state.menu_index);
        if (sel) {
            fill_rect({px + 8, ry - 2, pw - 16, 18}, kColSelected);
            fill_rect({px + 8, ry - 2, 3, 18}, kColAmber);
        }
        draw_text(rows[static_cast<std::size_t>(i)].label, {px + 18, ry + 3, 0, 0}, sel ? kColText : kColMuted);
        draw_text(rows[static_cast<std::size_t>(i)].value, {px + pw - 138, ry + 3, 0, 0}, sel ? kColAmber : kColText);
    }
    draw_text_centered("HAUT/BAS: CHOIX | GAUCHE/DROITE: REGLER | ENTREE: ACTIVER",
                       {px, py + ph - 18, pw, 14}, kColMuted);
}

void HudOverlay::draw_top_menu_bar(const HudState& state) noexcept {
    fill_rect({0, 0, width_, kMenuBarHeight}, kColHeaderBg);
    fill_rect({0, kMenuBarHeight - 1, width_, 1}, kColBorder);
    for (i32 i = 0; i < kTopMenuCount; ++i) {
        const MenuBarRect tab = top_menu_rect(i);
        const bool active = (i == state.open_dropdown);
        const bool hovered = (i == state.hover_menu);
        if (active || hovered) { fill_rect({tab.x, 1, tab.w, kMenuBarHeight - 2}, kColSelected); }
        if (active) { fill_rect({tab.x, kMenuBarHeight - 2, tab.w, 2}, kColAmber); }
        draw_text_centered(top_menu_label(i), {tab.x, 0, tab.w, kMenuBarHeight},
                           active ? kColAmber : (hovered ? kColText : kColMuted));
    }

    i32 right_x = width_ - 10;
    const std::string_view rom = state.rom_title.empty() ? "PRET" : std::string_view(state.rom_title).substr(0U, 18U);
    const i32 rom_w = static_cast<i32>(rom.size()) * 6;
    if (right_x - rom_w > 340) {
        right_x -= rom_w;
        draw_text(rom, {right_x, 8, 0, 0}, kColMuted);
        right_x -= 10;
    }
    auto draw_pill = [&](std::string_view tag) {
        const i32 pw = static_cast<i32>(tag.size()) * 6 + 10;
        if (right_x - pw < 334) { return; }
        right_x -= pw;
        fill_rect({right_x, 4, pw, 15}, kColSelected);
        stroke_rect({right_x, 4, pw, 15}, kColAmber);
        draw_text_centered(tag, {right_x, 4, pw, 15}, kColAmber);
        right_x -= 6;
    };
    const u32 eff = state.fast_forwarding ? std::max(state.speed_mult, state.locked_speed_mult) : state.locked_speed_mult;
    if (eff == 2U) { draw_pill("2X"); } else if (eff >= 4U) { draw_pill("4X"); }
    if (state.audio_muted) { draw_pill("MUET"); } else if (state.paused && state.rom_loaded) { draw_pill("PAUSE"); }
}

void HudOverlay::draw_dropdown(const HudState& state) noexcept {
    const i32 count = dropdown_item_count(state.open_dropdown);
    if (count <= 0) { return; }
    const MenuBarRect panel = dropdown_panel_rect(state.open_dropdown, width_);
    fill_rect(to_ui(panel), kColPanelBg);
    stroke_rect(to_ui(panel), kColAccentBorder);
    fill_rect({panel.x, panel.y, panel.w, 2}, kColAmber);

    char val[24]{};
    for (i32 i = 0; i < count; ++i) {
        const i32 ry = panel.y + 5 + (i * kDropdownItemHeight);
        const bool hovered = (i == state.hover_item);
        if (hovered) {
            fill_rect({panel.x + 4, ry, panel.w - 8, kDropdownItemHeight - 2}, kColSelected);
            fill_rect({panel.x + 4, ry, 2, kDropdownItemHeight - 2}, kColAmber);
        }
        draw_text(dropdown_item_label(state.open_dropdown, i), {panel.x + 12, ry + 7, 0, 0}, hovered ? kColText : kColMuted);
        format_dropdown_value(dropdown_item_action(state.open_dropdown, i), state, val);
        const std::string_view vv(val);
        draw_text(vv, {panel.x + panel.w - 12 - static_cast<i32>(vv.size()) * 6, ry + 7, 0, 0},
                  hovered ? kColAmber : kColText);
    }
}

void HudOverlay::draw_welcome_screen(const HudState& state) noexcept {
    if (state.rom_loaded || state.menu_open || state.controls_help_open) { return; }
    const UiRect card = to_ui(welcome_card_rect(width_, height_));
    fill_rect(card, kColPanelBg);
    fill_rect({card.x, card.y, card.w, 46}, kColHeaderBg);
    stroke_rect(card, kColAccentBorder);
    fill_rect({card.x, card.y, card.w, 3}, kColAmber);

    // ElmasCore dual-screen hardware emblem in the welcome header.
    const UiRect badge{card.x + 16, card.y + 11, 20, 26};
    fill_rect(badge, kColSelected);
    stroke_rect(badge, kColAccentBorder);
    fill_rect({badge.x + 3, badge.y + 3, 14, 9}, kColDarkText);
    fill_rect({badge.x + 3, badge.y + 14, 14, 9}, kColDarkText);
    stroke_rect({badge.x + 3, badge.y + 3, 14, 9}, kColAmber);
    fill_rect({badge.x + badge.w - 3, badge.y + 11, 2, 2}, kColAmber);

    draw_text("ELMASDS", {card.x + 44, card.y + 12, 0, 0}, kColText);
    draw_text("EMULATEUR NINTENDO DS", {card.x + 44, card.y + 26, 0, 0}, kColMuted);
    fill_rect({card.x + card.w - 64, card.y + 14, 48, 17}, kColSelected);
    stroke_rect({card.x + card.w - 64, card.y + 14, 48, 17}, kColAmber);
    draw_text_centered("V1.0.1", {card.x + card.w - 64, card.y + 14, 48, 17}, kColAmber);

    draw_text_centered("SELECTIONNEZ UNE CARTOUCHE .NDS", {card.x, card.y + 58, card.w, 14}, kColMuted);
    const UiRect btn = to_ui(welcome_open_button_rect(width_, height_));
    fill_rect(btn, state.hover_welcome_btn ? kColAmber : kColSelected);
    stroke_rect(btn, kColAmber);
    draw_text_centered("OUVRIR UNE ROM (.NDS)", btn, state.hover_welcome_btn ? kColDarkText : kColAmber);
    draw_text_centered("OU GLISSER-DEPOSER DIRECTEMENT SUR LA FENETRE", {card.x, btn.y + 42, card.w, 12}, kColMuted);

    const i32 tbl_y = btn.y + 64;
    fill_rect({card.x + 16, tbl_y, card.w - 32, card.h - (tbl_y - card.y) - 16}, kColDarkText);
    stroke_rect({card.x + 16, tbl_y, card.w - 32, card.h - (tbl_y - card.y) - 16}, kColBorder);
    constexpr std::array<const char*, 6U> kHints = {{
        "CROIX : FLECHES    | A / B / X / Y : L / K / I / J",
        "L / R : Q / E      | START / SEL   : ENTREE / MAJ",
        "STYLET TACTILE     : CLIC GAUCHE ECRAN INFERIEUR",
        "VITESSE X2 / X4    : MENU EMULATION OU TAB",
        "SAVE STATE / CAPT  : SLOTS 1-8 + F5 / F7  | F12",
        "PAUSE : ESPACE     | MUET : M | MENU RAPIDE : ECHAP",
    }};
    for (std::size_t i = 0U; i < kHints.size(); ++i) {
        draw_text(kHints[i], {card.x + 28, tbl_y + 12 + static_cast<i32>(i) * 19, 0, 0}, kColMuted);
    }
}

void HudOverlay::draw_controls_help() noexcept {
    fill_rect({0, kMenuBarHeight, width_, height_ - kMenuBarHeight}, kColDim);
    const UiRect modal = to_ui(controls_modal_rect(width_, height_));
    fill_rect(modal, kColPanelBg);
    fill_rect({modal.x, modal.y, modal.w, 30}, kColHeaderBg);
    stroke_rect(modal, kColAccentBorder);
    fill_rect({modal.x, modal.y, modal.w, 2}, kColAmber);
    draw_text("AIDE CONTROLES // ELMASDS", {modal.x + 16, modal.y + 11, 0, 0}, kColAmber);

    constexpr std::array<const char*, 10U> kLines = {{
        "CROIX DIRECTIONNELLE : FLECHES CLAVIER",
        "BOUTONS A / B / X / Y : L / K / I / J",
        "GACHETTES L / R       : Q / E",
        "START / SELECT        : ENTREE / MAJ DROIT",
        "STYLET TACTILE        : SOURIS ECRAN DU BAS",
        "OUVRIR ROM / RESET    : CTRL+O / CTRL+R",
        "PAUSE / MUET AUDIO    : ESPACE / M",
        "VITESSE X2 / X4       : MENU EMULATION / TAB",
        "DISPOSITION / FILTRE  : F6 / F8 / F2 / F11",
        "SAVE STATE / CAPTURE  : 1-8 + F5 / F7 / F12",
    }};
    for (std::size_t i = 0U; i < kLines.size(); ++i) {
        draw_text(kLines[i], {modal.x + 18, modal.y + 44 + static_cast<i32>(i) * 21, 0, 0}, kColText);
    }
    const UiRect close_btn = to_ui(controls_close_button_rect(width_, height_));
    fill_rect(close_btn, kColSelected);
    stroke_rect(close_btn, kColAmber);
    draw_text_centered("FERMER", close_btn, kColAmber);
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
