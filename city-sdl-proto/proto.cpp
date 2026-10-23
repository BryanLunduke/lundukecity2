// Lunduke City SDL3 shell prototype (evaluation only, not for the City repo).
// The map, tool palette, minimap and R/C/I widgets are drawn by the *unmodified*
// City Cairo code (MapView/ToolPalette/MinimapView/DemandView::on_draw) into one
// Cairo ARGB32 surface that is uploaded to an SDL3 streaming texture each frame.
// Menu bar, dropdown, tooltip and status text are new custom Cairo+Pango widgets
// styled after the LCOS Clearlooks-Phenix GTK theme. GTK is still linked here only
// because those four classes still derive from Gtk::DrawingArea (the real migration
// would re-base them on a tiny "CairoWidget" interface; their on_draw bodies stay).
#include "city_session.hpp"
#include "map_view.hpp"
#include "side_widgets.hpp"
#include "tool_palette.hpp"
#include "tools.hpp"
#include <SDL3/SDL.h>
#include <cairomm/cairomm.h>
#include <gtkmm.h>
#include <pango/pangocairo.h>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

struct MapX : MapView { using MapView::on_draw; };
struct PalX : ToolPalette { using ToolPalette::on_draw; };
struct MiniX : MinimapView { using MinimapView::on_draw; };
struct DemX : DemandView { using DemandView::on_draw; };

// ---------- tiny Cairo/Pango helpers ----------
static void rgb(cairo_t *c, int r, int g, int b) { cairo_set_source_rgb(c, r / 255.0, g / 255.0, b / 255.0); }
static int text(cairo_t *c, const std::string &s, double x, double y, int r = 0, int g = 0, int b = 0,
                bool mnemonic_underline = false, int align = 0 /*0 left,1 center,2 right*/) {
    PangoLayout *l = pango_cairo_create_layout(c);
    PangoFontDescription *fd = pango_font_description_from_string("Sans 10");
    pango_layout_set_font_description(l, fd);
    std::string plain; int mn = -1;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '_' && i + 1 < s.size()) { mn = (int)plain.size(); continue; } plain += s[i]; }
    pango_layout_set_text(l, plain.c_str(), -1);
    if (mnemonic_underline && mn >= 0) {
        PangoAttrList *al = pango_attr_list_new(); PangoAttribute *u = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        u->start_index = mn; u->end_index = mn + 1; pango_attr_list_insert(al, u); pango_layout_set_attributes(l, al); pango_attr_list_unref(al);
    }
    int w, h; pango_layout_get_pixel_size(l, &w, &h);
    double xx = align == 1 ? x - w / 2.0 : align == 2 ? x - w : x;
    rgb(c, r, g, b); cairo_move_to(c, xx, y - h / 2.0); pango_cairo_show_layout(c, l);
    pango_font_description_free(fd); g_object_unref(l); return w;
}

