/*
    This file is part of darktable,
    Copyright (C) 2011-2022 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/collection.h"
#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/develop.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "dtgtk/button.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include "bauhaus/bauhaus.h"

DT_MODULE(1)

typedef struct dt_lib_tool_filter_t
{
  GtkWidget *stars;
  GtkWidget *comparator;
  GtkWidget *sort;
  GtkWidget *reverse;
  GtkWidget *text;
  GtkWidget *colors[6];
  GtkWidget *colors_op;
  int time_out;
  double last_key_time;
} dt_lib_tool_filter_t;

#ifdef USE_LUA
typedef enum dt_collection_sort_order_t
{
  DT_COLLECTION_SORT_ORDER_ASCENDING = 0,
  DT_COLLECTION_SORT_ORDER_DESCENDING
} dt_collection_sort_order_t;
#endif

/* proxy function to intelligently reset filter */
static void _lib_filter_reset(dt_lib_module_t *self, gboolean smart_filter);

/* callback for filter combobox change */
static void _lib_filter_combobox_changed(GtkWidget *widget, gpointer user_data);
/* callback for sort combobox change */
static void _lib_filter_sort_combobox_changed(GtkWidget *widget, gpointer user_data);
/* callback for reverse sort check button change */
static void _lib_filter_reverse_button_changed(GtkDarktableToggleButton *widget, gpointer user_data);
/* callback for rating comparator combobox change */
static void _lib_filter_comparator_changed(GtkWidget *widget, gpointer user_data);
/* updates the query and redraws the view */
static void _lib_filter_update_query(dt_lib_module_t *self, dt_collection_properties_t changed_property);
/* make sure that the comparator button matches what is shown in the filter dropdown */
static gboolean _lib_filter_sync_combobox_and_comparator(dt_lib_module_t *self);
/* save the images order if the first collect filter is on tag*/
static void _lib_filter_set_tag_order(dt_lib_module_t *self);
/* images order change from outside */
static void _lib_filter_images_order_change(gpointer instance, int order, dt_lib_module_t *self);

const dt_collection_sort_t items[] =
{
  DT_COLLECTION_SORT_FILENAME,
  DT_COLLECTION_SORT_DATETIME,
  DT_COLLECTION_SORT_IMPORT_TIMESTAMP,
  DT_COLLECTION_SORT_CHANGE_TIMESTAMP,
  DT_COLLECTION_SORT_EXPORT_TIMESTAMP,
  DT_COLLECTION_SORT_PRINT_TIMESTAMP,
  DT_COLLECTION_SORT_RATING,
  DT_COLLECTION_SORT_ID,
  DT_COLLECTION_SORT_COLOR,
  DT_COLLECTION_SORT_GROUP,
  DT_COLLECTION_SORT_PATH,
  DT_COLLECTION_SORT_CUSTOM_ORDER,
  DT_COLLECTION_SORT_TITLE,
  DT_COLLECTION_SORT_DESCRIPTION,
  DT_COLLECTION_SORT_ASPECT_RATIO,
  DT_COLLECTION_SORT_SHUFFLE,
};
#define NB_ITEMS (sizeof(items) / sizeof(dt_collection_sort_t))

static const char *_sort_names[]
  = { N_("filename"),
      N_("capture time"),
      N_("import time"),
      N_("last modification time"),
      N_("last export time"),
      N_("last print time"),
      N_("rating"),
      N_("id"),
      N_("color label"),
      N_("group"),
      N_("full path"),
      N_("custom sort"),
      N_("title"),
      N_("description"),
      N_("aspect ratio"),
      N_("shuffle"),
      NULL };

static int _filter_get_items(const dt_collection_sort_t sort)
{
  for(int i = 0; i < NB_ITEMS; i++)
  {
    if(sort == items[i])
    return i;
  }
  return 0;
}

const char *name(dt_lib_module_t *self)
{
  return _("filter");
}

