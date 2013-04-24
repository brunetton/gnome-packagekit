/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2011 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <locale.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gpk-common.h"
#include "gpk-debug.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-gnome.h"

typedef struct {
	const gchar		*id_tmp;
	GCancellable		*cancellable;
	GSettings		*settings_gsd;
	GSettings		*settings_gpk;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkListStore		*list_store;
	GtkTreePath		*path_tmp;
	guint			 status_id;
	PkBitfield		 roles;
	PkClient		*client;
	PkStatusEnum		 status;
} GpkPrefsPrivate;

enum {
	GPK_COLUMN_ENABLED,
	GPK_COLUMN_TEXT,
	GPK_COLUMN_ID,
	GPK_COLUMN_ACTIVE,
	GPK_COLUMN_SENSITIVE,
	GPK_COLUMN_LAST
};

/* TRANSLATORS: check once an hour */
#define PK_FREQ_HOURLY_TEXT		_("Hourly")
/* TRANSLATORS: check once a day */
#define PK_FREQ_DAILY_TEXT		_("Daily")
/* TRANSLATORS: check once a week */
#define PK_FREQ_WEEKLY_TEXT		_("Weekly")
/* TRANSLATORS: never check for updates/upgrades */
#define PK_FREQ_NEVER_TEXT		_("Never")

/* TRANSLATORS: update everything */
#define PK_UPDATE_ALL_TEXT		_("All updates")
/* TRANSLATORS: update just security updates */
#define PK_UPDATE_SECURITY_TEXT		_("Only security updates")
/* TRANSLATORS: don't update anything */
#define PK_UPDATE_NONE_TEXT		_("Nothing")

#define GPK_PREFS_VALUE_NEVER		(0)
#define GPK_PREFS_VALUE_HOURLY		(60*60)
#define GPK_PREFS_VALUE_DAILY		(60*60*24)
#define GPK_PREFS_VALUE_WEEKLY		(60*60*24*7)

/**
 * gpk_prefs_help_cb:
 **/
static void
gpk_prefs_help_cb (GtkWidget *widget, GpkPrefsPrivate *priv)
{
	gpk_gnome_help ("prefs");
}

/**
 * gpk_prefs_check_now_cb:
 **/
static void
gpk_prefs_check_now_cb (GtkWidget *widget, GpkPrefsPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	gchar *command;

	command = g_build_filename (BINDIR, "gpk-update-viewer", NULL);
	ret = g_spawn_command_line_async (command, &error);
	if (!ret) {
		g_warning ("Couldn't execute %s: %s", command, error->message);
		g_error_free (error);
	}
	g_free (command);
}

/**
 * gpk_prefs_update_freq_combo_changed:
 **/
