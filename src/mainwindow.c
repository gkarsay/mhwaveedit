/*
 * Copyright (C) 2002 2003 2004 2005, Magnus Hjorth
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 */


#include <config.h>

#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "gtkfiles.h"
#include "filetypes.h"
#include "chunkview.h"
#include "mainwindow.h"
#include "recorddialog.h"
#include "configdialog.h"
#include "gotodialog.h"
#include "chunk.h"
#include "um.h"
#include "main.h"
#include "inifile.h"
#include "player.h"
#include "sound.h"
#include "effectbrowser.h"
#include "soxdialog.h"
#include "help.h"
#include "ladspadialog.h"
#include "gettext.h"

#include "button_open.xpm"
#include "button_save.xpm"
#include "button_undo.xpm"
#include "button_redo.xpm"
#include "button_cut.xpm"
#include "button_copy.xpm"
#include "button_paste.xpm"
#include "button_pasteover.xpm"
#include "button_delete.xpm"
#include "button_cursorstart.xpm"
#include "button_cursorend.xpm"
#include "button_play.xpm"
#include "button_playselection.xpm"
#include "button_stop.xpm"
#include "button_loop.xpm"
#include "button_follow.xpm"
#include "button_record.xpm"
#include "button_mixer.xpm"
#include "button_bounce.xpm"
#include "icon.xpm"
#include "vzoom.xpm"
#include "hzoom.xpm"
#include "speed.xpm"

/* #define SHOW_DEBUG_MENU */

ListObject *mainwindow_objects = NULL;

static gboolean window_geometry_stack_inited = FALSE;
static GSList *window_geometry_stack = NULL;
static GList *recent_filenames = NULL;
static GtkObjectClass *parent_class;
static Chunk *clipboard = NULL;
gboolean autoplay_mark_flag = FALSE;
gboolean varispeed_reset_flag = FALSE;

static void mainwindow_view_changed(Document *d, gpointer user_data);
static void mainwindow_selection_changed(Document *d, gpointer user_data);
static void mainwindow_cursor_changed(Document *d, gboolean rolling, 
				      gpointer user_data);
static void mainwindow_state_changed(Document *d, gpointer user_data);



static void load_recent(void)
{
     guint i;
     gchar *c,*d;
     g_assert(recent_filenames == NULL);
     for (i=1; ; i++) {
	  c = g_strdup_printf("recentFile%d",i);
	  d = inifile_get(c,NULL);
	  g_free(c);
	  if (d == NULL) break;
	  recent_filenames = g_list_append(recent_filenames, g_strdup(d));
     }
}

static void save_recent(void)
{
     guint i,j;
     GList *l;
     gchar *c;
     j = inifile_get_guint32("recentFiles",4);
     if (j > MAINWINDOW_RECENT_MAX) j=4;
     for (i=1,l=recent_filenames; i<=j && l!=NULL; i++,l=l->next) {
	  c = g_strdup_printf("recentFile%d",i);
	  inifile_set(c,l->data);
	  g_free(c);
     }
}

static void update_file_recent(Mainwindow *w)
{
     guint i = 1;
     GList *l = recent_filenames;
     GList *m = w->recent;
     gchar *c,*d;
     for (; m!=NULL && l!=NULL; m=m->next,l=l->next,i++) {
	  d = namepart((gchar *)l->data);
	  c = g_strdup_printf("%d. %s",i,d);
	  gtk_label_set_text(GTK_LABEL(GTK_BIN(m->data)->child),c);
	  gtk_widget_set_sensitive(GTK_WIDGET(m->data),TRUE);
	  g_free(c);
     }
     for (; m!=NULL; m=m->next,i++) {
	  c = g_strdup_printf("%d.",i);
	  gtk_label_set_text(GTK_LABEL(GTK_BIN(m->data)->child),c);
	  gtk_widget_set_sensitive(GTK_WIDGET(m->data),FALSE);
	  g_free(c);
     }
}

static void recent_file(gchar *filename)
{
     GList *l = recent_filenames;
     /* Remove other reference to this file, if it exists */
     while (l!=NULL) {
	  if (!strcmp((gchar *)l->data,filename)) {
	       recent_filenames = g_list_remove_link(recent_filenames,l);
	       g_free(l->data);
	       g_list_free_1(l);
	       break;
	  } else
	       l = l->next;
     }
     /* Put file at top */
     recent_filenames = g_list_prepend(recent_filenames,g_strdup(filename));
     list_object_foreach(mainwindow_objects,(GFunc)update_file_recent,NULL);
     save_recent();
}

static void update_desc(Mainwindow *w);

static void set_sensitive(GList *l, gboolean s)
{
     while (l != NULL) {
	  gtk_widget_set_sensitive(GTK_WIDGET(l->data),s);
	  l = l->next;
     }
}

/* This function makes the window un-sensitive and brings it to front.*/
static void procstart(StatusBar *bar, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     mainwindow_set_sensitive(w,FALSE);
     w->esc_pressed_flag = FALSE;
     if (GTK_WIDGET(w)->window)
	  gdk_window_raise(GTK_WIDGET(w)->window);
     gtk_grab_add(GTK_WIDGET(w));
}

static void procend(StatusBar *bar, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     mainwindow_set_sensitive(w,TRUE);
     mainwindow_update_texts();
     gtk_grab_remove(GTK_WIDGET(w));
}

static void fix_title(Mainwindow *wnd)
{
     gchar *c;
     if (wnd->doc != NULL) {
	  if (wnd->doc->chunk->format.type == DATAFORMAT_FLOAT)
	       
	       c = g_strdup_printf ( _("mhWaveEdit: %s (%s): %d Hz, "
				     "%s"), 
				     wnd->doc->titlename, 
				     chunk_get_time(wnd->doc->chunk,
						    wnd->doc->chunk->length,
						    NULL),
				     wnd->doc->chunk->format.samplerate,
				     (wnd->doc->chunk->format.samplesize >
				      sizeof(float))?_("double"):_("float"));
	  else
	       c = g_strdup_printf ( _("mhWaveEdit: %s (%s): %d Hz, "
				     "%d bit"), 
				     wnd->doc->titlename, 
				     chunk_get_time(wnd->doc->chunk,
						    wnd->doc->chunk->length,
						    NULL),
				     wnd->doc->chunk->format.samplerate,
				     wnd->doc->chunk->format.samplesize*8);

     } else 
	  c = g_strdup(PROGRAM_VERSION_STRING);
     gtk_window_set_title ( GTK_WINDOW ( wnd ), c );
     g_free ( c );
}

static void update_desc(Mainwindow *w)
{
     if (w->doc != NULL)
	  status_bar_set_info(w->statusbar,w->doc->cursorpos,
			      (playing_document==w->doc),w->doc->viewstart,
			      w->doc->viewend,w->doc->selstart,
			      w->doc->selend,
			      w->doc->chunk->format.samplerate,
			      w->doc->chunk->length);
     else
	  status_bar_reset(w->statusbar);
			      
}


static gchar *get_save_filename(gchar *old_filename, gchar *title_text)
{
     gchar *lsf,*lsd,*filename;
     gchar *c,*d;

     /* Get the lastSaveFile entry */
     lsf = inifile_get("lastSaveFile",NULL);
     /* If we didn't have a lastSaveFile entry, assume we want to
	save into the same directory as we loaded from. */
     if (lsf == NULL) lsf = inifile_get("lastOpenFile",NULL);
     /* Get the directory part and copy into lsd */
     if (lsf != NULL) {
	  lsd = g_strdup(lsf);
	  c = strrchr(lsd,'/');
	  if (c != NULL) c[1]=0;
	  else {
	       g_free(lsd);
	       lsd = NULL;
	  }
     } else lsd = NULL;
     
     if (lsd == NULL)
	  filename = get_filename ( old_filename, "*.wav", title_text, TRUE );
     else if (old_filename == NULL)
	  filename = get_filename ( lsd, "*.wav", title_text, TRUE );
     else { 
	  c = strrchr(old_filename, '/');
	  if (!c) c=old_filename;
	  else c = c+1;
	  d = g_strjoin("/",lsd,c,NULL);
	  filename = get_filename( d, "*.wav", title_text, TRUE);
	  g_free(d);
     }
     g_free(lsd);
     return filename;
     
}

static gboolean mainwindow_save ( Mainwindow *w, gchar *filename )
{
     gboolean r;
     gboolean fmal = FALSE; /* TRUE if filename should be g_free:d */    

     if (!document_can_undo(w->doc))
	  if (user_message(_("The file has not changed since last save. Press OK"
			   " if you want to save it anyway?"),UM_OKCANCEL) == 
	      MR_CANCEL) 
	       return FALSE;

     /* If there is no filename provided, get a filename from a dialog
      * box. Also set the fmal flag so we remember to free the
      * filename later. */  
     if (filename == NULL) {
	  filename = get_save_filename(w->doc->filename,_("Save File"));
	  if (filename == NULL) return TRUE;
	  fmal = TRUE;
     }

     g_assert(filename != NULL);
     /* Save the file */
     r = document_save(w->doc, filename);
     if (r) { if (fmal) g_free(filename); return TRUE; }

     inifile_set("lastSaveFile",filename);
     recent_file(filename);
     if (fmal) g_free(filename);

     return FALSE;
}

static gboolean change_check ( Mainwindow *w )
{
     gint i;
     gchar *c;
     if (w->doc == NULL || 
	 (w->doc->filename != NULL && !document_can_undo(w->doc))) 
	  return FALSE;
     c = g_strdup_printf( _("Save changes to %s?"),
			  w->doc->titlename );
     i = user_message ( c, UM_YESNOCANCEL );
     switch (i) {
     case MR_YES:
	  return mainwindow_save ( w, w->doc->filename );
     case MR_NO:
	  return FALSE;
     case MR_CANCEL:
	  return TRUE;
     }
     g_assert_not_reached();
     return TRUE;
}

static void mainwindow_destroy(GtkObject *obj)
{
     Mainwindow *w = MAINWINDOW(obj);

     list_object_remove(mainwindow_objects, obj);

     if ( w->doc != NULL ) {
	  gtk_object_unref(GTK_OBJECT(w->doc));
	  w->doc = NULL;
     }

     if (list_object_get_size(mainwindow_objects) == 0) {
	  if (clipboard) gtk_object_unref ( GTK_OBJECT(clipboard) );
	  clipboard = NULL;
	  quitflag = TRUE;
	  geometry_stack_save_to_inifile("windowGeometry",
					 window_geometry_stack);
     }

     parent_class->destroy(obj);
}

