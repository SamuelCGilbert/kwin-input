// SPDX-License-Identifier: GPL-3.0-or-later
// Screen capture and pointer/output queries for kwin-input, through KWin's
// D-Bus APIs:
//  - org.kde.KWin.ScreenShot2 (restricted: the .desktop file of kwin-input
//    lists it in X-KDE-DBUS-Restricted-Interfaces), no permission prompt
//  - KWin scripting, for the cursor position and the output layout
#define _GNU_SOURCE
#include "kwin-query.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static GDBusConnection *bus(void) {
  static GDBusConnection *connection;
  if (!connection) {
    GError *error = NULL;
    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!connection) {
      fprintf(stderr, "kwin-input: no session bus: %s\n", error->message);
      g_error_free(error);
    }
  }
  return connection;
}

struct reader {
  int fd;
  GByteArray *data;
};

static void *read_all(void *arg) {
  struct reader *r = arg;
  guint8 buf[1 << 16];
  for (;;) {
    ssize_t n = read(r->fd, buf, sizeof buf);
    if (n > 0) g_byte_array_append(r->data, buf, (guint)n);
    else if (n == 0 || errno != EINTR) break;
  }
  close(r->fd);
  return NULL;
}

int kq_capture(int x, int y, int w, int h, const char *path, char *reply, size_t reply_len) {
  GDBusConnection *connection = bus();
  if (!connection || w <= 0 || h <= 0) return -1;

  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) return -1;
  struct reader r = {fds[0], g_byte_array_new()};
  pthread_t thread;
  pthread_create(&thread, NULL, read_all, &r);

  GUnixFDList *fd_list = g_unix_fd_list_new();
  int index = g_unix_fd_list_append(fd_list, fds[1], NULL);
  close(fds[1]);

  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&options, "{sv}", "native-resolution", g_variant_new_boolean(TRUE));
  g_variant_builder_add(&options, "{sv}", "include-cursor", g_variant_new_boolean(FALSE));

  GError *error = NULL;
  GVariant *result = g_dbus_connection_call_with_unix_fd_list_sync(
      connection, "org.kde.KWin", "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2",
      "CaptureArea", g_variant_new("(iiuua{sv}h)", x, y, (guint32)w, (guint32)h, &options, index),
      G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 5000, fd_list, NULL, NULL, &error);
  g_object_unref(fd_list);
  // Our write end is closed, KWin's is closed once it has written the image
  pthread_join(thread, NULL);

  if (!result) {
    fprintf(stderr, "kwin-input: capture failed: %s\n", error->message);
    g_error_free(error);
    g_byte_array_free(r.data, TRUE);
    return -1;
  }

  GVariant *meta = g_variant_get_child_value(result, 0);
  guint32 width = 0, height = 0, stride = 0, format = 0;
  g_variant_lookup(meta, "width", "u", &width);
  g_variant_lookup(meta, "height", "u", &height);
  g_variant_lookup(meta, "stride", "u", &stride);
  g_variant_lookup(meta, "format", "u", &format);
  g_variant_unref(meta);
  g_variant_unref(result);

  int status = -1;
  if (r.data->len >= (guint)stride * height) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f && fwrite(r.data->data, 1, (size_t)stride * height, f) == (size_t)stride * height &&
        fclose(f) == 0 && rename(tmp, path) == 0) {
      // QImage formats: 4 RGB32, 5 ARGB32, 6 ARGB32_Premultiplied (BGRA bytes)
      snprintf(reply, reply_len, "%u %u %u %u", width, height, stride, format);
      status = 0;
    } else if (f) {
      fclose(f);
    }
  } else {
    fprintf(stderr, "kwin-input: capture returned %u bytes, expected %u\n", r.data->len,
            stride * height);
  }
  g_byte_array_free(r.data, TRUE);
  return status;
}

// --- KWin scripts ----------------------------------------------------------

static char *script_result;

