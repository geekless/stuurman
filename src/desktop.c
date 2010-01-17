/*
 *      desktop.c
 *      
 *      Copyright 2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *      
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include "desktop.h"
#include "pcmanfm.h"
#include "app-config.h"

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define SPACING 2
#define PADDING 6
#define MARGIN  2

struct _FmDesktopItem
{
    GtkTreeIter it;
    FmFileInfo* fi;
    GdkPixbuf* icon;
    int x; /* position of the item on the desktop */
    int y;
    GdkRectangle icon_rect;
    GdkRectangle text_rect;
    gboolean is_special : 1; /* is this a special item like "My Computer", mounted volume, or "Trash" */
    gboolean is_mount : 1; /* is this a mounted volume*/
    gboolean is_selected : 1;
    gboolean is_prelight : 1;
    gboolean custom_pos : 1;
};

/* static void fm_desktop_finalize              (GObject *object); */
static void fm_desktop_destroy               (GtkObject *object);

static FmDesktopItem* hit_test(FmDesktop* self, int x, int y);
static void calc_item_size(FmDesktop* desktop, FmDesktopItem* item);
static void layout_items(FmDesktop* self);
static void queue_layout_items(FmDesktop* desktop);
static void paint_item(FmDesktop* self, FmDesktopItem* item, cairo_t* cr, GdkRectangle* expose_area);
static void redraw_item(FmDesktop* desktop, FmDesktopItem* item);
static void calc_rubber_banding_rect(FmDesktop* self, int x, int y, GdkRectangle* rect);
static void update_rubberbanding(FmDesktop* self, int newx, int newy );
static void paint_rubber_banding_rect(FmDesktop* self, cairo_t* cr, GdkRectangle* expose_area);
static void update_background(FmDesktop* desktop);
static void update_working_area(FmDesktop* desktop);
static GList* get_selected_items(FmDesktop* desktop, int* n_items);
static void activate_selected_items(FmDesktop* desktop);

static FmDesktopItem* desktop_item_new(GtkTreeIter* it);
static void desktop_item_free(FmDesktopItem* item);

static gboolean on_expose( GtkWidget* w, GdkEventExpose* evt );
static void on_size_allocate( GtkWidget* w, GtkAllocation* alloc );
static void on_size_request( GtkWidget* w, GtkRequisition* req );
static gboolean on_button_press( GtkWidget* w, GdkEventButton* evt );
static gboolean on_button_release( GtkWidget* w, GdkEventButton* evt );
static gboolean on_motion_notify( GtkWidget* w, GdkEventMotion* evt );
static gboolean on_leave_notify( GtkWidget* w, GdkEventCrossing* evt );
static gboolean on_key_press( GtkWidget* w, GdkEventKey* evt );
static void on_style_set( GtkWidget* w, GtkStyle* prev );
static void on_direction_changed( GtkWidget* w, GtkTextDirection prev );
static void on_realize( GtkWidget* w );
static gboolean on_focus_in( GtkWidget* w, GdkEventFocus* evt );
static gboolean on_focus_out( GtkWidget* w, GdkEventFocus* evt );
static void on_drag_leave(GtkWidget* w, GdkDragContext* drag_ctx, guint time);

static void on_wallpaper_changed(FmConfig* cfg, gpointer user_data);
static void on_desktop_text_changed(FmConfig* cfg, gpointer user_data);
static void on_desktop_font_changed(FmConfig* cfg, gpointer user_data);
static void on_big_icon_size_changed(FmConfig* cfg, gpointer user_data);

static void on_row_inserted(GtkTreeModel* mod, GtkTreePath* tp, GtkTreeIter* it, FmDesktop* desktop);
static void on_row_deleted(GtkTreeModel* mod, GtkTreePath* tp, FmDesktop* desktop);
static void on_row_changed(GtkTreeModel* mod, GtkTreePath* tp, GtkTreeIter* it, FmDesktop* desktop);

static void on_dnd_src_data_get(FmDndSrc* ds, FmDesktop* desktop);
static gboolean on_dnd_dest_query_info(FmDndDest* dd, int x, int y,
                            			GdkDragAction* action, FmDesktop* desktop);
static void on_dnd_dest_files_dropped(FmDndDest* dd, GdkDragAction action,
                                       int info_type, FmList* files, FmDesktop* desktop);


static GdkFilterReturn on_root_event(GdkXEvent *xevent, GdkEvent *event, gpointer data);
static void on_screen_size_changed(GdkScreen* screen, FmDesktop* desktop);

G_DEFINE_TYPE(FmDesktop, fm_desktop, GTK_TYPE_WINDOW);

static GtkWindowGroup* win_group = NULL;
static GtkWidget **desktops = NULL;
static gint n_screens = 0;
static guint wallpaper_changed = 0;
static guint desktop_text_changed = 0;
static guint desktop_font_changed = 0;
static guint big_icon_size_changed = 0;

static PangoFontDescription* font_desc = NULL;

static FmFolderModel* model = NULL;

static Atom XA_NET_WORKAREA = 0;
static Atom XA_NET_NUMBER_OF_DESKTOPS = 0;
static Atom XA_NET_CURRENT_DESKTOP = 0;
static Atom XA_XROOTMAP_ID= 0;

enum {
    FM_DND_DEST_DESKTOP_ITEM = N_FM_DND_DEST_DEFAULT_TARGETS + 1
};

GtkTargetEntry dnd_targets[] = 
{
    {"application/x-desktop-item", GTK_TARGET_SAME_WIDGET, FM_DND_DEST_DESKTOP_ITEM}
};


static void fm_desktop_class_init(FmDesktopClass *klass)
{
    GObjectClass *g_object_class;
    GtkObjectClass *gtk_object_class;
    GtkWidgetClass* wc;
	typedef gboolean (*DeleteEvtHandler) (GtkWidget*, GdkEvent*);
    const char* atom_names[] = {"_NET_WORKAREA", "_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP", "_XROOTMAP_ID"};
    Atom atoms[G_N_ELEMENTS(atom_names)] = {0};

    /* g_object_class = G_OBJECT_CLASS(klass);
       g_object_class->finalize = fm_desktop_finalize; */
    gtk_object_class = GTK_OBJECT_CLASS(klass);
    gtk_object_class->destroy = fm_desktop_destroy;

    wc = GTK_WIDGET_CLASS(klass);
    wc->expose_event = on_expose;
    wc->size_allocate = on_size_allocate;
    wc->size_request = on_size_request;
    wc->button_press_event = on_button_press;
    wc->button_release_event = on_button_release;
    wc->motion_notify_event = on_motion_notify;
    wc->leave_notify_event = on_leave_notify;
    wc->key_press_event = on_key_press;
    wc->style_set = on_style_set;
    wc->direction_changed = on_direction_changed;
    wc->realize = on_realize;
    wc->focus_in_event = on_focus_in;
    wc->focus_out_event = on_focus_out;
    /* wc->scroll_event = on_scroll; */
    wc->delete_event = (DeleteEvtHandler)gtk_true;
    wc->drag_leave = on_drag_leave;

    if(XInternAtoms(GDK_DISPLAY(), atom_names, G_N_ELEMENTS(atom_names), False, atoms))
    {
        XA_NET_WORKAREA = atoms[0];
        XA_NET_NUMBER_OF_DESKTOPS = atoms[1];
        XA_NET_CURRENT_DESKTOP = atoms[2];
        XA_XROOTMAP_ID= atoms[3];
    }
}

