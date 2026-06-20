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
    int fill = openos_gui_fill_rect(win, 16, 112, 180, 24, 0xFFDDEEFFu);
    int draw = openos_gui_draw_text(win, 20, 118, "draw syscall OK", 0xFF003366u);

    printf("guiprobe: window=%d label=%d button=%d set_text=%d fill=%d draw=%d\n", win, label, button, update, fill, draw);
    if (openos_gui_poll_event(&event) == 0) {
        printf("guiprobe: event type=%u window=%u widget=%u\n",
               event.type, event.window_id, event.widget_id);
    }

    return 0;
}