const char **views(dt_lib_module_t *self)
{
  /* for now, show in all view due this affects filmroll too

     TODO: Consider to add flag for all views, which prevents
           unloading/loading a module while switching views.

   */
  static const char *v[] = {"*", NULL};
  return v;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_CENTER_TOP_CENTER;
}

int expandable(dt_lib_module_t *self)
{
  return 0;
}

int position()
{
  return 2001;
}

static void _set_widget_dimmed(GtkWidget *widget, const gboolean dimmed)
{
  GtkStyleContext *context = gtk_widget_get_style_context(widget);
  if(dimmed) gtk_style_context_add_class(context, "dt_dimmed");
  else gtk_style_context_remove_class(context, "dt_dimmed");
  gtk_widget_queue_draw(GTK_WIDGET(widget));
}

static char *_encode_text_filter(const char *entry)
{
  // by default adds start and end wildcard
  // " removes the corresponding wildcard
  char start[2] = {0};
  char *text = NULL;
  char *p = (char *)entry;
  if(strlen(entry) > 1 && !(entry[0] == '"' && entry[1] == '"'))
  {
    if(entry[0] == '"')
      p++;
    else if(entry[0])
      start[0] = '%';
    if(entry[strlen(entry) - 1] == '"')
    {
      text = g_strconcat(start, (char *)p, NULL);
      text[strlen(text) - 1] = '\0';
    }
    else if(entry[0])
      text = g_strconcat(start, (char *)p, "%", NULL);
  }
  return text;
}

static char *_decode_text_filter(const char *text)
{
  // revert the encoded filter for display
  char start[2] = {0};
  char *text1 = g_strdup(text);
  char *p = text1;
  char *text2;
  if(text1[0])
  {
    if(text1[0] == '%')
      p++;
    else
      start[0] = '\"';
    if(strlen(text1) > 1 && text1[strlen(text1) - 1] == '%')
    {
      text1[strlen(text1) - 1] = '\0';
      text2 = g_strconcat(start, (char *)p, NULL);
    }
    else
      text2 = g_strconcat(start, (char *)p, "\"", NULL);
    g_free(text1);
    return text2;
  }
  else return text1;
}

static gboolean _text_entry_changed_wait(gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  if(d->time_out)
  {
    d->time_out--;
    double clock = dt_get_wtime();
    if(clock - d->last_key_time >= 0.4)
    {
      d->time_out = 1; // force the query execution
      d->last_key_time = clock;
    }

    if(d->time_out == 1)
    { // tell we are busy
      _set_widget_dimmed(d->text, TRUE);
    }
    else if(!d->time_out)
    {
      char *text = _encode_text_filter(gtk_entry_get_text(GTK_ENTRY(d->text)));

      // avoids activating twice the same query
      if(g_strcmp0(dt_collection_get_text_filter(darktable.collection), text))
      {
        dt_collection_set_text_filter(darktable.collection, text);
        dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_SORT, NULL);
      }
      else g_free(text);
      _set_widget_dimmed(d->text, FALSE);
      return FALSE;
    }
  }
  return TRUE;
}

static void _launch_text_query(dt_lib_module_t *self)
{
  // two timeouts 1) 0.4 sec after the last key, 2) 1.5 sec of successive keys
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  d->last_key_time = dt_get_wtime();
  if(!d->time_out)
  {
    d->time_out = 15;
    g_timeout_add(100, _text_entry_changed_wait, self);
  }
}

static void _text_entry_changed(GtkEntry *entry, dt_lib_module_t *self)
{
    _launch_text_query(self);
}

static void _reset_text_filter(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  dt_collection_set_text_filter(darktable.collection, strdup(""));
  gtk_entry_set_text(GTK_ENTRY(d->text), "");
}

static void _reset_text_entry(GtkButton *button, dt_lib_module_t *self)
{
  _reset_text_filter(self);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_SORT, NULL);
}

#define CPF_USER_DATA_INCLUDE CPF_USER_DATA
#define CPF_USER_DATA_EXCLUDE CPF_USER_DATA << 1
#define CL_AND_MASK 0x80000000
#define CL_ALL_EXCLUDED 0x3F000
#define CL_ALL_INCLUDED 0x3F