static void desktop_item_free(FmDesktopItem* item)
{
    if(item->icon)
        g_object_unref(item->icon);
    g_slice_free(FmDesktopItem, item);
}

static void fm_desktop_destroy(GtkObject *object)
{
    FmDesktop *self;
    GdkScreen* screen;

    self = FM_DESKTOP(object);
    screen = gtk_widget_get_screen((GtkWidget*)self);
    gdk_window_remove_filter(gdk_screen_get_root_window(screen), on_root_event, self);

    g_list_foreach(self->items, (GFunc)desktop_item_free, NULL);
    g_list_free(self->items);
    self->items = NULL;

    g_object_unref(self->icon_render);
    g_object_unref(self->pl);

    g_signal_handlers_disconnect_by_func(model, on_row_inserted, self);
    g_signal_handlers_disconnect_by_func(model, on_row_deleted, self);
    g_signal_handlers_disconnect_by_func(model, on_row_changed, self);

    if(self->single_click_timeout_handler)
        g_source_remove(self->single_click_timeout_handler);

    if(self->idle_layout)
        g_source_remove(self->idle_layout);

    G_OBJECT_CLASS(fm_desktop_parent_class)->dispose(object);
}

static void fm_desktop_init(FmDesktop *self)
{
    GdkScreen* screen = gtk_widget_get_screen((GtkWidget*)self);
    GdkWindow* root;
    PangoContext* pc;
    GtkTreeIter it;
    GtkCellRenderer* renderer;
    GtkTargetList* targets;

    gtk_window_set_default_size((GtkWindow*)self, gdk_screen_get_width(screen), gdk_screen_get_height(screen));
    gtk_window_move(self, 0, 0);
    gtk_widget_set_app_paintable((GtkWidget*)self, TRUE);
    gtk_window_set_type_hint(self, GDK_WINDOW_TYPE_HINT_DESKTOP);
    gtk_widget_add_events((GtkWidget*)self,
                        GDK_POINTER_MOTION_MASK |
                        GDK_BUTTON_PRESS_MASK |
                        GDK_BUTTON_RELEASE_MASK |
                        GDK_KEY_PRESS_MASK|
                        GDK_PROPERTY_CHANGE_MASK);

    self->icon_render = gtk_cell_renderer_pixbuf_new();
    g_object_set( self->icon_render, "follow-state", TRUE, NULL);
    g_object_ref_sink(self->icon_render);

    /* FIXME: call pango_layout_context_changed() on the layout in response to the
     * "style-set" and "direction-changed" signals for the widget. */
    pc = gtk_widget_get_pango_context( (GtkWidget*)self );
    self->pl = gtk_widget_create_pango_layout( (GtkWidget*)self, NULL );
    pango_layout_set_alignment( self->pl, PANGO_ALIGN_CENTER );
    pango_layout_set_ellipsize( self->pl, PANGO_ELLIPSIZE_END );
    pango_layout_set_wrap(self->pl, PANGO_WRAP_WORD_CHAR);

    g_signal_connect(model, "row-inserted", G_CALLBACK(on_row_inserted), self);
    g_signal_connect(model, "row-deleted", G_CALLBACK(on_row_deleted), self);
    g_signal_connect(model, "row-changed", G_CALLBACK(on_row_changed), self);

    root = gdk_screen_get_root_window(screen);
    gdk_window_set_events(root, gdk_window_get_events(root)|GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(root, on_root_event, self);
    g_signal_connect(screen, "size-changed", G_CALLBACK(on_screen_size_changed), self);

    /* init dnd support */
    gtk_drag_source_set(self, 0,
            fm_default_dnd_dest_targets, N_FM_DND_DEST_DEFAULT_TARGETS,
            GDK_ACTION_COPY|GDK_ACTION_MOVE|GDK_ACTION_LINK|GDK_ACTION_ASK);
    targets = gtk_drag_source_get_target_list((GtkWidget*)self);
    /* add our own targets */
    gtk_target_list_add_table(targets, dnd_targets, G_N_ELEMENTS(dnd_targets));
    self->dnd_src = fm_dnd_src_new((GtkWidget*)self);
    g_signal_connect(self->dnd_src, "data-get", G_CALLBACK(on_dnd_src_data_get), self);

    gtk_drag_dest_set(self, 0, NULL, 0,
            GDK_ACTION_COPY|GDK_ACTION_MOVE|GDK_ACTION_LINK|GDK_ACTION_ASK);
    gtk_drag_dest_set_target_list(self, targets);

    self->dnd_dest = fm_dnd_dest_new((GtkWidget*)self);
    g_signal_connect(self->dnd_dest, "query-info", G_CALLBACK(on_dnd_dest_query_info), self);
    g_signal_connect(self->dnd_dest, "files_dropped", G_CALLBACK(on_dnd_dest_files_dropped), self);

    /* add items */
    if(gtk_tree_model_get_iter_first(model, &it))
    {
        do{
            FmDesktopItem* item = desktop_item_new(&it);
            self->items = g_list_prepend(self->items, item);
        }while(gtk_tree_model_iter_next(model, &it));
        self->items = g_list_reverse(self->items);
    }
}


GtkWidget *fm_desktop_new(void)
{
    return (GtkWidget*)g_object_new(FM_TYPE_DESKTOP, NULL);
}


void fm_desktop_manager_init()
{
    GdkDisplay * gdpy;
    gint i;

    if( ! win_group )
        win_group = gtk_window_group_new();

    if(!model)
    {
        FmFolder* folder = fm_folder_get_for_path(fm_path_get_desktop());
        if(folder)
            model = fm_folder_model_new(folder, FALSE);
    }

    if(app_config->desktop_font)
        font_desc = pango_font_description_from_string(app_config->desktop_font);

    gdpy = gdk_display_get_default();
    n_screens = gdk_display_get_n_screens(gdpy);
    desktops = g_new(GtkWidget*, n_screens);
    for( i = 0; i < n_screens; i++ )
    {
        GtkWidget* desktop = fm_desktop_new();
        desktops[i] = desktop;
        gtk_widget_realize(desktop);  /* without this, setting wallpaper won't work */
        gtk_widget_show_all(desktop);
        gdk_window_lower(desktop ->window);
        gtk_window_group_add_window( GTK_WINDOW_GROUP(win_group), desktop );
    }

    wallpaper_changed = g_signal_connect(app_config, "changed::wallpaper", G_CALLBACK(on_wallpaper_changed), NULL);
    desktop_text_changed = g_signal_connect(app_config, "changed::desktop_text", G_CALLBACK(on_desktop_text_changed), NULL);
    desktop_font_changed = g_signal_connect(app_config, "changed::desktop_font", G_CALLBACK(on_desktop_font_changed), NULL);
    big_icon_size_changed = g_signal_connect(app_config, "changed::big_icon_size", G_CALLBACK(on_big_icon_size_changed), NULL);

    pcmanfm_ref();
}

void fm_desktop_manager_finalize()
{
    int i;
    for( i = 0; i < n_screens; i++ )
    {
        gtk_widget_destroy(desktops[i]);
    }
    g_free(desktops);
    g_object_unref(win_group);
    win_group = NULL;

    if(model)
    {
        g_object_unref(model);
        model = NULL;
    }

    if(font_desc)
    {
        pango_font_description_free(font_desc);
        font_desc = NULL;
    }

    g_signal_handler_disconnect(app_config, wallpaper_changed);
    g_signal_handler_disconnect(app_config, desktop_text_changed);
    g_signal_handler_disconnect(app_config, desktop_font_changed);
    g_signal_handler_disconnect(app_config, big_icon_size_changed);

    pcmanfm_unref();
}

void activate_selected_items(FmDesktop* desktop)
{
    int n_sels;
    GList* items = get_selected_items(desktop, &n_sels);
    GList* l;

    if(!items)
        return;

    for(l=items;l;l=l->next)
    {
        FmDesktopItem* item = (FmDesktopItem*)l->data;

    }
    g_list_free(items);
}

gboolean on_button_press( GtkWidget* w, GdkEventButton* evt )
{
    FmDesktop* self = (FmDesktop*)w;
    FmDesktopItem *item, *clicked_item = NULL;
    GList* l;

    clicked_item = hit_test( FM_DESKTOP(w), (int)evt->x, (int)evt->y );

    if( evt->type == GDK_BUTTON_PRESS )
    {
        if( evt->button == 1 )  /* left button */
        {
            self->button_pressed = TRUE;    /* store button state for drag & drop */
            self->drag_start_x = evt->x;
            self->drag_start_y = evt->y;
        }

        /* if ctrl / shift is not pressed, deselect all. */
        if( ! (evt->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) )
        {
            /* don't cancel selection if clicking on selected items */
            if( !( (evt->button == 1 || evt->button == 3) && clicked_item && clicked_item->is_selected) )
            {
                for( l = self->items; l ;l = l->next )
                {
                    item = (FmDesktopItem*) l->data;
                    if( item->is_selected )
                    {
                        item->is_selected = FALSE;
                        redraw_item( self, item );
                    }
                }
            }
        }

        if( clicked_item )
        {
            if( evt->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK) )
                clicked_item->is_selected = ! clicked_item->is_selected;
            else
                clicked_item->is_selected = TRUE;

            if( self->focus && self->focus != item )
            {
                FmDesktopItem* old_focus = self->focus;
                self->focus = NULL;
                if( old_focus )
        		    redraw_item( FM_DESKTOP(w), old_focus );
            }
            self->focus = clicked_item;
            redraw_item( self, clicked_item );

            /* left single click */
            if( evt->button == 1 && fm_config->single_click && clicked_item->is_selected )
            {
                fm_launch_file(w, NULL, clicked_item->fi);
                goto out;
            }
            if( evt->button == 3 )  /* right click, context menu */
            {
                int n_sels;
                GList* items = get_selected_items(self, &n_sels);
                if( items )
                {
                    GList* sel;
                    
                }
            }
            goto out;
        }
        else /* no item is clicked */
        {
            if( evt->button == 3 )  /* right click on the blank area => desktop popup menu */
            {
#if 0
                if( ! app_settings.show_wm_menu ) /* if our desktop menu is used */
                {
                    GtkWidget *popup, *sort_by_items[ 4 ], *sort_type_items[ 2 ];
                    int i;
                    /* show the desktop menu */
                    for( i = 0; i < 4; ++i )
                        icon_menu[ i ].ret = &sort_by_items[ i ];
                    for( i = 0; i < 2; ++i )
                        icon_menu[ 5 + i ].ret = &sort_type_items[ i ];
                    popup = ptk_menu_new_from_data( (PtkMenuItemEntry*)&desktop_menu, self, NULL );
                    //gtk_check_menu_item_set_active( (GtkCheckMenuItem*)sort_by_items[ self->sort_by ], TRUE );
                    //gtk_check_menu_item_set_active( (GtkCheckMenuItem*)sort_type_items[ self->sort_type ], TRUE );
                    gtk_widget_show_all(popup);
                    g_signal_connect( popup, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL );

                    gtk_menu_popup( GTK_MENU(popup), NULL, NULL, NULL, NULL, evt->button, evt->time );
                    goto out;   /* don't forward the event to root win */
                }
#endif
            }
            else if( evt->button == 1 )
            {
                self->rubber_bending = TRUE;

                /* FIXME: if you foward the event here, this will break rubber bending... */
                /* forward the event to root window */
                /* forward_event_to_rootwin( gtk_widget_get_screen(w), evt ); */
				
                gtk_grab_add( w );
                self->rubber_bending_x = evt->x;
                self->rubber_bending_y = evt->y;
                goto out;
            }
        }
    }
    else if( evt->type == GDK_2BUTTON_PRESS ) /* activate items */
    {
        if( clicked_item && evt->button == 1)   /* left double click */
        {
            fm_launch_file(w, NULL, clicked_item->fi);
            goto out;
        }
    }
    /* forward the event to root window */
//    forward_event_to_rootwin( gtk_widget_get_screen(w), evt );

out:
    if( ! GTK_WIDGET_HAS_FOCUS(w) )
    {
        /* g_debug( "we don't have the focus, grab it!" ); */
        gtk_widget_grab_focus( w );
    }
    return TRUE;
}