static gint mainwindow_delete_event(GtkWidget *widget, GdkEventAny *event)
{
     Mainwindow *w = MAINWINDOW ( widget );
     GtkWidgetClass *pcw = GTK_WIDGET_CLASS(parent_class);
     if (change_check(w)) return TRUE;
     if (playing_document == w->doc) player_stop();
     geometry_stack_push(GTK_WINDOW(w),NULL,&window_geometry_stack);
     if (pcw->delete_event) return pcw->delete_event(widget,event);
     else return FALSE;
}

static void mainwindow_realize(GtkWidget *widget)
{
     if (GTK_WIDGET_CLASS(parent_class)->realize) 
	  GTK_WIDGET_CLASS(parent_class)->realize(widget);
     if (!icon) icon = gdk_pixmap_create_from_xpm_d(widget->window,NULL,NULL,
						    icon_xpm);
     gdk_window_set_icon(widget->window,NULL,icon,NULL);     

}

static void mainwindow_toggle_mark(Mainwindow *w, gchar *label)
{
     off_t u;
     u = document_get_mark(w->doc,label);
     if (u == w->doc->cursorpos) 
	  document_set_mark(w->doc,label,DOCUMENT_BAD_MARK);
     else
	  document_set_mark(w->doc,label,w->doc->cursorpos);
}

static void do_play(Mainwindow *w, off_t start, off_t end, gboolean loop)
{
     gfloat s;
     if (varispeed_reset_flag && 
	 !(player_playing() && playing_document == w->doc))
	  s = 1.0;
     else
	  s = player_get_speed();
     document_play(w->doc, start, end, loop, s);
}


static void mainwindow_goto_mark(Mainwindow *w, gchar *label)
{
     off_t u;
     u = document_get_mark(w->doc,label);
     if (u != DOCUMENT_BAD_MARK) {
	  if (autoplay_mark_flag)
	       do_play(w,u,w->doc->chunk->length,w->loopmode);
	  else
	       document_set_cursor(w->doc, u);
     }
}

static void view_zoomin(GtkMenuItem *menu_item, gpointer user_data);
static void view_zoomout(GtkMenuItem *menu_item, gpointer user_data);
static void view_zoomtoselection(GtkMenuItem *menu_item, gpointer user_data);
static void view_zoomall(GtkMenuItem *menu_item, gpointer user_data);
static void edit_play(GtkMenuItem *menu_item, gpointer user_data);
static void edit_playselection(GtkMenuItem *menu_item, gpointer user_data);
static void edit_stop(GtkMenuItem *menu_item, gpointer user_data);

static gint mainwindow_keypress(GtkWidget *widget, GdkEventKey *event)
{     
     Mainwindow *w = MAINWINDOW(widget);
     off_t o;
     /* printf("%d\n",event->keyval); */
     if (!w->sensitive) {
	  if (event->keyval == GDK_Escape) 
	       status_bar_break_progress(w->statusbar);
	  return TRUE;
     }
     if (w->doc == NULL) 
          return GTK_WIDGET_CLASS(parent_class)->key_press_event(widget,event);
     if ((event->state & GDK_CONTROL_MASK))
	  switch (event->keyval) {
	  case GDK_0: mainwindow_toggle_mark(w,"0"); return TRUE;
	  case GDK_1: mainwindow_toggle_mark(w,"1"); return TRUE;
	  case GDK_2: mainwindow_toggle_mark(w,"2"); return TRUE;
	  case GDK_3: mainwindow_toggle_mark(w,"3"); return TRUE;
	  case GDK_4: mainwindow_toggle_mark(w,"4"); return TRUE;
	  case GDK_5: mainwindow_toggle_mark(w,"5"); return TRUE;
	  case GDK_6: mainwindow_toggle_mark(w,"6"); return TRUE;
	  case GDK_7: mainwindow_toggle_mark(w,"7"); return TRUE;
	  case GDK_8: mainwindow_toggle_mark(w,"8"); return TRUE;
	  case GDK_9: mainwindow_toggle_mark(w,"9"); return TRUE;
	  case GDK_Left:
	  case GDK_KP_Left: 
	       if (playing_document == w->doc) player_nudge(-0.5); return TRUE;
	  case GDK_Right:
	  case GDK_KP_Right:
	       if (playing_document == w->doc) player_nudge(0.5); return TRUE;
	  }
     else 
	  switch (event->keyval) {	       
	  case GDK_0: mainwindow_goto_mark(w,"0"); return TRUE;
	  case GDK_1: mainwindow_goto_mark(w,"1"); return TRUE;
	  case GDK_2: mainwindow_goto_mark(w,"2"); return TRUE;
	  case GDK_3: mainwindow_goto_mark(w,"3"); return TRUE;
	  case GDK_4: mainwindow_goto_mark(w,"4"); return TRUE;
	  case GDK_5: mainwindow_goto_mark(w,"5"); return TRUE;
	  case GDK_6: mainwindow_goto_mark(w,"6"); return TRUE;
	  case GDK_7: mainwindow_goto_mark(w,"7"); return TRUE;
	  case GDK_8: mainwindow_goto_mark(w,"8"); return TRUE;
	  case GDK_9: mainwindow_goto_mark(w,"9"); return TRUE;
	  case GDK_plus: view_zoomin(NULL,widget); return TRUE;
	  case GDK_KP_Add: view_zoomin(NULL,widget); return TRUE;
	  case GDK_equal: view_zoomin(NULL,widget); return TRUE;
	  case GDK_minus: view_zoomout(NULL,widget); return TRUE;
	  case GDK_KP_Subtract: view_zoomout(NULL,widget); return TRUE;
	  case GDK_greater: view_zoomtoselection(NULL,widget); return TRUE;
	  case GDK_less: view_zoomall(NULL,widget); return TRUE;
	  case GDK_comma: edit_play(NULL,widget); return TRUE;
	  case GDK_period: edit_stop(NULL,widget); return TRUE;
	  case GDK_slash: edit_playselection(NULL, widget); return TRUE;
	  case GDK_space:
	       if (playing_document == w->doc) 
		    document_stop(w->doc,w->bouncemode);
	       else if (w->doc != NULL)
		    do_play(w,w->doc->cursorpos,w->doc->chunk->length,
			    w->loopmode);
	       return TRUE;	       
	  case GDK_Left:
	  case GDK_KP_Left:
	       document_scroll(w->doc, 
			       -(w->doc->viewend - w->doc->viewstart)/4);
	       return TRUE;
	  case GDK_Right:
	  case GDK_KP_Right:
	       document_scroll(w->doc,
			       (w->doc->viewend - w->doc->viewstart)/4);
	       return TRUE;
	  case GDK_parenleft:
	       o = 3*w->doc->chunk->format.samplerate;
	       if (w->doc->selstart == w->doc->selend) {
		    if (w->doc->chunk->length < o)
			 do_play(w,0,w->doc->chunk->length,FALSE);
		    else
			 do_play(w,0,o,FALSE);
	       } else if (w->doc->selstart+o > w->doc->selend)
		    do_play(w,w->doc->selstart,w->doc->selend,FALSE);
	       else
		    do_play(w,w->doc->selstart,w->doc->selstart+o,FALSE);
	       return TRUE;		    
	  case GDK_parenright:
	       o = 3*w->doc->chunk->format.samplerate;
	       if (w->doc->selstart == w->doc->selend) {
		    if (w->doc->chunk->length < o)
			 do_play(w,0,w->doc->chunk->length,FALSE);
		    else
			 do_play(w,w->doc->chunk->length-o,
				 w->doc->chunk->length,FALSE);
	       } else if (w->doc->selstart+o > w->doc->selend)
		    do_play(w,w->doc->selstart,w->doc->selend,FALSE);
	       else
		    do_play(w,w->doc->selend-o,w->doc->selend,FALSE);
	       return TRUE;		    
	  }
     return GTK_WIDGET_CLASS(parent_class)->key_press_event(widget,event);
}

static void mainwindow_class_init(GtkObjectClass *klass)
{
     parent_class = gtk_type_class(gtk_window_get_type());
     klass->destroy = mainwindow_destroy;
     GTK_WIDGET_CLASS(klass)->delete_event = mainwindow_delete_event;
     GTK_WIDGET_CLASS(klass)->realize = mainwindow_realize;
     GTK_WIDGET_CLASS(klass)->key_press_event = mainwindow_keypress;     

}

static void mainwindow_set_chunk(Mainwindow *w, Chunk *c, gchar *filename)
{
     Document *d;
     if (w->doc != NULL) {
	  w = MAINWINDOW ( mainwindow_new() );
	  gtk_widget_show ( GTK_WIDGET ( w ) );
     }
     d = document_new_with_chunk(c,filename,w->statusbar);
     w->doc = d;
     gtk_object_ref(GTK_OBJECT(w->doc));
     gtk_object_sink(GTK_OBJECT(w->doc));
     gtk_signal_connect(GTK_OBJECT(d),"view_changed",
			GTK_SIGNAL_FUNC(mainwindow_view_changed),w);
     gtk_signal_connect(GTK_OBJECT(d),"selection_changed",
			GTK_SIGNAL_FUNC(mainwindow_selection_changed),w);
     gtk_signal_connect(GTK_OBJECT(d),"cursor_changed",
			GTK_SIGNAL_FUNC(mainwindow_cursor_changed),w);
     gtk_signal_connect(GTK_OBJECT(d),"state_changed",
			GTK_SIGNAL_FUNC(mainwindow_state_changed),w);
     chunk_view_set_document ( w->view, d );
     fix_title(w);
     update_desc(w);
     set_sensitive(w->need_chunk_items,TRUE);
     if (!inifile_get_gboolean("varispeed",TRUE))
	  gtk_widget_set_sensitive(w->speed_slider,FALSE);
     list_object_add(mainwindow_objects,w);
}

void mainwindow_set_speed_sensitive(gboolean sensitive)
{
     GList *l;
     Mainwindow *w;
     for (l=mainwindow_objects->list;l!=NULL;l=l->next) {
	  w = MAINWINDOW(l->data);
	  gtk_widget_set_sensitive(w->speed_slider,sensitive);
     }
}

static void file_open(GtkMenuItem *menuitem, gpointer user_data)
{
     gchar *c;
     Chunk *chunk;
     Mainwindow *w = MAINWINDOW ( user_data );     
     if (w->doc != NULL && w->doc->filename != NULL)
	  c = get_filename(w->doc->filename,"*.wav", _("Load File"), FALSE );
     else {
	  c = inifile_get("lastOpenFile",NULL);
	  if (c == NULL) c = inifile_get("lastSaveFile",NULL);
	  c = get_filename(c,"*.wav", _("Load File"), FALSE);
     }
     if (!c) return;
     chunk = chunk_load ( c, dither_editing, w->statusbar );
     if (!chunk) {
	  g_free(c);
	  return;
     }
     mainwindow_set_chunk ( w, chunk, c );
     inifile_set("lastOpenFile",c);
     recent_file(c);
     g_free(c);
}