static void _update_colors_filter(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  int mask = dt_collection_get_colors_filter(darktable.collection);
  int mask_excluded = 0x1000;
  int mask_included = 1;
  int nb = 0;
  for(int i = 0; i <= DT_COLORLABELS_LAST; i++)
  {
    const int i_mask = mask & mask_excluded ? CPF_USER_DATA_EXCLUDE : mask & mask_included ? CPF_USER_DATA_INCLUDE : 0;
    dtgtk_button_set_paint(DTGTK_BUTTON(d->colors[i]), dtgtk_cairo_paint_label_sel,
                          (i | i_mask | CPF_BG_TRANSPARENT), NULL);
    gtk_widget_queue_draw(d->colors[i]);
    if((mask & mask_excluded) || (mask & mask_included))
      nb++;
    mask_excluded <<= 1;
    mask_included <<= 1;
  }
  if(nb <= 1)
  {
    mask |= CL_AND_MASK;
    dt_collection_set_colors_filter(darktable.collection, mask);
  }
  dtgtk_button_set_paint(DTGTK_BUTTON(d->colors_op),
                         (mask & CL_AND_MASK) ? dtgtk_cairo_paint_and : dtgtk_cairo_paint_or,
                         CPF_STYLE_FLAT, NULL);
  gtk_widget_set_sensitive(d->colors_op, nb > 1);
}

static void _reset_colors_filter(dt_lib_module_t *self)
{
  dt_collection_set_colors_filter(darktable.collection, CL_AND_MASK);
}

static gboolean _colorlabel_clicked(GtkWidget *w, GdkEventButton *e, dt_lib_module_t *self)
{
  const int mask = dt_collection_get_colors_filter(darktable.collection);
  const int k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "colors_index"));
  int mask_k = (1 << k) | (1 << (k + 12));
  if(k == DT_COLORLABELS_LAST)
  {
    if(mask & mask_k)
      mask_k = 0;
    else if(dt_modifier_is(e->state, GDK_CONTROL_MASK))
      mask_k = CL_ALL_EXCLUDED;
    else if(dt_modifier_is(e->state, 0))
      mask_k = CL_ALL_INCLUDED;
    dt_collection_set_colors_filter(darktable.collection, mask_k | (mask & CL_AND_MASK));
  }
  else
  {
    if(mask & mask_k)
      mask_k = 0;
    else if(dt_modifier_is(e->state, GDK_CONTROL_MASK))
      mask_k = 1 << (k + 12);
    else if(dt_modifier_is(e->state, 0))
      mask_k = 1 << k;
    dt_collection_set_colors_filter(darktable.collection, (mask & ~((1 << k) | (1 << (k + 12)))) | mask_k);
  }
  _update_colors_filter(self);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_COLORLABEL, NULL);
  return FALSE;
}

static void _colors_operation_clicked(GtkWidget *w, dt_lib_module_t *self)
{
  const int mask = dt_collection_get_colors_filter(darktable.collection);
  dt_collection_set_colors_filter(darktable.collection, mask ^ CL_AND_MASK);
  _update_colors_filter(self);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_COLORLABEL, NULL);
}

