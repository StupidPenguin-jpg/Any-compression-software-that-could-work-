#include "core.h"
#include <gtk/gtk.h>
#include <pthread.h>
#include <string.h>
extern char g_password[256];

static GtkWidget *window;
static GtkWidget *g_pw_entry;
static GtkWidget *dst_entry;
static GtkWidget *dsrc_entry, *ddst_entry;
static GtkWidget *files_list;
static GtkWidget *multi_scroll;
static GtkWidget *scroll_box;
static GtkWidget *progress_bar, *status_bar;
static GtkWidget *mode_combo;
static GtkWidget *mode_tip;
static GtkWidget *info_cl;
static GtkWidget *info_dl;
static GtkWidget *nb;
static GtkWidget *multi_toolbar;
static GtkWidget *compress_btn, *cancel_btn, *decompress_btn, *dcancel_btn;
static GtkListStore *file_store;

static volatile int g_running = 0;

static void set_status(const char *t)
{
    gtk_statusbar_push(GTK_STATUSBAR(status_bar),
        gtk_statusbar_get_context_id(GTK_STATUSBAR(status_bar), "m"), t);
}
static void update_progress(double f)
{ gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), f); }
static void set_compress_sensitive(gboolean s)
{
    gtk_widget_set_sensitive(mode_combo, s);
    gtk_widget_set_sensitive(multi_toolbar, s);
    gtk_widget_set_sensitive(dst_entry, s);
    gtk_widget_set_sensitive(compress_btn, s);
}
static void set_decompress_sensitive(gboolean s)
{
    gtk_widget_set_sensitive(dsrc_entry, s);
    gtk_widget_set_sensitive(ddst_entry, s);
    gtk_widget_set_sensitive(decompress_btn, s);
}

typedef struct { int ret; char *dst; unsigned long orig; int cancelled; } CompressResult;
typedef struct { int ret; char *dir; int cancelled; } DecompressResult;

