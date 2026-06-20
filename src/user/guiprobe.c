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

    printf("guiprobe: window=%d label=%d button=%d set_text=%d\n", win, label, button, update);
    if (openos_gui_poll_event(&event) == 0) {
        printf("guiprobe: event type=%u window=%u widget=%u\n",
               event.type, event.window_id, event.widget_id);
    }

    return 0;
}