static void file_save(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     mainwindow_save ( w, w->doc->filename );
}

static void file_saveas(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_save ( MAINWINDOW(user_data), NULL );
}

static void file_saveselection(GtkMenuItem *menuitem, gpointer user_data)
{
     gchar *fn;
     Chunk *c;
     Mainwindow *w = MAINWINDOW(user_data);
     fn = get_save_filename(NULL,_("Save selection as ..."));
     if (!fn) return;
     c = chunk_get_part(w->doc->chunk,w->doc->selstart,
			w->doc->selend-w->doc->selstart);
     if (!chunk_save(c,fn,dither_editing,w->statusbar))
	  inifile_set("lastSaveFile",fn);
     gtk_object_sink(GTK_OBJECT(c));     
     g_free(fn);
}


static void file_close(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     if (change_check(w)) return;
     if (playing_document == w->doc) player_stop();
     if (list_object_get_size(mainwindow_objects)==1 &&
	 w->doc != NULL) {
	  chunk_view_set_document(w->view, NULL);
	  gtk_object_unref(GTK_OBJECT(w->doc));
	  w->doc = NULL;
	  fix_title(w);
	  set_sensitive(w->need_chunk_items,FALSE);
	  set_sensitive(w->need_undo_items,FALSE);
	  list_object_remove(mainwindow_objects,w);
     } else {
	  geometry_stack_push(GTK_WINDOW(w),NULL,&window_geometry_stack);
	  gtk_widget_destroy(GTK_WIDGET(w));
     }
}

static void try_close(Mainwindow *w, gboolean *user_data)
{
     if (*user_data) return;
     *user_data = change_check(w);
}

static void file_exit(GtkMenuItem *menuitem, gpointer user_data)
{
     gboolean b = FALSE;
     guint i;
     GtkWidget *wid;
     if (list_object_get_size(mainwindow_objects) == 0)
	  file_close(menuitem,user_data);
     list_object_foreach(mainwindow_objects,(GFunc)try_close,&b);
     if (b) return;
     player_stop();
     /* Reverse the list so we get the oldest windows on the top of the 
      * geometry stack */
     i = list_object_get_size(mainwindow_objects);
     for (; i>0; i--) {
	  wid = GTK_WIDGET(list_object_get(mainwindow_objects,i-1));
	  geometry_stack_push(GTK_WINDOW(wid),NULL,&window_geometry_stack);
	  gtk_widget_destroy(wid);
     }
}

static void edit_undo(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     document_undo(w->doc);
}

static void edit_redo(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     document_redo(w->doc);
}

static void update_clipboard_items(Mainwindow *w, gpointer user_data)
{
     set_sensitive(w->need_clipboard_items,(int)user_data);
}

static void edit_cut(GtkMenuItem *menuitem, gpointer user_data)
{
     Chunk *chunk, *part;
     Mainwindow *w = MAINWINDOW(user_data);
     g_assert (w->doc->selend != w->doc->selstart);
     if (clipboard) gtk_object_unref(GTK_OBJECT(clipboard));
     part = chunk_get_part( w->doc->chunk, w->doc->selstart,
			    w->doc->selend - w->doc->selstart);
     chunk = chunk_remove_part( w->doc->chunk, w->doc->selstart,
				w->doc->selend - w->doc->selstart);
     clipboard = part;
     gtk_object_ref(GTK_OBJECT(clipboard));
     gtk_object_sink(GTK_OBJECT(clipboard));

     document_update(w->doc, chunk, w->doc->selstart, -(clipboard->length));
     list_object_foreach(mainwindow_objects, (GFunc)update_clipboard_items, 
			 (gpointer)1);
}

static void edit_crop(GtkMenuItem *menu_item, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     Chunk *c;
     c = chunk_get_part( w->doc->chunk, w->doc->selstart,
			 w->doc->selend - w->doc->selstart);
     document_update(w->doc, c, 0, -(w->doc->selstart));
}

static void edit_copy(GtkMenuItem *menu_item, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     g_assert (w->doc->selend != w->doc->selstart);
     if (clipboard) gtk_object_unref(GTK_OBJECT(clipboard));
     clipboard = chunk_get_part(w->doc->chunk, w->doc->selstart, 
				w->doc->selend - w->doc->selstart);
     gtk_object_ref(GTK_OBJECT(clipboard));
     gtk_object_sink(GTK_OBJECT(clipboard));

     list_object_foreach(mainwindow_objects, (GFunc)update_clipboard_items, 
			 (gpointer)1);
}

static void edit_paste(GtkMenuItem *menu_item, gpointer user_data)
{
     Chunk *c = clipboard, *nc;
     Mainwindow *w = MAINWINDOW(user_data);
     off_t cp;
     if (w->doc == NULL) {
	  mainwindow_set_chunk(w,clipboard,NULL);
	  return;
     }
     
     if (!dataformat_equal(&(w->doc->chunk->format),&(c->format))) {
	  c = chunk_convert(c,&(w->doc->chunk->format),dither_editing,
			    w->statusbar);
	  if (c == NULL) return;
     }

     nc = chunk_insert(w->doc->chunk,c,w->doc->cursorpos);
     gtk_object_sink(GTK_OBJECT(c));

     cp = w->doc->cursorpos;
     document_update(w->doc, nc, cp, clipboard->length);
     document_set_selection( w->doc, cp, cp + clipboard->length );
}

static void edit_pasteover(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     Chunk *c,*d=clipboard;
     off_t orig_len;
     if (w->doc == NULL) {
	  mainwindow_set_chunk(w,clipboard,NULL);
	  return;
     }

     if (!dataformat_equal(&(w->doc->chunk->format),&(d->format))) {
	  d = chunk_convert(d,&(w->doc->chunk->format),dither_editing,
			    w->statusbar);
	  if (d == NULL) return;
     }

     orig_len = MIN(w->doc->chunk->length-w->doc->cursorpos, d->length);
     c = chunk_replace_part(w->doc->chunk, w->doc->cursorpos, 
			    orig_len, d);
     gtk_object_sink(GTK_OBJECT(d));

     document_update(w->doc, c, 0, 0);
     document_set_selection( w->doc, w->doc->cursorpos,
			     w->doc->cursorpos + clipboard->length );
}

static void edit_mixpaste(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     Chunk *c,*p,*y,*d=clipboard;
     off_t partlen;
     if (w->doc == NULL) {
	  mainwindow_set_chunk(w,clipboard,NULL);
	  return;
     }

     if (!dataformat_equal(&(w->doc->chunk->format),&(d->format))) {
	  d = chunk_convert(d,&(w->doc->chunk->format),dither_editing,
			    w->statusbar);
	  if (d == NULL) return;
     }

     p = chunk_get_part(w->doc->chunk,w->doc->cursorpos,d->length);
     partlen = p->length;
     y = chunk_mix(p,d,dither_editing,w->statusbar);
     gtk_object_sink(GTK_OBJECT(p));
     if (!y) return;
     c = chunk_replace_part(w->doc->chunk,w->doc->cursorpos,partlen,y);
     gtk_object_sink(GTK_OBJECT(y));
     document_update(w->doc, c, 0, 0);
     document_set_selection( w->doc, w->doc->cursorpos,
			     w->doc->cursorpos + d->length );
     gtk_object_sink(GTK_OBJECT(d));
}

static void edit_pastetonew(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     mainwindow_set_chunk(w,clipboard,NULL);
}

static void edit_delete(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     Chunk *chunk;
     gint32 slen;
     slen = w->doc->selend - w->doc->selstart;
     chunk = chunk_remove_part(w->doc->chunk, w->doc->selstart, slen);
     document_update( w->doc, chunk, w->doc->selstart, -slen);
}

static Chunk *interp(Chunk *chunk, StatusBar *bar, gpointer user_data)
{
  return chunk_interpolate_endpoints(chunk,dither_editing,bar);
}

static void edit_silence(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     document_apply_cb(w->doc,interp,TRUE,NULL);
}

static void edit_selectall(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     document_set_selection(w->doc,0,w->doc->chunk->length);
}

static void view_zoomin(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     document_zoom(w->doc,2.0,TRUE);
}

static void view_zoomout(GtkMenuItem *item, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     document_zoom(w->doc,0.5,TRUE);
}

static void view_zoomtoselection(GtkMenuItem *menuitem, gpointer user_data)
{
     Document *d;
     d = MAINWINDOW(user_data)->doc;
     document_set_view(d,d->selstart,d->selend);
}

static void view_zoomall(GtkMenuItem *menuitem, gpointer user_data)
{     
     Document *d;
     d = MAINWINDOW(user_data)->doc;
     document_set_view(d,0,d->chunk->length);
}

#ifdef SHOW_DEBUG_MENU
static void debug_mark(GtkMenuItem *menuitem, gpointer user_data)
{
     MAINWINDOW(user_data)->changed = TRUE;
}

static gboolean dummy_proc(void *in, guint sample_size, 
			   chunk_writeout_func out_func, WriteoutID id,
			   Chunk *source)
{
     return out_func(id,in,sample_size);
}

static void debug_dummy(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     mainwindow_chunk_change(w,chunk_filter(w->view->chunk,dummy_proc,NULL,CHUNK_FILTER_MANY,FALSE,w,"DUMMY"),NULL,0,0,0);
}

static void checkoc_proc(Chunk *chunk, gpointer user_data)
{
     if (chunk->opencount > 0) 
	  printf(_("Chunk %p has opencount=%d\n"),chunk,chunk->opencount);
}

static void debug_checkoc(GtkMenuItem *menuitem, gpointer user_data)
{
     puts("--------");
     chunk_foreach((GFunc)checkoc_proc,NULL);
}

static void dump_format(Dataformat *df)
{
     printf("%dx%d@%dHz %c\n",df->channels,df->samplesize,df->samplerate,
	    df->sign?'S':'U');
}