gboolean on_button_release( GtkWidget* w, GdkEventButton* evt )
{
    FmDesktop* self = (FmDesktop*)w;
    FmDesktopItem* clicked_item = hit_test( self, evt->x, evt->y );

    self->button_pressed = FALSE;

    if( self->rubber_bending )
    {
        update_rubberbanding( self, evt->x, evt->y );
        gtk_grab_remove( w );
        self->rubber_bending = FALSE;
    }
    else if( self->dragging )
    {
        self->dragging = FALSE;
    }
    else if( fm_config->single_click && evt->button == 1 )
    {
        if( clicked_item )
        {
//            open_clicked_item( clicked_item );
            return TRUE;
        }
    }

    /* forward the event to root window */
//    if( ! clicked_item )
//        forward_event_to_rootwin( gtk_widget_get_screen(w), evt );

    return TRUE;
}

static gboolean on_single_click_timeout( FmDesktop* self )
{
    GtkWidget* w = (GtkWidget*)self;
    GdkEventButton evt;
    int x, y;
    /* generate a fake button press */
    /* FIXME: will this cause any problem? */
    evt.type = GDK_BUTTON_PRESS;
    evt.window = w->window;
    gdk_window_get_pointer( w->window, &x, &y, &evt.state );
    evt.x = x;
    evt.y = y;
    evt.state |= GDK_BUTTON_PRESS_MASK;
    evt.state &= ~GDK_BUTTON_MOTION_MASK;
    on_button_press( GTK_WIDGET(self), &evt );
    evt.type = GDK_BUTTON_RELEASE;
    evt.state &= ~GDK_BUTTON_PRESS_MASK;
    evt.state |= ~GDK_BUTTON_RELEASE_MASK;
    on_button_release( GTK_WIDGET(self), &evt );

    self->single_click_timeout_handler = 0;
    return FALSE;
}