#undef CPF_USER_DATA_INCLUDE
#undef CPF_USER_DATA_EXCLUDE
#undef CL_AND_MASK
#undef CL_ALL_EXCLUDED
#undef CL_ALL_INCLUDED

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)g_malloc0(sizeof(dt_lib_tool_filter_t));
  self->data = (void *)d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(self->widget, GTK_ALIGN_START);
  gtk_widget_set_valign(self->widget, GTK_ALIGN_CENTER);

  GtkWidget *label = gtk_label_new(C_("quickfilter", "filter"));
  gtk_box_pack_start(GTK_BOX(self->widget), label, TRUE, TRUE, 0);

  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 4);
  GtkWidget *overlay = gtk_overlay_new();

  DT_BAUHAUS_COMBOBOX_NEW_FULL(d->comparator, self, NULL, NULL,
                               _("filter by images rating"),
                               dt_collection_get_rating_comparator(darktable.collection),
                               _lib_filter_comparator_changed, self,
                               "<", // DT_COLLECTION_RATING_COMP_LT = 0,
                               "≤", // DT_COLLECTION_RATING_COMP_LEQ,
                               "=", // DT_COLLECTION_RATING_COMP_EQ,
                               "≥", // DT_COLLECTION_RATING_COMP_GEQ,
                               ">", // DT_COLLECTION_RATING_COMP_GT,
                               "≠");// DT_COLLECTION_RATING_COMP_NE,
  gtk_widget_set_halign(d->comparator, GTK_ALIGN_START);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), d->comparator);
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), d->comparator, TRUE);

  /* create the filter combobox */
  DT_BAUHAUS_COMBOBOX_NEW_FULL(d->stars, self, NULL, NULL,
                               _("filter by images rating"),
                               dt_collection_get_rating(darktable.collection),
                               _lib_filter_combobox_changed, self,
                               N_("all"),
                               N_("unstarred only"),
                               "★",
                               "★ ★",
                               "★ ★ ★",
                               "★ ★ ★ ★",
                               "★ ★ ★ ★ ★",
                               N_("rejected only"),
                               N_("all except rejected"));
  gtk_container_add(GTK_CONTAINER(overlay), d->stars);
  gtk_box_pack_start(GTK_BOX(hbox), overlay, FALSE, FALSE, 0);
  GtkStyleContext *context = gtk_widget_get_style_context(hbox);
  gtk_style_context_add_class(context, "quick_filter_box");

  // colorlabels filter
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  for(int k = 0; k < DT_COLORLABELS_LAST + 1; k++)
  {
    d->colors[k] = dtgtk_button_new(dtgtk_cairo_paint_label_sel, (k | CPF_BG_TRANSPARENT), NULL);
    g_object_set_data(G_OBJECT(d->colors[k]), "colors_index", GINT_TO_POINTER(k));
    gtk_box_pack_start(GTK_BOX(hbox), d->colors[k], FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(d->colors[k], _("filter by images color label"
                                                "\nclick to toggle the color label selection"
                                                "\nctrl+click to exclude the color label"
                                                "\nthe gray button affects all color labels"));
    g_signal_connect(G_OBJECT(d->colors[k]), "button-press-event", G_CALLBACK(_colorlabel_clicked), self);
  }
  d->colors_op = dtgtk_button_new(dtgtk_cairo_paint_and, CPF_STYLE_FLAT, NULL);
  _update_colors_filter(self);
  gtk_box_pack_start(GTK_BOX(hbox), d->colors_op, FALSE, FALSE, 2);
  gtk_widget_set_tooltip_text(d->colors_op, _("filter by images color label"
                                              "\nand (∩): images having all selected color labels"
                                              "\nor (∪): images with at least one of the selected color labels"));
  g_signal_connect(G_OBJECT(d->colors_op), "clicked", G_CALLBACK(_colors_operation_clicked), self);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, FALSE, FALSE, 2);
  context = gtk_widget_get_style_context(hbox);
  gtk_style_context_add_class(context, "quick_filter_box");
  gtk_style_context_add_class(context, "dt_font_resize_07");

  // text filter
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 4);
  d->text = gtk_search_entry_new();
  char *text = _decode_text_filter(dt_collection_get_text_filter(darktable.collection));
  gtk_entry_set_text(GTK_ENTRY(d->text), text);
  g_free(text);
  g_signal_connect(G_OBJECT(d->text), "search-changed", G_CALLBACK(_text_entry_changed), self);
  g_signal_connect(G_OBJECT(d->text), "stop-search", G_CALLBACK(_reset_text_entry), self);
  gtk_entry_set_width_chars(GTK_ENTRY(d->text), 14);
  gtk_widget_set_tooltip_text(d->text,
          /* xgettext:no-c-format */
                              _("filter by text from images metadata, tags, file path and name"
          /* xgettext:no-c-format */
                                "\n`%' is the wildcard character"
          /* xgettext:no-c-format */
                                "\nby default start and end wildcards are auto-applied"
          /* xgettext:no-c-format */
                                "\nstarting or ending with a double quote disables the corresponding wildcard"
          /* xgettext:no-c-format */
                                "\nis dimmed during the search execution"));
  context = gtk_widget_get_style_context(d->text);
  gtk_style_context_add_class(context, "dt_transparent_background");
  gtk_box_pack_start(GTK_BOX(hbox), d->text, FALSE, FALSE, 0);
  context = gtk_widget_get_style_context(hbox);
  gtk_style_context_add_class(context, "quick_filter_box");

  /* sort combobox */
  label = gtk_label_new(_("sort by"));
  gtk_box_pack_start(GTK_BOX(self->widget), label, TRUE, TRUE, 0);

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 4);
  const dt_collection_sort_t sort = dt_collection_get_sort_field(darktable.collection);
  d->sort = dt_bauhaus_combobox_new_full(DT_ACTION(self), NULL, NULL,
                                         _("determine the sort order of shown images"),
                                         _filter_get_items(sort), _lib_filter_sort_combobox_changed, self,
                                         _sort_names);
  gtk_box_pack_start(GTK_BOX(hbox), d->sort, FALSE, FALSE, 0);
  context = gtk_widget_get_style_context(hbox);
  gtk_style_context_add_class(context, "quick_filter_box");
  gtk_style_context_add_class(context, "dt_font_resize_07");

  /* reverse order checkbutton */
  d->reverse = dtgtk_togglebutton_new(dtgtk_cairo_paint_sortby, CPF_DIRECTION_UP, NULL);
  if(darktable.collection->params.descending)
    dtgtk_togglebutton_set_paint(DTGTK_TOGGLEBUTTON(d->reverse), dtgtk_cairo_paint_sortby,
                                 CPF_DIRECTION_DOWN, NULL);
  gtk_box_pack_start(GTK_BOX(hbox), d->reverse, FALSE, FALSE, 0);
  context = gtk_widget_get_style_context(d->reverse);
  gtk_style_context_add_class(context, "dt_transparent_background");

  /* select the last value and connect callback */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->reverse),
                               dt_collection_get_sort_descending(darktable.collection));
  g_signal_connect(G_OBJECT(d->reverse), "toggled", G_CALLBACK(_lib_filter_reverse_button_changed),
                   (gpointer)self);

  /* initialize proxy */
  darktable.view_manager->proxy.filter.module = self;
  darktable.view_manager->proxy.filter.reset_filter = _lib_filter_reset;

  g_signal_connect_swapped(G_OBJECT(d->comparator), "map",
                           G_CALLBACK(_lib_filter_sync_combobox_and_comparator), self);

  DT_DEBUG_CONTROL_SIGNAL_CONNECT(darktable.signals, DT_SIGNAL_IMAGES_ORDER_CHANGE,
                            G_CALLBACK(_lib_filter_images_order_change), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_collection_set_text_filter(darktable.collection, NULL);
  g_free(self->data);
  self->data = NULL;
}