static gboolean on_compress_done(gpointer data)
{
    CompressResult *r = (CompressResult *)data;
    g_running = 0; g_cancel = 0;
    update_progress(1.0);
    gtk_widget_hide(cancel_btn);
    gtk_widget_show(compress_btn);
    set_compress_sensitive(TRUE);
    if (r->cancelled) { set_status("\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88"); g_free(r->dst); free(r); return FALSE; }
    if (r->ret == 0) {
        FILE *f = fopen(r->dst, "rb"); long comp = 0;
        if (f) { fseek(f, 0, SEEK_END); comp = ftell(f); fclose(f); }
        char info[768];
        if (r->orig > 0) {
            double pct = (double)comp / r->orig * 100;
            if (comp >= r->orig)
                snprintf(info, sizeof(info), "\xe4\xbf\x9d\xe5\xad\x98\xe8\xb7\xaf\xe5\xbe\x84: %s\n\n\xe5\x8e\x9f\xe5\xa7\x8b: %lu \xe5\xad\x97\xe8\x8a\x82\n\xe5\x8e\x8b\xe7\xbc\xa9\xe5\x90\x8e: %ld \xe5\xad\x97\xe8\x8a\x82\n\n\xe5\xb0\x8f\xe6\x96\x87\xe4\xbb\xb6\xef\xbc\x8c\xe5\xa4\xb4\xe9\x83\xa8\xe5\xbc\x80\xe9\x94\x80\xe5\xa4\xa7\xe4\xba\x8e\xe5\x8e\x8b\xe7\xbc\xa9\xe8\x8a\x82\xe7\x9c\x81", r->dst, r->orig, comp);
            else { double saved = 100 - pct; snprintf(info, sizeof(info), "\xe4\xbf\x9d\xe5\xad\x98\xe8\xb7\xaf\xe5\xbe\x84: %s\n\n\xe5\x8e\x9f\xe5\xa7\x8b: %lu \xe5\xad\x97\xe8\x8a\x82\n\xe5\x8e\x8b\xe7\xbc\xa9\xe5\x90\x8e: %ld \xe5\xad\x97\xe8\x8a\x82\n\xe5\x8e\x8b\xe7\xbc\xa9\xe7\x8e\x87: %.1f%%\n\xe8\x8a\x82\xe7\x9c\x81: %.1f%%", r->dst, r->orig, comp, pct, saved); }
        } else snprintf(info, sizeof(info), "\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xae\x8c\xe6\x88\x90! \xe4\xbf\x9d\xe5\xad\x98\xe5\x88\xb0: %s", r->dst);
        /* \xe5\xbc\xb9\xe7\xaa\x97\xe6\x98\xbe\xe7\xa4\xba */
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", info);
        gtk_window_set_title(GTK_WINDOW(dlg), "\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xae\x8c\xe6\x88\x90");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        /* reset */
        gtk_list_store_clear(file_store);
        gtk_widget_hide(scroll_box);
        gtk_widget_hide(multi_scroll);
        gtk_entry_set_text(GTK_ENTRY(dst_entry), "");
        gtk_label_set_text(GTK_LABEL(info_cl), "");
        set_status("\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xae\x8c\xe6\x88\x90!");
    } else set_status("\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xa4\xb1\xe8\xb4\xa5!");
    g_free(r->dst); free(r);
    return FALSE;
}
static gboolean on_decompress_done(gpointer data)
{
    DecompressResult *r = (DecompressResult *)data;
    g_running = 0; g_cancel = 0;
    update_progress(1.0);
    gtk_widget_hide(dcancel_btn);
    gtk_widget_show(decompress_btn);
    set_decompress_sensitive(TRUE);
    if (r->cancelled) { set_status("\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88"); g_free(r->dir); free(r); return FALSE; }
    if (r->ret == 0) {
        char d_info[512]; snprintf(d_info, sizeof(d_info), "\xe8\xa7\xa3\xe5\x8e\x8b\xe5\xae\x8c\xe6\x88\x90! \xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98\xe5\x88\xb0: %s", r->dir);
        /* \xe5\xbc\xb9\xe7\xaa\x97\xe6\x98\xbe\xe7\xa4\xba */
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", d_info);
        gtk_window_set_title(GTK_WINDOW(dlg), "\xe8\xa7\xa3\xe5\x8e\x8b\xe5\xae\x8c\xe6\x88\x90");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        /* reset */
        gtk_entry_set_text(GTK_ENTRY(dsrc_entry), "");
        gtk_entry_set_text(GTK_ENTRY(ddst_entry), "");
        gtk_label_set_text(GTK_LABEL(info_dl), "");
        set_status("\xe8\xa7\xa3\xe5\x8e\x8b\xe5\xae\x8c\xe6\x88\x90!");
    } else set_status("\xe8\xa7\xa3\xe5\x8e\x8b\xe5\xa4\xb1\xe8\xb4\xa5!");
    g_free(r->dir); free(r);
    return FALSE;
}
static gboolean on_progress_tick(gpointer d)
{
    if (!g_running) return FALSE;
    if (g_progress_total > 0) {
        double f = (double)g_progress_processed / g_progress_total;
        if (f > 1.0) f = 1.0;
        update_progress(f);
    }
    return g_running;
}
static void on_cancel_compress(GtkWidget *b, gpointer d)
{ if (!g_running) return; g_cancel = 1; gtk_widget_set_sensitive(cancel_btn, FALSE); set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8f\x96\xe6\xb6\x88..."); }
static void on_cancel_decompress(GtkWidget *b, gpointer d)
{ if (!g_running) return; g_cancel = 1; gtk_widget_set_sensitive(dcancel_btn, FALSE); set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8f\x96\xe6\xb6\x88..."); }

typedef struct { char **files; int count; int mode; char *dst; unsigned long orig_total; } CompressJob;
static void *compress_thread(void *arg)
{
    CompressJob *job = (CompressJob *)arg;
    int ret = (job->mode == MODE_ZIP) ? compress_zip((const char**)job->files, job->count, job->dst) : compress_tar((const char**)job->files, job->count, job->dst);
    g_running = 0;
    CompressResult *r = malloc(sizeof(CompressResult));
    r->ret = ret; r->dst = strdup(job->dst); r->orig = job->orig_total; r->cancelled = g_cancel;
    g_cancel = 0;
    g_idle_add(on_compress_done, r);
    for (int i = 0; i < job->count; i++) g_free(job->files[i]); free(job->files); free(job->dst); free(job);
    return NULL;
}
static void *decompress_thread(void *arg)
{
    char **paths = (char **)arg;
    int ret = decompress_file(paths[0], paths[1]);
    g_running = 0;
    DecompressResult *r = malloc(sizeof(DecompressResult));
    r->ret = ret; r->dir = strdup(paths[1]); r->cancelled = g_cancel;
    g_cancel = 0;
    g_idle_add(on_decompress_done, r);
    free(paths[0]); free(paths[1]); free(paths);
    return NULL;
}

static void on_decompress_file_sel(GtkWidget *w, gpointer entry)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new("\xe9\x80\x89\xe6\x8b\xa9\xe5\x8e\x8b\xe7\xbc\xa9\xe6\x96\x87\xe4\xbb\xb6", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, "_\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, "_\xe7\xa1\xae\xe5\xae\x9a", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(entry), p);
        char buf[1024]; size_t l = strlen(p);
        if (l > 4 && strcmp(p + l - 4, ".zyx") == 0) { memcpy(buf, p, l - 4); buf[l - 4] = 0; }
        else snprintf(buf, sizeof(buf), "%s_out", p);
        gtk_entry_set_text(GTK_ENTRY(ddst_entry), buf);
        g_free(p);
    }
    gtk_widget_destroy(dlg);
}
static void on_dir_sel(GtkWidget *w, gpointer entry)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new("\xe9\x80\x89\xe6\x8b\xa9\xe7\x9b\xae\xe5\xbd\x95", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, "_\xe7\xa1\xae\xe5\xae\x9a", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg)); gtk_entry_set_text(GTK_ENTRY(entry), p); g_free(p);
    }
    gtk_widget_destroy(dlg);
}
static void on_add_files_clicked(GtkWidget *b, gpointer d)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new("\xe9\x80\x89\xe6\x8b\xa9\xe6\x96\x87\xe4\xbb\xb6(\xe5\x8f\xaf\xe5\xa4\x9a\xe9\x80\x89)", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, "_\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, "_\xe7\xa1\xae\xe5\xae\x9a", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        GSList *list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
        char first_path[1024] = "";
        for (GSList *p = list; p; p = p->next) {
            const char *path = (const char *)p->data;
            if (!*first_path) { strncpy(first_path, path, sizeof(first_path)-1); first_path[sizeof(first_path)-1] = 0; }
            const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
            FILE *fp = fopen(path, "rb"); long sz = 0;
            if (fp) { fseek(fp, 0, SEEK_END); sz = ftell(fp); fclose(fp); }
            char szs[64];
            if (sz < 1024) snprintf(szs, sizeof(szs), "%ld B", sz);
            else if (sz < 1024*1024) snprintf(szs, sizeof(szs), "%.1f KB", sz/1024.0);
            else snprintf(szs, sizeof(szs), "%.2f MB", sz/(1024.0*1024.0));
            GtkTreeIter it; gtk_list_store_append(file_store, &it);
            gtk_list_store_set(file_store, &it, 0, name, 1, szs, 2, path, -1);
        }
        g_slist_free_full(list, g_free);
        gtk_widget_show(scroll_box);
    gtk_widget_show(multi_scroll);
        if (*first_path && !*gtk_entry_get_text(GTK_ENTRY(dst_entry))) {
            char buf[1024]; const char *dot = strrchr(first_path, '.');
            if (dot) { size_t l = dot - first_path; memcpy(buf, first_path, l); buf[l] = 0; strcat(buf, ".zyx"); }
            else snprintf(buf, sizeof(buf), "%s.zyx", first_path);
            gtk_entry_set_text(GTK_ENTRY(dst_entry), buf);
        }
    }
    gtk_widget_destroy(dlg);
}
static void on_del_sel_clicked(GtkWidget *b, gpointer d)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(files_list));
    GtkTreeModel *m;
    GList *rows = gtk_tree_selection_get_selected_rows(sel, &m);
    if (!rows) return;
    for (GList *p = rows; p; p = p->next) {
        GtkTreePath *path = (GtkTreePath *)p->data;
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(m, &it, path))
            gtk_list_store_remove(GTK_LIST_STORE(m), &it);
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(file_store), NULL) == 0)
        gtk_widget_hide(scroll_box);
    gtk_widget_hide(multi_scroll);
}
static void on_clear_all_clicked(GtkWidget *b, gpointer d)
{ gtk_list_store_clear(file_store); gtk_widget_hide(scroll_box);
    gtk_entry_set_text(GTK_ENTRY(dst_entry), "");
    gtk_widget_hide(scroll_box);
    gtk_widget_hide(multi_scroll); }