gboolean on_motion_notify( GtkWidget* w, GdkEventMotion* evt )
{
    FmDesktop* self = (FmDesktop*)w;
    if( ! self->button_pressed )
    {
        if( fm_config->single_click )
        {
            FmDesktopItem* item = hit_test( self, evt->x, evt->y );
            if( item != self->hover_item )
            {
                if( 0 != self->single_click_timeout_handler )
                {
                    g_source_remove( self->single_click_timeout_handler );
                    self->single_click_timeout_handler = 0;
                }
            }
            if( item )
            {
//                gdk_window_set_cursor( w->window, self->hand_cursor );
                /* FIXME: timeout should be customizable */
                if( self->single_click_timeout_handler == 0)
                    self->single_click_timeout_handler = g_timeout_add( 400, on_single_click_timeout, self ); //400 ms
					/* Making a loop to aviod the selection of the item */
					/* on_single_click_timeout( self ); */
			}
            else
            {
                gdk_window_set_cursor( w->window, NULL );
            }
            self->hover_item = item;
        }
        return TRUE;
    }

    if( self->dragging )
    {
    }
    else if( self->rubber_bending )
    {
        update_rubberbanding( self, evt->x, evt->y );
    }
    else
    {
        if ( gtk_drag_check_threshold( w,
                                    self->drag_start_x,
                                    self->drag_start_y,
                                    evt->x, evt->y))
        {
            FmFileInfoList* files = fm_desktop_get_selected_files(self);
            GtkTargetList* target_list;
            if(files)
            {
                self->dragging = TRUE;
                target_list = gtk_drag_source_get_target_list(w);
                gtk_drag_begin( w, target_list,
                             GDK_ACTION_COPY|GDK_ACTION_MOVE|GDK_ACTION_LINK,
                             1, evt );
            }
        }
    }

    return TRUE;
}

gboolean on_leave_notify( GtkWidget* w, GdkEventCrossing *evt )
{
    FmDesktop* self = (FmDesktop*)w;
    if(self->single_click_timeout_handler)
    {
        g_source_remove(self->single_click_timeout_handler);
        self->single_click_timeout_handler = 0;
    }
    return TRUE;
}

gboolean on_key_press( GtkWidget* w, GdkEventKey* evt )
{
    GList* sels;
    FmDesktop* self = (FmDesktop*)w;
    int modifier = ( evt->state & ( GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK ) );
#if 0
    sels = fm_desktop_win_get_selected_files( self );

    if ( modifier == GDK_CONTROL_MASK )
    {
        switch ( evt->keyval )
        {
        case GDK_x:
            if( sels )
                ptk_clipboard_cut_or_copy_files( vfs_get_desktop_dir(), sels, FALSE );
            break;
        case GDK_c:
            if( sels )
                ptk_clipboard_cut_or_copy_files( vfs_get_desktop_dir(), sels, TRUE );
            break;
        case GDK_v:
            on_paste( NULL, self );
            break;
/*
        case GDK_i:
            ptk_file_browser_invert_selection( file_browser );
            break;
        case GDK_a:
            ptk_file_browser_select_all( file_browser );
            break;
*/
        }
    }
    else if ( modifier == GDK_MOD1_MASK )
    {
        switch ( evt->keyval )
        {
        case GDK_Return:
            if( sels )
                ptk_show_file_properties( NULL, vfs_get_desktop_dir(), sels );
            break;
        }
    }
    else if ( modifier == GDK_SHIFT_MASK )
    {
        switch ( evt->keyval )
        {
        case GDK_Delete:
            if( sels )
                ptk_delete_files( NULL, vfs_get_desktop_dir(), sels );
            break;
        }
    }
    else if ( modifier == 0 )
    {
        switch ( evt->keyval )
        {
        case GDK_F2:
            if( sels )
                ptk_rename_file( NULL, vfs_get_desktop_dir(), (FmFileInfo*)sels->data );
            break;
        case GDK_Delete:
            if( sels )
                ptk_delete_files( NULL, vfs_get_desktop_dir(), sels );
            break;
        }
    }

    if( sels )
        vfs_file_info_list_free( sels );
#endif

    return TRUE;
}

void on_style_set( GtkWidget* w, GtkStyle* prev )
{
    FmDesktop* self = (FmDesktop*)w;
    PangoContext* pc = gtk_widget_get_pango_context(w);
    if(font_desc)
        pango_context_set_font_description(pc, font_desc);
    pango_layout_context_changed(pc);
}

void on_direction_changed( GtkWidget* w, GtkTextDirection prev )
{
    FmDesktop* self = (FmDesktop*)w;
    /* FIXME: handle RTL */
    PangoContext* pc = gtk_widget_get_pango_context(w);
    pango_layout_context_changed(pc);
    queue_layout_items(self);
}

void on_realize( GtkWidget* w )
{
    FmDesktop* self = (FmDesktop*)w;

    GTK_WIDGET_CLASS(fm_desktop_parent_class)->realize( w );
    gtk_window_set_skip_pager_hint( GTK_WINDOW(w), TRUE );
    gtk_window_set_skip_taskbar_hint( GTK_WINDOW(w), TRUE );
    gtk_window_set_resizable( (GtkWindow*)w, FALSE );

    if( ! self->gc )
        self->gc = gdk_gc_new( w->window );

    update_background(self);
}