// show/hide the comparator dropdown as required
static gboolean _lib_filter_sync_combobox_and_comparator(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  const int filter = dt_bauhaus_combobox_get(d->stars);

  // 0 all
  // 1 unstarred only
  // 2 ★
  // 3 ★ ★
  // 4 ★ ★ ★
  // 5 ★ ★ ★ ★
  // 6 ★ ★ ★ ★ ★
  // 7 rejected only
  // 8 all except rejected

  gtk_widget_set_visible(d->comparator, filter > 1 && filter < 7);

  return FALSE;
}

static void _lib_filter_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  /* update last settings */
  const int i = dt_bauhaus_combobox_get(widget);

  uint32_t flags = dt_collection_get_filter_flags(darktable.collection)
    & ~(COLLECTION_FILTER_REJECTED | COLLECTION_FILTER_ALTERED | COLLECTION_FILTER_UNALTERED);

  /* update collection star filter flags */
  if(i == DT_COLLECTION_FILTER_ALL) // all
    flags &= ~(COLLECTION_FILTER_ATLEAST_RATING
               | COLLECTION_FILTER_EQUAL_RATING
               | COLLECTION_FILTER_CUSTOM_COMPARE);
  else if(i == DT_COLLECTION_FILTER_STAR_NO) // unstarred only
    flags = (flags | COLLECTION_FILTER_EQUAL_RATING) & ~(COLLECTION_FILTER_ATLEAST_RATING
                                                         | COLLECTION_FILTER_CUSTOM_COMPARE);
  else if(i == DT_COLLECTION_FILTER_REJECT) // rejected only
    flags = (flags & ~(COLLECTION_FILTER_ATLEAST_RATING
                       | COLLECTION_FILTER_EQUAL_RATING
                       | COLLECTION_FILTER_CUSTOM_COMPARE))
      | COLLECTION_FILTER_REJECTED;
  else if(i == DT_COLLECTION_FILTER_NOT_REJECT) // all except rejected
    flags = (flags | COLLECTION_FILTER_ATLEAST_RATING) & ~COLLECTION_FILTER_CUSTOM_COMPARE;
  else // explicit stars
    flags |= COLLECTION_FILTER_CUSTOM_COMPARE;

  dt_collection_set_filter_flags(darktable.collection, flags);

  /* set the star filter in collection */
  dt_collection_set_rating(darktable.collection, i);
  dt_control_set_mouse_over_id(-1); // maybe we are storing mouse_over_id (arrows)

  /* update the gui accordingly */
  _lib_filter_sync_combobox_and_comparator(user_data);

  /* update the query and view */
  _lib_filter_update_query(user_data, DT_COLLECTION_PROP_RATING);
}