static void dump_dp(DataPart *dp)
{
     printf("        Datasource@%p, L/S=%Ld/%Ld <%s> Format: ",dp->ds,
	    dp->ds->length,dp->ds->size,dp->ds->fake_pcm?"fake":"pcm");
     dump_format(&(dp->ds->format));
     printf("        Usage: %Ld + %Ld\n",dp->position,dp->length);
     switch (dp->ds->type) {
     case DATASOURCE_REAL:
	  printf("        REAL @ %p\n",dp->ds->data.real);
	  break;
     case DATASOURCE_VIRTUAL:
	  printf("        VIRTUAL @ %s:%Ld\n",dp->ds->data.virtual.filename,
		 dp->ds->data.virtual.offset);
	  break;
     case DATASOURCE_TEMPFILE:
	  printf("        TEMPFILE @ %s:%Ld\n",dp->ds->data.virtual.filename,
		 dp->ds->data.virtual.offset);
	  break;
     case DATASOURCE_SILENCE:
	  puts("        SILENCE");
	  break;
     case DATASOURCE_SNDFILE:
	  printf("        SNDFILE @ %s:%Ld <rr:%c>\n",
		 dp->ds->data.sndfile.filename, dp->ds->data.sndfile.pos,
		 dp->ds->data.sndfile.raw_readable?'T':'F');
	  break;
     case DATASOURCE_SNDFILE_TEMPORARY:
	  printf("        SNDFILE @ %s:%Ld <rr:%c>\n",
		 dp->ds->data.sndfile.filename, dp->ds->data.sndfile.pos,
		 dp->ds->data.sndfile.raw_readable?'T':'F');
	  break;
     case DATASOURCE_CLONE:
	  printf("        CLONE --> %p\n",dp->ds->data.clone);
	  break;
     case DATASOURCE_BYTESWAP:
	  printf("        BYTESWAP --> %p\n",dp->ds->data.clone);
	  break;
     }
}

static void dump_chunk(Chunk *c)
{
     printf("    Chunk@%p, L/S=%Ld/%Ld, Format: ",c,c->length,c->size);
     dump_format(&(c->format));
     puts("      Used datasources:");
     g_list_foreach(c->parts,(GFunc)dump_dp,NULL);
}

static void indirect_dump_chunk(Chunk **cp) 
{
	dump_chunk(*cp);
}

static void windowinfo_proc(gpointer item, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(item);     
     printf(_("\nWindow '%s'\n"),w->filename);
     puts(_("  Current chunk:"));
     dump_chunk(w->view->chunk);
     puts(_("  History:"));
     g_slist_foreach(w->history,(GFunc)indirect_dump_chunk,NULL);
}

static void debug_chunkinfo(GtkMenuItem *menuitem, gpointer user_data)
{    
     list_object_foreach(mainwindow_objects,windowinfo_proc,NULL);
}

#endif

static void mainwindow_show_effect_dialog(Mainwindow *mw, gchar *effect_name)
{
     EffectBrowser *eb;
     eb = EFFECT_BROWSER(effect_browser_new_with_effect(mw->doc,effect_name));
     gtk_widget_show(GTK_WIDGET(eb));
}

static void effects_volume(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),"volume");
}

static void effects_speed(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),"speed");
}

static void effects_samplerate(GtkMenuItem *menuitem, gpointer user_data)
{    
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),"srate");
}

static void effects_samplesize(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),"ssize");
}

static void effects_mixchannels(GtkMenuItem *menuitem, gpointer user_data)
{
     Chunk *c;
     Mainwindow *w = MAINWINDOW(user_data);
     player_stop();
     if (w->doc->chunk->format.channels == 1)
	  user_info(_("There already is only one channel!"));
     else {
	  c = chunk_onechannel(w->doc->chunk,dither_editing,w->statusbar);
	  if (c) document_update(w->doc,c,0,0);
     }
}

static void effects_splitchannel(GtkMenuItem *menuitem, gboolean user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     Chunk *c;
     c = chunk_copy_channel(w->doc->chunk,0,dither_editing,w->statusbar);
     if (c == NULL) return;
     player_stop();
     document_update(w->doc, c, 0, 0);
}

static void edit_stop(GtkMenuItem *menu_item, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     if (playing_document == NULL) {
	  if (w->doc->cursorpos != 0 || w->doc->chunk->length == 0)
	       document_set_cursor(w->doc,0);
	  else
	       document_set_cursor(w->doc,w->doc->chunk->length);
     } else
	  document_stop(w->doc, w->bouncemode);
}


gboolean mainwindow_update_caches(void)
{
     static guint last = 0;
     guint i,s;
     Mainwindow *wnd;

     s = list_object_get_size(mainwindow_objects);
     if (s == 0) return FALSE;
     if (last >= s) last=0;
     i = last+1;
     while (1) {	  
	  if (i >= s) i = 0;
	  wnd = MAINWINDOW(list_object_get(mainwindow_objects, i));
	  if (chunk_view_update_cache(wnd->view)) { last = i; return TRUE; }
	  if (i == last) return FALSE;
	  i++;
     }
}

static void edit_playselection(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     document_play_selection(w->doc,w->loopmode,
			     varispeed_reset_flag ? 1.0 : player_get_speed());
}

static void edit_playall(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     do_play(w,0,w->doc->chunk->length,w->loopmode);
}

static void help_readme(void)
{
     GtkAccelGroup* ag;
     GtkWidget *window,*table,*notebook,*frame,*label,*button,*box1,*box2;
     int i;
     GtkWidget *scrolledwindow1, *viewport1, *label2;
#if GTK_MAJOR_VERSION == 2
GtkStyle *style;
#endif
     ag = gtk_accel_group_new();

     window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

     gtk_container_set_border_width (GTK_CONTAINER (window), 10);
     gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);	// Centre the window
     gtk_window_set_default_size (GTK_WINDOW (window), -1, 400);	// Make readable
     gtk_window_set_title (GTK_WINDOW (window), _("mhWaveEdit Help"));

     box1 = gtk_vbox_new (FALSE, 0);
     gtk_container_add (GTK_CONTAINER (window), box1);
     gtk_widget_show (box1);

     box2 = gtk_vbox_new (FALSE, 10);
     gtk_container_set_border_width (GTK_CONTAINER (box2), 1);
     gtk_box_pack_start (GTK_BOX (box1), box2, TRUE, TRUE, 0);
     gtk_widget_show (box2);

     table = gtk_table_new (3, 6, FALSE);
     gtk_box_pack_start (GTK_BOX (box2), table, TRUE, TRUE, 0);

     notebook = gtk_notebook_new ();
     gtk_table_attach_defaults (GTK_TABLE (table), notebook, 0, 6, 0, 1);
     gtk_widget_show (notebook);

     for (i=0; i<help_page_count; i++)
     {
	frame = gtk_frame_new (NULL);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 10);
	gtk_widget_show (frame);

	scrolledwindow1 = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (scrolledwindow1);
	gtk_container_add (GTK_CONTAINER (frame), scrolledwindow1);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow1), 
					GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	viewport1 = gtk_viewport_new (NULL, NULL);
	gtk_widget_show (viewport1);
	gtk_container_add (GTK_CONTAINER (scrolledwindow1), viewport1);

	label2 = gtk_label_new (_(help_page_contents[i]));
#if GTK_MAJOR_VERSION == 2
if (i==HELP_PAGE_SHORTCUTS)	// Keyboard tab only
{
	style = gtk_style_copy (gtk_widget_get_style (label2));
	style->font_desc = pango_font_description_from_string("Monospace 11");  // Courier also works
	gtk_widget_set_style (label2, style);
}
#endif
	gtk_container_add (GTK_CONTAINER (viewport1), label2);
	GTK_WIDGET_SET_FLAGS (label2, GTK_CAN_FOCUS);
	gtk_label_set_justify (GTK_LABEL (label2), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap (GTK_LABEL (label2), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label2), 0, 0);
	gtk_misc_set_padding (GTK_MISC (label2), 5, 5);
	gtk_widget_show (label2);


	label = gtk_label_new (_(help_page_titles[i]));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
     }

     /* Show default page (defined in help.h) */
#if GTK_MAJOR_VERSION == 1
     gtk_notebook_set_page (GTK_NOTEBOOK (notebook), HELP_PAGE_DEFAULT);
#else
     gtk_notebook_set_current_page (GTK_NOTEBOOK(notebook),HELP_PAGE_DEFAULT);
#endif
     gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_LEFT);

     box2 = gtk_vbox_new (FALSE, 10);
     gtk_container_set_border_width (GTK_CONTAINER (box2), 1);
     gtk_box_pack_start (GTK_BOX (box1), box2, FALSE, TRUE, 0);
     gtk_widget_show (box2);

     button = gtk_button_new_with_label (_("Close"));
     gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, 
				 (GtkAccelFlags) 0);
     gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, 
				 (GtkAccelFlags) 0);
     gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, 
				 (GtkAccelFlags) 0);
     GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
     gtk_signal_connect_object(GTK_OBJECT(button),"clicked",
			       (GtkSignalFunc)gtk_widget_destroy,
			       GTK_OBJECT(window));
     gtk_box_pack_start (GTK_BOX (box2), button, TRUE, TRUE, 0);
     gtk_widget_show (button);

     gtk_widget_show (table);
     gtk_widget_show (window);
     gtk_window_add_accel_group(GTK_WINDOW (window), ag);
}

static void help_about(void)
{
     GtkAccelGroup* ag;
     gchar *p;
     GtkWidget *a,*b,*c;
     ag = gtk_accel_group_new();
     a = gtk_window_new(GTK_WINDOW_DIALOG);
gtk_window_set_modal(GTK_WINDOW(a),TRUE);
     gtk_window_set_title(GTK_WINDOW(a),_("About mhWaveEdit"));
     gtk_container_set_border_width(GTK_CONTAINER(a),5);
     gtk_window_set_position (GTK_WINDOW (a), GTK_WIN_POS_CENTER);	// Centre the window
     b = gtk_vbox_new(FALSE,10);
     gtk_container_add(GTK_CONTAINER(a),b);
     gtk_widget_show(b);
     c = gtk_label_new(PROGRAM_VERSION_STRING);
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     c = gtk_hseparator_new();
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     c = gtk_label_new(_("Created by Magnus Hjorth (magnus.hjorth@home.se)\n"
	  "Copyright 2002-2005, Magnus Hjorth"));
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     c = gtk_hseparator_new();
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);     
     p = g_strdup_printf(_("Current sound driver: %s"),sound_driver_name());
     c = gtk_label_new(p);
     g_free(p);
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     c = gtk_hseparator_new();
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     p = g_strdup_printf(_("Compiled %s %s"), __DATE__, __TIME__);
     c = gtk_label_new(p);
     g_free(p);
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
#ifdef USE_DOUBLE_SAMPLES
     c = gtk_label_new(_("Uses double-precision math"));
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
#endif
     c = gtk_hseparator_new();
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     c = gtk_label_new(_("Distributed under GNU General Public License.\n"
		       "For information, see the file COPYING"));
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     c = gtk_button_new_with_label(_("OK"));
     gtk_widget_add_accelerator (c, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
     gtk_widget_add_accelerator (c, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
     gtk_widget_add_accelerator (c, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
     gtk_signal_connect_object(GTK_OBJECT(c),"clicked",
			       (GtkSignalFunc)gtk_widget_destroy,GTK_OBJECT(a));
     gtk_box_pack_start(GTK_BOX(b),c,FALSE,FALSE,0);
     gtk_widget_show(c);
     
     gtk_widget_show(a);
     gtk_window_add_accel_group(GTK_WINDOW (a), ag);
}

static void edit_play(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW (user_data);
     do_play(w,w->doc->cursorpos,w->doc->chunk->length,w->loopmode);
}

static void edit_selstartcursor(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     if (w->doc->selstart == w->doc->selend) 
	  document_set_selection(w->doc,w->doc->cursorpos,
				 w->doc->chunk->length);
     else 
	  document_set_selection(w->doc,w->doc->cursorpos,w->doc->selend);
}

static void edit_selendcursor(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     if (w->doc->selstart == w->doc->selend)
	  document_set_selection(w->doc,0,w->doc->cursorpos+1);
     else
	  document_set_selection(w->doc,w->doc->selstart,w->doc->cursorpos);
}

static void edit_record(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *wnd = MAINWINDOW (user_data);
     Chunk *c;
     player_stop();
     c = record_dialog_execute();
     if (c) mainwindow_set_chunk(wnd,c,NULL);
}

static void edit_preferences(GtkMenuItem *menuitem, gpointer user_data)
{
    gtk_widget_show(config_dialog_new());
}

static void effects_combinechannels(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),"combine");
}