// ---------- custom menu model + menu bar widget ----------
struct Item { std::string label, accel; enum K { Normal, Check, Radio, Sep } kind = Normal; bool on = false; SDL_Keycode key = 0; SDL_Keymod mod = SDL_KMOD_NONE; std::function<void()> act; };
struct Menu { std::string title; std::vector<Item> items; double x = 0, w = 0; };
struct MenuBar {
    std::vector<Menu> menus; int open = -1, hot = -1; bool show_mnemonics = false; static constexpr int H = 28, IH = 29;
    static char mnemonic(const std::string &s) { auto p = s.find('_'); return p == std::string::npos ? 0 : (char)SDL_tolower(s[p + 1]); }
    void layout(cairo_t *c) { double x = 4; for (auto &m : menus) { m.x = x; int w = text(c, m.title, -1000, -1000); m.w = w + 14; x += m.w; } }
    void draw_bar(cairo_t *c, int W) {
        cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, 0, H);
        cairo_pattern_add_color_stop_rgb(p, 0, 234 / 255.0, 233 / 255.0, 232 / 255.0); cairo_pattern_add_color_stop_rgb(p, 1, 228 / 255.0, 227 / 255.0, 225 / 255.0);
        cairo_set_source(c, p); cairo_rectangle(c, 0, 0, W, H); cairo_fill(c); cairo_pattern_destroy(p);
        for (int i = 0; i < (int)menus.size(); ++i) {
            auto &m = menus[i]; bool sel = i == open;
            if (sel) { // Clearlooks selected item: blue vertical gradient with darker border
                cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, H);
                cairo_pattern_add_color_stop_rgb(g, 0, 163 / 255.0, 194 / 255.0, 232 / 255.0); cairo_pattern_add_color_stop_rgb(g, 1, 130 / 255.0, 167 / 255.0, 215 / 255.0);
                cairo_set_source(c, g); cairo_rectangle(c, m.x, 0, m.w, H); cairo_fill(c); cairo_pattern_destroy(g);
                rgb(c, 75, 110, 155); cairo_set_line_width(c, 1); cairo_rectangle(c, m.x + .5, .5, m.w - 1, H - 1); cairo_stroke(c);
            }
            text(c, m.title, m.x + 7, H / 2.0, sel ? 255 : 0, sel ? 255 : 0, sel ? 255 : 0, show_mnemonics);
        }
    }
    double dw = 238;
    void draw_drop(cairo_t *c) {
        if (open < 0) return; auto &m = menus[open]; double x = m.x, y = H; double h = 6;
        for (auto &it : m.items) h += it.kind == Item::Sep ? 9 : IH;
        rgb(c, 255, 255, 255); cairo_rectangle(c, x, y, dw, h); cairo_fill(c);
        rgb(c, 173, 170, 167); cairo_set_line_width(c, 1); cairo_rectangle(c, x + .5, y + .5, dw - 1, h - 1); cairo_stroke(c);
        double yy = y + 0;
        for (int i = 0; i < (int)m.items.size(); ++i) {
            auto &it = m.items[i];
            if (it.kind == Item::Sep) { rgb(c, 214, 212, 210); cairo_rectangle(c, x + 1, yy + 4, dw - 2, 1); cairo_fill(c); yy += 9; continue; }
            bool sel = i == hot; int fg = sel ? 255 : 28;
            if (sel) { rgb(c, 134, 171, 217); cairo_rectangle(c, x + 2, yy, dw - 4, IH); cairo_fill(c); }
            double cy = yy + IH / 2.0;
            if (it.kind == Item::Check) {
                cairo_new_path(c); rgb(c, 255, 255, 255); cairo_rectangle(c, x + 6, cy - 6, 12, 12); cairo_fill(c);
                rgb(c, 132, 132, 132); cairo_rectangle(c, x + 6.5, cy - 5.5, 11, 11); cairo_stroke(c);
                if (it.on) { rgb(c, 28, 28, 28); cairo_set_line_width(c, 2); cairo_move_to(c, x + 8.5, cy); cairo_line_to(c, x + 11, cy + 3); cairo_line_to(c, x + 16, cy - 4); cairo_stroke(c); cairo_set_line_width(c, 1); }
            } else if (it.kind == Item::Radio) {
                rgb(c, 255, 255, 255); cairo_new_sub_path(c); cairo_arc(c, x + 12, cy, 5.5, 0, 6.2832); cairo_fill_preserve(c); rgb(c, 160, 160, 160); cairo_stroke(c);
                if (it.on) { rgb(c, 28, 28, 28); cairo_new_sub_path(c); cairo_arc(c, x + 12, cy, 2.8, 0, 6.2832); cairo_fill(c); }
            }
            text(c, it.label, x + 22, cy, fg, fg, fg, show_mnemonics);
            if (!it.accel.empty()) text(c, it.accel, x + dw - 8, cy, sel ? 255 : 128, sel ? 255 : 128, sel ? 255 : 128, false, 2);
            yy += IH;
        }
    }
    int item_at(double px, double py) {
        if (open < 0) return -1; auto &m = menus[open]; if (px < m.x || px > m.x + dw) return -1; double yy = H + 0;
        for (int i = 0; i < (int)m.items.size(); ++i) { double ih = m.items[i].kind == Item::Sep ? 9 : IH; if (py >= yy && py < yy + ih) return m.items[i].kind == Item::Sep ? -1 : i; yy += ih; }
        return -1;
    }
    int title_at(double px, double py) { if (py >= H) return -1; for (int i = 0; i < (int)menus.size(); ++i) if (px >= menus[i].x && px < menus[i].x + menus[i].w) return i; return -1; }
    void activate(Item &it) { if (it.kind == Item::Check) it.on = !it.on; if (it.kind == Item::Radio) { for (auto &o : menus[open].items) if (o.kind == Item::Radio) o.on = false; it.on = true; } if (it.act) it.act(); open = -1; hot = -1; }
    void step(int d) { auto &v = menus[open].items; int n = (int)v.size(), i = hot; do { i = (i + d + n) % n; } while (v[i].kind == Item::Sep); hot = i; }
    // returns true if the event was consumed by the menu system
    bool event(const SDL_Event &e) {
        if (e.type == SDL_EVENT_KEY_DOWN) {
            SDL_Keycode k = e.key.key; SDL_Keymod md = e.key.mod;
            if (k == SDLK_LALT || k == SDLK_RALT) { show_mnemonics = true; return true; }
            if (k == SDLK_F10) { open = open < 0 ? 0 : -1; hot = -1; return true; }
            if (open < 0 && (md & SDL_KMOD_ALT)) { for (int i = 0; i < (int)menus.size(); ++i) if (mnemonic(menus[i].title) == (char)k) { open = i; hot = -1; step(1); return true; } }
            if (open >= 0) {
                if (k == SDLK_ESCAPE) { open = -1; } else if (k == SDLK_DOWN) step(1); else if (k == SDLK_UP) step(-1);
                else if (k == SDLK_LEFT) { open = (open + (int)menus.size() - 1) % menus.size(); hot = -1; step(1); }
                else if (k == SDLK_RIGHT) { open = (open + 1) % menus.size(); hot = -1; step(1); }
                else if (k == SDLK_RETURN && hot >= 0) activate(menus[open].items[hot]);
                else for (auto &it : menus[open].items) if (it.kind != Item::Sep && mnemonic(it.label) == (char)k) { activate(it); break; }
                return true;
            }
            // global accelerators (GtkAccelGroup equivalent)
            for (auto &m : menus) for (auto &it : m.items) if (it.key && it.key == k && (md & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == it.mod) { int o = open; open = int(&m - &menus[0]); activate(it); open = o; return true; }
        }
        if (e.type == SDL_EVENT_KEY_UP && (e.key.key == SDLK_LALT || e.key.key == SDLK_RALT)) { show_mnemonics = false; return false; }
        if (e.type == SDL_EVENT_MOUSE_MOTION) { int t = title_at(e.motion.x, e.motion.y); if (open >= 0 && t >= 0) open = t; if (open >= 0) { hot = item_at(e.motion.x, e.motion.y); return true; } }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) { int t = title_at(e.button.x, e.button.y); if (t >= 0) { open = open == t ? -1 : t; hot = -1; return true; } if (open >= 0) { int i = item_at(e.button.x, e.button.y); if (i >= 0) activate(menus[open].items[i]); else open = -1; return true; } }
        return false;
    }
};

