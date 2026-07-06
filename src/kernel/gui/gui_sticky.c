/* gui_sticky.c - Sticky Note manager (kernel-side GUI)
 * Extracted from gui.c (Step 2a of GUI modularization).
 * Depends on public GUI framework API (gui.h) and desktop note store
 * type shared via gui_internal.h.
 */
#include "gui.h"
#include "gui_internal.h"
#include "core/fs/vfs.h"
#include "i18n.h"
#include "string.h"
#include "serial.h"

/* === Sticky Note manager (kernel-side GUI) === */

#define STICKY_MAX_NOTES 16
#define STICKY_NOTE_LEN  256
#define STICKY_FILE_PATH "/stickynotes.txt"

static char g_sticky_notes[STICKY_MAX_NOTES][STICKY_NOTE_LEN];
static int  g_sticky_count = 0;
static int  g_sticky_view = 0;      /* 0 = list, 1 = edit */
static int  g_sticky_edit_idx = -1; /* -1 = new note, >=0 = editing existing */
static gui_widget_t *g_sticky_listview = 0;
static gui_widget_t *g_sticky_editbox = 0;
static gui_window_t *g_stickynote_win = 0;

/* Export in-memory sticky notes into a desktop note store (read-only VFS workaround). */
int sticky_export_to_desktop_store(gui_desktop_note_store_t *store) {
    int i;
    int len;
    if (!store) return 0;
    store->count = 0;
    for (i = 0; i < g_sticky_count && i < STICKY_MAX_NOTES; i++) {
        const char *s = g_sticky_notes[i];
        len = 0;
        while (s[len]) len++;
        if (len > 0) gui_desktop_note_add(store, s, len);
    }
    return store->count;
}

static void sticky_trim_newline(char *s) {
    int n = 0;
    if (!s) return;
    while (s[n]) n++;
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) { s[--n] = '\0'; }
}

/* Load notes from disk into memory store.
 * NOTE: x86_64 VFS is a read-only initrd (no write/create). Notes therefore
 * live in memory only. We must load from disk AT MOST ONCE, otherwise every
 * re-open of the sticky window would wipe the in-memory notes with the empty
 * (unwritable) file. */
static int g_sticky_loaded = 0;
static void sticky_load(void) {
    int fd;
    static char buf[STICKY_MAX_NOTES * STICKY_NOTE_LEN];
    int total = 0;
    int i;
    int line_len;
    const char *p;

    if (g_sticky_loaded) return;   /* already initialised: keep memory store */
    g_sticky_loaded = 1;

    g_sticky_count = 0;
    for (i = 0; i < STICKY_MAX_NOTES; i++) g_sticky_notes[i][0] = '\0';

    fd = vfs_open(STICKY_FILE_PATH, O_RDONLY, 0);
    if (fd < 0) return;
    total = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (total <= 0) return;
    buf[total] = '\0';

    p = buf;
    while (*p && g_sticky_count < STICKY_MAX_NOTES) {
        line_len = 0;
        while (p[line_len] && p[line_len] != '\n') line_len++;
        if (line_len > 0) {
            int cp = line_len;
            if (cp > STICKY_NOTE_LEN - 1) cp = STICKY_NOTE_LEN - 1;
            for (i = 0; i < cp; i++) g_sticky_notes[g_sticky_count][i] = p[i];
            g_sticky_notes[g_sticky_count][cp] = '\0';
            sticky_trim_newline(g_sticky_notes[g_sticky_count]);
            if (g_sticky_notes[g_sticky_count][0]) g_sticky_count++;
        }
        p += line_len;
        if (*p == '\n') p++;
    }
}

