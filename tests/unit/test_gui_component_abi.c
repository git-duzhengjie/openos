#include "unit_test.h"

#include <stddef.h>

#define GUI_USER_EVENT_TEXT_INPUT 3
#define GUI_USER_EVENT_TEXT_CHANGED 4
#define GUI_USER_EVENT_TEXT_SUBMIT 5
#define GUI_USER_EVENT_SELECTION_CHANGED 16

#define GUI_USER_TEXT_MAX 32
#define GUI_USER_WIDGET_TEXT_MAX 256

typedef struct gui_user_event_host_mirror {
    int owner_pid;
    int type;
    int window_id;
    int widget_id;
    int x;
    int y;
    int button;
    int keycode;
    int modifiers;
    char text[GUI_USER_TEXT_MAX];
    unsigned int text_len;
    unsigned int codepoint;
    int ime_state;
    int delta;
    int value;
    int value2;
} gui_user_event_host_mirror_t;

typedef struct gui_user_widget_request_host_mirror {
    int window_id;
    int widget_id;
    int x;
    int y;
    int w;
    int h;
    char text[GUI_USER_WIDGET_TEXT_MAX];
    int value;
    int min_value;
    int max_value;
    unsigned int flags;
    unsigned int color;
    unsigned int color2;
    unsigned int reserved[4];
} gui_user_widget_request_host_mirror_t;

UNIT_TEST_CASE(gui_event_text_input_abi_contract)
{
    gui_user_event_host_mirror_t event;

    ASSERT_EQ_INT(3, GUI_USER_EVENT_TEXT_INPUT);
    ASSERT_EQ_INT(4, GUI_USER_EVENT_TEXT_CHANGED);
    ASSERT_EQ_INT(5, GUI_USER_EVENT_TEXT_SUBMIT);
    ASSERT_EQ_INT(16, GUI_USER_EVENT_SELECTION_CHANGED);

    ASSERT_EQ_SIZE(32u, sizeof(event.text));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, owner_pid) == 0u);
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, type) > offsetof(gui_user_event_host_mirror_t, owner_pid));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, window_id) > offsetof(gui_user_event_host_mirror_t, type));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, widget_id) > offsetof(gui_user_event_host_mirror_t, window_id));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, text) > offsetof(gui_user_event_host_mirror_t, modifiers));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, text_len) > offsetof(gui_user_event_host_mirror_t, text));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, codepoint) > offsetof(gui_user_event_host_mirror_t, text_len));
    ASSERT_TRUE(offsetof(gui_user_event_host_mirror_t, ime_state) > offsetof(gui_user_event_host_mirror_t, codepoint));
}

UNIT_TEST_CASE(gui_component_request_abi_contract)
{
    gui_user_widget_request_host_mirror_t widget_request;

    ASSERT_EQ_SIZE(256u, sizeof(widget_request.text));
    ASSERT_TRUE(offsetof(gui_user_widget_request_host_mirror_t, window_id) == 0u);
    ASSERT_TRUE(offsetof(gui_user_widget_request_host_mirror_t, widget_id) > offsetof(gui_user_widget_request_host_mirror_t, window_id));
    ASSERT_TRUE(offsetof(gui_user_widget_request_host_mirror_t, x) > offsetof(gui_user_widget_request_host_mirror_t, widget_id));
    ASSERT_TRUE(offsetof(gui_user_widget_request_host_mirror_t, text) > offsetof(gui_user_widget_request_host_mirror_t, h));
}

int main(void)
{
    UNIT_TEST_RUN(gui_event_text_input_abi_contract);
    UNIT_TEST_RUN(gui_component_request_abi_contract);
    return unit_test_finish();
}