static void on_dst_sel(GtkWidget *w, gpointer entry)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new("\xe9\x80\x89\xe6\x8b\xa9\xe8\xb7\xaf\xe5\xbe\x84", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE, "_\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, "_\xe7\xa1\xae\xe5\xae\x9a", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg)); gtk_entry_set_text(GTK_ENTRY(entry), p); g_free(p);
    }
    gtk_widget_destroy(dlg);
}
static void on_mode_changed(GtkComboBox *combo, gpointer d)
{
    int idx = gtk_combo_box_get_active(combo);
    if (idx == 0) gtk_label_set_text(GTK_LABEL(mode_tip), "\xe7\x89\xb9\xe7\x82\xb9: \xe6\xaf\x8f\xe4\xb8\xaa\xe6\x96\x87\xe4\xbb\xb6\xe7\x8b\xac\xe7\xab\x8b\xe7\xbc\x96\xe7\xa0\x81\xef\xbc\x8c\xe5\x8e\x8b\xe7\xbc\xa9\xe7\x8e\x87\xe9\xab\x98\xef\xbc\x8c\xe4\xbd\x86\xe9\x80\x9f\xe5\xba\xa6\xe8\xbe\x83\xe6\x85\xa2");
    else gtk_label_set_text(GTK_LABEL(mode_tip), "\xe7\x89\xb9\xe7\x82\xb9: \xe5\x90\x88\xe5\xb9\xb6\xe7\xbc\x96\xe7\xa0\x81\xe9\x80\x9f\xe5\xba\xa6\xe5\xbf\xab\xef\xbc\x8c\xe4\xbd\x86\xe5\x8e\x8b\xe7\xbc\xa9\xe7\x8e\x87\xe7\x9b\xb8\xe5\xaf\xb9\xe8\xbe\x83\xe4\xbd\x8e");
}

