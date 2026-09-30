/* tm-application.c - the application
 *
 * Copyright (C) 2026 The timemachine authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Opens a window per file, and takes a few options that let a script do
 * what a person would -- open a model, simulate it, select a cell -- and
 * then photograph the window:
 *
 *     timemachine samples/launch.tm --simulate --select B12 --screenshot out.png
 */

#include "tm-application.h"
#include "tm-window.h"

struct _TmApplication {
  GtkApplication parent_instance;

  char *screenshot;      /* --screenshot FILE: render the window and exit */
  char *select;          /* --select RANGE */
  gboolean simulate;     /* --simulate */
  int width, height;     /* --size WxH */
};

G_DEFINE_FINAL_TYPE (TmApplication, tm_application, GTK_TYPE_APPLICATION)

/* ---- Screenshots ------------------------------------------------------ */

/* The window's widget tree rendered through GSK into a PNG, rather than
 * read back from the screen: it works with no compositor and in CI, and it
 * is how the README's pictures are made. */
static void
render_to_png (TmApplication *self, GtkWidget *window)
{
  int w = gtk_widget_get_width (window), h = gtk_widget_get_height (window);
  GdkPaintable *paintable = gtk_widget_paintable_new (window);
  GtkSnapshot *snapshot = gtk_snapshot_new ();
  GskRenderNode *node;
  cairo_surface_t *surface;
  cairo_t *cr;

  gdk_paintable_snapshot (paintable, snapshot, w, h);
  node = gtk_snapshot_free_to_node (snapshot);
  g_object_unref (paintable);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, MAX (w, 1), MAX (h, 1));
  cr = cairo_create (surface);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);
  if (node != NULL)
    {
      gsk_render_node_draw (node, cr);
      gsk_render_node_unref (node);
    }
  cairo_destroy (cr);
  if (cairo_surface_write_to_png (surface, self->screenshot) != CAIRO_STATUS_SUCCESS)
    g_printerr ("timemachine: could not write %s\n", self->screenshot);
  cairo_surface_destroy (surface);
}

static void
on_after_paint (GdkFrameClock *clock, gpointer data)
{
  TmApplication *self = data;
  GList *windows = gtk_application_get_windows (GTK_APPLICATION (self));

  g_signal_handlers_disconnect_by_func (clock, on_after_paint, data);
  if (windows != NULL)
    {
      render_to_png (self, GTK_WIDGET (windows->data));
      gtk_window_destroy (GTK_WINDOW (windows->data));
    }
  g_application_quit (G_APPLICATION (self));
}

/* Rendering waits for the window's frame clock to have painted, the one
 * moment the window is certain to have been laid out. */
static gboolean
take_screenshot (gpointer data)
{
  TmApplication *self = data;
  GList *windows = gtk_application_get_windows (GTK_APPLICATION (self));
  GdkFrameClock *clock;

  if (windows == NULL)
    {
      g_application_quit (G_APPLICATION (self));
      return G_SOURCE_REMOVE;
    }
  clock = gtk_widget_get_frame_clock (GTK_WIDGET (windows->data));
  if (clock == NULL)
    {
      render_to_png (self, GTK_WIDGET (windows->data));
      g_application_quit (G_APPLICATION (self));
      return G_SOURCE_REMOVE;
    }
  g_signal_connect (clock, "after-paint", G_CALLBACK (on_after_paint), self);
  gtk_widget_queue_draw (GTK_WIDGET (windows->data));
  return G_SOURCE_REMOVE;
}

/* ---- Windows ---------------------------------------------------------- */

static TmWindow *
open_window (TmApplication *self, GFile *file)
{
  TmWindow *window = tm_window_new (GTK_APPLICATION (self));

  if (self->width > 0 && self->height > 0)
    gtk_window_set_default_size (GTK_WINDOW (window), self->width, self->height);
  if (file != NULL)
    {
      char *path = g_file_get_path (file);
      GError *error = NULL;

      if (path != NULL && !tm_window_load (window, path, &error))
        {
          g_printerr ("timemachine: %s\n", error->message);
          g_error_free (error);
        }
      g_free (path);
    }
  gtk_window_present (GTK_WINDOW (window));

  if (self->simulate)
    tm_window_simulate (window);
  if (self->select != NULL)
    {
      TmRange r;
      if (tm_range_parse (self->select, &r))
        tm_window_select (window, &r);
      else
        g_printerr ("timemachine: not a range: %s\n", self->select);
    }
  if (self->screenshot != NULL)
    g_timeout_add (500, take_screenshot, self);
  return window;
}