static void
gpk_prefs_update_freq_combo_changed (GtkWidget *widget, GpkPrefsPrivate *priv)
{
	gchar *value;
	guint freq = 0;

	value = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (widget));
	if (strcmp (value, PK_FREQ_HOURLY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_HOURLY;
	else if (strcmp (value, PK_FREQ_DAILY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_DAILY;
	else if (strcmp (value, PK_FREQ_WEEKLY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_WEEKLY;
	else if (strcmp (value, PK_FREQ_NEVER_TEXT) == 0)
		freq = GPK_PREFS_VALUE_NEVER;
	else
		g_assert (FALSE);

	g_debug ("Changing %s to %i", GSD_SETTINGS_FREQUENCY_GET_UPDATES, freq);
	g_settings_set_int (priv->settings_gsd, GSD_SETTINGS_FREQUENCY_GET_UPDATES, freq);
	g_free (value);
}

/**
 * gpk_prefs_upgrade_freq_combo_changed:
 **/
static void
gpk_prefs_upgrade_freq_combo_changed (GtkWidget *widget, GpkPrefsPrivate *priv)
{
	gchar *value;
	guint freq = 0;

	value = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (widget));
	if (strcmp (value, PK_FREQ_DAILY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_DAILY;
	else if (strcmp (value, PK_FREQ_WEEKLY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_WEEKLY;
	else if (strcmp (value, PK_FREQ_NEVER_TEXT) == 0)
		freq = GPK_PREFS_VALUE_NEVER;
	else
		g_assert (FALSE);

	g_debug ("Changing %s to %i", GSD_SETTINGS_FREQUENCY_GET_UPGRADES, freq);
	g_settings_set_int (priv->settings_gsd, GSD_SETTINGS_FREQUENCY_GET_UPGRADES, freq);
	g_free (value);
}

/**
 * gpk_prefs_update_freq_combo_setup:
 **/
static void
gpk_prefs_update_freq_combo_setup (GpkPrefsPrivate *priv)
{
	gboolean is_writable;
	GtkWidget *widget;
	guint value;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_check"));
	is_writable = g_settings_is_writable (priv->settings_gsd, GSD_SETTINGS_FREQUENCY_GET_UPDATES);
	value = g_settings_get_int (priv->settings_gsd, GSD_SETTINGS_FREQUENCY_GET_UPDATES);
	g_debug ("value from settings %i", value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* use a simple text model */
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_HOURLY_TEXT);
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_DAILY_TEXT);
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_WEEKLY_TEXT);
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_NEVER_TEXT);

	/* select the correct entry */
	if (value == GPK_PREFS_VALUE_HOURLY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	else if (value == GPK_PREFS_VALUE_DAILY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
	else if (value == GPK_PREFS_VALUE_WEEKLY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);
	else if (value == GPK_PREFS_VALUE_NEVER)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 3);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (gpk_prefs_update_freq_combo_changed), priv);
}

/**
 * gpk_prefs_upgrade_freq_combo_setup:
 **/
static void
gpk_prefs_upgrade_freq_combo_setup (GpkPrefsPrivate *priv)
{
	gboolean is_writable;
	GtkWidget *widget;
	guint value;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_upgrade"));
	is_writable = g_settings_is_writable (priv->settings_gsd, GSD_SETTINGS_FREQUENCY_GET_UPGRADES);
	value = g_settings_get_int (priv->settings_gsd, GSD_SETTINGS_FREQUENCY_GET_UPGRADES);
	g_debug ("value from settings %i", value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* use a simple text model */
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_DAILY_TEXT);
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_WEEKLY_TEXT);
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (widget), PK_FREQ_NEVER_TEXT);

	/* select the correct entry */
	if (value == GPK_PREFS_VALUE_DAILY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	else if (value == GPK_PREFS_VALUE_WEEKLY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
	else if (value == GPK_PREFS_VALUE_NEVER)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (gpk_prefs_upgrade_freq_combo_changed), priv);
}

/**
 * gpk_prefs_notify_network_state_cb:
 **/
static void
gpk_prefs_notify_network_state_cb (PkControl *control, GParamSpec *pspec, GpkPrefsPrivate *priv)
{
	GtkWidget *widget;
	PkNetworkEnum state;

	/* only show label on mobile broadband */
	g_object_get (control,
		      "network-state", &state,
		      NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "hbox_mobile_broadband"));
	if (state == PK_NETWORK_ENUM_MOBILE)
		gtk_widget_show (widget);
	else
		gtk_widget_hide (widget);
}

/**
 * gpk_prefs_find_iter_model_cb:
 **/
static gboolean
gpk_prefs_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, GpkPrefsPrivate *priv)
{
	gchar *repo_id_tmp = NULL;
	gtk_tree_model_get (model, iter,
			    GPK_COLUMN_ID, &repo_id_tmp,
			    -1);
	if (strcmp (repo_id_tmp, priv->id_tmp) == 0) {
		priv->path_tmp = gtk_tree_path_copy (path);
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_prefs_mark_nonactive_cb:
 **/
static gboolean
gpk_prefs_mark_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, GpkPrefsPrivate *priv)
{
	gtk_list_store_set (GTK_LIST_STORE(model), iter,
			    GPK_COLUMN_ACTIVE, FALSE,
			    -1);
	return FALSE;
}

/**
 * gpk_prefs_mark_nonactive:
 **/
static void
gpk_prefs_mark_nonactive (GpkPrefsPrivate *priv, GtkTreeModel *model)
{
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_prefs_mark_nonactive_cb, priv);
}

/**
 * gpk_prefs_model_get_iter:
 **/
static gboolean
gpk_prefs_model_get_iter (GpkPrefsPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter, const gchar *id)
{
	gboolean ret = TRUE;
	priv->id_tmp = id;
	priv->path_tmp = NULL;
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_prefs_find_iter_model_cb, priv);
	if (priv->path_tmp == NULL) {
		gtk_list_store_append (GTK_LIST_STORE(model), iter);
	} else {
		ret = gtk_tree_model_get_iter (model, iter, priv->path_tmp);
		gtk_tree_path_free (priv->path_tmp);
	}
	return ret;
}

/**
 * gpk_prefs_remove_nonactive_cb:
 **/
static gboolean
gpk_prefs_remove_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gboolean *ret)
{
	gboolean active;
	gtk_tree_model_get (model, iter,
			    GPK_COLUMN_ACTIVE, &active,
			    -1);
	if (!active) {
		*ret = TRUE;
		gtk_list_store_remove (GTK_LIST_STORE(model), iter);
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_prefs_remove_nonactive:
 **/
static void
gpk_prefs_remove_nonactive (GtkTreeModel *model)
{
	gboolean ret;
	/* do this again and again as removing in gtk_tree_model_foreach causes errors */
	do {
		ret = FALSE;
		gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_prefs_remove_nonactive_cb, &ret);
	} while (ret);
}

/**
 * gpk_prefs_status_changed_timeout_cb:
 **/
static gboolean
gpk_prefs_status_changed_timeout_cb (GpkPrefsPrivate *priv)
{
	const gchar *text;
	GtkWidget *widget;

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_repo"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "viewport_status"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	text = gpk_status_enum_to_localised_text (priv->status);
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* set icon */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_status"));
	gtk_image_set_from_icon_name (GTK_IMAGE (widget),
				      gpk_status_enum_to_icon_name (priv->status),
				      GTK_ICON_SIZE_DIALOG);

	/* never repeat */
	priv->status_id = 0;
	return FALSE;
}

/**
 * gpk_prefs_progress_cb:
 **/
static void
gpk_prefs_progress_cb (PkProgress *progress, PkProgressType type, GpkPrefsPrivate *priv)
{
	GtkWidget *widget;

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;

	/* get value */
	g_object_get (progress,
		      "status", &priv->status,
		      NULL);
	g_debug ("now %s", pk_status_enum_to_string (priv->status));

	if (priv->status == PK_STATUS_ENUM_FINISHED) {
		/* we've not yet shown, so don't bother */
		if (priv->status_id > 0) {
			g_source_remove (priv->status_id);
			priv->status_id = 0;
		}
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "viewport_status"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_repo"));
		gtk_widget_show (widget);
		goto out;
	}

	/* already pending show */
	if (priv->status_id > 0)
		goto out;

	/* only show after some time in the transaction */
	priv->status_id = g_timeout_add (GPK_UI_STATUS_SHOW_DELAY, (GSourceFunc) gpk_prefs_status_changed_timeout_cb, priv);
	g_source_set_name_by_id (priv->status_id, "[GpkRepo] status");
out:
	return;
}

/**
 * gpk_prefs_process_messages_cb:
 **/
static void
gpk_prefs_process_messages_cb (PkMessage *item, GpkPrefsPrivate *priv)
{
	const gchar *title;
	gchar *details;
	GtkWindow *window;
	PkMessageEnum type;

	/* get data */
	g_object_get (item,
		      "type", &type,
		      "details", &details,
		      NULL);

	/* show a modal window */
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
	title = gpk_message_enum_to_localised_text (type);
	gpk_error_dialog_modal (window, title, details, NULL);

	g_free (details);
}

/**
 * gpk_prefs_repo_enable_cb
 **/
static void
gpk_prefs_repo_enable_cb (GObject *object, GAsyncResult *res, GpkPrefsPrivate *priv)
{
	GError *error = NULL;
	GPtrArray *array;
	GtkWindow *window;
	PkClient *client = PK_CLIENT (object);
	PkError *error_code = NULL;
	PkResults *results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get set repo: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to set repo: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
		/* TRANSLATORS: for one reason or another, we could not enable or disable a software source */
		gpk_error_dialog_modal (window, _("Failed to change status"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* process messages */
	array = pk_results_get_message_array (results);
	g_ptr_array_foreach (array, (GFunc) gpk_prefs_process_messages_cb, priv);
	g_ptr_array_unref (array);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

static void
gpk_misc_enabled_toggled (GtkCellRendererToggle *cell, gchar *path_str, GpkPrefsPrivate *priv)
{
	gboolean enabled;
	gchar *repo_id = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	GtkTreeView *treeview;

	/* do we have the capability? */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_REPO_ENABLE) == FALSE) {
		g_debug ("can't change state");
		goto out;
	}

	/* get toggled iter */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    GPK_COLUMN_ENABLED, &enabled,
			    GPK_COLUMN_ID, &repo_id, -1);

	/* do something with the value */
	enabled ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    GPK_COLUMN_SENSITIVE, FALSE,
			    -1);

	/* set the repo */
	g_debug ("setting %s to %i", repo_id, enabled);
	pk_client_repo_enable_async (priv->client, repo_id, enabled,
				     priv->cancellable,
				     (PkProgressCallback) gpk_prefs_progress_cb, priv,
				     (GAsyncReadyCallback) gpk_prefs_repo_enable_cb, priv);

out:
	/* clean up */
	g_free (repo_id);
	gtk_tree_path_free (path);
}