static void view_timescale(GtkCheckMenuItem *checkmenuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     chunk_view_set_timescale(w->view, checkmenuitem->active);
}

static void view_horizoom(GtkCheckMenuItem *checkmenuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     if (w->hzoom_icon == NULL) return;
     if (checkmenuitem->active) {
	  gtk_widget_show(w->hzoom_icon);
	  gtk_widget_show(w->hzoom_slider);
     } else {
	  gtk_widget_hide(w->hzoom_icon);
	  gtk_widget_hide(w->hzoom_slider);
     }
}

static void view_vertzoom(GtkCheckMenuItem *checkmenuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     if (w->vzoom_icon == NULL) return;
     if (checkmenuitem->active) {
	  gtk_widget_show(w->vzoom_icon);
	  gtk_widget_show(w->vzoom_slider);
     } else {
	  gtk_widget_hide(w->vzoom_icon);
	  gtk_widget_hide(w->vzoom_slider);
     }
}

static void view_speed(GtkCheckMenuItem *checkmenuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     if (w->speed_icon == NULL) return;
     if (checkmenuitem->active) {
	  gtk_widget_show(w->speed_icon);
	  gtk_widget_show(w->speed_slider);
	  gtk_widget_show(GTK_WIDGET(w->speed_label));
     } else {
	  gtk_widget_hide(w->speed_icon);
	  gtk_widget_hide(w->speed_slider);
	  gtk_widget_hide(GTK_WIDGET(w->speed_label));
     }
}

static void edit_insertsilence(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     gfloat f;
     off_t cp;
     Chunk *c,*nc;
     if (user_input_float(_("Seconds of silence: "),_("Insert Silence"),0.0,&f)) 
	  return;
     if (f<=0.0) return;
     c = chunk_new_silent(&(w->doc->chunk->format),f);
     nc = chunk_insert(w->doc->chunk, c, w->doc->cursorpos);
     cp = w->doc->cursorpos;
     document_update(w->doc,nc,cp,c->length);
     document_set_selection( w->doc, cp, cp + c->length );
     gtk_object_sink(GTK_OBJECT(c));
}

static void edit_clearclipboard(GtkMenuItem *menuitem, gpointer user_data)
{
     gtk_object_unref(GTK_OBJECT(clipboard));
     clipboard = NULL;
     list_object_foreach(mainwindow_objects, 
			 (GFunc)update_clipboard_items, (gpointer)0);
}

static void edit_positioncursor(GtkMenuItem *menuitem, gpointer user_data)
{
     gtk_widget_show(goto_dialog_new(MAINWINDOW(user_data)));
}

static Chunk *effects_normalize_proc(Chunk *chunk, StatusBar *bar, 
				     gpointer user_data)
{
     sample_t s;
     Chunk *c;
     s = chunk_peak_level(chunk,bar);
     if (s <= 0.0) return NULL;
     c = chunk_amplify(chunk,maximum_float_value(&(chunk->format))/s,
		       dither_editing,bar);
     return c;
}

static void effects_normalize(GtkMenuItem *menuitem, gpointer user_data)
{     
     document_apply_cb(MAINWINDOW(user_data)->doc,effects_normalize_proc,
		       TRUE,NULL);
}

static void effects_pipe(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),"pipe");
}

static void cursor_tobeginning(GtkMenuItem *menuitem, gpointer user_data)
{
     document_set_cursor(MAINWINDOW(user_data)->doc,0);
}

static void cursor_toend(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     document_set_cursor(w->doc,w->doc->chunk->length);
}

static void cursor_toselstart(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     document_set_cursor(w->doc,w->doc->selstart);
}

static void cursor_toselend(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     document_set_cursor(w->doc,w->doc->selend);
}

static Chunk *byteswap_proc(Chunk *chunk, StatusBar *bar, 
			    gpointer user_data)
{
     return chunk_byteswap(chunk);
}

static void effects_byteswap(GtkMenuItem *menuitem, gpointer user_data)
{
     document_apply_cb(MAINWINDOW(user_data)->doc,byteswap_proc,TRUE,NULL);
}

static Chunk *fadein_proc(Chunk *chunk, StatusBar *bar, gpointer user_data)
{
     return chunk_volume_ramp(chunk,0.0,1.0,dither_editing,bar);
}

static Chunk *fadeout_proc(Chunk *chunk, StatusBar *bar, gpointer user_data)
{
     return chunk_volume_ramp(chunk,1.0,0.0,dither_editing,bar);
}

static void effects_fadein(GtkMenuItem *menuitem, gpointer user_data)
{
     document_apply_cb(MAINWINDOW(user_data)->doc,fadein_proc,TRUE,NULL);
}

static void effects_fadeout(GtkMenuItem *menuitem, gpointer user_data)
{
     document_apply_cb(MAINWINDOW(user_data)->doc,fadeout_proc,TRUE,NULL);
}

static void effects_dialog(GtkMenuItem *menuitem, gpointer user_data)
{
     mainwindow_show_effect_dialog(MAINWINDOW(user_data),NULL);     
}

static void file_recent(GtkMenuItem *menuitem, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     Chunk *chunk;
     gchar *fn;
     GList *l = w->recent;
     GList *m = recent_filenames;
     while (l->data != menuitem) { l=l->next; m=m->next; }
     fn = g_strdup((gchar *)m->data);
     chunk = chunk_load(fn, dither_editing, w->statusbar);
     if (!chunk) { g_free(fn); return; }
     mainwindow_set_chunk(w, chunk, fn);
     inifile_set("lastOpenFile",fn);
     recent_file(fn);
     g_free(fn);
}

static gchar *translate_menu_path(const gchar *path, gpointer func_data)
{
    return _(path);
}

