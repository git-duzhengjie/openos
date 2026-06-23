#include "openos.h"

int main(void)
{
    int win;
    int label;
    int button;
    int panel;
    int slider;
    int vscrollbar;
    int hscrollbar;
    int icon_button;
    int checkbox;
    int radio_a;
    int radio_b;
    int select;
    int combo;
    int listview;
    int checklist;
    int tableview;
    int treeview;
    int contextmenu;
    int contextmenu_show;
    int contextmenu_disabled_set;
    int contextmenu_set;
    int contextmenu_value;
    int select_set;
    int select_value;
    int combo_items_set;
    int combo_set;
    int combo_value;
    int listview_set;
    int listview_value;
    int listview_items_set;
    int checklist_set;
    int checklist_value;
    int tableview_rows_set;
    int tableview_set;
    int tableview_value;
    int treeview_nodes_set;
    int treeview_set;
    int treeview_value;
    int text_editor;
    int log_view;
    int scrollview;
    int checkbox_set;
    int checkbox_value;
    int checkbox_enabled_set;
    int checkbox_enabled;
    int radio_set;
    int radio_a_value;
    int radio_b_value;
    int slider_set;
    int slider_value;
    int scrollbar_set;
    int scrollbar_step_set;
    int scrollbar_value;
    int scrollbar_step;
    int scrollview_offset_set;
    int scrollview_size_set;
    int scrollview_x = -1;
    int scrollview_y = -1;
    int scrollview_w = -1;
    int scrollview_h = -1;
    int label_width = 0;
    int label_height = 0;
    int label_measure;
    openos_gui_event_t event;

    win = openos_gui_create_window("User GUI Probe", 120, 120, 440, 460);
    if (win < 0) {
        printf("guiprobe: failed to create window\n");
        return 1;
    }

    label = openos_gui_add_label(win, 16, 32, 260, 20, "Hello from user mode GUI ABI");
    button = openos_gui_add_button(win, 16, 72, 120, 24, "OK");
    panel = openos_gui_add_panel(win, 150, 66, 150, 42, 0xFFEAF4FFu);
    slider = openos_gui_add_slider(win, 16, 156, 180, 22, 0, 100, 40, 5);
    vscrollbar = openos_gui_add_scrollbar(win, 382, 32, 18, 120, 0, 100, 25, 10);
    hscrollbar = openos_gui_add_scrollbar(win, 210, 188, 120, 18, 0, 100, 30, 10);
    icon_button = openos_gui_add_icon_button(win, 220, 138, 72, 64, "Files", OPENOS_GUI_ICON_FOLDER);
    checkbox = openos_gui_add_checkbox(win, 16, 128, 160, 22, "Enable sync", 1);
    radio_a = openos_gui_add_radiobutton(win, 180, 128, 90, 22, "Fast", 1, 1);
    radio_b = openos_gui_add_radiobutton(win, 270, 128, 90, 22, "Safe", 1, 0);
    select = openos_gui_add_select(win, 16, 102, 120, 22, "Red\nGreen\nBlue", 1);
    combo = openos_gui_add_combobox(win, 150, 102, 120, 22, "One\nTwo\nThree", 0);
    listview = openos_gui_add_listview(win, 16, 240, 140, 52, "Alpha\r\nBeta\r\nGamma\r\nDelta", 1, 0);
    checklist = openos_gui_add_listview(win, 170, 240, 150, 52, "One\nTwo\nThree\nFour", 0, OPENOS_GUI_LISTVIEW_MULTI_SELECT | OPENOS_GUI_LISTVIEW_SHOW_CHECKBOXES);
    tableview = openos_gui_add_tableview(win, 16, 300, 400, 64, "Name:150,Status:120,Cost:70", "Kernel,Ready,1\r\nGUI,Active,2\r\nApps,Pending,3", 1, OPENOS_GUI_TABLEVIEW_SHOW_HEADER | OPENOS_GUI_TABLEVIEW_GRID_LINES | OPENOS_GUI_TABLEVIEW_SORTABLE);
    treeview = openos_gui_add_treeview(win, 16, 370, 210, 64, "-Root\r\n>+Apps\r\n>>L:Editor\r\n>>L:Terminal\r\n>-System\r\n>>L:Kernel", 0, OPENOS_GUI_TREEVIEW_SHOW_LINES | OPENOS_GUI_TREEVIEW_SHOW_ICONS);
    contextmenu = openos_gui_add_contextmenu(win, 238, 370, 160, 84, "Open\tEnter|Rename\tF2|Delete\tDel", 0, (1u << 1));
    contextmenu_show = openos_gui_show_contextmenu(win, contextmenu, 238, 370);
    contextmenu_disabled_set = openos_gui_set_contextmenu_disabled(win, contextmenu, (1u << 1));
    contextmenu_set = openos_gui_set_contextmenu_index(win, contextmenu, 2);
    (void)openos_gui_add_info_dialog(win, 192, 118, 210, 108, "Info", "信息弹窗：用于普通提示。");
    (void)openos_gui_add_warning_dialog(win, 414, 118, 210, 108, "Warning", "警告弹窗：用于风险提示。");
    (void)openos_gui_add_error_dialog(win, 192, 232, 210, 108, "Error", "错误弹窗：用于失败状态。");
    (void)openos_gui_add_dialog(win, 414, 232, 210, 108, "Confirm", "Confirm dialog: default OK, Esc cancels.",
                                OPENOS_GUI_DIALOG_CONFIRM | OPENOS_GUI_DIALOG_CANCEL | OPENOS_GUI_DIALOG_MODAL | OPENOS_GUI_DIALOG_DEFAULT_OK);
    if (openos_gui_get_contextmenu_index(win, contextmenu, &contextmenu_value) < 0) {
        contextmenu_value = -1;
    }
    text_editor = openos_gui_add_text_editor(win, 16, 190, 180, 48, "edit line 1\nedit line 2");
    log_view = openos_gui_add_log_view(win, 210, 210, 180, 48, "log: boot ok\nlog: gui ok");
    scrollview = openos_gui_add_scrollview(win, 310, 66, 76, 64, 180, 150);
    scrollview_offset_set = openos_gui_set_scrollview_offset(win, scrollview, 24, 36);
    scrollview_size_set = openos_gui_set_scrollview_content_size(win, scrollview, 200, 160);
    if (openos_gui_get_scrollview_offset(win, scrollview, &scrollview_x, &scrollview_y) < 0) {
        scrollview_x = -1;
        scrollview_y = -1;
    }
    if (openos_gui_get_scrollview_content_size(win, scrollview, &scrollview_w, &scrollview_h) < 0) {
        scrollview_w = -1;
        scrollview_h = -1;
    }
    slider_set = openos_gui_set_slider_value(win, slider, 55);
    if (openos_gui_get_slider_value(win, slider, &slider_value) < 0) {
        slider_value = -1;
    }
    checkbox_set = openos_gui_set_checkbox_checked(win, checkbox, 0);
    if (openos_gui_get_checkbox_checked(win, checkbox, &checkbox_value) < 0) {
        checkbox_value = -1;
    }
    checkbox_enabled_set = openos_gui_set_widget_enabled(win, checkbox, 0);
    if (openos_gui_get_widget_enabled(win, checkbox, &checkbox_enabled) < 0) {
        checkbox_enabled = -1;
    }
    radio_set = openos_gui_set_radiobutton_checked(win, radio_b, 1);
    if (openos_gui_get_radiobutton_checked(win, radio_a, &radio_a_value) < 0) {
        radio_a_value = -1;
    }
    if (openos_gui_get_radiobutton_checked(win, radio_b, &radio_b_value) < 0) {
        radio_b_value = -1;
    }
    select_set = openos_gui_set_select_index(win, select, 2);
    if (openos_gui_get_select_index(win, select, &select_value) < 0) {
        select_value = -1;
    }
    combo_items_set = openos_gui_set_select_items(win, combo, "Cyan\nMagenta\nYellow");
    combo_set = openos_gui_set_select_index(win, combo, 1);
    if (openos_gui_get_select_index(win, combo, &combo_value) < 0) {
        combo_value = -1;
    }
    listview_items_set = openos_gui_set_listview_items(win, listview, "North\r\nEast\r\nSouth\r\nWest");
    listview_set = openos_gui_set_listview_index(win, listview, 3);
    if (openos_gui_get_listview_index(win, listview, &listview_value) < 0) {
        listview_value = -1;
    }
    checklist_set = openos_gui_set_listview_index(win, checklist, 2);
    if (openos_gui_get_listview_index(win, checklist, &checklist_value) < 0) {
        checklist_value = -1;
    }
    tableview_rows_set = openos_gui_set_tableview_rows(win, tableview, "Kernel,Ready,1\r\nGUI,Active,2\r\nApps,Done,3\r\nNet,Queued,4");
    tableview_set = openos_gui_set_tableview_row(win, tableview, 2);
    if (openos_gui_get_tableview_row(win, tableview, &tableview_value) < 0) {
        tableview_value = -1;
    }
    treeview_nodes_set = openos_gui_set_treeview_nodes(win, treeview, "-Root\r\n>-Apps\r\n>>L:Editor\r\n>>L:Terminal\r\n>-System\r\n>>L:Kernel\r\n>>L:Drivers");
    treeview_set = openos_gui_set_treeview_node(win, treeview, 2);
    if (openos_gui_get_treeview_node(win, treeview, &treeview_value) < 0) {
        treeview_value = -1;
    }
    scrollbar_set = openos_gui_set_scrollbar_value(win, vscrollbar, 45);
    scrollbar_step_set = openos_gui_set_scrollbar_step(win, vscrollbar, 5);
    if (openos_gui_get_scrollbar_value(win, vscrollbar, &scrollbar_value) < 0) {
        scrollbar_value = -1;
    }
    if (openos_gui_get_scrollbar_step(win, vscrollbar, &scrollbar_step) < 0) {
        scrollbar_step = -1;
    }
    int update = openos_gui_set_text(win, label, "GUI syscall text update OK");
    label_measure = openos_gui_measure_label(win, label, 160, &label_width, &label_height);
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

    printf("guiprobe: window=%d label=%d button=%d panel=%d slider=%d vscroll=%d hscroll=%d icon=%d checkbox=%d radio_a=%d radio_b=%d select=%d combo=%d listview=%d checklist=%d tableview=%d treeview=%d contextmenu=%d contextmenu_show=%d contextmenu_disabled_set=%d contextmenu_set=%d contextmenu_value=%d editor=%d log=%d scrollview=%d set_text=%d label_measure=%d label_size=%dx%d slider_set=%d slider_value=%d checkbox_set=%d checkbox_value=%d checkbox_enabled_set=%d checkbox_enabled=%d radio_set=%d radio_a_value=%d radio_b_value=%d select_set=%d select_value=%d combo_items_set=%d combo_set=%d combo_value=%d listview_items_set=%d listview_set=%d listview_value=%d checklist_set=%d checklist_value=%d tableview_rows_set=%d tableview_set=%d tableview_value=%d treeview_nodes_set=%d treeview_set=%d treeview_value=%d scrollbar_set=%d scrollbar_step_set=%d scrollbar_value=%d scrollbar_step=%d scrollview_offset_set=%d scrollview_size_set=%d scrollview_offset=%d,%d scrollview_size=%dx%d fill=%d draw=%d blit=%d scroll=%d present=%d\n", win, label, button, panel, slider, vscrollbar, hscrollbar, icon_button, checkbox, radio_a, radio_b, select, combo, listview, checklist, tableview, treeview, contextmenu, contextmenu_show, contextmenu_disabled_set, contextmenu_set, contextmenu_value, text_editor, log_view, scrollview, update, label_measure, label_width, label_height, slider_set, slider_value, checkbox_set, checkbox_value, checkbox_enabled_set, checkbox_enabled, radio_set, radio_a_value, radio_b_value, select_set, select_value, combo_items_set, combo_set, combo_value, listview_items_set, listview_set, listview_value, checklist_set, checklist_value, tableview_rows_set, tableview_set, tableview_value, treeview_nodes_set, treeview_set, treeview_value, scrollbar_set, scrollbar_step_set, scrollbar_value, scrollbar_step, scrollview_offset_set, scrollview_size_set, scrollview_x, scrollview_y, scrollview_w, scrollview_h, fill, draw, blit, scroll, present);
    if (openos_gui_poll_event(&event) > 0) {
        printf("guiprobe: event type=%u window=%u widget=%u x=%d\n",
               event.type, event.window_id, event.widget_id, event.x);
    }

    return 0;
}
