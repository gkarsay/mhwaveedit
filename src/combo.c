/*
 * Copyright (C) 2004 2006 2009, Magnus Hjorth
 *
 * This file is part of mhWaveEdit.
 *
 * mhWaveEdit is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by        
 * the Free Software Foundation; either version 2 of the License, or  
 * (at your option) any later version.
 *
 * mhWaveEdit is distributed in the hope that it will be useful,   
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mhWaveEdit; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */


#include <config.h>

#include "combo.h"

G_DEFINE_TYPE(Combo,combo,GTK_TYPE_COMBO_BOX)

enum { CHANGED_SIGNAL, LAST_SIGNAL };
static guint combo_signals[LAST_SIGNAL] = { 0 };

static gboolean updating_flag = FALSE;

static void combo_size_request(GtkWidget *widget, GtkRequisition *req)
{
     Combo *obj = COMBO(widget);
     GTK_WIDGET_CLASS(combo_parent_class)->size_request(widget,req);
     if (obj->max_request_width >= 0 && req->width > obj->max_request_width)
	  req->width = obj->max_request_width;
}

static void combo_destroy(GtkObject *obj)
{
     Combo *combo = COMBO(obj);
     if (combo->strings) {
	  g_list_foreach(combo->strings, (GFunc)g_free, NULL);
	  g_list_free(combo->strings);
	  combo->strings = NULL;
     }
     GTK_OBJECT_CLASS(combo_parent_class)->destroy(obj);
}

static void combo_changed(GtkComboBox *combo)
{
     if (!updating_flag)
	  g_signal_emit(G_OBJECT(combo),combo_signals[CHANGED_SIGNAL],0);
     if (GTK_COMBO_BOX_CLASS(combo_parent_class)->changed)
	  GTK_COMBO_BOX_CLASS(combo_parent_class)->changed(combo);
}

static void combo_class_init(ComboClass *klass)
{
     GtkObjectClass *oc = GTK_OBJECT_CLASS(klass);
     GtkWidgetClass *wc = GTK_WIDGET_CLASS(klass);
     oc->destroy = combo_destroy;
     GTK_COMBO_BOX_CLASS(klass)->changed = combo_changed;
     klass->selection_changed = NULL;
     wc->size_request = combo_size_request;
     combo_signals[CHANGED_SIGNAL] = 
	  g_signal_new("selection_changed", G_TYPE_FROM_CLASS(klass),
	               G_SIGNAL_RUN_LAST,
		       G_STRUCT_OFFSET(ComboClass,selection_changed),
		       NULL, NULL,
		       g_cclosure_marshal_VOID__VOID,G_TYPE_NONE,0);
     gtk_object_class_add_signals(oc,combo_signals,LAST_SIGNAL);
}


static void combo_init(Combo *obj)
{
     /* Most of this was taken from the code for gtk_combo_box_new_text in 
      * GTK+ 2.4.13 */
  GtkWidget *combo_box = GTK_WIDGET(obj);
  GtkCellRenderer *cell;
  GtkListStore *store;
    
  store = gtk_list_store_new (1, G_TYPE_STRING);
  gtk_combo_box_set_model (GTK_COMBO_BOX(combo_box),GTK_TREE_MODEL (store));
  g_object_unref (store);

  cell = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), cell, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), cell,
                                  "text", 0,
                                  NULL);

  obj->strings = NULL;
  obj->max_request_width = -1;
}

void combo_set_items(Combo *combo, GList *item_strings, int default_index)
{
     GList *l;
     gchar *c;
     int len;
     updating_flag = TRUE;
     for (l=combo->strings; l!=NULL; l=l->next) {
	  g_free(l->data);
	  gtk_combo_box_remove_text(GTK_COMBO_BOX(combo),0);
     }
     g_list_free(combo->strings);
     combo->strings = NULL;
     for (l=item_strings,len=0; l!=NULL; l=l->next,len++) {
	  c = (gchar *)l->data;
	  gtk_combo_box_append_text(GTK_COMBO_BOX(combo),c);
	  combo->strings = g_list_append(combo->strings,g_strdup(c));
     }
     if (default_index >= len || default_index < 0) default_index = 0;
     gtk_combo_box_set_active(GTK_COMBO_BOX(combo),default_index);
     updating_flag = FALSE;
     g_signal_emit(G_OBJECT(combo),combo_signals[CHANGED_SIGNAL],0);
}

void combo_set_selection(Combo *combo, int item_index)
{
     gtk_combo_box_set_active(GTK_COMBO_BOX(combo),item_index);
}

int combo_selected_index(Combo *combo)
{
     return gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
}

char *combo_selected_string(Combo *combo)
{
     int i; 
     char *c;
     i = combo_selected_index(combo);
     /* j = g_list_length(combo->strings);
	printf("combo_selected_string: i=%d, len=%d\n",i,j); */
     c = g_strdup(g_list_nth_data(combo->strings,i));
     /* printf("                       c=%s\n",c); */
     return c;
}

void combo_remove_item(Combo *combo, int item_index)
{
     int i;
     gchar *c;
     i = combo_selected_index(combo);
     g_assert(i != item_index);
     gtk_combo_box_remove_text(GTK_COMBO_BOX(combo),item_index);
     c = (gchar *)g_list_nth_data(combo->strings,item_index);
     /* printf("Removing:  selected_index %d, string %s\n",i,c); */
     combo->strings = g_list_remove(combo->strings,c);
     g_free(c);
}

GtkWidget *combo_new(void)
{
     return (GtkWidget *)g_object_new(COMBO_TYPE, NULL);
}

void combo_set_max_request_width(Combo *c, int width)
{
     c->max_request_width = width;
}
