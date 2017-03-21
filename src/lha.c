/*
 *  Copyright (C) 2008 Giuseppe Torelli - <colossus73@gmail.com>
 *  Copyright (C) 2006 Lukasz 'Sil2100' Zemczak - <sil2100@vexillium.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include "lha.h"
#include "main.h"
#include "string_utils.h"
#include "support.h"
#include "window.h"

static gboolean data_line, last_line;

gboolean xa_lha_check_program (gchar *path)
{
	gchar *stdout, *stderr;
	gboolean full_lha;

	g_spawn_command_line_sync(path, &stdout, &stderr, NULL, NULL);
	full_lha = (g_ascii_strncasecmp("Lhasa ", stdout, 6) != 0);
	g_free(stderr);
	g_free(stdout);

	return full_lha;
}

void xa_lha_ask (XArchive *archive)
{
	archive->can_test = TRUE;
	archive->can_extract = TRUE;
	archive->can_add = archiver[archive->type].is_compressor;
	archive->can_delete = archiver[archive->type].is_compressor;
	archive->can_overwrite = TRUE;
	archive->can_full_path = TRUE;
	archive->can_update = archiver[archive->type].is_compressor;
	archive->can_move = archiver[archive->type].is_compressor;
}

static void xa_lha_parse_output (gchar *line, XArchive *archive)
{
	gpointer item[6];
	unsigned int linesize,n,a;
	gboolean dir;
	gchar *filename;
	XEntry *entry;

	if (last_line)
		return;
	if (!data_line)
	{
		if (line[0] == '-')
		{
			data_line = TRUE;
			return;
		}
		return;
	}
	if (strncmp(line,"----",4) == 0)
	{
		last_line = TRUE;
		return;
	}
	linesize = strlen(line);
	archive->nr_of_files++;

	/* Permission */
	line[10] = '\0';
	item[1] = line;
	dir = (line[0] == 'd');

	/* UID/GID */
	line[22] = '\0';
	item[2] = line + 11;

	//TODO verify the len of the size column with a big archive
	/* Size */
	for(n = 23;n < linesize;n++)
	if(line[n] != ' ')
		break;

	a = n;
	for(;n < linesize;n++)
	if(line[n] == ' ')
		break;

	line[a+(n-a)] = '\0';
	item[3] = line + a;
	archive->files_size += g_ascii_strtoull(item[3],NULL,0);

    /* Ratio */
    line[37] = '\0';
    item[4] = line + 31;

    /* Timestamp */
    line[50] = '\0';
    item[5] = line + 38;

	line[(linesize- 1)] = '\0';
	filename = line + 51;

	/* Symbolic link */
	gchar *temp = g_strrstr (filename,"->");
	if (temp)
	{
		gint len = strlen(filename) - strlen(temp);
		item[0] = (filename +=3) + len;
		filename -= 3;
		filename[strlen(filename) - strlen(temp)-1] = '\0';
	}
	else
		item[0] = NULL;

	entry = xa_set_archive_entries_for_each_row (archive,filename,item);

	if (entry && dir)
		entry->is_dir = TRUE;
}

void xa_lha_open (XArchive *archive)
{
	gchar *command;
	guint i;

	data_line = FALSE;
	last_line = FALSE;
	command = g_strconcat(archiver[archive->type].program[0], " l ", archive->path[1], NULL);
	archive->files_size = 0;
	archive->nr_of_files = 0;
	archive->nc = 6;
	archive->parse_output = xa_lha_parse_output;
	xa_spawn_async_process (archive,command);
	g_free (command);

	GType types[]= {GDK_TYPE_PIXBUF,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_STRING,G_TYPE_POINTER};
	archive->column_types = g_malloc0(sizeof(types));
	for (i = 0; i < 8; i++)
		archive->column_types[i] = types[i];

	char *names[]= {(_("Points to")),(_("Permissions")),(_("UID/GID")),(_("Size")),(_("Ratio")),(_("Timestamp"))};
	xa_create_liststore (archive,names);
}

void xa_lha_test (XArchive *archive)
{
	gchar *command = NULL;
	GSList *list = NULL;

	command = g_strconcat(archiver[archive->type].program[0], " t ", archive->path[1], NULL);

	list = g_slist_append(list,command);
	xa_run_command (archive,list);
}

/*
 * Note: lha does not seem to be able to handle wildcards in file names.
 */

gboolean xa_lha_extract (XArchive *archive, GSList *file_list)
{
	GString *files;
	gchar *command;
	GSList *list = NULL;

	files = xa_quote_filenames(file_list, NULL, TRUE);
	command = g_strconcat(archiver[archive->type].program[0],
	                      archive->do_full_path ? " x" : " xi",
	                      archive->do_overwrite ? "f" : "",
	                      "w=", archive->extraction_dir, " ",
	                      archive->path[1], files->str, NULL);
	g_string_free(files,TRUE);
	list = g_slist_append(list,command);

	return xa_run_command(archive, list);
}

void xa_lha_add (XArchive *archive, GSList *file_list, gchar *compression)
{
	GString *files;
	GSList *list = NULL;
	gchar *command = NULL;

	if (archive->location_path != NULL)
		archive->child_dir = g_strdup(archive->working_dir);

	if (!compression)
		compression = "5";

	files = xa_quote_filenames(file_list, NULL, TRUE);
	command = g_strconcat(archiver[archive->type].program[0],
	                      archive->do_update ? " u" : " a",
	                      archive->do_move ? "d" : "",
	                      "o", compression, " ",
	                      archive->path[1], files->str, NULL);
	g_string_free(files,TRUE);
	list = g_slist_append(list,command);

	xa_run_command (archive,list);
}

void xa_lha_delete (XArchive *archive, GSList *file_list)
{
	GString *files;
	gchar *command;
	GSList *list = NULL;

	files = xa_quote_filenames(file_list, NULL, TRUE);
	command = g_strconcat(archiver[archive->type].program[0], " d ", archive->path[1], files->str, NULL);
	g_string_free(files,TRUE);
	list = g_slist_append(list,command);

	xa_run_command (archive,list);
}