static void
tm_application_activate (GApplication *app)
{
  open_window (TM_APPLICATION (app), NULL);
}

static void
tm_application_open (GApplication *app, GFile **files, int n_files, const char *hint)
{
  for (int i = 0; i < n_files; i++)
    open_window (TM_APPLICATION (app), files[i]);
}

static void
action_quit (GSimpleAction *action, GVariant *param, gpointer data)
{
  GList *windows = g_list_copy (gtk_application_get_windows (GTK_APPLICATION (data)));

  for (GList *l = windows; l != NULL; l = l->next)
    gtk_window_close (GTK_WINDOW (l->data));
  g_list_free (windows);
}

static void
load_css (void)
{
  GtkCssProvider *css = gtk_css_provider_new ();

  gtk_css_provider_load_from_resource (css, "/net/office42/timemachine/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (css);
}

static void
tm_application_startup (GApplication *app)
{
  static const GActionEntry actions[] = {
    { "quit", action_quit, NULL, NULL, NULL, { 0 } },
  };
  static const struct { const char *action; const char *accels[3]; } accels[] = {
    { "app.quit",           { "<Control>q", NULL } },
    { "win.new",            { "<Control>n", NULL } },
    { "win.open",           { "<Control>o", NULL } },
    { "win.save",           { "<Control>s", NULL } },
    { "win.save-as",        { "<Control><Shift>s", NULL } },
    { "win.simulate",       { "F5", NULL } },
    { "win.recalc",         { "F9", NULL } },
    { "win.functions",      { "F1", NULL } },
  };

  G_APPLICATION_CLASS (tm_application_parent_class)->startup (app);

  g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), app);
  for (guint i = 0; i < G_N_ELEMENTS (accels); i++)
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), accels[i].action,
                                           accels[i].accels);
  load_css ();
}

static int
tm_application_handle_local_options (GApplication *app, GVariantDict *options)
{
  TmApplication *self = TM_APPLICATION (app);
  const char *s;

  if (g_variant_dict_contains (options, "version"))
    {
      g_print ("timemachine %s\n", TM_VERSION);
      return 0;
    }
  if (g_variant_dict_lookup (options, "screenshot", "^&ay", &s))
    {
      char *cwd = g_get_current_dir ();

      self->screenshot = g_path_is_absolute (s) ? g_strdup (s) : g_build_filename (cwd, s, NULL);
      g_free (cwd);
    }
  if (g_variant_dict_lookup (options, "select", "&s", &s))
    self->select = g_strdup (s);
  if (g_variant_dict_lookup (options, "size", "&s", &s))
    sscanf (s, "%dx%d", &self->width, &self->height);
  self->simulate = g_variant_dict_contains (options, "simulate");
  return -1;
}

static void
tm_application_finalize (GObject *object)
{
  TmApplication *self = TM_APPLICATION (object);

  g_free (self->screenshot);
  g_free (self->select);
  G_OBJECT_CLASS (tm_application_parent_class)->finalize (object);
}

static void
tm_application_class_init (TmApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  G_OBJECT_CLASS (klass)->finalize = tm_application_finalize;
  app_class->activate = tm_application_activate;
  app_class->open = tm_application_open;
  app_class->startup = tm_application_startup;
  app_class->handle_local_options = tm_application_handle_local_options;
}

static void
tm_application_init (TmApplication *self)
{
  static const GOptionEntry entries[] = {
    { "simulate", 0, 0, G_OPTION_ARG_NONE, NULL, "Run the simulation once the file is open", NULL },
    { "select", 0, 0, G_OPTION_ARG_STRING, NULL, "Select a cell or range, such as B12 or B5:M5", "RANGE" },
    { "screenshot", 0, 0, G_OPTION_ARG_FILENAME, NULL, "Render the window to a PNG and quit", "FILE" },
    { "size", 0, 0, G_OPTION_ARG_STRING, NULL, "The window's size, such as 1280x800", "WxH" },
    { "version", 0, 0, G_OPTION_ARG_NONE, NULL, "Print the version and quit", NULL },
    { NULL }
  };

  g_application_add_main_option_entries (G_APPLICATION (self), entries);
}

TmApplication *
tm_application_new (void)
{
  /* Not unique: each run is its own process, so that a screenshot run
   * does not hand its work to a window already open. */
  return g_object_new (TM_TYPE_APPLICATION,
                       "application-id", "net.office42.timemachine",
                       "flags", G_APPLICATION_HANDLES_OPEN | G_APPLICATION_NON_UNIQUE,
                       NULL);
}
