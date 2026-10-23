// gtkmm-3 + Cairo shell for the same core (LCOS-native look: menus/dialogs from GTK).
#include "../core/game.hpp"
#include <gtkmm.h>
#include <cstdlib>
#include <vector>
class View : public Gtk::DrawingArea {
public:
    Game g; std::vector<uint32_t> buf; int frames = 0, max_frames = -1;
    View() {
        if (const char *fe = std::getenv("XPLAT_FRAMES")) max_frames = std::atoi(fe);
        add_events(Gdk::BUTTON_PRESS_MASK);
        Glib::signal_timeout().connect([this] { g.update(1 / 60.0, get_width(), get_height()); queue_draw(); return true; }, 16);
    }
    bool on_button_press_event(GdkEventButton *e) override { g.tap(e->x, e->y); return true; }
    bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override {
        int w = get_width(), h = get_height(); buf.assign(w * h, 0); Framebuffer fb{buf.data(), w, h, w}; g.render(fb);
        // core writes RGBA bytes; Cairo wants native-endian ARGB32 -> swap R/B
        for (auto &p : buf) p = (p & 0xff00ff00u) | ((p & 0xff) << 16) | ((p >> 16) & 0xff);
        auto surf = Cairo::ImageSurface::create(reinterpret_cast<unsigned char *>(buf.data()), Cairo::FORMAT_ARGB32, w, h, w * 4);
        cr->set_source(surf, 0, 0); cr->paint();
        if (max_frames > 0 && ++frames >= max_frames) { surf->write_to_png("frame-gtk.png"); Gtk::Main::quit(); }
        return true;
    }
};
int main(int argc, char **argv) {
    Gtk::Main kit(argc, argv);
    Gtk::Window win; win.set_title("XPlat Hello (gtkmm)"); win.set_default_size(640, 400);
    Gtk::Box box(Gtk::ORIENTATION_VERTICAL); Gtk::MenuBar bar; Gtk::MenuItem game("_Game", true); bar.append(game);
    View view; box.pack_start(bar, Gtk::PACK_SHRINK); box.pack_start(view); win.add(box); win.show_all();
    Gtk::Main::run(win); return 0;
}