/**
 * gpk_treeview_add_columns:
 **/
static void
gpk_treeview_add_columns (GpkPrefsPrivate *priv, GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* column for enabled toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_misc_enabled_toggled), priv);

	/* TRANSLATORS: column if the source is enabled */
	column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
							   "active", GPK_COLUMN_ENABLED,
							   "sensitive", GPK_COLUMN_SENSITIVE,
							   NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for the source description */
	column = gtk_tree_view_column_new_with_attributes (_("Software Source"), renderer,
							   "markup", GPK_COLUMN_TEXT,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * gpk_repos_treeview_clicked_cb:
 **/
static void
gpk_repos_treeview_clicked_cb (GtkTreeSelection *selection, GpkPrefsPrivate *priv)
{
	gchar *repo_id;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, GPK_COLUMN_ID, &repo_id, -1);
		g_debug ("selected row is: %s", repo_id);
		g_free (repo_id);
	} else {
		g_debug ("no row selected");
	}
}

/**
 * gpk_prefs_get_repo_list_cb
 **/
static void
gpk_prefs_get_repo_list_cb (GObject *object, GAsyncResult *res, GpkPrefsPrivate *priv)
{
	gboolean enabled;
	gchar *description;
	gchar *repo_id;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *treeview;
	GtkWindow *window;
	guint i;
	PkClient *client = PK_CLIENT (object);
	PkError *error_code = NULL;
	PkRepoDetail *item;
	PkResults *results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get repo list: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get repo list: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
		/* TRANSLATORS: for one reason or another, we could not get the list of sources */
		gpk_error_dialog_modal (window, _("Failed to get the list of sources"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* add repos */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	array = pk_results_get_repo_detail_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "repo-id", &repo_id,
			      "description", &description,
			      "enabled", &enabled,
			      NULL);
		g_debug ("repo = %s:%s:%i", repo_id, description, enabled);
		gpk_prefs_model_get_iter (priv, model, &iter, repo_id);
		gtk_list_store_set (priv->list_store, &iter,
				    GPK_COLUMN_ENABLED, enabled,
				    GPK_COLUMN_TEXT, description,
				    GPK_COLUMN_ID, repo_id,
				    GPK_COLUMN_ACTIVE, TRUE,
				    GPK_COLUMN_SENSITIVE, TRUE,
				    -1);

		g_free (repo_id);
		g_free (description);
	}

	/* remove the items that are not now present */
	gpk_prefs_remove_nonactive (model);

	/* sort */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(priv->list_store), GPK_COLUMN_TEXT, GTK_SORT_ASCENDING);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_prefs_repo_list_refresh:
 **/