gboolean on_focus_in( GtkWidget* w, GdkEventFocus* evt )
{
    FmDesktop* self = (FmDesktop*) w;
    GTK_WIDGET_SET_FLAGS( w, GTK_HAS_FOCUS );
    if( self->focus )
        redraw_item( self, self->focus );
    return FALSE;
}

gboolean on_focus_out( GtkWidget* w, GdkEventFocus* evt )
{
    FmDesktop* self = (FmDesktop*) w;
    if( self->focus )
    {
        GTK_WIDGET_UNSET_FLAGS( w, GTK_HAS_FOCUS );
        redraw_item( self, self->focus );
    }
    return FALSE;
}

void on_drag_leave(GtkWidget* w, GdkDragContext* drag_ctx, guint time)
{
    FmDesktop* desktop = (FmDesktop*)w;
    if(desktop->drop_hilight)
    {
        FmDesktopItem* old_drop = desktop->drop_hilight;
        desktop->drop_hilight = NULL;
        redraw_item(desktop, old_drop);
    }
}

gboolean on_expose( GtkWidget* w, GdkEventExpose* evt )
{
    FmDesktop* self = (FmDesktop*)w;
    GList* l;
    cairo_t* cr;

    if( G_UNLIKELY( ! GTK_WIDGET_VISIBLE (w) || ! GTK_WIDGET_MAPPED (w) ) )
        return TRUE;

    cr = gdk_cairo_create(w->window);
    if( self->rubber_bending )
        paint_rubber_banding_rect( self, cr, &evt->area );

    for( l = self->items; l; l = l->next )
    {
        FmDesktopItem* item = (FmDesktopItem*)l->data;
        GdkRectangle* intersect, tmp, tmp2;
        if(gdk_rectangle_intersect( &evt->area, &item->icon_rect, &tmp ))
            intersect = &tmp;
        else
            intersect = NULL;

        if(gdk_rectangle_intersect( &evt->area, &item->text_rect, &tmp2 ))
        {
            if(intersect)
                gdk_rectangle_union(intersect, &tmp2, intersect);
            else
                intersect = &tmp2;
        }

        if(intersect)
            paint_item( self, item, cr, intersect );
    }
    cairo_destroy(cr);

    return TRUE;
}

void on_size_allocate( GtkWidget* w, GtkAllocation* alloc )
{
    GdkPixbuf* pix;
    FmDesktop* self = (FmDesktop*)w;
    GdkRectangle wa;

    /* calculate item size */
    PangoContext* pc;
    PangoFontMetrics *metrics;
    int font_h;
    pc = gtk_widget_get_pango_context( (GtkWidget*)self );

    metrics = pango_context_get_metrics( pc, NULL, NULL);

    font_h = pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent (metrics);
    font_h /= PANGO_SCALE;

    self->spacing = SPACING;
    self->xpad = self->ypad = PADDING;
    self->xmargin = self->ymargin = MARGIN;
    self->text_h = font_h * 2;
    self->text_w = 100;
    self->pango_text_h = self->text_h * PANGO_SCALE;
    self->pango_text_w = self->text_w * PANGO_SCALE;
    self->text_h += 4;
    self->text_w += 4; /* 4 is for drawing border */
    self->cell_h = fm_config->big_icon_size + self->spacing + self->text_h + self->ypad * 2;
    self->cell_w = MAX(self->text_w, fm_config->big_icon_size) + self->xpad * 2;

    update_working_area(self);
    /* queue_layout_items(self); this is called in update_working_area */

    GTK_WIDGET_CLASS(fm_desktop_parent_class)->size_allocate( w, alloc );
}

void on_size_request( GtkWidget* w, GtkRequisition* req )
{
    GdkScreen* scr = gtk_widget_get_screen( w );
    req->width = gdk_screen_get_width( scr );
    req->height = gdk_screen_get_height( scr );
}

static gboolean is_point_in_rect( GdkRectangle* rect, int x, int y )
{
    return rect->x < x && x < (rect->x + rect->width) && y > rect->y && y < (rect->y + rect->height);
}

FmDesktopItem* hit_test(FmDesktop* self, int x, int y)
{
    FmDesktopItem* item;
    GList* l;
    for( l = self->items; l; l = l->next )
    {
        item = (FmDesktopItem*) l->data;
        if( is_point_in_rect( &item->icon_rect, x, y )
         || is_point_in_rect( &item->text_rect, x, y ) )
            return item;
    }
    return NULL;
}

inline FmDesktopItem* desktop_item_new(GtkTreeIter* it)
{
    FmDesktopItem* item = g_slice_new0(FmDesktopItem);
    item->it = *it;
    gtk_tree_model_get(model, it, COL_FILE_BIG_ICON, &item->icon, COL_FILE_INFO, &item->fi, -1);
    return item;
}

void on_row_inserted(GtkTreeModel* mod, GtkTreePath* tp, GtkTreeIter* it, FmDesktop* desktop)
{
    FmDesktopItem* item = desktop_item_new(it);
    desktop->items = g_list_insert(desktop->items, item, gtk_tree_path_get_indices(tp)[0]);
    queue_layout_items(desktop);
}

void on_row_deleted(GtkTreeModel* mod, GtkTreePath* tp, FmDesktop* desktop)
{
    GList* l;
    int i = 0, idx = gtk_tree_path_get_indices(tp)[0];
    for(l=desktop->items;l;l=l->next, ++i)
    {
        FmDesktopItem* item = (FmDesktopItem*)l->data;
        if(i == idx)
        {
            desktop_item_free(item);
            desktop->items = g_list_delete_link(desktop->items, l);
            break;
        }
    }

    queue_layout_items(desktop);
}

void on_row_changed(GtkTreeModel* mod, GtkTreePath* tp, GtkTreeIter* it, FmDesktop* desktop)
{
    queue_layout_items(desktop);
}

void calc_item_size(FmDesktop* desktop, FmDesktopItem* item)
{
    int text_x, text_y, text_w, text_h;
    PangoRectangle rc, rc2;

    /* icon rect */
    if(item->icon)
    {
        item->icon_rect.width = gdk_pixbuf_get_width(item->icon);
        item->icon_rect.height = gdk_pixbuf_get_height(item->icon);
        item->icon_rect.x = item->x + (desktop->cell_w - item->icon_rect.width) / 2;
        item->icon_rect.y = item->y + desktop->ypad + (fm_config->big_icon_size - item->icon_rect.height) / 2;
        item->icon_rect.height += desktop->spacing;
    }
    else
    {
        item->icon_rect.width = fm_config->big_icon_size;
        item->icon_rect.height = fm_config->big_icon_size;
        item->icon_rect.x = item->x + desktop->ypad;
        item->icon_rect.y = item->y + desktop->ypad;
        item->icon_rect.height += desktop->spacing;
    }

    /* text label rect */
    pango_layout_set_text(desktop->pl, NULL, 0);
    /* FIXME: we should cache text_h * PANGO_SCALE and text_w * PANGO_SCALE */
    pango_layout_set_height(desktop->pl, desktop->pango_text_h);
    pango_layout_set_width(desktop->pl, desktop->pango_text_w);
    pango_layout_set_text(desktop->pl, fm_file_info_get_disp_name(item->fi), -1);

    pango_layout_get_pixel_extents(desktop->pl, &rc, &rc2);
    pango_layout_set_text(desktop->pl, NULL, 0);

    item->text_rect.x = item->x + (desktop->cell_w - rc2.width - 4) / 2;
    item->text_rect.y = item->icon_rect.y + item->icon_rect.height + rc2.y;
    item->text_rect.width = rc2.width + 4;
    item->text_rect.height = rc2.height + 4;
}

