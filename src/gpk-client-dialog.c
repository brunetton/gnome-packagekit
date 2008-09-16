/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/wait.h>
#include <fcntl.h>
#include <glib/gi18n.h>
#include <glade/glade.h>

#include <pk-common.h>
#include <pk-client.h>
#include <pk-enum.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-animated-icon.h"
#include "gpk-client-dialog.h"
#include "gpk-common.h"
#include "gpk-gnome.h"

static void     gpk_client_dialog_class_init	(GpkClientDialogClass	*klass);
static void     gpk_client_dialog_init		(GpkClientDialog	*dialog);
static void     gpk_client_dialog_finalize	(GObject		*object);

#define GPK_CLIENT_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CLIENT_DIALOG, GpkClientDialogPrivate))

struct _GpkClientDialogPrivate
{
	GladeXML		*glade_xml;
	guint			 pulse_timer_id;
	gboolean		 show_progress_files;
	GMainLoop		*loop;
	GtkResponseType		 response;
};

enum {
	GPK_CLIENT_DIALOG_CLOSE,
	GPK_CLIENT_DIALOG_QUIT,
	GPK_CLIENT_DIALOG_ACTION,
	GPK_CLIENT_DIALOG_HELP,
	GPK_CLIENT_DIALOG_CANCEL,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkClientDialog, gpk_client_dialog, G_TYPE_OBJECT)

/**
 * gpk_client_dialog_show_page:
 **/
gboolean
gpk_client_dialog_show_page (GpkClientDialog *dialog, GpkClientDialogPage page, guint32 timestamp)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "label_title");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "image_status");
	gtk_widget_show (widget);

	egg_debug ("setting page: %i", page);
	if (page == GPK_CLIENT_DIALOG_PAGE_CONFIRM) {
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "progressbar_percent");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_cancel");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_close");
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_action");
		gtk_widget_show (widget);
		gtk_widget_grab_focus (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_help");
		gtk_widget_show (widget);
	} else if (page == GPK_CLIENT_DIALOG_PAGE_PROGRESS) {
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "progressbar_percent");
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_cancel");
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_close");
		gtk_widget_show (widget);
		gtk_widget_grab_focus (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_action");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_help");
		gtk_widget_show (widget);
	} else if (page == GPK_CLIENT_DIALOG_PAGE_FINISHED || page == GPK_CLIENT_DIALOG_PAGE_WARNING) {
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "progressbar_percent");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_cancel");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_close");
		gtk_widget_show (widget);
		gtk_widget_grab_focus (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_action");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_help");
		gtk_widget_hide (widget);
	} else {
		egg_error ("unknown contant");
	}

	/* show */
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	gtk_widget_realize (widget);
	gtk_window_present_with_time (GTK_WINDOW (widget), timestamp);

	return TRUE;
}

/**
 * gpk_client_dialog_set_parent:
 **/
gboolean
gpk_client_dialog_set_parent (GpkClientDialog *dialog, GdkWindow *window)
{
	GtkWidget *widget;
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (GDK_WINDOW (window), FALSE);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	gtk_widget_realize (widget);
	gdk_window_set_transient_for (GTK_WIDGET(widget)->window, window);
	return TRUE;
}

/**
 * gpk_client_dialog_set_window_title:
 **/
gboolean
gpk_client_dialog_set_window_title (GpkClientDialog *dialog, const gchar *title)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (title != NULL, FALSE);

	egg_debug ("setting window title: %s", title);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	gtk_window_set_title (GTK_WINDOW (widget), title);
	return TRUE;
}

/**
 * gpk_client_dialog_set_window_icon:
 **/
gboolean
gpk_client_dialog_set_window_icon (GpkClientDialog *dialog, const gchar *icon)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (icon != NULL, FALSE);

	egg_debug ("setting window icon: %s", icon);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	gtk_window_set_icon_name (GTK_WINDOW (widget), icon);
	return TRUE;
}

/**
 * gpk_client_dialog_set_title:
 **/
gboolean
gpk_client_dialog_set_title (GpkClientDialog *dialog, const gchar *title)
{
	GtkWidget *widget;
	gchar *title_bold;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (title != NULL, FALSE);

	title_bold = g_strdup_printf ("<b><big>%s</big></b>", title);
	egg_debug ("setting title: %s", title_bold);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "label_title");
	gtk_label_set_markup (GTK_LABEL (widget), title_bold);
	g_free (title_bold);
	return TRUE;
}