/* 拖拽 */
static void on_drag_data(GtkWidget *w, GdkDragContext *ctx, int x, int y, GtkSelectionData *sel, guint info, guint time, gpointer d)
{
    gchar **uris = gtk_selection_data_get_uris(sel);
    if (!uris) return;
    char first_path[1024] = "";
    for (int i = 0; uris[i]; i++) {
        char *path = g_filename_from_uri(uris[i], NULL, NULL);
        if (!path) continue;
        if (!*first_path) { strncpy(first_path, path, sizeof(first_path)-1); first_path[sizeof(first_path)-1] = 0; }
        const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
        FILE *fp = fopen(path, "rb"); long sz = 0;
        if (fp) { fseek(fp, 0, SEEK_END); sz = ftell(fp); fclose(fp); }
        char szs[64];
        if (sz < 1024) snprintf(szs, sizeof(szs), "%ld B", sz);
        else if (sz < 1024*1024) snprintf(szs, sizeof(szs), "%.1f KB", sz/1024.0);
        else snprintf(szs, sizeof(szs), "%.2f MB", sz/(1024.0*1024.0));
        GtkTreeIter it; gtk_list_store_append(file_store, &it);
        gtk_list_store_set(file_store, &it, 0, name, 1, szs, 2, path, -1);
        g_free(path);
    }
    g_strfreev(uris);
    gtk_widget_show(scroll_box);
    gtk_widget_show(multi_scroll);
    if (*first_path && !*gtk_entry_get_text(GTK_ENTRY(dst_entry))) {
        char buf[1024]; const char *dot = strrchr(first_path, '.');
        if (dot) { size_t l = dot - first_path; memcpy(buf, first_path, l); buf[l] = 0; strcat(buf, ".zyx"); }
        else snprintf(buf, sizeof(buf), "%s.zyx", first_path);
        gtk_entry_set_text(GTK_ENTRY(dst_entry), buf);
    }
    gtk_drag_finish(ctx, TRUE, FALSE, time);
}