static void
gpk_prefs_repo_list_refresh (GpkPrefsPrivate *priv)
{
	gboolean show_details;
	GtkTreeModel *model;
	GtkTreeView *treeview;
	GtkWidget *widget;
	PkBitfield filters;

	/* mark the items as not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gpk_prefs_mark_nonactive (priv, model);

	g_debug ("refreshing list");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_detail"));
	show_details = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (!show_details)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	else
		filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	pk_client_get_repo_list_async (priv->client, filters,
				       priv->cancellable,
				       (PkProgressCallback) gpk_prefs_progress_cb, priv,
				       (GAsyncReadyCallback) gpk_prefs_get_repo_list_cb, priv);
}

/**
 * gpk_prefs_repo_list_changed_cb:
 **/
static void
gpk_prefs_repo_list_changed_cb (PkControl *control, GpkPrefsPrivate *priv)
{
	gpk_prefs_repo_list_refresh (priv);
}

/**
 * gpk_prefs_checkbutton_detail_cb:
 **/
static void
gpk_prefs_checkbutton_detail_cb (GtkWidget *widget, GpkPrefsPrivate *priv)
{
	gpk_prefs_repo_list_refresh (priv);
}

/**
 * gpk_prefs_get_properties_cb:
 **/
static void
gpk_prefs_get_properties_cb (GObject *object, GAsyncResult *res, GpkPrefsPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	PkControl *control = PK_CONTROL(object);
	PkNetworkEnum state;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_print ("%s: %s\n", _("Exiting as backend details could not be retrieved"), error->message);
		g_error_free (error);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &priv->roles,
		      "network-state", &state,
		      NULL);

	/* only show label on mobile broadband */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "hbox_mobile_broadband"));
	gtk_widget_set_visible (widget, (state == PK_NETWORK_ENUM_MOBILE));

	/* hide if not supported */
	if (!pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_upgrade"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_upgrade"));
		gtk_widget_hide (widget);
	}

	/* setup sources GUI elements */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		gpk_prefs_repo_list_refresh (priv);
	} else {
		GtkTreeIter iter;
		GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
		GtkTreeModel *model = gtk_tree_view_get_model (treeview);

		gtk_list_store_append (GTK_LIST_STORE(model), &iter);
		gtk_list_store_set (priv->list_store, &iter,
				    GPK_COLUMN_ENABLED, FALSE,
				    GPK_COLUMN_TEXT, _("Getting software source list not supported by backend"),
				    GPK_COLUMN_ACTIVE, FALSE,
				    GPK_COLUMN_SENSITIVE, FALSE,
				    -1);

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_repo"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_detail"));
		gtk_widget_set_sensitive (widget, FALSE);
	}