/**
 * gpk_client_dialog_set_message:
 **/
gboolean
gpk_client_dialog_set_message (GpkClientDialog *dialog, const gchar *message)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	/* infer we want to show by setting this */
	if (!egg_strzero (message))
		gpk_client_dialog_set_show_message (dialog, TRUE);

	/* ignore this if it's uninteresting */
	if (!dialog->priv->show_progress_files)
		return FALSE;

	egg_debug ("setting message: %s", message);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "label_message");
	gtk_label_set_markup (GTK_LABEL (widget), message);
	return TRUE;
}

/**
 * gpk_client_dialog_set_action:
 **/
gboolean
gpk_client_dialog_set_action (GpkClientDialog *dialog, const gchar *action)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (action != NULL, FALSE);

	egg_debug ("setting action: %s", action);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_action");
	gtk_button_set_label (GTK_BUTTON (widget), action);
	return TRUE;
}

/**
 * gpk_client_dialog_pulse_progress:
 **/
static gboolean
gpk_client_dialog_pulse_progress (GpkClientDialog *dialog)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	/* debug so we can catch polling */
	egg_debug ("polling check");

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "progressbar_percent");
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget));

	/* if there's no slider, optimise out the polling */
	if (!GTK_WIDGET_VISIBLE (widget)) {
		dialog->priv->pulse_timer_id = 0;
		return FALSE;
	}
	return TRUE;
}

/**
 * gpk_client_dialog_make_progressbar_pulse:
 **/
static void
gpk_client_dialog_make_progressbar_pulse (GpkClientDialog *dialog)
{
	GtkWidget *widget;
	if (dialog->priv->pulse_timer_id == 0) {
		widget = glade_xml_get_widget (dialog->priv->glade_xml, "progressbar_percent");
		gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (widget ), 0.04);
		dialog->priv->pulse_timer_id = g_timeout_add (75, (GSourceFunc) gpk_client_dialog_pulse_progress, dialog);
	}
}

/**
 * gpk_client_dialog_set_percentage:
 **/
gboolean
gpk_client_dialog_set_percentage (GpkClientDialog *dialog, guint percentage)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	egg_debug ("setting percentage: %u", percentage);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "progressbar_percent");
	if (dialog->priv->pulse_timer_id != 0) {
		g_source_remove (dialog->priv->pulse_timer_id);
		dialog->priv->pulse_timer_id = 0;
	}

	/* either pulse or set percentage */
	if (percentage == PK_CLIENT_PERCENTAGE_INVALID)
		gpk_client_dialog_make_progressbar_pulse (dialog);
	else
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
	return TRUE;
}

/**
 * gpk_client_dialog_set_image:
 **/
gboolean
gpk_client_dialog_set_image (GpkClientDialog *dialog, const gchar *image)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (image != NULL, FALSE);

	egg_debug ("setting image: %s", image);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "image_status");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), FALSE);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), image, GTK_ICON_SIZE_DIALOG);
	return TRUE;
}

/**
 * gpk_client_dialog_set_image_status:
 **/
gboolean
gpk_client_dialog_set_image_status (GpkClientDialog *dialog, PkStatusEnum status)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "image_status");
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (widget), status, GTK_ICON_SIZE_DIALOG);
	return TRUE;
}

/**
 * gpk_client_dialog_get_window:
 **/
GtkWindow *
gpk_client_dialog_get_window (GpkClientDialog *dialog)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), NULL);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	return GTK_WINDOW (widget);
}

/**
 * gpk_client_dialog_set_allow_cancel:
 **/
gboolean
gpk_client_dialog_set_allow_cancel (GpkClientDialog *dialog, gboolean can_cancel)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, can_cancel);

	return TRUE;
}

/**
 * gpk_client_dialog_set_show_message:
 **/
gboolean
gpk_client_dialog_set_show_message (GpkClientDialog *dialog, gboolean show_message)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	egg_debug ("showing message: %i", show_message);

	/* if we're never going to show it, hide the allocation */
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "hbox_message");
	if (!show_message)
		gtk_widget_hide (widget);
	else
		gtk_widget_show (widget);
	dialog->priv->show_progress_files = show_message;

	return TRUE;
}

/**
 * gpk_client_dialog_run:
 **/
GtkResponseType
gpk_client_dialog_run (GpkClientDialog *dialog)
{
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	dialog->priv->response = GTK_RESPONSE_NONE;
	g_main_loop_run (dialog->priv->loop);
	return dialog->priv->response;
}