static GtkWidget *create_menu(Mainwindow *w)
{
     guint i,j;
     GtkWidget *item;
     GtkItemFactoryEntry entry = {};

     GtkItemFactoryEntry menu_items1[] = {
	  { N_("/_File"),         NULL,         NULL,           0, "<Branch>"    },
	  { N_("/File/_Open..."), "<control>O", file_open,      0, NULL          },
	  { N_("/File/_Save"),    "<control>S", file_save,      0, NULL          },
	  { N_("/File/Save _as..."),NULL,       file_saveas,    0, NULL          },
	  {N_("/File/Save selection as..."),"<control>U",file_saveselection,0,NULL},
	  { N_("/_Edit"),         NULL,         NULL,           0, "<Branch>"    },
	  { N_("/Edit/_Undo"),    "<control>Z", edit_undo,      0, NULL          },
	  { N_("/Edit/_Redo"),    NULL,         edit_redo,      0, NULL },
	  { N_("/Edit/sep1"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/Edit/Cu_t"),     "<control>X", edit_cut,       0, NULL          },
	  { N_("/Edit/_Copy"),    "<control>C", edit_copy,      0, NULL          },
	  { N_("/Edit/_Paste"),   "<control>V", edit_paste,     0, NULL          },
	  { N_("/Edit/Paste _over"),NULL,       edit_pasteover, 0, NULL          },
	  { N_("/Edit/_Mix paste"),NULL,        edit_mixpaste,  0, NULL          },
	  { N_("/Edit/Insert _silence"),NULL,   edit_insertsilence,0,NULL        },
	  { N_("/Edit/Paste to _new"),NULL,     edit_pastetonew,0, NULL          },
	  { N_("/Edit/Cr_op"),    NULL,         edit_crop,      0, NULL          },
	  { N_("/Edit/_Delete"),  "<control>D", edit_delete,    0, NULL          },
	  { N_("/Edit/Silence selection"),NULL,edit_silence,   0, NULL          },
	  { N_("/Edit/sep2"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/Edit/Select _all"),"<control>A",edit_selectall,0, NULL          },
	  { N_("/Edit/sep3"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/Edit/Clear clipboard"),NULL,   edit_clearclipboard,0,NULL       },
	  { N_("/Edit/sep4"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/Edit/Preferences"),"<control>P",      edit_preferences,0,NULL          },
	  { N_("/_View"),         NULL,         NULL,           0, "<Branch>"    },
	  { N_("/View/Zoom _in"), NULL,         view_zoomin,    0, NULL          },
	  { N_("/View/Zoom _out"),NULL,         view_zoomout,   0, NULL          },
	  { N_("/View/Zoom to _selection"),NULL,view_zoomtoselection,0,NULL      },
	  { N_("/View/sep1"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/View/Zoom _all"),NULL,         view_zoomall,   0, NULL          },
	  { N_("/View/sep2"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/View/_Time scale"),NULL,       view_timescale, 0, "<CheckItem>" },
	  { N_("/View/_Horizontal zoom"),NULL,  view_horizoom,  0, "<CheckItem>" },
	  { N_("/View/_Vertical zoom"),NULL,    view_vertzoom,  0, "<CheckItem>" },
	  { N_("/View/Sp_eed slider"),NULL,     view_speed,     0, "<CheckItem>" },
	  { N_("/_Cursor"),       NULL,         NULL,           0, "<Branch>" },
	  {N_("/Cursor/Set selection start"),"<control>Q",edit_selstartcursor,0,
	   NULL},
	  { N_("/Cursor/Set selection end"),"<control>W",edit_selendcursor,0,NULL},
	  { N_("/Cursor/sep1"),   NULL,         NULL,           0, "<Separator>" },
	  {N_("/Cursor/Move to beginning"),"<control>H",cursor_tobeginning,0,
	   NULL},
	  {N_("/Cursor/Move to end"),"<control>J",cursor_toend, 0, NULL          },
	  {N_("/Cursor/Move to selection start"),"<control>K",cursor_toselstart,
	   0,NULL},
	  {N_("/Cursor/Move to selection end"),"<control>L",cursor_toselend,0,
	   NULL},
	  { N_("/Cursor/sep2"),   NULL,         NULL,           0, "<Separator>" },
	  {N_("/Cursor/Position cursor..."),"<control>G",edit_positioncursor,0,
	   NULL},
	  { N_("/_Play"),         NULL,         NULL,           0, "<Branch>"    },
	  { N_("/Play/_Play from cursor"),NULL, edit_play,      0, NULL          },
	  { N_("/Play/Play _all"),NULL,         edit_playall,   0, NULL          },
	  { N_("/Play/Play se_lection"),NULL,   edit_playselection,0,NULL        },
	  { N_("/Play/_Stop"),    NULL,         edit_stop,      0, NULL          },
	  { N_("/Play/sep1"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/Play/_Record..."),  "F12",        edit_record,    0, NULL          },
	  { N_("/Effec_ts"),      NULL,         NULL,           0, "<Branch>"    },
	  { N_("/Effects/Fade _in"),NULL,       effects_fadein, 0, NULL          },
	  { N_("/Effects/Fade o_ut"),NULL,      effects_fadeout,0, NULL          },
	  { N_("/Effects/_Normalize"),"<control>N",effects_normalize,0, NULL     },
	  { N_("/Effects/_Volume adjust (fade)..."),NULL,effects_volume,0,NULL   },
	  { N_("/Effects/sep1"),  NULL,         NULL,           0, "<Separator>" },
	  { N_("/Effects/Convert sample_rate..."),NULL,effects_samplerate,0,NULL },
	  { N_("/Effects/Convert sample _format..."),NULL,effects_samplesize,0,
	    NULL},
	  { N_("/Effects/B_yte swap"),NULL,      effects_byteswap,0,NULL         },
	  { N_("/Effects/sep2"),  NULL,         NULL,           0, "<Separator>" },
	  { N_("/Effects/_Mix channels..."),NULL,effects_mixchannels,0,NULL      },
	  { N_("/Effects/Sp_lit channel..."),NULL,effects_splitchannel,0,NULL    },
	  {N_("/Effects/_Combine channels..."),NULL,effects_combinechannels,0,NULL},
	  { N_("/Effects/sep3"),  NULL,         NULL,           0, "<Separator>" },
	  { N_("/Effects/_Speed adjustment..."),NULL,effects_speed, 0, NULL      },
	  { N_("/Effects/Pipe through program..."),NULL,effects_pipe,0,NULL      },
	  { N_("/Effects/sep4"),  NULL,         NULL,           0, "<Separator>" },
	  { N_("/Effects/Effects dialog..."),"<control>E",effects_dialog,0,NULL  },
#ifdef SHOW_DEBUG_MENU
	  { N_("/Debug"),         NULL,         NULL,           0, "<Branch>"    },
	  { N_("/Debug/Mark as modified"),NULL, debug_mark,     0, NULL          },
	  { N_("/Debug/Dummy effect"),NULL,     debug_dummy,    0, NULL          },
	  { N_("/Debug/Check opencount"),NULL,  debug_checkoc,  0, NULL          },
	  { N_("/Debug/Dump chunk info"),NULL,  debug_chunkinfo,0, NULL          },
#endif
	  { N_("/_Help"),         NULL,         NULL,           0, "<LastBranch>"},
	  { N_("/Help/_Documentation"),"F1",    help_readme,    0, NULL          },
	  { N_("/Help/_About"),   NULL,         help_about,     0, NULL          }
     };

     GtkItemFactoryEntry menu_items2[] = {
	  { N_("/File/sep1"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/File/_Close"),   NULL,         file_close,     0, NULL          },
	  { N_("/File/sep2"),     NULL,         NULL,           0, "<Separator>" },
	  { N_("/File/_Exit"),    NULL,         file_exit,      0, NULL          }
     };

     gchar *need_chunk_names[] = {
	  "/File/Save", "/File/Save as...", 
	  "/Edit/Insert silence", 
	  "/Edit/Select all", "/Cursor", "/View", "/Play/Play from cursor",
	  "/Play/Play all", "/Play/Stop", "/Effects", 
	  "/Cursor/Move to beginning", "/Cursor/Move to end", 
	  "/Cursor/Position cursor...", "/Effects/Normalize",
	  "/Effects/Effects dialog..."
     };

     gchar *need_selection_names[] = {
	  "/Edit/Cut", "/Edit/Copy", "/Edit/Delete", "/Edit/Crop", 
	  "/Edit/Silence selection",
	  "/View/Zoom to selection", "/File/Save selection as...",
	  "/Play/Play selection", "/Cursor/Move to selection start", 
	  "/Cursor/Move to selection end"
     };

     gchar *need_clipboard_names[] = {
	  "/Edit/Paste", "/Edit/Paste over", "/Edit/Mix paste", 
	  "/Edit/Paste to new", "/Edit/Clear clipboard"
     };

     GtkAccelGroup *accel_group;
     GtkItemFactory *item_factory;
     
     accel_group = gtk_accel_group_new();
     item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR,"<main>",
					 accel_group);

#ifdef ENABLE_NLS
     gtk_item_factory_set_translate_func(item_factory,
	     translate_menu_path, NULL, NULL);
#endif

     gtk_item_factory_create_items_ac(item_factory,ARRAY_LENGTH(menu_items1),
				      menu_items1,w,2);
     w->recent = NULL;
     j = inifile_get_guint32("recentFiles",4);
     if (j > MAINWINDOW_RECENT_MAX) j=4;
     if (j > 0) {
	  entry.path = "/File/sep3";
	  entry.item_type = "<Separator>";
	  entry.callback = NULL;
	  gtk_item_factory_create_item(item_factory,&entry,w,2);
	  item = gtk_item_factory_get_item(item_factory,entry.path);
     }
     for (i=1; i<=j; i++) {
	  entry.item_type = NULL;	  
	  entry.path = g_strdup_printf("/File/Last%d",i);
	  entry.callback = (GtkItemFactoryCallback)file_recent;
	  gtk_item_factory_create_item(item_factory,&entry,w,2);
	  item = gtk_item_factory_get_item(item_factory,entry.path);
	  w->recent = g_list_append(w->recent, item);
	  gtk_widget_set_sensitive(item,FALSE);
	  g_free(entry.path);
     }
     gtk_item_factory_create_items_ac(item_factory,ARRAY_LENGTH(menu_items2),
				      menu_items2,w,2);
     gtk_window_add_accel_group(GTK_WINDOW(w),accel_group);

     for (i=0; i<ARRAY_LENGTH(need_chunk_names); i++)
	  w->need_chunk_items = 
	       g_list_append(w->need_chunk_items,gtk_item_factory_get_item(
				  item_factory,need_chunk_names[i]));

     for (i=0; i<ARRAY_LENGTH(need_selection_names); i++)
	  w->need_selection_items = 
	       g_list_append(w->need_selection_items,gtk_item_factory_get_item(
				  item_factory,need_selection_names[i]));

     for (i=0; i<ARRAY_LENGTH(need_clipboard_names); i++)
	  w->need_clipboard_items = 
	       g_list_append(w->need_clipboard_items,gtk_item_factory_get_item(
				  item_factory,need_clipboard_names[i]));
     
     w->need_undo_items = 
	  g_list_append(w->need_undo_items,gtk_item_factory_get_item
			(item_factory,"/Edit/Undo") );
     w->need_redo_items = 
	  g_list_append(w->need_redo_items,gtk_item_factory_get_item
			(item_factory,"/Edit/Redo"));

     update_file_recent(w);

     item = gtk_item_factory_get_item(item_factory,"/View/Zoom in");
     w->zoom_items = g_list_append(w->zoom_items,item);
     item = gtk_item_factory_get_item(item_factory,"/View/Zoom out");
     w->zoom_items = g_list_append(w->zoom_items,item);     

     item = gtk_item_factory_get_item(item_factory,"/View/Time scale");
     gtk_check_menu_item_set_active(
	  GTK_CHECK_MENU_ITEM(item),
	  inifile_get_gboolean(INI_SETTING_TIMESCALE,
			       INI_SETTING_TIMESCALE_DEFAULT));

     item = gtk_item_factory_get_item(item_factory,"/View/Horizontal zoom");
     gtk_check_menu_item_set_active
	  (GTK_CHECK_MENU_ITEM(item),
	   inifile_get_gboolean(INI_SETTING_HZOOM,INI_SETTING_HZOOM_DEFAULT));

     item = gtk_item_factory_get_item(item_factory,"/View/Vertical zoom");
     gtk_check_menu_item_set_active
	  (GTK_CHECK_MENU_ITEM(item),
	   inifile_get_gboolean(INI_SETTING_VZOOM,INI_SETTING_VZOOM_DEFAULT));

     item = gtk_item_factory_get_item(item_factory,"/View/Speed slider");
     gtk_check_menu_item_set_active
	  (GTK_CHECK_MENU_ITEM(item),
	   inifile_get_gboolean(INI_SETTING_SPEED,INI_SETTING_SPEED_DEFAULT));
							  
     item = gtk_item_factory_get_item(item_factory,"/Play/Record...");
     gtk_widget_set_sensitive(item,input_supported());

     return gtk_item_factory_get_widget(item_factory,"<main>");
}

static void loopmode_toggle(GtkToggleButton *button, gboolean *user_data)
{
     *user_data = gtk_toggle_button_get_active(button);
     inifile_set_gboolean("loopMode", *user_data);
}

static void followmode_toggle(GtkToggleButton *button, gboolean *user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     w->followmode = gtk_toggle_button_get_active(button);
     if (w->doc != NULL)
	  document_set_followmode(w->doc, w->followmode);
     inifile_set_gboolean("followMode", w->followmode);
}

static void bouncemode_toggle(GtkToggleButton *button, gboolean *user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     w->bouncemode = gtk_toggle_button_get_active(button);
     inifile_set_gboolean("bounceMode", gtk_toggle_button_get_active(button));
}

static GtkWidget *create_toolbar(Mainwindow *w)
{
     GtkWidget *t,*b,*r;
     GdkPixmap *p;
     GdkBitmap *bmp;
#if GTK_MAJOR_VERSION == 2
     t = gtk_toolbar_new();
#else
     t = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
#endif
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_open_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Load a file from disk"),"X",b,GTK_SIGNAL_FUNC(file_open),w);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_save_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Save the current file to disk"),"X",b,GTK_SIGNAL_FUNC(file_save),w);
     w->need_chunk_items = g_list_append(w->need_chunk_items,r);
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_undo_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Undo the last change"),"X",b,GTK_SIGNAL_FUNC(edit_undo),w);
     w->need_undo_items = g_list_append(w->need_undo_items, r);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_redo_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Redo the last undo operation"),"X",b,GTK_SIGNAL_FUNC(edit_redo),
	  w);
     w->need_redo_items = g_list_append(w->need_redo_items, r);
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_cut_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Cut out the current selection"),"X",b,GTK_SIGNAL_FUNC(edit_cut),w);
     w->need_selection_items = g_list_append(w->need_selection_items, r);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_copy_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Copy the current selection"),"X",b,GTK_SIGNAL_FUNC(edit_copy),w);
     w->need_selection_items = g_list_append(w->need_selection_items, r);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_paste_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Paste at cursor position"),"X",b,GTK_SIGNAL_FUNC(edit_paste),w);
     w->need_clipboard_items = g_list_append(w->need_clipboard_items, r);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_pasteover_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Paste, overwriting the data after the cursor position"),"X",b,
	  GTK_SIGNAL_FUNC(edit_pasteover),w);
     w->need_clipboard_items = g_list_append(w->need_clipboard_items, r);
     gtk_widget_set_sensitive(r,FALSE);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_delete_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Delete the selection"),"X",b,GTK_SIGNAL_FUNC(edit_delete),w);
     w->need_selection_items = g_list_append(w->need_selection_items, r);
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_cursorstart_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Set selection start to cursor position"),"X",b,
	  GTK_SIGNAL_FUNC(edit_selstartcursor),w);
     w->need_chunk_items = g_list_append(w->need_chunk_items, r);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_cursorend_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Set selection end to cursor position"),"X",b,
	  GTK_SIGNAL_FUNC(edit_selendcursor),w);
     w->need_chunk_items = g_list_append(w->need_chunk_items, r);
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_play_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Play from cursor position"),"X",b,
	  GTK_SIGNAL_FUNC(edit_play),w);
     w->need_chunk_items = g_list_append(w->need_chunk_items, r);
     p = gdk_pixmap_colormap_create_from_xpm_d
       (NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	button_playselection_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Play selected area"),"X",b,
	  GTK_SIGNAL_FUNC(edit_playselection),w);
     w->need_chunk_items = g_list_append(w->need_chunk_items,r);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_stop_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Stop playing"),"X",b,
	  GTK_SIGNAL_FUNC(edit_stop),w);
     w->need_chunk_items = g_list_append(w->need_chunk_items, r);
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_loop_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_TOGGLEBUTTON,NULL,NULL,
	  _("Loop mode (play over and over)"),"X",b,
	  GTK_SIGNAL_FUNC(loopmode_toggle),&(w->loopmode));
     if ( inifile_get_gboolean("loopMode",FALSE) ) 
	  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(r), TRUE);
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_follow_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_TOGGLEBUTTON,NULL,NULL,
	  _("Follow cursor while playing"),"X",b,
	  GTK_SIGNAL_FUNC(followmode_toggle),w);
     if ( inifile_get_gboolean("followMode",FALSE) ) {
	  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(r), TRUE);
	  w->followmode = TRUE;
     } else
	  w->followmode = FALSE;
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_bounce_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_TOGGLEBUTTON,NULL,NULL,
	  _("Auto return to playback start"),"X",b,
	  GTK_SIGNAL_FUNC(bouncemode_toggle),w);
     if ( inifile_get_gboolean("bounceMode",FALSE) ) {
	  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(r), TRUE);
	  w->bouncemode = TRUE;
     }
     
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_record_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Record"),"X",b,GTK_SIGNAL_FUNC(edit_record),w);
     gtk_widget_set_sensitive(r,input_supported());
     gtk_toolbar_append_space(GTK_TOOLBAR(t));
     p = gdk_pixmap_colormap_create_from_xpm_d(
	  NULL, gtk_widget_get_colormap(GTK_WIDGET(w)), &bmp, NULL,
	  button_mixer_xpm);
     b = gtk_pixmap_new(p, bmp);
     r = gtk_toolbar_append_element(
	  GTK_TOOLBAR(t),GTK_TOOLBAR_CHILD_BUTTON,NULL,NULL,
	  _("Launch mixer"),"X",b,GTK_SIGNAL_FUNC(launch_mixer),w);     
     return t;
}

static gboolean setting_zoom_flag = FALSE;

static void mainwindow_view_changed(Document *d, gpointer user_data)
{
     float max_samp,min_samp,current_samp;
     Mainwindow *w = MAINWINDOW(user_data);
     w->view_adj->page_size = (d->viewend - d->viewstart);
     w->view_adj->upper = d->chunk->length;
     w->view_adj->value = d->viewstart;
     w->view_adj->step_increment = 16;
     w->view_adj->page_increment = w->view_adj->page_size / 2;     
     gtk_adjustment_changed ( w->view_adj );
     setting_zoom_flag = TRUE;
     max_samp = w->doc->chunk->length;
     min_samp = GTK_WIDGET(w->view)->allocation.width;
     current_samp = w->doc->viewend - w->doc->viewstart;
     if (current_samp < min_samp) {
	  if (current_samp < w->doc->chunk->length) {
	       /* We are zoomed in longer than the slider allows, so set it 
		* to max */
	       w->zoom_adj->value = 1.0;
	       w->zoom_adj->page_size = 0.2;
	       set_sensitive(w->zoom_items,TRUE);
	  } else {
	       /* We're viewing the whole chunk and can't zoom in or out */
	       w->zoom_adj->value = 0.0;
	       w->zoom_adj->page_size = 1.2;
	       set_sensitive(w->zoom_items,FALSE);
	  }
     } else {
	  w->zoom_adj->value = 
	       log(current_samp/max_samp)/log(min_samp/max_samp);
	  w->zoom_adj->page_size = 0.2;
	  set_sensitive(w->zoom_items,TRUE);
     }
     gtk_adjustment_changed ( w->zoom_adj );
     setting_zoom_flag = FALSE;
     update_desc(w);
}

static void mainwindow_selection_changed(Document *d, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW ( user_data );
     update_desc ( w );
     set_sensitive(w->need_selection_items, (d->selstart != d->selend));
}

static void mainwindow_cursor_changed(Document *d, gboolean rolling, 
				      gpointer user_data)
{
     Mainwindow *w;
     w = MAINWINDOW ( user_data );
     if (!rolling || status_bar_roll_cursor) update_desc(w);
}

static void mainwindow_state_changed(Document *d, gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     fix_title(w);
     update_desc(w);
     set_sensitive(w->need_undo_items,document_can_undo(d));
     set_sensitive(w->need_redo_items,document_can_redo(d));
     set_sensitive(w->need_selection_items,(d->selstart != d->selend));
}

static void mainwindow_value_changed(GtkAdjustment *adjustment, 
				     gpointer user_data)
{
     document_set_view( MAINWINDOW(user_data)->doc, adjustment->value, 
			adjustment->value + adjustment->page_size );
}

static void mainwindow_zoom_changed(GtkAdjustment *adjustment, 
				    gpointer user_data)
{
     float max_samp,min_samp,target_samp,current_samp;
     Mainwindow *w = MAINWINDOW(user_data);

     if (setting_zoom_flag) return;
     current_samp = w->doc->viewend - w->doc->viewstart;
     min_samp = GTK_WIDGET(w->view)->allocation.width;
     max_samp = w->doc->chunk->length;
     target_samp = max_samp * pow(min_samp/max_samp,adjustment->value);
     document_zoom(w->doc,current_samp/target_samp,(target_samp<current_samp));
}

static void mainwindow_vertical_zoom_changed(GtkAdjustment *adjustment,
					     gpointer user_data)
{
     gfloat s;
     Mainwindow *w = MAINWINDOW(user_data);
     s = pow(100.0, adjustment->value);
     chunk_view_set_scale(w->view, s);
}

static void mainwindow_speed_changed(GtkAdjustment *adjustment,
				     gpointer user_data)
{
     Mainwindow *w = MAINWINDOW(user_data);
     gchar c[32];
     if (w->doc == playing_document)
	  player_set_speed(-adjustment->value);
     g_snprintf(c,sizeof(c),"%d%%",(int)(-adjustment->value*100.0+0.5));
     gtk_label_set_text(w->speed_label,c);
}

static void dc_func(gchar *label, off_t pos, gpointer fud)
{
     off_t *ud = (off_t *)fud;
     if (pos < ud[0] && pos > ud[1]) ud[1] = pos;
     else if (pos > ud[0] && pos < ud[2]) ud[2] = pos;
}

static void mainwindow_view_double_click(ChunkView *view, 
					 off_t *sample, Mainwindow *wnd)
{
     off_t o[3] = { *sample, 0, wnd->doc->chunk->length };               
     document_foreach_mark(wnd->doc,dc_func,o);
     document_set_selection(wnd->doc,o[1],o[2]);
}

static void mainwindow_init(Mainwindow *obj)
{
     GtkWidget *a,*b,*c;
     GdkPixmap *p;
     GdkBitmap *mask;
     GtkRequisition req;

     if (!window_geometry_stack_inited) {
	  if (inifile_get_gboolean("useGeometry",FALSE))
	       window_geometry_stack = 
		    geometry_stack_from_inifile("windowGeometry");
	  window_geometry_stack_inited = TRUE;
	  load_recent();
     }

     /* St�ll in datastrukturen */
     obj->view = CHUNKVIEW(chunk_view_new());

     chunk_view_set_timescale(
	  obj->view, inifile_get_gboolean(INI_SETTING_TIMESCALE,
					  INI_SETTING_TIMESCALE_DEFAULT));
     gtk_signal_connect( GTK_OBJECT(obj->view), "double-click",
			 GTK_SIGNAL_FUNC(mainwindow_view_double_click), obj);
     obj->view_adj = GTK_ADJUSTMENT( gtk_adjustment_new ( 0,0,0,0,0,0 ));
     gtk_signal_connect( GTK_OBJECT(obj->view_adj), "value-changed", 
			 GTK_SIGNAL_FUNC(mainwindow_value_changed), obj);
     obj->zoom_adj = GTK_ADJUSTMENT( gtk_adjustment_new ( 0, 0, 1.2,0.01, 0.1, 
							  0.2 ));
     gtk_signal_connect( GTK_OBJECT(obj->zoom_adj), "value-changed",
			 GTK_SIGNAL_FUNC(mainwindow_zoom_changed), obj);
     obj->vertical_zoom_adj = 
	  GTK_ADJUSTMENT(gtk_adjustment_new( 0.0,0.0,1.2,0.01,0.1,0.2) );
     gtk_signal_connect( GTK_OBJECT(obj->vertical_zoom_adj), "value-changed",
			 GTK_SIGNAL_FUNC(mainwindow_vertical_zoom_changed),
			 obj);
     obj->speed_adj =
	  GTK_ADJUSTMENT(gtk_adjustment_new(-1.0,-5.0,0.19,0.01,0.1,0.2));
     gtk_signal_connect(GTK_OBJECT(obj->speed_adj),"value-changed",
			GTK_SIGNAL_FUNC(mainwindow_speed_changed),
			obj);

     obj->statusbar = STATUSBAR(status_bar_new());
     gtk_signal_connect(GTK_OBJECT(obj->statusbar),"progress_begin",
			GTK_SIGNAL_FUNC(procstart),obj);
     gtk_signal_connect(GTK_OBJECT(obj->statusbar),"progress_end",
			GTK_SIGNAL_FUNC(procend),obj);
     obj->loopmode = inifile_get_gboolean("loopMode",FALSE);
     obj->bouncemode = FALSE; /* Set to true by create_toolbar */
     obj->sensitive = TRUE;
     obj->vzoom_icon = obj->vzoom_slider = NULL;
     obj->hzoom_icon = obj->hzoom_slider = NULL;
     obj->speed_icon = obj->speed_slider = NULL;
     obj->speed_label = GTK_LABEL(gtk_label_new("500%"));     
     gtk_misc_set_alignment(GTK_MISC(obj->speed_label),1.0,0.0);
     gtk_widget_size_request(GTK_WIDGET(obj->speed_label),&req);
     gtk_widget_set_usize(GTK_WIDGET(obj->speed_label),req.width,req.height);

     obj->need_chunk_items = NULL;
     obj->need_selection_items = NULL;
     obj->need_clipboard_items = NULL;
     obj->need_undo_items = NULL;
     obj->zoom_items = NULL;
     
     fix_title ( obj );
     
     /* Bygg upp komponenterna */
     a = gtk_vbox_new( FALSE, 0 );
     gtk_container_add(GTK_CONTAINER(obj),a);
     b = create_menu( obj );
     obj->menubar = b;
     gtk_box_pack_start(GTK_BOX(a),b,FALSE,FALSE,0);
     b = gtk_table_new(3,4,FALSE);
     gtk_box_pack_start(GTK_BOX(a),b,TRUE,TRUE,0);

     c = create_toolbar( obj );
     obj->toolbar = c;

     c = gtk_event_box_new();
     gtk_container_add(GTK_CONTAINER(c),GTK_WIDGET(obj->toolbar));
     gtk_widget_size_request(c,&req);
     gtk_widget_set_usize(c,10,req.height);

     gtk_table_attach(GTK_TABLE(b),c,0,1,0,1,GTK_SHRINK|GTK_FILL,GTK_FILL,0,0);

     p = gdk_pixmap_colormap_create_from_xpm_d
	  (NULL,gtk_widget_get_colormap(GTK_WIDGET(obj)),&mask,NULL,vzoom_xpm);
     c = gtk_pixmap_new(p,mask);
     gtk_table_attach(GTK_TABLE(b),c,1,2,0,1,GTK_FILL,0,0,0);
     obj->vzoom_icon = c;

     p = gdk_pixmap_colormap_create_from_xpm_d
	  (NULL,gtk_widget_get_colormap(GTK_WIDGET(obj)),&mask,NULL,hzoom_xpm);
     c = gtk_pixmap_new(p,mask);
     gtk_table_attach(GTK_TABLE(b),c,2,3,0,1,GTK_FILL,0,0,0);
     obj->hzoom_icon = c;

     p = gdk_pixmap_colormap_create_from_xpm_d
	  (NULL,gtk_widget_get_colormap(GTK_WIDGET(obj)),&mask,NULL,speed_xpm);
     c = gtk_pixmap_new(p,mask);
     gtk_table_attach(GTK_TABLE(b),c,3,4,0,1,GTK_FILL,0,0,0);
     obj->speed_icon = c;

     gtk_table_attach(GTK_TABLE(b),GTK_WIDGET(obj->view),0,1,1,2,
		      GTK_EXPAND|GTK_FILL|GTK_SHRINK,
		      GTK_EXPAND|GTK_FILL|GTK_SHRINK,0,0);

     c = gtk_vscale_new ( obj->zoom_adj );
     gtk_scale_set_digits(GTK_SCALE(c),3);
     gtk_scale_set_draw_value (GTK_SCALE(c), FALSE);
     gtk_table_attach(GTK_TABLE(b),c,2,3,1,2,GTK_FILL,GTK_EXPAND|GTK_FILL,0,0);
     obj->need_chunk_items = g_list_append(obj->need_chunk_items,c);
     obj->hzoom_slider = c;

     c = gtk_vscale_new ( obj->vertical_zoom_adj );
     gtk_scale_set_digits(GTK_SCALE(c),3);
     gtk_scale_set_draw_value (GTK_SCALE(c), FALSE);
     gtk_table_attach(GTK_TABLE(b),c,1,2,1,2,GTK_FILL,GTK_EXPAND|GTK_FILL,0,0);
     obj->need_chunk_items = g_list_append(obj->need_chunk_items,c);
     obj->vzoom_slider = c;

     c = gtk_vscale_new ( obj->speed_adj );
     gtk_scale_set_digits(GTK_SCALE(c),2);
     gtk_scale_set_draw_value (GTK_SCALE(c), FALSE);
     /* gtk_range_set_update_policy(GTK_RANGE(c),GTK_UPDATE_DELAYED); */
     gtk_table_attach(GTK_TABLE(b),c,3,4,1,2,GTK_FILL,GTK_EXPAND|GTK_FILL,0,0);
     obj->need_chunk_items = g_list_append(obj->need_chunk_items,c);
     obj->speed_slider = c;

     c = gtk_hscrollbar_new ( obj->view_adj );
     gtk_table_attach(GTK_TABLE(b),c,0,1,2,3,GTK_FILL,0,0,0);

     c = GTK_WIDGET(obj->speed_label);
     gtk_table_attach(GTK_TABLE(b),c,1,4,2,3,GTK_FILL,0,0,0);

     gtk_box_pack_start(GTK_BOX(a),GTK_WIDGET(obj->statusbar),FALSE,TRUE,0);
     gtk_adjustment_value_changed(obj->speed_adj);
     gtk_widget_show_all(a);

     if (!inifile_get_gboolean(INI_SETTING_VZOOM,INI_SETTING_VZOOM_DEFAULT)) {
	  gtk_widget_hide(obj->vzoom_icon);
	  gtk_widget_hide(obj->vzoom_slider);
     }

     if (!inifile_get_gboolean(INI_SETTING_HZOOM,INI_SETTING_HZOOM_DEFAULT)) {
	  gtk_widget_hide(obj->hzoom_icon);
	  gtk_widget_hide(obj->hzoom_slider);
     }
     
     if (!inifile_get_gboolean(INI_SETTING_SPEED,INI_SETTING_SPEED_DEFAULT)) {
	  gtk_widget_hide(obj->speed_icon);
	  gtk_widget_hide(obj->speed_slider);
	  gtk_widget_hide(GTK_WIDGET(obj->speed_label));
     }

     gtk_window_set_policy(GTK_WINDOW(obj),FALSE,TRUE,FALSE);
     
     set_sensitive(obj->need_chunk_items, FALSE);
     set_sensitive(obj->need_selection_items, FALSE);
     set_sensitive(obj->need_clipboard_items, clipboard != NULL);
     set_sensitive(obj->need_undo_items, FALSE);
     set_sensitive(obj->need_redo_items, FALSE);

     if (!geometry_stack_pop(&window_geometry_stack,NULL,GTK_WINDOW(obj)))
	  gtk_window_set_default_size(GTK_WINDOW(obj),540,230);
}

guint mainwindow_get_type(void)
{
static guint id=0;
if (!id) {
	GtkTypeInfo info = {
	     "Mainwindow",
	     sizeof(Mainwindow),
	     sizeof(MainwindowClass),
	     (GtkClassInitFunc) mainwindow_class_init,
	     (GtkObjectInitFunc) mainwindow_init 
	};
	id=gtk_type_unique(gtk_window_get_type(),&info);
	}
return id;
}

GtkWidget *mainwindow_new(void)
{
     return GTK_WIDGET( gtk_type_new(mainwindow_get_type()) );
}

GtkWidget *mainwindow_new_with_file(char *filename)
{
     Mainwindow *w;
     Chunk *c;
     gchar *d;
     w = MAINWINDOW ( mainwindow_new() );
     d = make_filename_rooted(filename);
     c = chunk_load(d, dither_editing, w->statusbar);
     if (c) {
          mainwindow_set_chunk(w, c, d);
          inifile_set("lastOpenFile",d);
	  recent_file(d);
     }
     g_free(d);
     return GTK_WIDGET(w);
}

static void reset_statusbar(Mainwindow *w)
{
     status_bar_reset(w->statusbar);
}

void mainwindow_update_texts(void)
{     
     list_object_foreach(mainwindow_objects,(GFunc)fix_title,NULL);
     list_object_foreach(mainwindow_objects,(GFunc)reset_statusbar,NULL);
     list_object_foreach(mainwindow_objects,(GFunc)update_desc,NULL);
}

void mainwindow_set_sensitive(Mainwindow *mw, gboolean sensitive)
{
     gtk_widget_set_sensitive(mw->menubar,sensitive);
     gtk_widget_set_sensitive(mw->toolbar,sensitive);
     gtk_widget_set_sensitive(GTK_WIDGET(mw->view),sensitive);
     mw->sensitive = sensitive;     
}

void mainwindow_set_all_sensitive(gboolean sensitive)
{
     list_object_foreach(mainwindow_objects,(GFunc)mainwindow_set_sensitive,
			 GINT_TO_POINTER(sensitive));
}

void mainwindow_repaint_views(void)
{
     GList *l;          
     for (l=mainwindow_objects->list; l!=NULL; l=l->next) {
	  chunk_view_force_repaint(MAINWINDOW(l->data)->view);
     }
}