/* save the images order if the first collect filter is on tag*/
static void _lib_filter_set_tag_order(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  if(darktable.collection->tagid)
  {
    const uint32_t sort = items[dt_bauhaus_combobox_get(d->sort)];
    const gboolean descending = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->reverse));
    dt_tag_set_tag_order_by_id(darktable.collection->tagid, sort, descending);
  }
}

static void _lib_filter_images_order_change(gpointer instance, const int order, dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  dt_bauhaus_combobox_set(d->sort, _filter_get_items(order & ~DT_COLLECTION_ORDER_FLAG));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->reverse), order & DT_COLLECTION_ORDER_FLAG);
}

static void _lib_filter_reverse_button_changed(GtkDarktableToggleButton *widget, gpointer user_data)
{
  const gboolean reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if(reverse)
    dtgtk_togglebutton_set_paint(widget, dtgtk_cairo_paint_sortby, CPF_DIRECTION_DOWN, NULL);
  else
    dtgtk_togglebutton_set_paint(widget, dtgtk_cairo_paint_sortby, CPF_DIRECTION_UP, NULL);
  gtk_widget_queue_draw(GTK_WIDGET(widget));

  /* update last settings */
  dt_collection_set_sort(darktable.collection, DT_COLLECTION_SORT_NONE, reverse);

  /* save the images order */
  _lib_filter_set_tag_order(user_data);

  /* update query and view */
  _lib_filter_update_query(user_data, DT_COLLECTION_PROP_SORT);
}

static void _lib_filter_comparator_changed(GtkWidget *widget, gpointer user_data)
{
  dt_collection_set_rating_comparator(darktable.collection, dt_bauhaus_combobox_get(widget));

  _lib_filter_update_query(user_data, DT_COLLECTION_PROP_RATING);
}

static void _lib_filter_sort_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  /* update the ui last settings */
  dt_collection_set_sort(darktable.collection, items[dt_bauhaus_combobox_get(widget)], -1);

  /* save the images order */
  _lib_filter_set_tag_order(user_data);

  /* update the query and view */
  _lib_filter_update_query(user_data, DT_COLLECTION_PROP_SORT);
}