/**
 * gpk_client_dialog_close:
 **/
gboolean
gpk_client_dialog_close (GpkClientDialog *dialog)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	gtk_widget_hide (widget);

	if (dialog->priv->pulse_timer_id != 0) {
		g_source_remove (dialog->priv->pulse_timer_id);
		dialog->priv->pulse_timer_id = 0;
	}

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "image_status");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), FALSE);

	return TRUE;
}

/**
 * gpk_client_dialog_window_delete_cb:
 **/
static gboolean
gpk_client_dialog_window_delete_cb (GtkWidget *widget, GdkEvent *event, GpkClientDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_DELETE_EVENT;
	gpk_client_dialog_close (dialog);
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	/* do not destroy the window */
	return TRUE;
}

/**
 * gpk_client_dialog_button_close_cb:
 **/
static void
gpk_client_dialog_button_close_cb (GtkWidget *widget_button, GpkClientDialog *dialog)
{
	GtkWidget *widget;

	dialog->priv->response = GTK_RESPONSE_CLOSE;
	g_main_loop_quit (dialog->priv->loop);

	if (dialog->priv->pulse_timer_id != 0) {
		g_source_remove (dialog->priv->pulse_timer_id);
		dialog->priv->pulse_timer_id = 0;
	}

	widget = glade_xml_get_widget (dialog->priv->glade_xml, "image_status");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), FALSE);
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	else
		g_signal_emit (dialog, signals [GPK_CLIENT_DIALOG_CLOSE], 0);
}

/**
 * gpk_client_dialog_button_help_cb:
 **/
static void
gpk_client_dialog_button_help_cb (GtkWidget *widget_button, GpkClientDialog *dialog)
{
	gpk_gnome_help (NULL);
	g_signal_emit (dialog, signals [GPK_CLIENT_DIALOG_HELP], 0);
}

/**
 * gpk_client_dialog_button_action_cb:
 **/
static void
gpk_client_dialog_button_action_cb (GtkWidget *widget_button, GpkClientDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_OK;
	g_main_loop_quit (dialog->priv->loop);
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	else
		g_signal_emit (dialog, signals [GPK_CLIENT_DIALOG_ACTION], 0);
}

/**
 * gpk_client_dialog_button_cancel_cb:
 **/
static void
gpk_client_dialog_button_cancel_cb (GtkWidget *widget_button, GpkClientDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_CANCEL;
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	else
		g_signal_emit (dialog, signals [GPK_CLIENT_DIALOG_CANCEL], 0);
}

/**
 * gpk_client_create_custom_widget:
 **/
static GtkWidget *
gpk_client_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				 gchar *string1, gchar *string2,
				 gint int1, gint int2, gpointer user_data)
{
	if (egg_strequal (name, "image_status"))
		return gpk_animated_icon_new ();
	egg_warning ("name unknown=%s", name);
	return NULL;
}

/**
 * gpk_client_dialog_class_init:
 * @klass: The GpkClientDialogClass
 **/
static void
gpk_client_dialog_class_init (GpkClientDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_client_dialog_finalize;
	g_type_class_add_private (klass, sizeof (GpkClientDialogPrivate));
	signals [GPK_CLIENT_DIALOG_QUIT] =
		g_signal_new ("quit",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [GPK_CLIENT_DIALOG_CLOSE] =
		g_signal_new ("close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [GPK_CLIENT_DIALOG_ACTION] =
		g_signal_new ("action",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [GPK_CLIENT_DIALOG_CANCEL] =
		g_signal_new ("cancel",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_client_dialog_init:
 * @dialog: This class instance
 **/
static void
gpk_client_dialog_init (GpkClientDialog *dialog)
{
	GtkWidget *widget;

	dialog->priv = GPK_CLIENT_DIALOG_GET_PRIVATE (dialog);

	/* use custom widgets */
	glade_set_custom_handler (gpk_client_create_custom_widget, dialog);

	dialog->priv->glade_xml = glade_xml_new (PK_DATA "/gpk-client.glade", NULL, NULL);
	dialog->priv->loop = g_main_loop_new (NULL, FALSE);
	dialog->priv->response = GTK_RESPONSE_NONE;
	dialog->priv->pulse_timer_id = 0;
	dialog->priv->show_progress_files = TRUE;

	/* common stuff */
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "window_client");
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_client_dialog_window_delete_cb), dialog);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_close");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_dialog_button_close_cb), dialog);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_dialog_button_help_cb), dialog);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_action");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_dialog_button_action_cb), dialog);
	widget = glade_xml_get_widget (dialog->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_dialog_button_cancel_cb), dialog);

	/* clear status and progress text */
	gpk_client_dialog_set_window_title (dialog, "");
	gpk_client_dialog_set_title (dialog, "");
	gpk_client_dialog_set_message (dialog, "");
}