out:
	return;
}

/**
 * gpk_prefs_close_cb:
 **/
static void
gpk_prefs_close_cb (GtkWidget *widget, gpointer data)
{
	GpkPrefsPrivate *priv = (GpkPrefsPrivate *) data;
	g_application_release (G_APPLICATION (priv->application));
}

/**
 * gpk_pack_startup_cb:
 **/
static void
gpk_pack_startup_cb (GtkApplication *application, GpkPrefsPrivate *priv)
{
	GError *error = NULL;
	GtkTreeSelection *selection;
	GtkWidget *main_window;
	GtkWidget *widget;
	guint retval;
	PkControl *control;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* get actions */
	control = pk_control_new ();
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (gpk_prefs_notify_network_state_cb), priv);
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_prefs_repo_list_changed_cb), priv);

	/* get UI */
	retval = gtk_builder_add_from_file (priv->builder, GPK_DATA "/gpk-prefs.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* bind the mobile broadband and battery checkbox */
	if (priv->settings_gsd != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_mobile_broadband"));
		g_settings_bind (priv->settings_gsd,
				 GSD_SETTINGS_CONNECTION_USE_MOBILE,
				 widget, "active",
				 G_SETTINGS_BIND_DEFAULT);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_battery_power"));
		g_settings_bind (priv->settings_gsd,
				 GSD_SETTINGS_UPDATE_BATTERY,
				 widget, "active",
				 G_SETTINGS_BIND_DEFAULT);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_close_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_help_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_check_now"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_check_now_cb), priv);

	/* update the combo boxes */
	if (priv->settings_gsd != NULL) {
		gpk_prefs_update_freq_combo_setup (priv);
		gpk_prefs_upgrade_freq_combo_setup (priv);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_preferences"));
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), 0);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_detail"));
	g_settings_bind (priv->settings_gpk,
			 GPK_SETTINGS_REPO_SHOW_DETAILS,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_checkbutton_detail_cb), priv);

	/* create repo tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_repo"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (priv->list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_repos_treeview_clicked_cb), priv);

	/* add columns to the tree view */
	gpk_treeview_add_columns (priv, GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_prefs"));
	gtk_application_add_window (application, GTK_WINDOW (main_window));

	gtk_widget_show (main_window);

	/* get some data */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) gpk_prefs_get_properties_cb, priv);