int main(int argc, char **argv) {
    Gtk::Main kit(argc, argv); // only so the reused Gtk::DrawingArea subclasses can be constructed
    CitySession session;
    const char *scen = std::getenv("PROTO_SCENARIO");
    if (!(scen && session.load_scenario(std::atoi(scen)))) session.new_city("New City", 1234);
    for (int i = 0; i < 40; ++i) session.tick();
    if (const char *sv = std::getenv("PROTO_SAVE")) session.save_city_as(sv);

    const int W = 1100, H = 740, MX = 92, MY = 56, MW = 990, MH = 584;
    MapX map; map.set_session(&session); map.sync();
    PalX pal; pal.set_selected(3);
    MiniX mini; mini.set_session(&session);
    DemX dem; dem.set_session(&session);
    // Parent them in an offscreen GTK window only so they get a real allocation + style context.
    Gtk::OffscreenWindow off; Gtk::Fixed fx; off.add(fx);
    mini.set_size_request(78, 65); dem.set_size_request(80, 84);
    fx.put(pal, 0, 0); fx.put(mini, 100, 0); fx.put(dem, 200, 0); fx.put(map, 0, 400); off.show_all();
    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
    const double ox = (map.pixel_width() - MW) / 2.0, oy = (map.pixel_height() - MH) / 2.0;
    mini.set_viewport_provider([&](double &x, double &y, double &w, double &h) { x = ox / map.pixel_width(); y = oy / map.pixel_height(); w = double(MW) / map.pixel_width(); h = double(MH) / map.pixel_height(); });
    mini.sync();
    bool paused = true; std::string status_tip = tool_by_index(3)->hint; int hover_tool = -1;
    MenuBar mb;
    auto I = [](std::string l, std::string a = "", SDL_Keycode k = 0, SDL_Keymod m = SDL_KMOD_CTRL) { Item it; it.label = l; it.accel = a; it.key = k; it.mod = k ? m : SDL_KMOD_NONE; return it; };
    auto C = [](std::string l, bool on) { Item it; it.label = l; it.kind = Item::Check; it.on = on; return it; };
    auto R = [](std::string l, std::string a, SDL_Keycode k, bool on) { Item it; it.label = l; it.accel = a; it.kind = Item::Radio; it.on = on; it.key = k; it.mod = SDL_KMOD_CTRL; return it; };
    Item S; S.kind = Item::Sep;
    mb.menus = {
        {"_System", {I("_New City", "Ctrl+N", SDLK_N), I("_Load City...", "Ctrl+O", SDLK_O), I("Open _Recent  \u25B8"), I("Save _City", "Ctrl+S", SDLK_S),
                     I("Save City _As...", "Shift+Ctrl+S", SDLK_S, SDL_Keymod(SDL_KMOD_CTRL | SDL_KMOD_SHIFT)), I("Play _Scenario\u2026"), I("_Rename City\u2026"), S, I("_Quit", "Ctrl+Q", SDLK_Q)}},
        {"_Options", {C("Auto _budget", true), C("Auto b_ulldoze", true), C("Enable _disasters", true), C("Auto-_goto", true), C("_Mute sound", false), S,
                      R("_Pause", "Ctrl+P", SDLK_P, true), R("Slo_w", "Ctrl+1", SDLK_1, false), R("M_edium", "Ctrl+2", SDLK_2, false), R("_Fast", "Ctrl+3", SDLK_3, false), S,
                      I("Zoom _in", "Ctrl+=", SDLK_EQUALS), I("Zoom _out", "Ctrl+-", SDLK_MINUS)}},
        {"_Disasters", {I("_Fire\u2026"), I("F_lood\u2026"), I("_Tornado\u2026"), I("_Earthquake\u2026"), I("M_onster\u2026"), I("_Meltdown\u2026")}},
        {"_Windows", {I("_Budget"), I("_Graphs"), I("_Evaluation"), S, I("_Power"), I("_Water"), I("P_ollution"), I("_Crime"), I("_Land value"), I("_Traffic")}},
        {"_Help", {I("_About Lunduke City")}}};
    for (int i = 6; i <= 9; ++i) mb.menus[1].items[i].act = [&paused, i] { paused = i == 6; };

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win; SDL_Renderer *ren;
    SDL_CreateWindowAndRenderer("Lunduke City (SDL3 prototype)", W, H, 0, &win, &ren);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H); // == CAIRO_FORMAT_ARGB32 on little-endian
    auto surf = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, W, H);
    auto Cr = Cairo::Context::create(surf);
    SDL_Log("SDL %d.%d.%d video=%s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver());

    // Scripted input for the headless screenshot: PROTO_SCRIPT=menu|tooltip|none
    const char *script = std::getenv("PROTO_SCRIPT"); std::string sc = script ? script : "none";
    auto key = [](SDL_Keycode k, SDL_Keymod m, bool down = true) { SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.key = k; e.key.mod = m; e.key.down = down; SDL_PushEvent(&e); };
    auto motion = [](float x, float y) { SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = x; e.motion.y = y; SDL_PushEvent(&e); };
    Uint64 hover_since = 0;
    for (int frame = 0; frame < 30; ++frame) {
        if (frame == 2 && sc == "menu") { key(SDLK_LALT, SDL_KMOD_ALT); key(SDLK_O, SDL_KMOD_ALT); for (int i = 0; i < 6; ++i) key(SDLK_DOWN, SDL_KMOD_ALT); }
        if (frame == 2 && sc == "tooltip") motion(8 + 4 + 36 + 18, 65 + 4 + 18);
        if (frame == 2 && sc == "accel") { key(SDLK_1, SDL_KMOD_CTRL); key(SDLK_O, SDL_KMOD_ALT); }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (mb.event(e)) continue;
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                double px = e.motion.x - 8 - 4, py = e.motion.y - 65 - 4; int idx = (px >= 0 && py >= 0 && px < 72) ? int(py / 36) * 2 + int(px / 36) : -1;
                if (idx >= kToolCount) idx = -1; if (idx != hover_tool) { hover_tool = idx; hover_since = SDL_GetTicks(); }
            }
        }
        cairo_t *c = Cr->cobj();
        rgb(c, 237, 236, 235); cairo_paint(c);
        mb.layout(c); mb.draw_bar(c, W);
        text(c, session.funds_text(), 10, 41); text(c, session.city_name(), W / 2.0, 41, 0, 0, 0, false, 1);
        text(c, std::string(paused ? "Paused" : "Slow") + "   " + session.date_text(), W - 12, 41, 0, 0, 0, false, 2);
        // reused City widgets, each into its own region of the same Cairo surface
        auto put = [&](auto &w, double x, double y, double ww, double hh) { Cr->save(); Cr->translate(x, y); Cr->rectangle(0, 0, ww, hh); Cr->clip(); w.on_draw(Cr); Cr->restore(); };
        put(pal, 8, 65, 80, 296); put(mini, 6, 362, 78, 65); put(dem, 6, 437, 80, 84);
        rgb(c, 140, 140, 140); cairo_rectangle(c, MX - .5, MY - .5, MW + 1, MH + 1); cairo_stroke(c);
        Cr->save(); Cr->rectangle(MX, MY, MW, MH); Cr->clip(); Cr->translate(MX - ox, MY - oy); map.on_draw(Cr); Cr->restore();
        text(c, "Welcome to Lunduke City. The clock is paused. Choose a speed under Options, start a city with", 6, 677);
        text(c, "System -> New City, or pick System -> Play Scenario. Point at a tool to see its name and cost.", 6, 696);
        text(c, hover_tool >= 0 ? tool_by_index(hover_tool)->hint : status_tip, 12, 724);
        if (hover_tool >= 0 && SDL_GetTicks() - hover_since > 0) { // tooltip (GTK waits ~500 ms; scripted shot shows it immediately)
            std::string h = tool_by_index(hover_tool)->hint; int tw = text(c, h, -1000, -1000);
            double tx = 2, ty = 113; rgb(c, 245, 245, 181); cairo_rectangle(c, tx, ty, tw + 10, 22); cairo_fill(c);
            rgb(c, 120, 120, 90); cairo_rectangle(c, tx + .5, ty + .5, tw + 9, 21); cairo_stroke(c); text(c, h, tx + 5, ty + 11);
        }
        mb.draw_drop(c); // popups drawn last, over everything (in-window; SDL_WINDOW_POPUP_MENU would be needed to overhang the window edge)
        surf->flush();
        SDL_UpdateTexture(tex, nullptr, surf->get_data(), surf->get_stride());
        SDL_RenderClear(ren); SDL_RenderTexture(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
        if (frame == 29) { SDL_Surface *s = SDL_RenderReadPixels(ren, nullptr); std::string out = std::string("sdl-") + sc + ".bmp"; SDL_SaveBMP(s, out.c_str()); SDL_DestroySurface(s); SDL_Log("wrote %s paused=%d", out.c_str(), paused); }
        SDL_Delay(10);
    }
    SDL_Quit(); return 0;
}