static void on_compress_clicked(GtkWidget *b, gpointer d)
{
    const char *dst = gtk_entry_get_text(GTK_ENTRY(dst_entry));
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &it)) { set_status("\xe8\xaf\xb7\xe6\xb7\xbb\xe5\x8a\xa0\xe8\xa6\x81\xe5\x8e\x8b\xe7\xbc\xa9\xe7\x9a\x84\xe6\x96\x87\xe4\xbb\xb6"); return; }
    if (!dst || !*dst) {
        gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &it);
        char *first_path; gtk_tree_model_get(GTK_TREE_MODEL(file_store), &it, 2, &first_path, -1);
        char buf[1024]; const char *dot = strrchr(first_path, '.');
        if (dot) { size_t l = dot - first_path; memcpy(buf, first_path, l); buf[l] = 0; strcat(buf, ".zyx"); }
        else snprintf(buf, sizeof(buf), "%s.zyx", first_path);
        g_free(first_path); gtk_entry_set_text(GTK_ENTRY(dst_entry), buf);
        dst = gtk_entry_get_text(GTK_ENTRY(dst_entry));
    }

    g_progress_processed = 0; g_progress_total = 0;
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &it);
    do {
        char *path; gtk_tree_model_get(GTK_TREE_MODEL(file_store), &it, 2, &path, -1);
        FILE *ff = fopen(path, "rb");
        if (ff) { fseek(ff, 0, SEEK_END); g_progress_total += ftell(ff); fclose(ff); }
        g_free(path);
    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(file_store), &it));

    strcpy(g_password, gtk_entry_get_text(GTK_ENTRY(g_pw_entry)));
    update_progress(0.0);
    set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8e\x8b\xe7\xbc\xa9...");
    gtk_widget_hide(compress_btn);
    gtk_widget_show(cancel_btn);
    gtk_widget_set_sensitive(cancel_btn, TRUE);
    set_compress_sensitive(FALSE);
    gtk_label_set_text(GTK_LABEL(info_cl), "");

    /* 收集文件列表 */
    int cnt = 0; GtkTreeIter ti;
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &ti);
    do { cnt++; } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(file_store), &ti));
    char **files = malloc(sizeof(char *) * cnt);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &ti);
    for (int i = 0; i < cnt; i++) {
        char *p; gtk_tree_model_get(GTK_TREE_MODEL(file_store), &ti, 2, &p, -1);
        files[i] = p; if (i < cnt - 1) gtk_tree_model_iter_next(GTK_TREE_MODEL(file_store), &ti);
    }

    CompressJob *job = malloc(sizeof(CompressJob));
    job->files = files; job->count = cnt;
    job->mode = gtk_combo_box_get_active(GTK_COMBO_BOX(mode_combo)) + 2;
    job->dst = strdup(dst); job->orig_total = g_progress_total;

    g_running = 1;
    pthread_t tid; pthread_create(&tid, NULL, compress_thread, job); pthread_detach(tid);
    g_timeout_add(100, on_progress_tick, NULL);
}

static void on_decompress_clicked(GtkWidget *b, gpointer d)
{
    const char *src = gtk_entry_get_text(GTK_ENTRY(dsrc_entry));
    const char *dir = gtk_entry_get_text(GTK_ENTRY(ddst_entry));
    if (!src || !*src) { set_status("\xe8\xaf\xb7\xe9\x80\x89\xe6\x8b\xa9\xe5\x8e\x8b\xe7\xbc\xa9\xe6\x96\x87\xe4\xbb\xb6"); return; }
    if (!dir || !*dir) {
        char buf[1024]; size_t l = strlen(src);
        if (l > 4 && strcmp(src + l - 4, ".zyx") == 0) { memcpy(buf, src, l - 4); buf[l - 4] = 0; }
        else snprintf(buf, sizeof(buf), "%s_out", src);
        gtk_entry_set_text(GTK_ENTRY(ddst_entry), buf);
        dir = gtk_entry_get_text(GTK_ENTRY(ddst_entry));
    }
    update_progress(0.0);
    strcpy(g_password, gtk_entry_get_text(GTK_ENTRY(g_pw_entry)));
    set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe8\xa7\xa3\xe5\x8e\x8b...");
    g_progress_processed = 0; g_progress_total = 0;
    FILE *tf = fopen(src, "rb");
    if (tf) { fseek(tf, 0, SEEK_END); g_progress_total = (ftell(tf) > 0) ? ftell(tf) * 10 : 1; fclose(tf); }
    else g_progress_total = 1;

    gtk_widget_hide(decompress_btn);
    gtk_widget_show(dcancel_btn);
    gtk_widget_set_sensitive(dcancel_btn, TRUE);
    set_decompress_sensitive(FALSE);
    gtk_label_set_text(GTK_LABEL(info_dl), "");

    char **paths = malloc(sizeof(char *) * 2);
    paths[0] = strdup(src); paths[1] = strdup(dir);
    g_running = 1;
    pthread_t tid; pthread_create(&tid, NULL, decompress_thread, paths); pthread_detach(tid);
    g_timeout_add(100, on_progress_tick, NULL);
}

static void on_about(GtkWidget *item, gpointer d)
{
    GtkWidget *dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "\xe5\xba\x94\xe8\xaf\xa5\xe8\x83\xbd\xe7\x94\xa8\xe7\x9a\x84\xe8\xa7\xa3\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xb7\xa5\xe5\x85\xb7");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), "v1.0");
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dlg), "Copyright (C) 2026 ZhangYixin/StupidPenguin");
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(window));
    gtk_dialog_run(GTK_DIALOG(dlg)); gtk_widget_destroy(dlg);
}

