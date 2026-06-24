#include "openos.h"

#define NOTE_FILE "/home/stickynotes.txt"
#define LEGACY_NOTE_FILE "/home/stickynote.txt"
#define NOTE_MAX_COUNT 5
#define NOTE_MAX_TEXT 255
#define NOTE_ITEM_TEXT 96

typedef struct note_item {
    char text[NOTE_MAX_TEXT + 1];
} note_item_t;

typedef struct note_store {
    note_item_t items[NOTE_MAX_COUNT];
    int count;
} note_store_t;

static int note_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void note_trim(char *text) {
    int len;
    int start = 0;
    int i;

    if (!text) {
        return;
    }

    len = openos_strlen(text);
    while (start < len && note_is_space(text[start])) {
        start++;
    }

    if (start > 0) {
        for (i = 0; i <= len - start; i++) {
            text[i] = text[i + start];
        }
        len = openos_strlen(text);
    }

    while (len > 0 && note_is_space(text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

static void note_normalize_line(char *text) {
    int i;

    if (!text) {
        return;
    }

    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n' || text[i] == '\r') {
            text[i] = ' ';
        }
    }
    note_trim(text);
}

static void note_copy_line(char *dst, unsigned int dst_size, const char *src) {
    unsigned int i = 0;

    if (!dst || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (!src) {
        return;
    }

    while (src[i] != '\0' && src[i] != '\n' && src[i] != '\r' && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    note_trim(dst);
}

static int note_store_add(note_store_t *store, const char *text) {
    if (!store || store->count >= NOTE_MAX_COUNT || !text || text[0] == '\0') {
        return -1;
    }

    openos_strncpy(store->items[store->count].text, text, NOTE_MAX_TEXT);
    store->items[store->count].text[NOTE_MAX_TEXT] = '\0';
    note_normalize_line(store->items[store->count].text);
    if (store->items[store->count].text[0] == '\0') {
        return -1;
    }
    store->count++;
    return 0;
}

static int note_store_save(const note_store_t *store) {
    int fd;
    int i;

    if (!store) {
        return -1;
    }

    fd = openos_open(NOTE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }

    for (i = 0; i < store->count; i++) {
        int len = openos_strlen(store->items[i].text);
        if (len > 0 && openos_write(fd, store->items[i].text, len) < 0) {
            openos_close(fd);
            return -1;
        }
        if (openos_write(fd, "\n", 1) < 0) {
            openos_close(fd);
            return -1;
        }
    }

    openos_close(fd);
    openos_gui_present(0);
    return 0;
}

static void note_store_parse(note_store_t *store, char *content) {
    char line[NOTE_MAX_TEXT + 1];
    int pos = 0;
    int i;

    if (!store || !content) {
        return;
    }

    for (i = 0; content[i] != '\0' && store->count < NOTE_MAX_COUNT; i++) {
        char ch = content[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[pos] = '\0';
            note_trim(line);
            if (line[0] != '\0') {
                note_store_add(store, line);
            }
            pos = 0;
            continue;
        }
        if (pos < NOTE_MAX_TEXT) {
            line[pos++] = ch;
        }
    }

    if (pos > 0 && store->count < NOTE_MAX_COUNT) {
        line[pos] = '\0';
        note_trim(line);
        if (line[0] != '\0') {
            note_store_add(store, line);
        }
    }
}

static void note_store_load(note_store_t *store) {
    int fd;
    int n;
    char content[(NOTE_MAX_TEXT + 2) * NOTE_MAX_COUNT];

    if (!store) {
        return;
    }

    store->count = 0;
    fd = openos_open(NOTE_FILE, O_RDONLY, 0);
    if (fd >= 0) {
        n = openos_read(fd, content, sizeof(content) - 1);
        openos_close(fd);
        if (n > 0) {
            content[n] = '\0';
            note_store_parse(store, content);
        }
        return;
    }

    fd = openos_open(LEGACY_NOTE_FILE, O_RDONLY, 0);
    if (fd >= 0) {
        n = openos_read(fd, content, NOTE_MAX_TEXT);
        openos_close(fd);
        if (n > 0) {
            content[n] = '\0';
            note_copy_line(store->items[0].text, sizeof(store->items[0].text), content);
            if (store->items[0].text[0] != '\0') {
                store->count = 1;
                note_store_save(store);
                return;
            }
        }
    }

    note_store_add(store, "欢迎使用桌面便签，点击右上角添加便签。");
    note_store_save(store);
}

static void note_summary(const char *text, char *buffer, unsigned int size) {
    unsigned int i;
    unsigned int max_copy;

    if (!buffer || size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!text || text[0] == '\0') {
        openos_strncpy(buffer, "空便签", size - 1);
        buffer[size - 1] = '\0';
        return;
    }

    max_copy = size > 8 ? size - 8 : size - 1;
    for (i = 0; text[i] != '\0' && i < max_copy; i++) {
        buffer[i] = (text[i] == '\n' || text[i] == '\r') ? ' ' : text[i];
    }
    if (text[i] != '\0' && i + 4 < size) {
        buffer[i++] = '.';
        buffer[i++] = '.';
        buffer[i++] = '.';
    }
    buffer[i] = '\0';
}

typedef struct note_list_ui {
    int win;
    int count_label;
    int empty_label;
    int add_button;
    int item_buttons[NOTE_MAX_COUNT];
} note_list_ui_t;

static void note_list_refresh(note_list_ui_t *ui, const note_store_t *store) {
    int i;
    char title[64];

    if (!ui || !store) {
        return;
    }

    snprintf(title, sizeof(title), "共 %d 条便签", store->count);
    openos_gui_set_text(ui->win, ui->count_label, title);

    if (store->count == 0) {
        openos_gui_set_text(ui->win, ui->empty_label, "还没有便签，点击右上角添加便签。");
    } else {
        openos_gui_set_text(ui->win, ui->empty_label, "点击下方便签可编辑，最多保存 5 条。");
    }

    for (i = 0; i < NOTE_MAX_COUNT; i++) {
        char item[NOTE_ITEM_TEXT];
        if (i < store->count) {
            char summary[72];
            note_summary(store->items[i].text, summary, sizeof(summary));
            snprintf(item, sizeof(item), "%d. %s", i + 1, summary);
            openos_gui_set_text(ui->win, ui->item_buttons[i], item);
            openos_gui_set_widget_enabled(ui->win, ui->item_buttons[i], 1);
        } else {
            snprintf(item, sizeof(item), "%d. --", i + 1);
            openos_gui_set_text(ui->win, ui->item_buttons[i], item);
            openos_gui_set_widget_enabled(ui->win, ui->item_buttons[i], 0);
        }
    }

    openos_gui_set_widget_enabled(ui->win, ui->add_button, store->count < NOTE_MAX_COUNT);
}

static int note_list_create(note_list_ui_t *ui, const note_store_t *store) {
    int i;

    if (!ui || !store) {
        return -1;
    }

    ui->win = openos_gui_create_window("桌面便签", 150, 110, 520, 390);
    if (ui->win < 0) {
        openos_printf("无法创建便签窗口\n");
        return -1;
    }

    openos_gui_add_label(ui->win, 18, 16, 180, 24, "桌面便签");
    ui->add_button = openos_gui_add_button(ui->win, 360, 44, 142, 34, "+ 添加便签");
    ui->count_label = openos_gui_add_label(ui->win, 18, 46, 160, 22, "共 0 条便签");
    ui->empty_label = openos_gui_add_label(ui->win, 18, 76, 380, 22, "");

    for (i = 0; i < NOTE_MAX_COUNT; i++) {
        ui->item_buttons[i] = openos_gui_add_button(ui->win, 18, 112 + i * 42, 484, 34, "");
    }

    if (ui->add_button < 0 || ui->count_label < 0 || ui->empty_label < 0) {
        openos_gui_destroy_window(ui->win);
        ui->win = -1;
        return -1;
    }
    for (i = 0; i < NOTE_MAX_COUNT; i++) {
        if (ui->item_buttons[i] < 0) {
            openos_gui_destroy_window(ui->win);
            ui->win = -1;
            return -1;
        }
    }

    note_list_refresh(ui, store);
    return 0;
}

static void note_list_destroy(note_list_ui_t *ui) {
    if (ui && ui->win >= 0) {
        openos_gui_destroy_window(ui->win);
        ui->win = -1;
    }
}

static int note_open_editor(note_store_t *store, int index) {
    int is_new = index < 0;
    int win;
    int text_id;
    int save_id;
    int delete_id;
    int cancel_id;
    int running = 1;
    char initial[NOTE_MAX_TEXT + 1];
    char current[NOTE_MAX_TEXT + 1];

    if (!store) {
        return 0;
    }
    if (is_new && store->count >= NOTE_MAX_COUNT) {
        openos_printf("便签数量已满\n");
        return 0;
    }
    if (!is_new && (index < 0 || index >= store->count)) {
        return 0;
    }

    initial[0] = '\0';
    if (!is_new) {
        openos_strncpy(initial, store->items[index].text, NOTE_MAX_TEXT);
        initial[NOTE_MAX_TEXT] = '\0';
    }

    win = openos_gui_create_window(is_new ? "添加便签" : "编辑便签", 210, 150, 480, 330);
    if (win < 0) {
        openos_printf("无法创建编辑窗口\n");
        return 0;
    }

    openos_gui_add_label(win, 18, 16, 260, 22, is_new ? "新增便签" : "编辑便签");
    openos_gui_add_label(win, 18, 42, 360, 20, "最多 255 字符，保存后回到列表。");
    text_id = openos_gui_add_textbox(win, 18, 70, 444, 170, initial);
    save_id = openos_gui_add_button(win, 18, 262, 100, 34, "保存");
    delete_id = openos_gui_add_button(win, 132, 262, 100, 34, is_new ? "清空" : "删除");
    cancel_id = openos_gui_add_button(win, 362, 262, 100, 34, "返回");

    if (text_id < 0 || save_id < 0 || delete_id < 0 || cancel_id < 0) {
        openos_gui_destroy_window(win);
        return 0;
    }

    while (running) {
        openos_gui_event_t ev;
        int has_event = openos_gui_poll_event(&ev);
        if (!has_event) {
            openos_sleep(1);
            continue;
        }
        if (ev.window_id != win) {
            continue;
        }
        if (ev.type != OPENOS_GUI_EVENT_BUTTON_CLICK) {
            continue;
        }

        if (ev.widget_id == save_id) {
            if (openos_gui_get_text(win, text_id, current, sizeof(current)) < 0) {
                openos_printf("读取便签内容失败\n");
                continue;
            }
            note_normalize_line(current);
            if (current[0] == '\0') {
                openos_printf("便签内容不能为空\n");
                continue;
            }
            if (is_new) {
                if (note_store_add(store, current) == 0 && note_store_save(store) == 0) {
                    running = 0;
                } else {
                    openos_printf("保存便签失败\n");
                }
            } else {
                openos_strncpy(store->items[index].text, current, NOTE_MAX_TEXT);
                store->items[index].text[NOTE_MAX_TEXT] = '\0';
                if (note_store_save(store) == 0) {
                    running = 0;
                } else {
                    openos_printf("保存便签失败\n");
                }
            }
        } else if (ev.widget_id == delete_id) {
            if (is_new) {
                openos_gui_set_text(win, text_id, "");
            } else {
                int i;
                for (i = index; i + 1 < store->count; i++) {
                    openos_strncpy(store->items[i].text, store->items[i + 1].text, NOTE_MAX_TEXT);
                    store->items[i].text[NOTE_MAX_TEXT] = '\0';
                }
                store->count--;
                note_store_save(store);
                running = 0;
            }
        } else if (ev.widget_id == cancel_id) {
            running = 0;
        }
    }

    openos_gui_destroy_window(win);
    return 1;
}

int main(void) {
    note_store_t store;
    note_list_ui_t ui;
    int i;
    int running = 1;

    note_store_load(&store);
    ui.win = -1;

    if (note_list_create(&ui, &store) < 0) {
        return 1;
    }

    while (running) {
        openos_gui_event_t ev;
        int has_event = openos_gui_poll_event(&ev);
        if (!has_event) {
            openos_sleep(1);
            continue;
        }
        if (ev.window_id != ui.win) {
            continue;
        }
        if (ev.type != OPENOS_GUI_EVENT_BUTTON_CLICK) {
            continue;
        }

        if (ev.widget_id == ui.add_button) {
            note_list_destroy(&ui);
            note_open_editor(&store, -1);
            if (note_list_create(&ui, &store) < 0) {
                return 1;
            }
        } else {
            for (i = 0; i < NOTE_MAX_COUNT; i++) {
                if (ev.widget_id == ui.item_buttons[i] && i < store.count) {
                    note_list_destroy(&ui);
                    note_open_editor(&store, i);
                    if (note_list_create(&ui, &store) < 0) {
                        return 1;
                    }
                    break;
                }
            }
        }
    }

    note_list_destroy(&ui);
    return 0;
}
