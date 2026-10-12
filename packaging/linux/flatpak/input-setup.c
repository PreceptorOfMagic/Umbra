/* Umbra Flatpak input setup. Uses the existing helper and normal host polkit prompt. */
#include <gtk/gtk.h>
#include <unistd.h>

enum { SETUP = 1, CONTINUE = 2 };
static GtkWidget *dialog, *message;
static gboolean busy, ready;
static gchar *deferred;

static gboolean input_ready(void) {
  return access("/dev/uinput", W_OK) == 0 && access("/dev/uhid", W_OK) == 0;
}

static gboolean closing(GtkWidget *widget, GdkEvent *event, gpointer data) {
  (void) widget; (void) event; (void) data;
  return busy;
}

static void finished(GObject *object, GAsyncResult *result, gpointer data) {
  (void) data;
  gchar *output = NULL;
  GError *error = NULL;
  gboolean communicated = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object), result, &output, NULL, &error);
  ready = communicated && g_subprocess_get_successful(G_SUBPROCESS(object)) && input_ready();
  busy = FALSE;
  if (ready) {
    gtk_label_set_text(GTK_LABEL(message), "Keyboard, mouse and controller access is ready.\nIf Umbra is already running, restart it to detect the devices.");
    unlink(deferred);
  } else {
    gchar *detail = g_strdup_printf("Input setup did not finish. You can retry or continue and set it up later.\n\n%s",
                                   error ? error->message : (output ? output : "The devices are still unavailable."));
    gtk_label_set_text(GTK_LABEL(message), detail);
    g_free(detail);
  }
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), SETUP, !ready);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), CONTINUE, TRUE);
  gtk_button_set_label(GTK_BUTTON(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), CONTINUE)), ready ? "Continue" : "Set up later");
  gtk_button_set_label(GTK_BUTTON(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), SETUP)), "Retry setup");
  g_clear_error(&error);
  g_free(output);
  g_object_unref(object);
}

int main(int argc, char **argv) {
  gboolean first_run = argc == 2 && g_str_equal(argv[1], "--first-run");
  deferred = g_build_filename(g_get_user_config_dir(), "umbra", "input-setup-deferred", NULL);
  ready = input_ready();
  if (first_run && (ready || g_file_test(deferred, G_FILE_TEST_EXISTS))) {
    g_free(deferred);
    return 0;
  }
  if (!gtk_init_check(&argc, &argv)) {
    g_printerr("Input setup needs a graphical desktop. Run the app's Input Setup action when logged in.\n");
    g_free(deferred);
    return 1;
  }
  dialog = gtk_dialog_new_with_buttons("Umbra input setup", NULL, GTK_DIALOG_MODAL,
                                        "Set up later", CONTINUE, "Enable input", SETUP, NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 560, -1);
  gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
  g_signal_connect(dialog, "delete-event", G_CALLBACK(closing), NULL);
  message = gtk_label_new(ready ? "Keyboard, mouse and controller access is already available." :
    "Enable keyboard, mouse and controller input from your streaming client.\n\n"
    "Your desktop will ask for administrator permission to install input-device rules. "
    "Umbra itself runs as your normal user.\n\n"
    "You can do this later from the app menu's Input Setup action. "
    "Video sharing has a separate desktop permission prompt.");
  gtk_label_set_line_wrap(GTK_LABEL(message), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(message), 68);
  gtk_label_set_selectable(GTK_LABEL(message), TRUE);
  gtk_widget_set_margin_start(message, 24);
  gtk_widget_set_margin_end(message, 24);
  gtk_widget_set_margin_top(message, 24);
  gtk_widget_set_margin_bottom(message, 24);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), message);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), SETUP, !ready);
  if (ready) gtk_button_set_label(GTK_BUTTON(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), CONTINUE)), "Close");
  gtk_widget_show_all(dialog);
  for (;;) {
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (busy) continue;
    if (response != SETUP) break;
    GError *error = NULL;
    GSubprocess *child = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
                                         &error, "/app/bin/additional-install.sh", NULL);
    if (!child) {
      gtk_label_set_text(GTK_LABEL(message), error->message);
      g_clear_error(&error);
      continue;
    }
    busy = TRUE;
    gtk_label_set_text(GTK_LABEL(message), "Complete the administrator prompt on your desktop.\nCancelling that prompt leaves setup unfinished.");
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), SETUP, FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), CONTINUE, FALSE);
    g_subprocess_communicate_utf8_async(child, NULL, NULL, finished, NULL);
  }
  if (first_run && !ready) {
    gchar *parent = g_path_get_dirname(deferred);
    if (g_mkdir_with_parents(parent, 0700) == 0)
      g_file_set_contents(deferred, "Deferred by user; reopen Input Setup from the app menu.\n", -1, NULL);
    g_free(parent);
  }
  gtk_widget_destroy(dialog);
  g_free(deferred);
  return 0;
}