static void _lib_filter_update_query(dt_lib_module_t *self, dt_collection_properties_t changed_property)
{
  /* sometimes changes */
  dt_collection_set_query_flags(darktable.collection, COLLECTION_QUERY_FULL);

  /* updates query */
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, changed_property, NULL);
}

static void _reset_stars_filter(dt_lib_module_t *self, gboolean smart_filter)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;

  if(smart_filter == TRUE)
  {
    /* initial import rating setting */
    const int initial_rating = dt_conf_get_int("ui_last/import_initial_rating");

    /* current selection in filter dropdown */
    const int current_filter = dt_bauhaus_combobox_get(d->stars);

    /* convert filter dropdown to rating: 2-6 is 1-5 stars, for anything else, assume 0 stars */
    const int current_filter_rating = (current_filter >= 2 && current_filter <= 6) ? current_filter - 1 : 0;

    /* new filter is the lesser of the initial rating and the current filter rating */
    const int new_filter_rating = MIN(initial_rating, current_filter_rating);

    /* convert new filter rating to filter dropdown selector */
    const int new_filter = (new_filter_rating >= 1 && new_filter_rating <= 5) ? new_filter_rating + 1
                                                                              : new_filter_rating;

    /* Reset to new filter dropdown item */
    dt_bauhaus_combobox_set(d->stars, new_filter);
  }
  else
  {
    /* Reset to topmost item, 'all' */
    dt_bauhaus_combobox_set(d->stars, 0);
  }
}

static void _lib_filter_reset(dt_lib_module_t *self, gboolean smart_filter)
{
  _reset_stars_filter(self, smart_filter);
  _reset_text_filter(self);
  _reset_colors_filter(self);
}

static gboolean _reset_filters(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                               GdkModifierType modifier, dt_lib_module_t *self)
{
  _lib_filter_reset(self, FALSE);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_SORT, NULL);
  return TRUE;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "reset filters"), 0, 0);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_accel_connect_lib(self, "reset filters", g_cclosure_new(G_CALLBACK(_reset_filters), self, NULL));
}

#ifdef USE_LUA
static int sort_cb(lua_State *L)
{
  dt_lib_module_t *self = lua_touserdata(L, lua_upvalueindex(1));
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  const dt_collection_sort_t tmp = dt_collection_get_sort_field(darktable.collection);

  if(lua_gettop(L) > 0)
  {
    dt_collection_sort_t value;
    luaA_to(L,dt_collection_sort_t,&value,1);
    dt_collection_set_sort(darktable.collection, (uint32_t)value, 0);
    const dt_collection_sort_t sort = dt_collection_get_sort_field(darktable.collection);
    dt_bauhaus_combobox_set(d->sort, _filter_get_items(sort));
    _lib_filter_update_query(self, DT_COLLECTION_PROP_SORT);
  }
  luaA_push(L, dt_collection_sort_t, &tmp);
  return 1;
}
static int sort_order_cb(lua_State *L)
{
  dt_lib_module_t *self = lua_touserdata(L, lua_upvalueindex(1));
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  const gboolean tmp = dt_collection_get_sort_descending(darktable.collection);

  if(lua_gettop(L) > 0)
  {
    dt_collection_sort_order_t value;
    luaA_to(L,dt_collection_sort_order_t,&value,1);
    dt_collection_sort_t sort_value = dt_collection_get_sort_field(darktable.collection);
    dt_collection_set_sort(darktable.collection, sort_value, value);
    const dt_collection_sort_t sort = dt_collection_get_sort_field(darktable.collection);
    dt_bauhaus_combobox_set(d->sort, _filter_get_items(sort));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->reverse),
                               dt_collection_get_sort_descending(darktable.collection));
    _lib_filter_update_query(self, DT_COLLECTION_PROP_SORT);
  }
  luaA_push(L, dt_collection_sort_order_t, &tmp);
  return 1;
}
static int rating_cb(lua_State *L)
{
  dt_lib_module_t *self = lua_touserdata(L, lua_upvalueindex(1));
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  const dt_collection_filter_t tmp = dt_collection_get_rating(darktable.collection);

  if(lua_gettop(L) > 0)
  {
    dt_collection_filter_t value;
    luaA_to(L,dt_collection_filter_t,&value,1);
    dt_collection_set_rating(darktable.collection, (uint32_t)value);
    dt_bauhaus_combobox_set(d->stars, dt_collection_get_rating(darktable.collection));
    _lib_filter_update_query(self, DT_COLLECTION_PROP_RATING);
  }
  luaA_push(L, dt_collection_filter_t, &tmp);
  return 1;
}
static int rating_comparator_cb(lua_State *L)
{
  dt_lib_module_t *self = lua_touserdata(L, lua_upvalueindex(1));
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  const dt_collection_rating_comperator_t tmp = dt_collection_get_rating_comparator(darktable.collection);

  if(lua_gettop(L) > 0)
  {
    dt_collection_rating_comperator_t value;
    luaA_to(L,dt_collection_rating_comperator_t,&value,1);
    dt_collection_set_rating_comparator(darktable.collection, (uint32_t)value);
    dt_bauhaus_combobox_set(d->comparator, dt_collection_get_rating_comparator(darktable.collection));
    _lib_filter_update_query(self, DT_COLLECTION_PROP_RATING);
  }
  luaA_push(L, dt_collection_rating_comperator_t, &tmp);
  return 1;
}

