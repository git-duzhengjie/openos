#include "openos.h"

int main(void)
{
    int win;
    int label;
    int button;
    openos_gui_event_t event;

    win = openos_gui_create_window("User GUI Probe", 120, 120, 300, 140);
    if (win < 0) {
        printf("guiprobe: failed to create window\n");
        return 1;
    }

    label = openos_gui_add_label(win, 16, 32, 260, 20, "Hello from user mode GUI ABI");
    button = openos_gui_add_button(win, 16, 72, 120, 24, "OK");
    int update = openos_gui_set_text(win, label, "GUI syscall text update OK");
    unsigned int pixels[16] = {
        0xFFFF0000u, 0xFFFF8000u, 0xFFFFFF00u, 0xFF80FF00u,
        0xFF00FF00u, 0xFF00FF80u, 0xFF00FFFFu, 0xFF0080FFu,
        0xFF0000FFu, 0xFF8000FFu, 0xFFFF00FFu, 0xFFFF0080u,
        0xFFFFFFFFu, 0xFFCCCCCCu, 0xFF888888u, 0xFF000000u
    };
    int fill = openos_gui_fill_rect(win, 16, 112, 180, 24, 0xFFDDEEFFu);
    int draw = openos_gui_draw_text(win, 20, 118, "draw syscall OK", 0xFF003366u);
    int blit = openos_gui_blit_rgba32(win, 210, 112, 4, 4, pixels, 4);
    int scroll = openos_gui_scroll_rect(win, 16, 142, 16, 112, 180, 24);
    int present = openos_gui_present(win);

    printf("guiprobe: window=%d label=%d button=%d set_text=%d fill=%d draw=%d blit=%d scroll=%d present=%d\n", win, label, button, update, fill, draw, blit, scroll, present);
    if (openos_gui_poll_event(&event) == 0) {
        printf("guiprobe: event type=%u window=%u widget=%u\n",
               event.type, event.window_id, event.widget_id);
    }

    return 0;
}