int run_gui(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "\xe5\xba\x94\xe8\xaf\xa5\xe8\x83\xbd\xe7\x94\xa8\xe7\x9a\x84\xe8\xa7\xa3\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xb7\xa5\xe5\x85\xb7v1.0");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 560);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* 菜单栏 */
    GtkWidget *mb = gtk_menu_bar_new();
    GtkWidget *ei = gtk_menu_item_new_with_label("\xe9\x80\x80\xe5\x87\xba");
    g_signal_connect(ei, "activate", G_CALLBACK(gtk_main_quit), NULL);
    GtkWidget *hm = gtk_menu_new();
    GtkWidget *hi = gtk_menu_item_new_with_label("\xe5\xb8\xae\xe5\x8a\xa9(H)");
    GtkWidget *ai = gtk_menu_item_new_with_label("\xe5\x85\xb3\xe4\xba\x8e");
    g_signal_connect(ai, "activate", G_CALLBACK(on_about), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(hm), ai);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(hi), hm);
    gtk_menu_shell_append(GTK_MENU_SHELL(mb), ei);
    gtk_menu_shell_append(GTK_MENU_SHELL(mb), hi);
    gtk_box_pack_start(GTK_BOX(vbox), mb, FALSE, FALSE, 0);

    GtkWidget *w_nb = gtk_notebook_new();
    nb = w_nb;
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 5);

    /* ===== 压缩页 ===== */
    GtkWidget *cp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(cp), 10);

    GtkWidget *mw = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(cp), mw, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mw), gtk_label_new("\xe5\x8e\x8b\xe7\xbc\xa9\xe6\xa8\xa1\xe5\xbc\x8f:"), FALSE, FALSE, 0);
    mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "ZIP\xe5\xbc\x8f\xe5\xbd\x92\xe6\xa1\xa3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "TAR\xe5\xbc\x8f\xe5\xbd\x92\xe6\xa1\xa3");
    g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 0);
    gtk_box_pack_start(GTK_BOX(mw), mode_combo, TRUE, TRUE, 0);

    mode_tip = gtk_label_new("\xe7\x89\xb9\xe7\x82\xb9: \xe6\xaf\x8f\xe4\xb8\xaa\xe6\x96\x87\xe4\xbb\xb6\xe7\x8b\xac\xe7\xab\x8b\xe7\xbc\x96\xe7\xa0\x81\xef\xbc\x8c\xe5\x8e\x8b\xe7\xbc\xa9\xe7\x8e\x87\xe9\xab\x98\xef\xbc\x8c\xe4\xbd\x86\xe9\x80\x9f\xe5\xba\xa6\xe8\xbe\x83\xe6\x85\xa2");
    gtk_label_set_xalign(GTK_LABEL(mode_tip), 0);
    gtk_box_pack_start(GTK_BOX(cp), mode_tip, FALSE, FALSE, 0);

    multi_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *add_btn = gtk_button_new_with_label("\xe6\xb7\xbb\xe5\x8a\xa0\xe6\x96\x87\xe4\xbb\xb6");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_files_clicked), NULL);
    GtkWidget *del_btn = gtk_button_new_with_label("\xe5\x88\xa0\xe9\x99\xa4\xe9\x80\x89\xe4\xb8\xad");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_del_sel_clicked), NULL);
    GtkWidget *clear_btn = gtk_button_new_with_label("\xe6\xb8\x85\xe7\xa9\xba\xe5\x85\xa8\xe9\x83\xa8");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_all_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(multi_toolbar), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(multi_toolbar), del_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(multi_toolbar), clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cp), multi_toolbar, FALSE, FALSE, 0);

    /* 文件列表 (scrolled, 占据剩余空间) */
    multi_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(multi_scroll, -1, 150);
    file_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(file_store));
    files_list = tv;
    GtkCellRenderer *mr = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("\xe6\x96\x87\xe4\xbb\xb6\xe5\x90\x8d", mr, "text", 0, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("\xe5\xa4\xa7\xe5\xb0\x8f", mr, "text", 1, NULL));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    gtk_drag_dest_set(tv, GTK_DEST_DEFAULT_ALL, NULL, 0, FALSE);
    gtk_drag_dest_add_uri_targets(tv);
    g_signal_connect(tv, "drag-data-received", G_CALLBACK(on_drag_data), NULL);
    gtk_container_add(GTK_CONTAINER(multi_scroll), tv);
    /* multi_scroll 使用 cp 的剩余空间 */
    GtkWidget *scroll_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(scroll_box), multi_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(cp), scroll_box, TRUE, TRUE, 0);
    gtk_widget_hide(scroll_box);

    /* 底部区域：目标路径 + 压缩/取消按钮 + 信息 — 不扩展 */
    GtkWidget *dw = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(dw), gtk_label_new("\xe7\x9b\xae\xe6\xa0\x87:"), FALSE, FALSE, 0);
    dst_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(dw), dst_entry, TRUE, TRUE, 0);
    GtkWidget *db = gtk_button_new_with_label("\xe6\xb5\x8f\xe8\xa7\x88");
    g_signal_connect(db, "clicked", G_CALLBACK(on_dst_sel), dst_entry);
    gtk_box_pack_start(GTK_BOX(dw), db, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cp), dw, FALSE, FALSE, 0);

    compress_btn = gtk_button_new_with_label("\xe5\xbc\x80\xe5\xa7\x8b\xe5\x8e\x8b\xe7\xbc\xa9");
    g_signal_connect(compress_btn, "clicked", G_CALLBACK(on_compress_clicked), NULL);
    cancel_btn = gtk_button_new_with_label("\xe5\x8f\x96\xe6\xb6\x88");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_compress), NULL);
    gtk_widget_hide(cancel_btn);
    gtk_box_pack_start(GTK_BOX(cp), compress_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(cp), cancel_btn, FALSE, FALSE, 5);

    info_cl = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(info_cl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(info_cl), 0);
    gtk_box_pack_start(GTK_BOX(cp), info_cl, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), cp, gtk_label_new("\xe5\x8e\x8b\xe7\xbc\xa9"));

    /* ===== 解压页 ===== */
    GtkWidget *dp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(dp), 10);

    GtkWidget *dsw = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(dsw), gtk_label_new("\xe5\x8e\x8b\xe7\xbc\xa9\xe6\x96\x87\xe4\xbb\xb6:"), FALSE, FALSE, 0);
    dsrc_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(dsw), dsrc_entry, TRUE, TRUE, 0);
    GtkWidget *dsb = gtk_button_new_with_label("\xe6\xb5\x8f\xe8\xa7\x88");
    g_signal_connect(dsb, "clicked", G_CALLBACK(on_decompress_file_sel), dsrc_entry);
    gtk_box_pack_start(GTK_BOX(dsw), dsb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dp), dsw, FALSE, FALSE, 0);

    GtkWidget *ddw = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(ddw), gtk_label_new("\xe8\xa7\xa3\xe5\x8e\x8b\xe5\x88\xb0:"), FALSE, FALSE, 0);
    ddst_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(ddw), ddst_entry, TRUE, TRUE, 0);
    GtkWidget *ddb = gtk_button_new_with_label("\xe6\xb5\x8f\xe8\xa7\x88");
    g_signal_connect(ddb, "clicked", G_CALLBACK(on_dir_sel), ddst_entry);
    gtk_box_pack_start(GTK_BOX(ddw), ddb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dp), ddw, FALSE, FALSE, 0);

    decompress_btn = gtk_button_new_with_label("\xe5\xbc\x80\xe5\xa7\x8b\xe8\xa7\xa3\xe5\x8e\x8b");
    g_signal_connect(decompress_btn, "clicked", G_CALLBACK(on_decompress_clicked), NULL);
    dcancel_btn = gtk_button_new_with_label("\xe5\x8f\x96\xe6\xb6\x88");
    g_signal_connect(dcancel_btn, "clicked", G_CALLBACK(on_cancel_decompress), NULL);
    gtk_widget_hide(dcancel_btn);
    gtk_box_pack_start(GTK_BOX(dp), decompress_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(dp), dcancel_btn, FALSE, FALSE, 5);

    info_dl = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(info_dl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(info_dl), 0);
    gtk_box_pack_start(GTK_BOX(dp), info_dl, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), dp, gtk_label_new("\xe8\xa7\xa3\xe5\x8e\x8b"));

    /* ===== 关于页 ===== */
    GtkWidget *ap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(ap), 20);
    GtkWidget *about_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(about_title), "<span size=\"xx-large\" weight=\"bold\">\xe5\xba\x94\xe8\xaf\xa5\xe8\x83\xbd\xe7\x94\xa8\xe7\x9a\x84\xe8\xa7\xa3\xe5\x8e\x8b\xe7\xbc\xa9\xe5\xb7\xa5\xe5\x85\xb7</span>");
    gtk_box_pack_start(GTK_BOX(ap), about_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ap), gtk_label_new("\xe7\x89\x88\xe6\x9c\xac: v1.0"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ap), gtk_label_new("\xe4\xbd\x9c\xe8\x80\x85: ZhangYixin/StupidPenguin"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ap), gtk_label_new("\xe5\x9f\xba\xe4\xba\x8e Huffman \xe7\xbc\x96\xe7\xa0\x81\xe7\x9a\x84\xe6\x97\xa0\xe6\x8d\x9f\xe5\x8e\x8b\xe7\xbc\xa9/\xe8\xa7\xa3\xe5\x8e\x8b\xe5\xb7\xa5\xe5\x85\xb7\n\xe6\x94\xaf\xe6\x8c\x81 ZIP \xe5\xbc\x8f\xe5\x92\x8c TAR \xe5\xbc\x8f\xe5\xbd\x92\xe6\xa1\xa3"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ap), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);
    GtkWidget *about_info = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(about_info), TRUE);
    gtk_label_set_xalign(GTK_LABEL(about_info), 0);
    gtk_label_set_markup(GTK_LABEL(about_info), "<span size=\"small\">\xe6\xa0\xbc\xe5\xbc\x8f\xe8\xaf\xb4\xe6\x98\x8e:\n  \xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb4: ZYX + \xe7\x89\x88\xe6\x9c\xac\xe5\x8f\xb7 + \xe6\xa8\xa1\xe5\xbc\x8f + \xe6\x96\x87\xe4\xbb\xb6\xe6\x95\xb0 + \xe5\x8e\x9f\xe5\xa7\x8b\xe5\xa4\xa7\xe5\xb0\x8f\n  \xe6\x96\x87\xe4\xbb\xb6\xe5\x88\x97\xe8\xa1\xa8: \xe6\xaf\x8f\xe4\xb8\xaa\xe6\x96\x87\xe4\xbb\xb6\xe5\x90\xab\xe6\x96\x87\xe4\xbb\xb6\xe5\x90\x8d\xe3\x80\x81\xe5\xa4\xa7\xe5\xb0\x8f\xe3\x80\x81\xe5\x81\x8f\xe7\xa7\xbb\xe9\x87\x8f\n  \xe9\xa2\x91\xe7\x8e\x87\xe8\xa1\xa8: 256 \xc3\x97 unsigned long \xe5\xad\x97\xe7\xac\xa6\xe9\xa2\x91\xe7\x8e\x87\n  \xe5\x8e\x8b\xe7\xbc\xa9\xe6\x95\xb0\xe6\x8d\xae: Huffman \xe7\xbc\x96\xe7\xa0\x81\xe5\x90\x8e\xe7\x9a\x84\xe6\xaf\x94\xe7\x89\xb9\xe6\xb5\x81\n\n\xe4\xb8\xa4\xe7\xa7\x8d\xe6\xa8\xa1\xe5\xbc\x8f:\n  ZIP:    \xe6\xaf\x8f\xe4\xb8\xaa\xe6\x96\x87\xe4\xbb\xb6\xe7\x8b\xac\xe7\xab\x8b\xe9\xa2\x91\xe7\x8e\x87\xe8\xa1\xa8\xef\xbc\x8c\xe5\x8e\x8b\xe7\xbc\xa9\xe7\x8e\x87\xe9\xab\x98\n  TAR:    \xe7\xbb\x9f\xe4\xb8\x80\xe9\xa2\x91\xe7\x8e\x87\xe8\xa1\xa8\xe5\x90\x88\xe5\xb9\xb6\xe5\x8e\x8b\xe7\xbc\xa9\xef\xbc\x8c\xe9\x80\x9f\xe5\xba\xa6\xe5\xbf\xab\n\n\xe4\xbd\xbf\xe7\x94\xa8 GTK+3 \xe5\x9b\xbe\xe5\xbd\xa2\xe7\x95\x8c\xe9\x9d\xa2 (C\xe8\xaf\xad\xe8\xa8\x80 + GCC)\n\xe7\xbc\x96\xe8\xaf\x91: make\n\xe8\xbf\x90\xe8\xa1\x8c: ./Huffman\n\nCopyright (C) 2026 ZhangYixin/StupidPenguin\n</span>");
    gtk_box_pack_start(GTK_BOX(ap), about_info, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), ap, gtk_label_new("\xe5\x85\xb3\xe4\xba\x8e"));

    /* 密码 */
    GtkWidget *pw_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(pw_hbox), gtk_label_new("\xe5\xaf\x86\xe7\xa0\x81:"), FALSE, FALSE, 0);
    g_pw_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(g_pw_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(pw_hbox), g_pw_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), pw_hbox, FALSE, FALSE, 5);

    progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), progress_bar, FALSE, FALSE, 5);

    status_bar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), status_bar, FALSE, FALSE, 0);
    set_status("\xe5\xb0\xb1\xe7\xbb\xaa");

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