void layout_items(FmDesktop* self)
{
    GList* l;
    FmDesktopItem* item;
    GtkWidget* widget = (GtkWidget*)self;
    int x, y, w, bottom, right;

    x = self->working_area.x + self->xmargin;
    y = self->working_area.y + self->ymargin;
    bottom = self->working_area.y + self->working_area.height - self->ymargin - self->cell_h;

    for( l = self->items; l; l = l->next )
    {
        item = (FmDesktopItem*)l->data;
        item->x = x;
        item->y = y;
        calc_item_size(self, item);
        y += self->cell_h;
        if(y > bottom)
        {
            x += self->cell_w;
            y = self->working_area.y + self->ymargin;
        }
    }
    gtk_widget_queue_draw( GTK_WIDGET(self) );
}

static gboolean on_idle_layout(FmDesktop* desktop)
{
    desktop->idle_layout = 0;
    layout_items(desktop);
    return FALSE;
}

void queue_layout_items(FmDesktop* desktop)
{
    if(0 == desktop->idle_layout)
        desktop->idle_layout = g_idle_add((GSourceFunc)on_idle_layout, desktop);
}

void paint_item(FmDesktop* self, FmDesktopItem* item, cairo_t* cr, GdkRectangle* expose_area)
{
    GtkWidget* widget = (GtkWidget*)self;
    GtkCellRendererState state = 0;
    GdkColor* fg;
    int text_x, text_y;
    /* g_debug("%s, %d, %d, %d, %d", item->fi->path->name, expose_area->x, expose_area->y, expose_area->width, expose_area->height); */

    pango_layout_set_text(self->pl, NULL, 0);
    pango_layout_set_width(self->pl, self->pango_text_w);
    pango_layout_set_height(self->pl, self->pango_text_h);

    pango_layout_set_text(self->pl, fm_file_info_get_disp_name(item->fi), -1);

    /* FIXME: do we need to cache this? */
    text_x = item->x + (self->cell_w - self->text_w)/2 + 2;
    text_y = item->icon_rect.y + item->icon_rect.height + 2;

    if(item->is_selected || item == self->drop_hilight) /* draw background for text label */
    {
        GdkRectangle rc;
        state = GTK_CELL_RENDERER_SELECTED;

        cairo_save(cr);
        gdk_cairo_rectangle(cr, &item->text_rect);
        gdk_cairo_set_source_color(cr, &widget->style->bg[GTK_STATE_SELECTED]);
        cairo_clip(cr);
        cairo_paint(cr);
        cairo_restore(cr);
        fg = &widget->style->fg[GTK_STATE_SELECTED];
    }
    else
    {
        /* the shadow */
        gdk_gc_set_rgb_fg_color(self->gc, &app_config->desktop_shadow);
        gdk_draw_layout( widget->window, self->gc, text_x + 1, text_y + 1, self->pl );
        fg = &app_config->desktop_fg;
    }
    /* real text */
    gdk_gc_set_rgb_fg_color(self->gc, fg);
    gdk_draw_layout( widget->window, self->gc, text_x, text_y, self->pl );
    pango_layout_set_text(self->pl, NULL, 0);
    
    if(item == self->focus && GTK_WIDGET_HAS_FOCUS(self) )
        gtk_paint_focus(widget->style, widget->window, gtk_widget_get_state(widget), 
                        expose_area, widget, "icon_view",
                        item->text_rect.x, item->text_rect.y, item->text_rect.width, item->text_rect.height);

    /* draw the icon */
    g_object_set( self->icon_render, "pixbuf", item->icon, NULL );
    gtk_cell_renderer_render(self->icon_render, widget->window, widget, &item->icon_rect, &item->icon_rect, expose_area, state);
}

void redraw_item(FmDesktop* desktop, FmDesktopItem* item)
{
    GdkRectangle rect;
    gdk_rectangle_union(&item->icon_rect, &item->text_rect, &rect);
    --rect.x;
    --rect.y;
    rect.width += 2;
    rect.height += 2;
    gdk_window_invalidate_rect( ((GtkWidget*)desktop)->window, &rect, FALSE );
}

void calc_rubber_banding_rect( FmDesktop* self, int x, int y, GdkRectangle* rect )
{
    int x1, x2, y1, y2, w, h;
    if( self->drag_start_x < x )
    {
        x1 = self->drag_start_x;
        x2 = x;
    }
    else
    {
        x1 = x;
        x2 = self->drag_start_x;
    }

    if( self->drag_start_y < y )
    {
        y1 = self->drag_start_y;
        y2 = y;
    }
    else
    {
        y1 = y;
        y2 = self->drag_start_y;
    }

    rect->x = x1;
    rect->y = y1;
    rect->width = x2 - x1;
    rect->height = y2 - y1;
}

void update_rubberbanding( FmDesktop* self, int newx, int newy )
{
    GList* l;
    GdkRectangle old_rect, new_rect;
    GdkRegion *region;

    calc_rubber_banding_rect(self, self->rubber_bending_x, self->rubber_bending_y, &old_rect );
    calc_rubber_banding_rect(self, newx, newy, &new_rect );

    gdk_window_invalidate_rect(((GtkWidget*)self)->window, &old_rect, FALSE );
    gdk_window_invalidate_rect(((GtkWidget*)self)->window, &new_rect, FALSE );
//    gdk_window_clear_area(((GtkWidget*)self)->window, new_rect.x, new_rect.y, new_rect.width, new_rect.height );
/*
    region = gdk_region_rectangle( &old_rect );
    gdk_region_union_with_rect( region, &new_rect );

//    gdk_window_invalidate_region( ((GtkWidget*)self)->window, &region, TRUE );

    gdk_region_destroy( region );
*/
    self->rubber_bending_x = newx;
    self->rubber_bending_y = newy;

    /* update selection */
    for( l = self->items; l; l = l->next )
    {
        FmDesktopItem* item = (FmDesktopItem*)l->data;
        gboolean selected;
        if( gdk_rectangle_intersect( &new_rect, &item->icon_rect, NULL ) ||
            gdk_rectangle_intersect( &new_rect, &item->text_rect, NULL ) )
            selected = TRUE;
        else
            selected = FALSE;

        if( item->is_selected != selected )
        {
            item->is_selected = selected;
            redraw_item( self, item );
        }
    }
}