/**
 * gpk_client_dialog_finalize:
 * @object: The object to finalize
 **/
static void
gpk_client_dialog_finalize (GObject *object)
{
	GpkClientDialog *dialog;
	g_return_if_fail (GPK_IS_CLIENT_DIALOG (object));

	dialog = GPK_CLIENT_DIALOG (object);
	g_return_if_fail (dialog->priv != NULL);
	g_object_unref (dialog->priv->glade_xml);
	g_main_loop_unref (dialog->priv->loop);
	if (dialog->priv->pulse_timer_id != 0)
		g_source_remove (dialog->priv->pulse_timer_id);

	G_OBJECT_CLASS (gpk_client_dialog_parent_class)->finalize (object);
}

/**
 * gpk_client_dialog_new:
 *
 * Return value: a new GpkClientDialog object.
 **/
GpkClientDialog *
gpk_client_dialog_new (void)
{
	GpkClientDialog *dialog;
	dialog = g_object_new (GPK_TYPE_CLIENT_DIALOG, NULL);
	return GPK_CLIENT_DIALOG (dialog);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_client_dialog_test (EggTest *test)
{
	GtkResponseType button;
	GpkClientDialog *dialog = NULL;

	if (!egg_test_start (test, "GpkClientDialog"))
		return;

	/************************************************************/
	egg_test_title (test, "get GpkClientDialog object");
	dialog = gpk_client_dialog_new ();
	if (dialog != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "help button");
	gpk_client_dialog_set_window_title (dialog, "PackageKit self test");
	gpk_client_dialog_set_title (dialog, "Button press test");
	gpk_client_dialog_set_message (dialog, "Please press close");
	gpk_client_dialog_set_image (dialog, "dialog-warning");
	gpk_client_dialog_show_page (dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0);
	button = gpk_client_dialog_run (dialog);
	if (button == GTK_RESPONSE_CLOSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got id %i", button);

	/************************************************************/
	egg_test_title (test, "confirm button");
	gpk_client_dialog_set_title (dialog, "Button press test with a really really long title");
	gpk_client_dialog_set_message (dialog, "Please press Uninstall\n\nThis is a really really, really,\nreally long title <i>with formatting</i>");
	gpk_client_dialog_set_image (dialog, "dialog-information");
	gpk_client_dialog_set_action (dialog, _("Uninstall"));
	gpk_client_dialog_show_page (dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, 0);
	button = gpk_client_dialog_run (dialog);
	if (button == GTK_RESPONSE_OK)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got id %i", button);

	/************************************************************/
	egg_test_title (test, "no message");
	gpk_client_dialog_set_title (dialog, "Refresh cache");
	gpk_client_dialog_set_image_status (dialog, PK_STATUS_ENUM_REFRESH_CACHE);
	gpk_client_dialog_set_show_message (dialog, FALSE);
	gpk_client_dialog_set_percentage (dialog, 101);
	gpk_client_dialog_show_page (dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0);
	gpk_client_dialog_run (dialog);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "progress");
	gpk_client_dialog_set_title (dialog, "Button press test");
	gpk_client_dialog_set_message (dialog, "Please press cancel");
	gpk_client_dialog_set_image_status (dialog, PK_STATUS_ENUM_RUNNING);
	gpk_client_dialog_set_percentage (dialog, 50);
	gpk_client_dialog_show_page (dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0);
	button = gpk_client_dialog_run (dialog);
	if (button == GTK_RESPONSE_CANCEL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got id %i", button);

	/************************************************************/
	egg_test_title (test, "progress");
	gpk_client_dialog_set_title (dialog, "Button press test");
	gpk_client_dialog_set_message (dialog, "Please press close");
	gpk_client_dialog_set_image_status (dialog, PK_STATUS_ENUM_INSTALL);
	gpk_client_dialog_set_percentage (dialog, 101);
	gpk_client_dialog_show_page (dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0);
	button = gpk_client_dialog_run (dialog);
	if (button == GTK_RESPONSE_CLOSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got id %i", button);

	gpk_client_dialog_close (dialog);

	g_object_unref (dialog);

	egg_test_end (test);
}
#endif