void init(struct dt_lib_module_t *self)
{
  lua_State *L = darktable.lua_state.state;
  int my_type = dt_lua_module_entry_get_type(L, "lib", self->plugin_name);
  lua_pushlightuserdata(L, self);
  lua_pushcclosure(L, sort_cb, 1);
  dt_lua_gtk_wrap(L);
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  dt_lua_type_register_const_type(L, my_type, "sort");
  lua_pushlightuserdata(L, self);
  lua_pushcclosure(L, sort_order_cb,1);
  dt_lua_gtk_wrap(L);
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  dt_lua_type_register_const_type(L, my_type, "sort_order");
  lua_pushlightuserdata(L, self);
  lua_pushcclosure(L, rating_cb,1);
  dt_lua_gtk_wrap(L);
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  dt_lua_type_register_const_type(L, my_type, "rating");
  lua_pushlightuserdata(L, self);
  lua_pushcclosure(L, rating_comparator_cb,1);
  dt_lua_gtk_wrap(L);
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  dt_lua_type_register_const_type(L, my_type, "rating_comparator");

  luaA_enum(L,dt_collection_sort_t);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_NONE);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_FILENAME);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_DATETIME);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_IMPORT_TIMESTAMP);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_CHANGE_TIMESTAMP);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_EXPORT_TIMESTAMP);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_PRINT_TIMESTAMP);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_RATING);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_ID);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_COLOR);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_GROUP);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_PATH);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_CUSTOM_ORDER);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_TITLE);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_DESCRIPTION);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_ASPECT_RATIO);
  luaA_enum_value(L,dt_collection_sort_t,DT_COLLECTION_SORT_SHUFFLE);

  luaA_enum(L,dt_collection_filter_t);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_ALL);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_STAR_NO);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_STAR_1);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_STAR_2);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_STAR_3);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_STAR_4);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_STAR_5);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_REJECT);
  luaA_enum_value(L,dt_collection_filter_t,DT_COLLECTION_FILTER_NOT_REJECT);

  luaA_enum(L,dt_collection_sort_order_t);
  luaA_enum_value(L,dt_collection_sort_order_t,DT_COLLECTION_SORT_ORDER_ASCENDING);
  luaA_enum_value(L,dt_collection_sort_order_t,DT_COLLECTION_SORT_ORDER_DESCENDING);

  luaA_enum(L,dt_collection_rating_comperator_t);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_COMP_LT);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_COMP_LEQ);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_COMP_EQ);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_COMP_GEQ);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_COMP_GT);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_COMP_NE);
  luaA_enum_value(L,dt_collection_rating_comperator_t,DT_COLLECTION_RATING_N_COMPS);

}
#endif

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
