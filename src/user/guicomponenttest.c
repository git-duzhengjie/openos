#include "openos.h"

static int require_widget(const char *name, int id)
{
    if (id < 0) {
        printf("guicomponenttest: %s failed id=%d\n", name, id);
        return 1;
    }
    return 0;
}

int main(void)
{
    int win;
    int textbox;
    int button;
    int list;
    int dialog;
    openos_gui_event_t event;
    int failures = 0;

    win = openos_gui_create_window("GUI Component Smoke", 80, 80, 360, 260);
    if (win < 0) {
        printf("guicomponenttest: window failed id=%d\n", win);
        return 1;
    }

    textbox = openos_gui_add_textbox(win, 16, 24, 180, 24, "text-input");
    button = openos_gui_add_button(win, 210, 24, 96, 24, "Button");
    list = openos_gui_add_listview(win, 16, 64, 180, 72, "Alpha\nBeta\nGamma", 1, 0);
    dialog = openos_gui_add_dialog(win, 32, 148, 280, 84, "Dialog", "Smoke dialog", OPENOS_GUI_DIALOG_INFO | OPENOS_GUI_DIALOG_DEFAULT_OK);

    failures += require_widget("textbox", textbox);
    failures += require_widget("button", button);
    failures += require_widget("listview", list);
    failures += require_widget("dialog", dialog);

    if (sizeof(event.text) != 32u) {
        printf("guicomponenttest: event text ABI size mismatch=%u\n", (unsigned)sizeof(event.text));
        failures++;
    }

    event.type = OPENOS_GUI_EVENT_TEXT_INPUT;
    event.window_id = (unsigned int)win;
    event.widget_id = (unsigned int)textbox;
    event.text[0] = 'A';
    event.text[1] = 0;
    event.text_len = 1;
    event.codepoint = 65;
    event.ime_state = 0;
    if (event.type != OPENOS_GUI_EVENT_TEXT_INPUT || event.text_len != 1u || event.codepoint != 65u || event.text[0] != 'A') {
        printf("guicomponenttest: event ABI field roundtrip failed\n");
        failures++;
    }

    while (openos_gui_poll_event(&event) > 0) {
        printf("guicomponenttest: drained event type=%u widget=%u text_len=%u codepoint=%u\n",
               event.type, event.widget_id, event.text_len, event.codepoint);
    }

    if (failures != 0) {
        printf("guicomponenttest: FAIL failures=%d\n", failures);
        return 1;
    }

    printf("guicomponenttest: PASS window=%d textbox=%d button=%d list=%d dialog=%d\n",
           win, textbox, button, list, dialog);
    return 0;
}