static void on_reply(GDBusConnection *c, const gchar *sender, const gchar *path,
                     const gchar *iface, const gchar *method, GVariant *params,
                     GDBusMethodInvocation *invocation, gpointer data) {
  (void)c; (void)sender; (void)path; (void)iface; (void)method; (void)data;
  const gchar *value = NULL;
  g_variant_get(params, "(&s)", &value);
  g_free(script_result);
  script_result = g_strdup(value);
  g_dbus_method_invocation_return_value(invocation, NULL);
}

static const GDBusInterfaceVTable vtable = {on_reply, NULL, NULL, {0}};

// Run a KWin script whose last statement calls reply(string). Returns a newly
// allocated string or NULL.
static char *run_script(const char *body) {
  GDBusConnection *connection = bus();
  if (!connection) return NULL;
  static guint registration;
  static GDBusNodeInfo *node;
  if (!registration) {
    node = g_dbus_node_info_new_for_xml(
        "<node><interface name='io.github.kwininput.Reply'><method name='Reply'>"
        "<arg type='s' direction='in'/></method></interface></node>", NULL);
    registration = g_dbus_connection_register_object(connection, "/io/github/kwininput/Reply",
                                                     node->interfaces[0], &vtable, NULL, NULL, NULL);
  }

  char path[4096], name[64];
  const char *runtime = g_get_user_runtime_dir();
  snprintf(name, sizeof name, "kwin-input-%d", getpid());
  snprintf(path, sizeof path, "%s/%s.js", runtime, name);
  char *script = g_strdup_printf(
      "function reply(s) { callDBus('%s', '/io/github/kwininput/Reply', "
      "'io.github.kwininput.Reply', 'Reply', String(s)); }\n%s\n",
      g_dbus_connection_get_unique_name(connection), body);
  gboolean written = g_file_set_contents(path, script, -1, NULL);
  g_free(script);
  if (!written) return NULL;

  g_free(script_result);
  script_result = NULL;
  GError *error = NULL;
  GVariant *loaded = g_dbus_connection_call_sync(
      connection, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript",
      g_variant_new("(ss)", path, name), G_VARIANT_TYPE("(i)"), G_DBUS_CALL_FLAGS_NONE, 2000,
      NULL, &error);
  if (!loaded) {
    fprintf(stderr, "kwin-input: loadScript failed: %s\n", error->message);
    g_error_free(error);
    unlink(path);
    return NULL;
  }
  gint32 id = -1;
  g_variant_get(loaded, "(i)", &id);
  g_variant_unref(loaded);
  char object[64];
  snprintf(object, sizeof object, "/Scripting/Script%d", id);
  GVariant *ran = g_dbus_connection_call_sync(connection, "org.kde.KWin", object,
                                              "org.kde.kwin.Script", "run", NULL, NULL,
                                              G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
  if (ran) g_variant_unref(ran);

  GMainContext *context = g_main_context_default();
  gint64 deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
  while (!script_result && g_get_monotonic_time() < deadline)
    g_main_context_iteration(context, TRUE);

  GVariant *unloaded = g_dbus_connection_call_sync(
      connection, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript",
      g_variant_new("(s)", name), NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
  if (unloaded) g_variant_unref(unloaded);
  unlink(path);
  char *result = script_result;
  script_result = NULL;
  return result;
}

int kq_pointer(char *reply, size_t reply_len) {
  char *r = run_script("reply(workspace.cursorPos.x + ' ' + workspace.cursorPos.y);");
  if (!r) return -1;
  snprintf(reply, reply_len, "%s", r);
  g_free(r);
  return 0;
}

int kq_outputs(char *reply, size_t reply_len) {
  // "name x y w h scale;..." in logical coordinates
  char *r = run_script(
      "reply(workspace.screens.map(function (o) { var g = o.geometry; "
      "return [o.name, g.x, g.y, g.width, g.height, o.devicePixelRatio].join(' '); }).join(';'));");
  if (!r) return -1;
  snprintf(reply, reply_len, "%s", r);
  g_free(r);
  return 0;
}