out:
	g_object_unref (control);
}


/**
 * gpm_prefs_commandline_cb:
 **/
static int
gpm_prefs_commandline_cb (GApplication *application,
			  GApplicationCommandLine *cmdline,
			  GpkPrefsPrivate *priv)
{
	gboolean ret;
	gchar **argv;
	gint argc;
	GOptionContext *context;
	GtkWindow *window;
	guint xid = 0;

	const GOptionEntry options[] = {
		{ "parent-window", 'p', 0, G_OPTION_ARG_INT, &xid,
		  /* TRANSLATORS: we can make this modal (stay on top of) another window */
		  _("Set the parent window to make this modal"), NULL },
		{ NULL}
	};

	/* get arguments */
	argv = g_application_command_line_get_arguments (cmdline, &argc);

	context = g_option_context_new (NULL);
	/* TRANSLATORS: program name, an application to set per-user policy for updates */
	g_option_context_set_summary(context, _("Software Update Preferences"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	ret = g_option_context_parse (context, &argc, &argv, NULL);
	if (!ret)
		goto out;

	/* make sure the window is raised */
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
	gtk_window_present (window);

	/* set the parent window if it is specified */
	if (xid != 0) {
		g_debug ("Setting xid %i", xid);
		gpk_window_set_parent_xid (window, xid);
	}
out:
	g_strfreev (argv);
	g_option_context_free (context);
	return ret;
}

/**
 * gpk_prefs_has_gsd_schema:
 **/
static gboolean
gpk_prefs_has_gsd_schema (GpkPrefsPrivate *priv)
{
	const char * const *schemas;
	guint i;

	/* find the schema in the global list */
	schemas = g_settings_list_schemas ();
	for (i = 0; schemas[i] != NULL; i++) {
		if (g_strcmp0 (schemas[i], GSD_SETTINGS_SCHEMA) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gint status = 0;
	GpkPrefsPrivate *priv = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	priv = g_new0 (GpkPrefsPrivate, 1);
	priv->cancellable = g_cancellable_new ();
	priv->builder = gtk_builder_new ();
	priv->settings_gpk = g_settings_new (GPK_SETTINGS_SCHEMA);
	priv->list_store = gtk_list_store_new (GPK_COLUMN_LAST, G_TYPE_BOOLEAN,
					       G_TYPE_STRING, G_TYPE_STRING,
					       G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	priv->client = pk_client_new ();
	g_object_set (priv->client,
		      "background", FALSE,
		      NULL);

	/* have we got a GSD with update schema */
	ret = gpk_prefs_has_gsd_schema (priv);
	if (ret)
		priv->settings_gsd = g_settings_new (GSD_SETTINGS_SCHEMA);

	/* are we already activated? */
	priv->application = gtk_application_new ("org.freedesktop.PackageKit.Prefs",
						 G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (gpk_pack_startup_cb), priv);
	g_signal_connect (priv->application, "command-line",
			  G_CALLBACK (gpm_prefs_commandline_cb), priv);

	/* run */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	if (priv != NULL) {
		g_cancellable_cancel (priv->cancellable);
		g_object_unref (priv->cancellable);
		g_object_unref (priv->builder);
		g_object_unref (priv->settings_gpk);
		g_object_unref (priv->list_store);
		g_object_unref (priv->client);
		if (priv->settings_gsd != NULL)
			g_object_unref (priv->settings_gsd);
		g_free (priv);
	}
	return status;
}
