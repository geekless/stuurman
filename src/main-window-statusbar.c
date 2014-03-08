/*
 *      main-window-statusbar.c
 *
 *      Copyright 2009 - 2012 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *      Copyright 2012 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *      Copyright 2013 - 2014 Vadim Ushakov <igeekless@gmail.com>
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

static void fm_main_win_inititialize_statusbar(FmMainWin *win)
{
    gtk_rc_parse_string(
        "style \"stuurman-statusbar\" {\n"
        "  GtkStatusbar::shadow-type = GTK_SHADOW_NONE\n"
        "}\n"
        "class \"GtkStatusbar\" style:application \"stuurman-statusbar\"\n"
    );

    win->statusbar.statusbar = (GtkStatusbar *) gtk_statusbar_new();

    //gtk_widget_style_get(GTK_WIDGET(win->statusbar), "shadow-type", &shadow_type, NULL);
/*
    {
        win->statusbar.icon_scale = gtk_hscale_new_with_range(0, 256, 4);
        gtk_scale_set_draw_value((GtkScale *) win->statusbar.icon_scale, FALSE);
        g_object_set(win->statusbar.icon_scale,
            "width-request", 128,
            "height-request", 16,
            NULL
        );
        gtk_box_pack_end(GTK_BOX(win->statusbar.statusbar), win->statusbar.icon_scale, FALSE, TRUE, 0);
    }
*/
    {
        win->statusbar.volume_progress_bar = gtk_progress_bar_new();
        gtk_box_pack_end(GTK_BOX(win->statusbar.statusbar), win->statusbar.volume_progress_bar, FALSE, TRUE, 0);
    }

    {
        win->statusbar.volume_frame = gtk_frame_new(NULL);
        gtk_frame_set_shadow_type((GtkFrame *) win->statusbar.volume_frame, GTK_SHADOW_NONE);
        gtk_box_pack_end(GTK_BOX(win->statusbar.statusbar), win->statusbar.volume_frame, FALSE, TRUE, 0);

        win->statusbar.volume_label = gtk_label_new(NULL);
        gtk_container_add(GTK_CONTAINER(win->statusbar.volume_frame), win->statusbar.volume_label);
    }

    win->statusbar.ctx = gtk_statusbar_get_context_id(win->statusbar.statusbar, "status");
    win->statusbar.ctx2 = gtk_statusbar_get_context_id(win->statusbar.statusbar, "status2");

    gtk_widget_show((GtkWidget *) win->statusbar.statusbar);
}

static void fm_main_win_destroy_statusbar(FmMainWin * win)
{
    win->statusbar.statusbar = NULL;
    win->statusbar.volume_frame = NULL;
    win->statusbar.volume_label = NULL;
    win->statusbar.volume_progress_bar = NULL;
    win->statusbar.icon_scale = NULL;
}

static void update_status(FmMainWin * win, guint type, const char*  status_text)
{
    if (!win->statusbar.statusbar)
        return;

    switch(type)
    {
        case FM_STATUS_TEXT_NORMAL:
        {
            gtk_statusbar_pop(win->statusbar.statusbar, win->statusbar.ctx);
            if(status_text)
                gtk_statusbar_push(win->statusbar.statusbar, win->statusbar.ctx, status_text);
            break;
        }
        case FM_STATUS_TEXT_SELECTED_FILES:
        {
            gtk_statusbar_pop(win->statusbar.statusbar, win->statusbar.ctx2);
            if(status_text)
                gtk_statusbar_push(win->statusbar.statusbar, win->statusbar.ctx2, status_text);
            break;
        }
        case FM_STATUS_TEXT_FS_INFO:
        {
            if (status_text && app_config->show_space_information)
            {
                if (app_config->show_space_information_in_bar)
                {
                    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(win->statusbar.volume_progress_bar), status_text);
                    gtk_progress_bar_set_fraction(
                        GTK_PROGRESS_BAR(win->statusbar.volume_progress_bar),
                        1 - fm_tab_page_volume_free_space_fraction(win->current_page));
                    gtk_widget_show(win->statusbar.volume_progress_bar);
                    gtk_widget_hide(win->statusbar.volume_frame);
                }
                else
                {
                    gtk_label_set_text((GtkLabel *) win->statusbar.volume_label, status_text);
                    gtk_widget_show(win->statusbar.volume_frame);
                    gtk_widget_hide(win->statusbar.volume_progress_bar);
                }
            }
            else
            {
                gtk_widget_hide(GTK_WIDGET(win->statusbar.volume_frame));
                gtk_widget_hide(GTK_WIDGET(win->statusbar.volume_progress_bar));
            }
            break;
        }
    }
}

static void update_statusbar(FmMainWin * win)
{
    FmTabPage * page = win->current_page;

    if (!win->statusbar.statusbar)
        return;

    if (!page)
        return;

    const char * status_text;

    gtk_statusbar_pop(win->statusbar.statusbar, win->statusbar.ctx);

    status_text = fm_tab_page_get_status_text(page, FM_STATUS_TEXT_NORMAL);
    update_status(win, FM_STATUS_TEXT_NORMAL, status_text);

    status_text = fm_tab_page_get_status_text(page, FM_STATUS_TEXT_SELECTED_FILES);
    update_status(win, FM_STATUS_TEXT_SELECTED_FILES, status_text);

    status_text = fm_tab_page_get_status_text(page, FM_STATUS_TEXT_FS_INFO);
    update_status(win, FM_STATUS_TEXT_FS_INFO, status_text);
}

/* This callback is only connected to current active tab page. */
static void on_tab_page_status_text(FmTabPage* page, guint type, const char* status_text, FmMainWin* win)
{
    if (page != win->current_page)
        return;
    update_status(win, type, status_text);
}