void paint_rubber_banding_rect(FmDesktop* self, cairo_t* cr, GdkRectangle* expose_area)
{
    GtkWidget* widget = (GtkWidget*)self;
    GdkRectangle rect;
    GdkColor clr;
    guchar alpha;

    calc_rubber_banding_rect( self, self->rubber_bending_x, self->rubber_bending_y, &rect );

    if( rect.width <= 0 || rect.height <= 0 )
        return;

    if(!gdk_rectangle_intersect(expose_area, &rect, &rect))
        return;
/*
    gtk_widget_style_get( icon_view,
                        "selection-box-color", &clr,
                        "selection-box-alpha", &alpha,
                        NULL);
*/
    clr = widget->style->base[GTK_STATE_SELECTED];
    alpha = 64;  /* FIXME: should be themable in the future */

    cairo_save(cr);
    cairo_set_source_rgba(cr, (gdouble)clr.red/65535, (gdouble)clr.green/65536, (gdouble)clr.blue/65535, (gdouble)alpha/100);
    gdk_cairo_rectangle(cr, &rect);
    cairo_clip (cr);
    cairo_paint (cr);
    gdk_cairo_set_source_color(cr, &clr);
    cairo_rectangle (cr, rect.x + 0.5, rect.y + 0.5, rect.width - 1, rect.height - 1);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void update_background(FmDesktop* desktop)
{
    GtkWidget* widget = (GtkWidget*)desktop;
    GdkPixbuf* pix, *scaled;
    GdkPixmap* pixmap;
    Pixmap pixmap_id;
    int src_w, src_h;
    int dest_w, dest_h;
    GdkWindow* root = gdk_screen_get_root_window(gtk_widget_get_screen(widget));

    if(app_config->wallpaper_mode == FM_WP_COLOR
       || !app_config->wallpaper
       || !*app_config->wallpaper
       || ! (pix = gdk_pixbuf_new_from_file(app_config->wallpaper, NULL)) ) /* solid color only */
    {
        GdkColor bg = app_config->desktop_bg;
        gdk_rgb_find_color(gdk_drawable_get_colormap(widget->window), &bg);
        gdk_window_set_back_pixmap(widget->window, NULL, FALSE);
        gdk_window_set_background(widget->window, &bg);
        gdk_window_set_back_pixmap(root, NULL, FALSE);
        gdk_window_set_background(root, &bg);
        gdk_window_clear(root);
        gdk_window_clear(widget->window);
        gdk_window_invalidate_rect(widget->window, NULL, TRUE);
        return;
    }

    src_w = gdk_pixbuf_get_width(pix);
    src_h = gdk_pixbuf_get_height(pix);
    if(app_config->wallpaper_mode == FM_WP_TILE)
    {
        dest_w = src_w;
        dest_h = src_h;
        pixmap = gdk_pixmap_new(widget->window, dest_w, dest_h, -1);
    }
    else
    {
        GdkScreen* screen = gtk_widget_get_screen(widget);
        dest_w = gdk_screen_get_width(screen);
        dest_h = gdk_screen_get_height(screen);
        pixmap = gdk_pixmap_new(widget->window, dest_w, dest_h, -1);
    }
    
    if(gdk_pixbuf_get_has_alpha(pix) 
        || app_config->wallpaper_mode == FM_WP_CENTER
        || app_config->wallpaper_mode == FM_WP_FIT)
    {
        gdk_gc_set_rgb_fg_color(desktop->gc, &app_config->desktop_bg);
        gdk_draw_rectangle(pixmap, desktop->gc, TRUE, 0, 0, dest_w, dest_h);
    }

    switch(app_config->wallpaper_mode)
    {
        case FM_WP_TILE:
            gdk_draw_pixbuf(pixmap, desktop->gc, pix, 0, 0, 0, 0, dest_w, dest_h, GDK_RGB_DITHER_NORMAL, 0, 0);
            break;
        case FM_WP_STRETCH:
            if(dest_w == src_w && dest_h == src_h)
                scaled = (GdkPixbuf*)g_object_ref(pix);
            else
                scaled = gdk_pixbuf_scale_simple(pix, dest_w, dest_h, GDK_INTERP_BILINEAR);
            gdk_draw_pixbuf(pixmap, desktop->gc, scaled, 0, 0, 0, 0, dest_w, dest_h, GDK_RGB_DITHER_NORMAL, 0, 0);
            g_object_unref(scaled);
            break;
        case FM_WP_FIT:
            if(dest_w != src_w || dest_h != src_h)
            {
                gdouble w_ratio = (float)dest_w / src_w;
                gdouble h_ratio = (float)dest_h / src_h;
                gdouble ratio = MIN(w_ratio, h_ratio);
                if(ratio != 1.0)
                {
                    src_w *= ratio;
                    src_h *= ratio;
                    scaled = gdk_pixbuf_scale_simple(pix, src_w, src_h, GDK_INTERP_BILINEAR);
                    g_object_unref(pix);
                    pix = scaled;
                }
            }
            /* continue to execute code in case FM_WP_CENTER */
        case FM_WP_CENTER:
            {
                int x, y;
                x = (dest_w - src_w)/2;
                y = (dest_h - src_h)/2;
                gdk_draw_pixbuf(pixmap, desktop->gc, pix, 0, 0, x, y, -1, -1, GDK_RGB_DITHER_NORMAL, 0, 0);
            }
            break;
    }
    gdk_window_set_back_pixmap(root, pixmap, FALSE);
    gdk_window_set_back_pixmap(widget->window, NULL, TRUE);

    pixmap_id = GDK_DRAWABLE_XID(pixmap);
    XChangeProperty(GDK_WINDOW_XDISPLAY(root), GDK_WINDOW_XID(root), 
                    XA_XROOTMAP_ID, XA_PIXMAP, 32, PropModeReplace, (guchar*)&pixmap_id, 1);

    g_object_unref(pixmap);
    if(pix)
        g_object_unref(pix);

    gdk_window_clear(root);
    gdk_window_clear(widget->window);
    gdk_window_invalidate_rect(widget->window, NULL, TRUE);
}

GdkFilterReturn on_root_event(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
    XPropertyEvent * evt = ( XPropertyEvent* ) xevent;
    FmDesktop* self = (FmDesktop*)data;
    if ( evt->type == PropertyNotify )
    {
        if(evt->atom == XA_NET_WORKAREA)
            update_working_area(self);
    }
    return GDK_FILTER_TRANSLATE;
}

void update_working_area(FmDesktop* desktop)
{
    GdkScreen* screen = gtk_widget_get_screen((GtkWidget*)desktop);
    GdkWindow* root = gdk_screen_get_root_window(screen);
    Atom ret_type;
    gulong len, after;
    int format;
    guchar* prop;
    guint32 n_desktops, cur_desktop;
    gulong* working_area;

    /* default to screen size */
    desktop->working_area.x = 0;
    desktop->working_area.y = 0;
    desktop->working_area.width = gdk_screen_get_width(screen);
    desktop->working_area.height = gdk_screen_get_height(screen);

    if( XGetWindowProperty(GDK_WINDOW_XDISPLAY(root), GDK_WINDOW_XID(root),
                       XA_NET_NUMBER_OF_DESKTOPS, 0, 1, False, XA_CARDINAL, &ret_type,
                       &format, &len, &after, &prop) != Success)
        goto _out;
    n_desktops = *(guint32*)prop;
    XFree(prop);

    if( XGetWindowProperty(GDK_WINDOW_XDISPLAY(root), GDK_WINDOW_XID(root),
                       XA_NET_CURRENT_DESKTOP, 0, 1, False, XA_CARDINAL, &ret_type,
                       &format, &len, &after, &prop) != Success)
        goto _out;
    cur_desktop = *(guint32*)prop;
    XFree(prop);

    if( XGetWindowProperty(GDK_WINDOW_XDISPLAY(root), GDK_WINDOW_XID(root),
                       XA_NET_WORKAREA, 0, 4 * 32, False, AnyPropertyType, &ret_type,
                       &format, &len, &after, &prop) != Success)
        goto _out;
	if(ret_type == None || format == 0 || len != n_desktops*4 )
    {
        if(prop)
            XFree(prop);
        goto _out;
    }
    working_area = ((gulong*)prop) + cur_desktop * 4;

    desktop->working_area.x = (gint)working_area[0];
    desktop->working_area.y = (gint)working_area[1];
    desktop->working_area.width = (gint)working_area[2];
    desktop->working_area.height = (gint)working_area[3];

    XFree(prop);
_out:
    queue_layout_items(desktop);
    return;
}

void on_screen_size_changed(GdkScreen* screen, FmDesktop* desktop)
{
    gtk_window_resize((GtkWindow*)desktop, gdk_screen_get_width(screen), gdk_screen_get_height(screen));
}

void on_dnd_src_data_get(FmDndSrc* ds, FmDesktop* desktop)
{
    FmFileInfoList* files = fm_desktop_get_selected_files(desktop);
    if(files)
    {
        fm_dnd_src_set_files(ds, files);
        fm_list_unref(files);
    }
}

gboolean on_dnd_dest_query_info(FmDndDest* dd, int x, int y,
                    			GdkDragAction* action, FmDesktop* desktop)
{
    FmDesktopItem* item = hit_test(desktop, x, y);
    if(item)
    {
        *action = GDK_ACTION_COPY;
        fm_dnd_dest_set_dest_file(dd, item->fi);
    }
    else
    {
        *action = GDK_ACTION_COPY;
        /* FIXME: prevent direct access to data member */
        fm_dnd_dest_set_dest_file(dd, model->dir->dir_fi);
    }

    if(desktop->drop_hilight != item)
    {
        FmDesktopItem* old_drop = desktop->drop_hilight;
        desktop->drop_hilight = item;
        if(old_drop)
            redraw_item(desktop, old_drop);
        if(item)
            redraw_item(desktop, item);
    }
    return TRUE;
}

void on_dnd_dest_files_dropped(FmDndDest* dd, GdkDragAction action,
                               int info_type, FmList* files, FmDesktop* desktop)
{

}

void on_wallpaper_changed(FmConfig* cfg, gpointer user_data)
{
    int i;
    for(i=0; i < n_screens; ++i)
        update_background(desktops[i]);
}

void on_desktop_text_changed(FmConfig* cfg, gpointer user_data)
{
    int i;
    /* FIXME: we only need to redraw text lables */
    for(i=0; i < n_screens; ++i)
        gtk_widget_queue_draw(desktops[i]);
}

void on_desktop_font_changed(FmConfig* cfg, gpointer user_data)
{
    /* FIXME: this is a little bit dirty */
    if(font_desc)
        pango_font_description_free(font_desc);

    if(app_config->desktop_font)
    {
        font_desc = pango_font_description_from_string(app_config->desktop_font);
        if(font_desc)
        {
            int i;
            for(i=0; i < n_screens; ++i)
            {
                FmDesktop* desktop = desktops[i];
                PangoContext* pc = gtk_widget_get_pango_context( (GtkWidget*)desktop );
                pango_context_set_font_description(pc, font_desc);
                pango_layout_context_changed(pc);
                gtk_widget_queue_resize(desktop);
                /* layout_items(desktop); */
                /* gtk_widget_queue_draw(desktops[i]); */
            }
        }
    }
    else
        font_desc = NULL;
}

void on_big_icon_size_changed(FmConfig* cfg, gpointer user_data)
{
    int i;
    for(i=0; i < n_screens; ++i)
    {
        FmDesktop* desktop = desktops[i];
        GList* l;
        for(l=desktop->items;l;l=l->next)
        {
            FmDesktopItem* item = (FmDesktopItem*)l->data;
            if(item->icon)
            {
                g_object_unref(item->icon);
                item->icon = NULL;
                gtk_tree_model_get(model, &item->it, COL_FILE_BIG_ICON, &item->icon, -1);
            }
        }
        gtk_widget_queue_resize(desktop);
    }
}

GList* get_selected_items(FmDesktop* desktop, int* n_items)
{
    GList* items = NULL;
    GList* l;
    int n = 0;
    for(l=desktop->items; l; l=l->next)
    {
        FmDesktopItem* item = (FmDesktopItem*)l->data;
        if(item->is_selected && item != desktop->focus)
        {
            items = g_list_prepend(items, item);
            ++n;
        }
    }
    items = g_list_reverse(items);
    if(desktop->focus)
        items = g_list_prepend(items, desktop->focus);
    if(n_items)
        *n_items = n;
    return items;
}

FmFileInfoList* fm_desktop_get_selected_files(FmDesktop* desktop)
{
    GList* l;
    FmFileInfoList* files = fm_file_info_list_new();
    for(l=desktop->items; l; l=l->next)
    {
        FmDesktopItem* item = (FmDesktopItem*)l->data;
        if(item->is_selected)
            fm_list_push_tail(files, item->fi);
    }
    if(fm_list_is_empty(files))
    {
        fm_list_unref(files);
        files = NULL;
    }
    return files;
}