/* Persist memory store to disk */
static void sticky_save(void) {
    int fd;
    int i, j;
    static char buf[STICKY_MAX_NOTES * STICKY_NOTE_LEN];
    int total = 0;

    for (i = 0; i < g_sticky_count; i++) {
        for (j = 0; g_sticky_notes[i][j] && total < (int)sizeof(buf) - 2; j++)
            buf[total++] = g_sticky_notes[i][j];
        if (total < (int)sizeof(buf) - 1) buf[total++] = '\n';
    }

    {
        char dbg[64]; int n=0; const char *pf="[STICKY] save count=";
        while(pf[n]){dbg[n]=pf[n];n++;} dbg[n++]='0'+(g_sticky_count%10);
        dbg[n++]=' ';dbg[n++]='b';dbg[n++]='=';dbg[n++]='0'+((total/100)%10);
        dbg[n++]='0'+((total/10)%10);dbg[n++]='0'+(total%10);dbg[n++]='\n';dbg[n]='\0';
        serial_write(dbg);
    }
    fd = vfs_open(STICKY_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { serial_write("[STICKY] save OPEN FAIL\n"); return; }
    if (total > 0) vfs_write(fd, buf, total);
    vfs_close(fd);
    serial_write("[STICKY] save OK\n");
}

/* Build the pipe-free list string for the listview ('\n' separated) */
static void sticky_build_list_items(char *out, int cap) {
    int i, o = 0;
    int j;
    out[0] = '\0';
    for (i = 0; i < g_sticky_count && o < cap - 2; i++) {
        for (j = 0; g_sticky_notes[i][j] && o < cap - 2; j++) {
            char c = g_sticky_notes[i][j];
            if (c == '\n' || c == '\r') c = ' ';
            out[o++] = c;
        }
        if (i < g_sticky_count - 1 && o < cap - 1) out[o++] = '\n';
    }
    out[o] = '\0';
    serial_write("[STICKY] listitems='");
    serial_write(out);
    serial_write("'\n");
}

void gui_stickynote_open(void);

/* ---- list view callbacks ---- */
static void sticky_cb_add(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    g_sticky_view = 1;
    g_sticky_edit_idx = -1;
    gui_stickynote_open();
}

static void sticky_cb_edit(gui_widget_t *w, void *ud) {
    int sel = -1;
    (void)ud; (void)w;
    if (!g_sticky_listview) return;
    gui_listview_get_selected(g_sticky_listview, &sel);
    if (sel < 0 || sel >= g_sticky_count) { gui_notify("Select a note first"); return; }
    g_sticky_view = 1;
    g_sticky_edit_idx = sel;
    gui_stickynote_open();
}

static void sticky_cb_delete(gui_widget_t *w, void *ud) {
    int sel = -1, i;
    (void)w; (void)ud;
    if (!g_sticky_listview) return;
    gui_listview_get_selected(g_sticky_listview, &sel);
    if (sel < 0 || sel >= g_sticky_count) { gui_notify("Select a note first"); return; }
    for (i = sel; i < g_sticky_count - 1; i++) {
        int k;
        for (k = 0; k < STICKY_NOTE_LEN; k++) g_sticky_notes[i][k] = g_sticky_notes[i + 1][k];
    }
    g_sticky_count--;
    g_sticky_notes[g_sticky_count][0] = '\0';
    sticky_save();
    g_sticky_view = 0;
    gui_stickynote_open();
}

/* ---- edit view callbacks ---- */
static void sticky_cb_save(gui_widget_t *w, void *ud) {
    const char *txt;
    int i;
    (void)w; (void)ud;
    if (!g_sticky_editbox) return;
    txt = gui_widget_get_text(g_sticky_editbox);
    serial_write("[STICKY] save txt='");
    serial_write(txt ? txt : "(null)");
    serial_write("'\n");
    if (!txt || !txt[0]) { gui_notify("Note is empty"); return; }

    if (g_sticky_edit_idx >= 0 && g_sticky_edit_idx < g_sticky_count) {
        for (i = 0; i < STICKY_NOTE_LEN - 1 && txt[i]; i++)
            g_sticky_notes[g_sticky_edit_idx][i] = txt[i];
        g_sticky_notes[g_sticky_edit_idx][i] = '\0';
    } else if (g_sticky_count < STICKY_MAX_NOTES) {
        for (i = 0; i < STICKY_NOTE_LEN - 1 && txt[i]; i++)
            g_sticky_notes[g_sticky_count][i] = txt[i];
        g_sticky_notes[g_sticky_count][i] = '\0';
        g_sticky_count++;
    } else {
        gui_notify("Note list is full");
        return;
    }
    sticky_save();
    g_sticky_view = 0;
    g_sticky_edit_idx = -1;
    gui_stickynote_open();
}

static void sticky_cb_cancel(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    g_sticky_view = 0;
    g_sticky_edit_idx = -1;
    gui_stickynote_open();
}

static void stickynote_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_stickynote_win = 0;
    g_sticky_listview = 0;
    g_sticky_editbox = 0;
}

void gui_stickynote_open(void) {
    int win_w = 340;
    int win_h = 300;
    static int loaded = 0;

    if (!loaded) { sticky_load(); loaded = 1; }

    if (g_stickynote_win) {
        gui_window_set_on_close(g_stickynote_win, 0, 0);
        gui_destroy_window(g_stickynote_win);
        g_stickynote_win = 0;
    }
    g_sticky_listview = 0;
    g_sticky_editbox = 0;

    if (g_sticky_view == 0) {
        /* ---------- List view ---------- */
        static char items[STICKY_MAX_NOTES * STICKY_NOTE_LEN];
        g_stickynote_win = gui_create_window(200, 130, win_w, win_h, "Sticky Notes");
        if (!g_stickynote_win) return;

        gui_add_label(g_stickynote_win, 10, 8, 120, 20, "My Notes");
        gui_add_button(g_stickynote_win, win_w - 70, 6, 60, 24, "+ New", sticky_cb_add, 0);

        sticky_build_list_items(items, sizeof(items));
        g_sticky_listview = gui_add_listview(g_stickynote_win, 10, 38, win_w - 20, win_h - 90, items, -1, 0, 0, 0);

        gui_add_button(g_stickynote_win, 10, win_h - 42, 90, 28, "Edit", sticky_cb_edit, 0);
        gui_add_button(g_stickynote_win, 110, win_h - 42, 90, 28, "Delete", sticky_cb_delete, 0);
    } else {
        /* ---------- Edit view ---------- */
        const char *init = "";
        g_stickynote_win = gui_create_window(200, 130, win_w, win_h, "Edit Note");
        if (!g_stickynote_win) return;

        if (g_sticky_edit_idx >= 0 && g_sticky_edit_idx < g_sticky_count)
            init = g_sticky_notes[g_sticky_edit_idx];

        gui_add_label(g_stickynote_win, 10, 8, 200, 20, (g_sticky_edit_idx < 0) ? "New Note" : "Edit Note");
        g_sticky_editbox = gui_add_textarea(g_stickynote_win, 10, 32, win_w - 20, win_h - 90, init);
        if (g_sticky_editbox) {
            gui_widget_set_textbox_flags(g_sticky_editbox,
                GUI_TEXTBOX_FLAG_MULTILINE | GUI_TEXTBOX_FLAG_WRAP);
            gui_widget_set_placeholder(g_sticky_editbox, "Type your note here...");
            gui_set_focused_widget(g_sticky_editbox);
        }

        gui_add_button(g_stickynote_win, 10, win_h - 42, 90, 28, "Save", sticky_cb_save, 0);
        gui_add_button(g_stickynote_win, 110, win_h - 42, 90, 28, "Cancel", sticky_cb_cancel, 0);
    }

    gui_window_set_on_close(g_stickynote_win, stickynote_on_close, 0);
    gui_render();
}
