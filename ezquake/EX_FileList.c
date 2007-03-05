/*
 * File List
 *
 * display directory contents, allows for changing dir,
 * drive etc..
 *
 * meant for use by MP3 player, mayby demo player too
 *
 * FileList have four colums:
 *  - file name (with ext)
 *  - size
 *  - date (last modification)
 *  - time (last modification)
 *
 * and allows to display different sets of columns
 * also do sorting
 *
 */

#include "quakedef.h"
#include "localtime.h"
#include "EX_misc.h"
#include "EX_FunNames.h"

// column width
#define COL_SIZE        4
#define COL_DATE        8
#define COL_TIME        5

extern void cvar_toggle (cvar_t *var);
extern void _splitpath (const char *path, char *drive, char *dir, char *file, char *ext);

#define FL_SEARCH_TIMEOUT	2.0
static double last_search_enter_time = 0.0;

//
// When changing RGBA color-value for the text in the file list (dirs/zips).
//
static qbool FL_OnChangeTextColor (cvar_t *var, char *newval)
{
	byte *color = StringToRGB(newval);
	var->value = RGBA_2_Int(color[0], color[1], color[2], color[3]);

	return true;
}

static void FL_RegisterColor (cvar_t *var)
{
	if (var)
	{
		char tempval[256];

		var->OnChange = FL_OnChangeTextColor;
		strlcpy (tempval, var->string, sizeof(tempval));
		Cvar_Set (var, tempval);
	}
}

//
// Create list
//
void FL_Init(filelist_t	*	fl,
             cvar_t *       sort_mode,
             cvar_t *       show_size,
             cvar_t *       show_date,
             cvar_t *       show_time,
             cvar_t *       strip_names,
             cvar_t *       interline,
             cvar_t *       show_status,
			 cvar_t *		scroll_names,
			 cvar_t *		file_color,
			 cvar_t *		selected_color,
			 cvar_t *		dir_color,
#ifdef WITH_ZIP
			 cvar_t *		zip_color,
#endif
			 char *			 initdir)
{
    Sys_getcwd(fl->current_dir, MAX_PATH);
	FL_SetCurrentDir(fl, initdir);
    fl->error = false;
    fl->need_refresh = true;
    fl->num_entries = 0;
    fl->current_entry = 0;
    fl->num_filetypes = 0;

    fl->sort_mode = sort_mode;
    fl->show_size = show_size;
    fl->show_date = show_date;
    fl->show_time = show_time;
    fl->strip_names = strip_names;
    fl->interline = interline;
    fl->show_status = show_status;
	fl->scroll_names = scroll_names;
	fl->search_string[0] = 0;
    fl->last_page_size = 0;

    fl->search_valid = false;
    fl->cdup_find = false;

	fl->file_color = file_color;
	fl->selected_color = selected_color;
	fl->dir_color = dir_color;

	FL_RegisterColor (fl->file_color);
	FL_RegisterColor (fl->selected_color);
	FL_RegisterColor (fl->dir_color);

	#ifdef WITH_ZIP
	fl->zip_color = zip_color;
	FL_RegisterColor (fl->zip_color);

	fl->current_zip[0] = 0;
	fl->in_zip = false;
	#endif // WITH_ZIP
}

//
// Set directory
//
void FL_SetCurrentDir(filelist_t *fl, const char *dir)
{
    char buf[MAX_PATH+1];

    if (Sys_fullpath(buf, dir, MAX_PATH+1) == NULL)
        return;

    if (strlen(buf) > MAX_PATH)    // Should never fail in this
        return;

	strlcpy (fl->current_dir, buf, sizeof(fl->current_dir));
    fl->need_refresh = true;
}

//
// Get current directory
//
char *FL_GetCurrentDir(filelist_t *fl)
{
    return fl->current_dir;
}

//
// add new file type (.qwd, .qwz, .mp3)
//
void FL_AddFileType(filelist_t *fl, int id, char *ext)
{
    int num = fl->num_filetypes;

    if (num >= MAX_FILELIST_TYPES)
        return;

    if (strlen(ext) > MAX_EXTENSION_LENGTH)
        return;

    strcpy(fl->filetypes[num].extension, ext);
    fl->filetypes[num].id = id;

    fl->num_filetypes ++;
}

//
// get current entry
//
 filedesc_t * FL_GetCurrentEntry(filelist_t *fl)
{
    if (fl->num_entries <= 0)
        return NULL;

    return &fl->entries[fl->current_entry];
}

//
// get current path
//
 char *FL_GetCurrentPath(filelist_t *fl)
{
    if (fl->num_entries <= 0)
        return NULL;

    return fl->entries[fl->current_entry].name;
}

//
// get current display
//
 char *FL_GetCurrentDisplay(filelist_t *fl)
{
    if (fl->num_entries <= 0)
        return NULL;

    return fl->entries[fl->current_entry].display;
}

//
// get current entry type
//
int FL_GetCurrentEntryType(filelist_t *fl)
{
    if (fl->num_entries <= 0)
        return -1;

    return fl->filetypes[fl->entries[fl->current_entry].type_index].id;
}

//
// is current entry a dir ?
//
qbool FL_IsCurrentDir(filelist_t *fl)
{
    if (fl->num_entries <= 0)
        return true;    // we can handle that

    return fl->entries[fl->current_entry].is_directory;
}

#ifdef WITH_ZIP
//
// Is current entry a zip file?
//
qbool FL_IsCurrentZip(filelist_t *fl)
{
	if (fl->num_entries <= 0)
	{
		return true;
	}

	return fl->entries[fl->current_entry].is_zip;
}
#endif // WITH_ZIP

void FL_StripFileName(filelist_t *fl, filedesc_t *f)
{
    int i;
    char namebuf[_MAX_FNAME] = {0};
    char extbuf[_MAX_EXT] = {0};
    char *t;

	// Don't try to strip this.
	if (!strcmp(f->name, ".."))
	{
		strlcpy (f->display, "/..", sizeof(f->display));
		return;
	}

	// Common for dir/file, get name without path but with ext
	strlcpy (namebuf, COM_SkipPath (f->name), sizeof (namebuf));

	// File specific.
	if (!f->is_directory)
	{
		// Get extension.
		snprintf (extbuf, sizeof(extbuf), ".%s", COM_FileExtension (namebuf));

		// If the extension is only a . it means we have no extension.
		if (strlen (extbuf) == 1)
		{
			extbuf[0] = '\0';
		}

		// Remove extension from the name.
		COM_StripExtension(namebuf, namebuf); 
	}

    if (fl->strip_names->value && !f->is_directory)
    {
        char *s;

        for (i=0; i < strlen(namebuf); i++)
            if (namebuf[i] == '_')
                namebuf[i] = ' ';

        // Remove spaces from the beginning
        s = namebuf;
        while (*s == ' ')
            s++;
        memmove(namebuf, s, strlen(s) + 1);

        // Remove spaces from the end
        s = namebuf + strlen(namebuf);
        while (s > namebuf  &&  *(s-1) == ' ')
            s--;
        *s = 0;

        // Remove few spaces in a row
        s = namebuf;
        while (s < namebuf + strlen(namebuf))
        {
            if (*s == ' ')
            {
                char *q = s+1;
                while (*q == ' ')
                    q++;

                if (q > s+1)
                    memmove(s+1, q, strlen(q)+1);
            }
            s++;
        }

        if (namebuf[0] == 0)
            strcpy(namebuf, "_");
    }

    // Replace all non-standard characters with '_'
    for (i=0; i < strlen(namebuf); i++)
    {
        if (namebuf[i] < ' '  ||  namebuf[i] > '~')
            namebuf[i] = '_';
    }

    f->display[0] = 0;
    t = f->display;
	// Add a slash before a dir.
    if (f->is_directory)
        *t++ = '/';

	// Add the name and extension to the final buffer.
    for (i=0; i < strlen(namebuf)  &&  (t - f->display) < MAX_PATH; i++)
        *t++ = namebuf[i];
    for (i=0; i < strlen(extbuf)  &&  (t - f->display) < MAX_PATH; i++)
        *t++ = extbuf[i];
    *t = 0;
}

//
// Goto specific file by its name
//
void FL_GotoFile(filelist_t *fl, char *name)
{
    int i;

    fl->current_entry = 0;
    if (!name[0])
        return;

    for (i=0; i < fl->num_entries; i++)
    {
        if (!strcmp(name, fl->entries[i].name))
        {
            fl->current_entry = i;
            return;
        }
    }

    return;
}

//
// file compare func
// set FL_CompareFunc_FileList before calling..
//
filelist_t * FL_CompareFunc_FileList;    // global
int FL_CompareFunc(const void * p_d1, const void * p_d2)
{
	extern int  SYSTEMTIMEcmp(const SYSTEMTIME *, const SYSTEMTIME *);
    int reverse = 0;
    filelist_t *fl = FL_CompareFunc_FileList;
    char *sort_string = fl->sort_mode->string;
    const filedesc_t *d1 = (filedesc_t *)p_d1;
    const filedesc_t *d2 = (filedesc_t *)p_d2;

    // Directories always first.
    if (d1->is_directory &&  !d2->is_directory)
        return -1;
    if (d2->is_directory  && !d1->is_directory)
        return 1;

    // Directories sorted always by name, ascending
    if (d1->is_directory && d2->is_directory)
        return strcasecmp(d1->name, d2->name);

#ifdef WITH_ZIP
	// Zips after directories.
	if (d1->is_zip && !d2->is_zip)
		return -1;
	if (d2->is_zip && !d1->is_zip)
		return 1;
	if (d1->is_zip && d2->is_zip)
		return strcasecmp(d1->name, d2->name);
#endif // WITH_ZIP

    while (true)
    {
        long int d;  // difference
        char c = *sort_string++;

        if (c & 128)
        {
            reverse = 1;
            c -= 128;
        }

        switch (c)
        {
            case '1':   // name
                d = strcasecmp(d1->name, d2->name); break;
            case '2':   // size
		        d = d1->size - d2->size;
                break;
            case '3':   // date / time
                d = SYSTEMTIMEcmp(&(d1->time), &(d2->time));
                break;
            case '4':   // type
            {
                char *ext1 = fl->filetypes[d1->type_index].extension;
                char *ext2 = fl->filetypes[d2->type_index].extension;
                d = strcasecmp(ext1, ext2);
                break;
            }
            default:
                d = d1 - d2;
        }

        if (d)
            return reverse ? -d : d;
    }
}

//
// sort directory
//
void FL_SortDir(filelist_t *fl)
{
    char name[MAX_PATH+1] = "";

    if (fl->num_entries <= 0  ||
        fl->sort_mode->string == NULL  ||
        fl->sort_mode->string[0] == 0)
        return;

    FL_CompareFunc_FileList = fl;
    if (fl->current_entry >= 0  &&  fl->current_entry < fl->num_entries)
        strcpy(name, fl->entries[fl->current_entry].name);

    qsort(fl->entries, fl->num_entries, sizeof(filedesc_t), FL_CompareFunc);

    FL_GotoFile(fl, name);

    fl->need_resort = false;
}

//
// Find out if a given directory entry is a registered file type and
// if so, returns the index of the file type. Otherwise -1 is returned.
//
static int FL_FindRegisteredType(filelist_t *fl, sys_dirent *ent)
{
	int i = 0;
	int result = -1;

	if (!ent->directory)
    {
        for (i=0; i < fl->num_filetypes; i++)
        {
            char ext[_MAX_EXT];

			snprintf (ext, sizeof(ext), ".%s", COM_FileExtension (ent->fname));

            if (!strcasecmp(fl->filetypes[i].extension, ext))
            {
                result = i;
                break;
            }
        }
    }

	return result;
}

//
// Finds which entry to highlight after doing "cd up".
//
static void FL_FindHighlightEntry (filelist_t *fl)
{
	int i = 0;

	// Look for what we have to highlight when listing the files in the
	// parent directory. If we're in c:\quake\qw\some_directory\ and do a cdup ".."
	// we want to highlight (move the cursor to) "some_directory" in the file list
	// of c:\quake\qw\ so that it's obvious which directory we just left.
    if (fl->cdup_find)
    {
        int uplen = strlen (fl->cdup_name);
        fl->cdup_find = false;
        for (i=0; i < fl->num_entries; i++)
        {
            int clen = strlen (fl->entries[i].name);

            if (uplen > clen)
			{
                continue;
			}

            if (strcmp(fl->cdup_name, fl->entries[i].name + clen - uplen))
			{
                continue;
			}

            fl->current_entry = i;
            break;
        }
    }
}

#ifdef WITH_ZIP
//
// Read the ZIP file.
//
void FL_ReadZip (filelist_t *fl)
{
	int temp = 0;
	unzFile zip_file;
	sys_dirent ent;

	fl->error = true;
    fl->need_refresh = false;
    fl->display_entry = 0;
	fl->num_entries = 0;
	fl->current_entry = 0;

	// Open the zip file.
	if (fl->current_zip != NULL)
	{
		zip_file = COM_ZipUnpackOpenFile (fl->current_zip);
	}
	else
	{
		return;
	}

	if (zip_file == NULL)
	{
		return;
	}

	// Get the first file from the zip file.
	if (!COM_ZipGetFirst (zip_file, &ent))
	{
		goto finish;
	}

	fl->error = false;

	do
    {
		// Pointer to the current file entry.
        filedesc_t *f = &fl->entries[fl->num_entries];
        f->type_index = -1;

		// Skip current/above dir and hidden files.
		if (!strcmp(ent.fname, ".") || !strcmp(ent.fname, "..") || ent.hidden)
		{
            goto skip;
		}

        // Find registered type if it's not a directory.
		f->type_index = FL_FindRegisteredType (fl, &ent);

		// We're not interested in this file type since it wasn't registered.
		if (!ent.directory && f->type_index < 0)
		{
			goto skip;
		}

        // We found a file that we're interested in so save the info about it
		// in the file description structure.
		f->is_directory = ent.directory;

		// Get the full path for the file.
		snprintf (f->name, sizeof(f->name), "%s%c%s", fl->current_zip, PATH_SEPARATOR, ent.fname);

		f->size = ent.size;
		memcpy(&f->time, &ent.time, sizeof(f->time));

        // Find friendly name.
        FL_StripFileName(fl, f);

        // Increase counter of how many files have been found.
        fl->num_entries++;
        if (fl->num_entries >= MAX_FILELIST_ENTRIES)
		{
            break;
		}
skip:
        // Get next filesystem entry
		//temp = Sys_ReadDirNext(search, &ent);
		temp = COM_ZipGetNextFile (zip_file, &ent);
    }
    while (temp > 0);

	fl->need_resort = true;

finish:
	// Close the zip file.
	COM_ZipUnpackCloseFile (zip_file);

	// Re-sort the file list if needed.
    if (fl->need_resort)
	{
        FL_SortDir (fl);
	}

	// Resort might have changed this.
    fl->current_entry = 0;

	// Find which item to highlight in the list.
	FL_FindHighlightEntry(fl);
}
#endif // WITH_ZIP

//
// read directory
//
void FL_ReadDir(filelist_t *fl)
{
    sys_dirent ent;
    unsigned long search;
    int temp;
    char  olddir[MAX_PATH+1];

    fl->error = true;
    fl->need_refresh = false;
    fl->display_entry = 0;

	// Save the current directory. (we want to restore this later)
    if (Sys_getcwd(olddir, MAX_PATH+1) == NULL)
	{
        return;
	}

	// Change to the new dir.
    if (!Sys_chdir(fl->current_dir))
	{
		// Set the current dir.
        return;
	}

	fl->num_entries = 0;
	fl->current_entry = 0;
    fl->error = false;

	// Get the first entry in the dir.

	search = Sys_ReadDirFirst(&ent);
	if (!search)
	{
		goto finish;
	}

    do
    {
		// Pointer to the current file entry.
        filedesc_t *f = &fl->entries[fl->num_entries];
		memset (f, 0, sizeof(filedesc_t));
        f->type_index = -1;

		// Skip current/above dir and hidden files.
		if (!strcmp(ent.fname, ".") || ent.hidden)
		{
            goto skip;
		}

        // Find registered type if it's not a directory.
		f->type_index = FL_FindRegisteredType (fl, &ent);

		#ifdef WITH_ZIP
		if (COM_ZipIsArchive (ent.fname))
		{
			f->is_zip = true;
		}
		#endif

		// We're not interested in this file type since it wasn't registered.
        if (!ent.directory && f->type_index < 0
		#ifdef WITH_ZIP
			// Or isn't a zip.
			&& !f->is_zip
		#endif
			)
		{
			goto skip;
		}

        // We found a file that we're interested in so save the info about it
		// in the file description structure.
		{
			f->is_directory = ent.directory;

			if (!strcmp(ent.fname, ".."))
			{
				// Don't get the full path for the ".." (parent) dir, that's just confusing.
				strlcpy (f->name, ent.fname, sizeof (f->name));
			}
			else
			{
				// Get full path for normal files/dirs.
				Sys_fullpath(f->name, ent.fname, MAX_PATH+1);
			}

			f->size = ent.size;
			memcpy(&f->time, &ent.time, sizeof(f->time));
		}

        // Find friendly name.
        FL_StripFileName(fl, f);

        // Increase counter of how many files have been found.
        fl->num_entries++;
        if (fl->num_entries >= MAX_FILELIST_ENTRIES)
		{
            break;
		}
skip:
		memset (&ent, 0, sizeof(ent));

        // Get next filesystem entry
		temp = Sys_ReadDirNext(search, &ent);
    }
    while (temp > 0);

	// Close the handle for the directory.
	Sys_ReadDirClose(search);

    fl->need_resort = true;

finish:
	// Change the current dir back to what it was.
	Sys_chdir (olddir);

    // Re-sort the file list if needed.
    if (fl->need_resort)
	{
        FL_SortDir (fl);
	}

	// Resort might have changed this.
    fl->current_entry = 0;

	// Find which item to highlight in the list.
	FL_FindHighlightEntry(fl);

    return;
}

//
// Search by name for next item.
//
qbool FL_Search(filelist_t *fl)
{
	int start = fl->current_entry;
	int stop = fl->num_entries;
	int i = 0;
	char *s = NULL;
	char tmp[512];

	fl->search_dirty = false;

search :

	// First search at the beginning of the string (like when you type in a window in explorer).
	for (i = start; i < stop; i++)
	{
		strlcpy (tmp, fl->entries[i].display, sizeof (tmp));
		s = tmp;

		// Get rid of slashes for dirs.
		while (s && (*s) == '/')
		{
			s++;
		}

		if (!strncasecmp (s, fl->search_string, strlen (fl->search_string)))
		{
			fl->current_entry = i;
			return true;
		}
	}

	// Nothing found, so search in any part of the string.
	for (i = start; i < stop; i++)
	{
		strlcpy (tmp, fl->entries[i].display, sizeof (tmp));
		FunToSort (tmp);

		if (strstr(tmp, fl->search_string))
		{
			fl->current_entry = i;
			return true;
		}
	}

	// Try to search from the start if we didn't find it below the
	// current position of the cursor.
	if (start > 0)
	{
		start = 0;
		stop = fl->current_entry; // No idea searching anything below this again (we just did that).
		goto search;
	}

	// We couldn't find anything.
	fl->search_error = true;
	return false;
}

//
// FL_ChangeZip - enter a zip file
//
void FL_ChangeZip(filelist_t *fl, char *newzip)
{
	strlcpy (fl->current_zip, newzip, sizeof(fl->current_zip));
	fl->in_zip = true;

	fl->need_refresh = true;
}

//
// FL_ChangeDir - changes directory
//
void FL_ChangeDir(filelist_t *fl, char *newdir)
{
	char olddir[MAX_PATH+1];

	// Get the current dir from the OS and save it.
    if (Sys_getcwd(olddir, MAX_PATH+1) == NULL)
	{
		return;
	}

	// Change to the current dir that we're in (might be different from the OS's).
	// If we're changing dirs in a relative fashion ".." for instance we need to
	// be in this dir, and not the dir that the OS is in.
    if (Sys_chdir(fl->current_dir) == 0)
	{
		// Normal directory.
        return;
	}

	// Change to the new dir requested.
	Sys_chdir (newdir);

	// Save the current dir we just changed to.
	Sys_getcwd (fl->current_dir, MAX_PATH+1);

	// Go back to where the OS wants to be.
    Sys_chdir (olddir);

    fl->need_refresh = true;

	#ifdef WITH_ZIP
	// Since we just changed to a new directory we can't be in a zip file.
	fl->current_zip[0] = 0;
	fl->in_zip = false;
	#endif // WITH_ZIP
}


//
// FL_ChangeDirUp - cd ..
//
void FL_ChangeDirUp(filelist_t *fl)
{
	int current_len = 0;

	// No point doing anything.
    if (strlen(fl->current_dir) < 2)
	{
        return;
	}

	// Get the name of the directory we're leaving, so that we can highlight it
	// in the file list of it's parent. (makes it easier keep track of where you are)
	{
		current_len = strlen(fl->current_dir);

		fl->cdup_find = true;

		#ifdef WITH_ZIP
		if (fl->in_zip)
		{
			strlcpy (fl->cdup_name, COM_SkipPath(fl->current_zip), sizeof(fl->cdup_name));
		}
		else
		#endif // WITH_ZIP
		{
			strlcpy (fl->cdup_name, COM_SkipPath(fl->current_dir), sizeof(fl->cdup_name));
		}

		// fl->cdup_name will be:
		// the_directory_were_leaving
		// If the full path was:
		// c:\quake\qw\the_directory_were_leaving
	}

	// Change the dir to "c:\quake\qw" (from above example).
	#ifdef WITH_ZIP
	if (fl->in_zip)
	{
		FL_ChangeDir(fl, fl->current_dir);
	}
	else
	#endif // WITH_ZIP
	{
		FL_ChangeDir(fl, "..");
	}
}

//
// check current position
//
void FL_CheckPosition(filelist_t *fl)
{
    if (fl->current_entry < 0)
        fl->current_entry = 0;
    if (fl->current_entry >= fl->num_entries)
        fl->current_entry = fl->num_entries - 1;
}

#ifdef WITH_ZIP
void FL_CompressFile (filelist_t *fl)
{
	char *file_path = FL_GetCurrentPath(fl);

	// Remember where we were.
	int ce = fl->current_entry;	 

	// Compress the file
	int ret = COM_GZipPack(file_path, va("%s.gz", file_path), false); 

	if (ret)
	{
		_unlink(file_path);
	}
	else
	{
		Com_Printf("Failed compressing file to GZip.");
	}

	FL_ReadDir(fl);				// Reload dir.
	fl->current_entry = ce;		// Set previous position.
	FL_CheckPosition(fl);
}

void FL_DecompressFile (filelist_t *fl)
{
	int ret = 0;

	// Remember where we were.
	int ce = fl->current_entry;	 
	char *gzip_path = FL_GetCurrentPath(fl);
	char file_path[MAX_PATH];

	COM_StripExtension(gzip_path, file_path);

	ret = COM_GZipUnpack(gzip_path, file_path, false); 

	if (ret)
	{
		_unlink(gzip_path);
	}
	else
	{
		Com_Printf("Failed decompressing file from GZip.");
	}

	FL_ReadDir(fl);				// Reload dir.
	fl->current_entry = ce;		// Set previous position.
	FL_CheckPosition(fl);
}
#endif // WITH_ZIP

//
// Delete the current file.
//
void FL_DeleteFile(filelist_t *fl)
{
	int ce = fl->current_entry;		// Remember where we were.
	unlink(FL_GetCurrentPath(fl));	// Delete.
	FL_ReadDir(fl);					// Reload dir.
	fl->current_entry = ce - 1;		// Set previous position.
	FL_CheckPosition(fl);
}

//
// Check display position
//
void FL_CheckDisplayPosition(filelist_t *fl, int lines)
{
    // FIXME: move list earlier..
    if (fl->current_entry > fl->display_entry + lines - 1)
        fl->display_entry = fl->current_entry - lines + 1;

    if (fl->display_entry > fl->num_entries - lines)
        fl->display_entry = max(fl->num_entries - lines, 0);

    if (fl->current_entry < fl->display_entry)
        fl->display_entry = fl->current_entry;
}

//
// keys handling
// returns: true if processed, false if ignored
//
 qbool FL_Key(filelist_t *fl, int key)
{
	if (fl->mode != FL_MODE_NORMAL)
	{
		switch(key) 
		{
			case 'y':
			case 'Y':
			case K_ENTER:
				if (!FL_IsCurrentDir(fl))
				{
					if (fl->mode == FL_MODE_DELETE)
					{
						FL_DeleteFile(fl);
					}
					#ifdef WITH_ZIP
					else if (fl->mode == FL_MODE_COMPRESS)
					{
						FL_CompressFile(fl); // Compress file.
					}
					else if (fl->mode == FL_MODE_DECOMPRESS)
					{
						FL_DecompressFile(fl); // Decompress file.
					}
					#endif // WITH_ZIP
				}

				fl->mode = FL_MODE_NORMAL;
				return true;
			case 'n':
			case 'N':
			case K_ESCAPE:
				fl->mode = FL_MODE_NORMAL;
				return true;
		}

		return false;
	}

    // Check for search
    if ((key >= ' ' && key <= '~') && (fl->search_valid || (!isAltDown() && !isCtrlDown() && !isShiftDown())))
    {
        int len;

        if (!fl->search_valid)
        {
            // Start searching
            fl->search_valid = true;
            fl->search_error = false;
            fl->search_string[0] = 0;
        }

        len = strlen(fl->search_string);

        if (len < MAX_SEARCH_STRING && !fl->search_error)
        {
            fl->search_string[len] = key;
            fl->search_string[len+1] = 0;
			fl->search_dirty = true;

			// Save the last time the user entered a char in the
			// search term, so that we know when to timeout the search prompt.
			// (See beginning of FL_Draw)
			last_search_enter_time = Sys_DoubleTime();
        }

        return true;    // handled
    }
    else
    {
        fl->search_valid = false;   // finish search mode
    }

    // sorting mode / displaying columns
	if (key >= '1' && key <= '4') {
		if (isCtrlDown() && !isAltDown() && !isShiftDown())
		{
			switch (key)
			{
			case '2':
				cvar_toggle(fl->show_size); break;
			case '3':
				cvar_toggle(fl->show_date); break;
			case '4':
				cvar_toggle(fl->show_time); break;
			default:
				break;
			}
			return true;
		}
		else if (!isCtrlDown() && isAltDown() && !isShiftDown())
		{
			char buf[128];

			strncpy(buf, fl->sort_mode->string, 32);
			if (key  ==  buf[0])
			{
				// reverse order
				buf[0] ^= 128;
			}
			else
			{
				// add new
				memmove(buf+1, buf, strlen(buf)+1);
				buf[0] = key;
			}
			buf[8] = 0;
			Cvar_Set(fl->sort_mode, buf);

			fl->need_resort = true;
			return true;
		}
	}

    // change drive
#ifdef _WIN32
    if (isAltDown()  &&  isCtrlDown()  &&
        tolower(key) >= 'a'  &&  tolower(key) <= 'z')
    {
        char newdir[MAX_PATH+1];
        char olddir[MAX_PATH+1];

        sprintf(newdir, "%c:\\", tolower(key));

        // validate
        if (Sys_getcwd(olddir, MAX_PATH+1) == 0)
            return true;
        if (Sys_chdir(newdir) == 0)
            return true;
        Sys_chdir(olddir);

        // and set
        FL_SetCurrentDir(fl, newdir);
        fl->need_refresh = true;

        return true;
    }
#endif

    if (key == '\\'  ||  key == '/')
    {
        FL_ChangeDir(fl, va("%c", PATH_SEPARATOR));
        return true;
    }

	if (key == K_ENTER || key == K_MOUSE1)
    {
        if (FL_IsCurrentDir(fl))
        {
            FL_ChangeDir(fl, FL_GetCurrentPath(fl));
            return true;
        }
		#ifdef WITH_ZIP
		else if (FL_IsCurrentZip(fl))
		{
			FL_ChangeZip(fl, FL_GetCurrentPath(fl));
			return true;
		}
		#endif //WITH_ZIP
        else
        {
			return false;
        }
    }

    if (key == K_BACKSPACE)
    {
        FL_ChangeDirUp(fl);
        return true;
    }

    if (key == K_UPARROW || key == K_MWHEELUP)
    {
        fl->current_entry--;
        FL_CheckPosition(fl);
        return true;
    }

    if (key == K_DOWNARROW || key == K_MWHEELDOWN)
    {
        fl->current_entry++;
        FL_CheckPosition(fl);
        return true;
    }

    if (key == K_PGUP)
    {
        fl->current_entry -= fl->last_page_size;
        FL_CheckPosition(fl);
        return true;
    }

    if (key == K_PGDN)
    {
        fl->current_entry += fl->last_page_size;
        FL_CheckPosition(fl);
        return true;
    }

    if (key == K_HOME)
    {
        fl->current_entry = 0;
        FL_CheckPosition(fl);
        return true;
    }

    if (key == K_END)
    {
        fl->current_entry = fl->num_entries - 1;
        FL_CheckPosition(fl);
        return true;
    }

	#ifdef WITH_ZIP
	//
	// Compress the current file.
	//
	if ((key == 'c' || key == 'C') && isAltDown())
	{
		if (!FL_IsCurrentDir(fl))
		{
			if (isShiftDown())
			{
				// Alt + shift + c == Compress without confirming.
				FL_CompressFile(fl);
			}
			else
			{
				// Alt + c == Confirm before compressing.
				fl->mode = FL_MODE_COMPRESS;
			}
		}
		return true;
	}

	//
	// Decompress the current file.
	//
	if ((key == 'd' || key == 'D') && isAltDown())
	{
		if (!strcmp(COM_FileExtension(FL_GetCurrentPath(fl)), "gz"))
		{
			FL_DecompressFile(fl);
		}
	}
	#endif // WITH_ZIP

	//
	// Delete the current file.
	//
	if (key == K_DEL)
	{
		if (!FL_IsCurrentDir(fl)) 
		{
			if (isShiftDown())
			{
				// Shift + del == Delete without confirming.
				FL_DeleteFile(fl);
			}
			else
			{
				// Del == Confirm before deleting.
				fl->mode = FL_MODE_DELETE;
			}
		}
		return true;
	}

    return false;
}

qbool FL_Mouse_Move(filelist_t *fl, const mouse_state_t *ms)
{
	int entry;

	if (ms->x > fl->width || ms->y > fl->height)
		return false;

	// we presume that each line is 8 px high

	entry = (int) ms->y / 8 - 2;
	fl->current_entry = fl->display_entry + entry;
	FL_CheckPosition(fl);

	return true;
}

//
// This is used only in FL_Draw below, EX_browser.c has Add_Column2
//
static void Add_Column(char *line, int *pos, char *t, int w)
{
	// Adds columns starting from the right.

	// If we're too far to the left we can't fit a column
	// of this width.
    if ((*pos) - w - 1  <=  1)
	{
        return;
	}

	// Move the position to the left by the width of the column.
    (*pos) -= w;

	// Copy the contents into the column.
    memcpy(line + (*pos), t, min(w, strlen(t)));

	// Create a space for the next column.
    (*pos)--;
    line[*pos] = ' ';
}

//
// FileList drawing func
//
void FL_Draw(filelist_t *fl, int x, int y, int w, int h)
{
	int i;
	int listsize, pos, interline, inter_up, inter_dn, rowh;
	char line[1024];
	char sname[MAX_PATH] = {0}, ssize[COL_SIZE+1] = {0}, sdate[COL_DATE+1] = {0}, stime[COL_TIME+1] = {0};

	// Check if it's time for us to reset the search.
	// (FL_SEARCH_TIMEOUT seconds after the user entered the last char in the search term)
	if (Sys_DoubleTime() - last_search_enter_time >= FL_SEARCH_TIMEOUT)
	{
		fl->search_valid = false;
		fl->search_string[0] = 0;
	}

	if (fl->mode == FL_MODE_DELETE)
	{
		UI_Print_Center(x, y + 8,  w, "Are you sure you want to delete this file?", true);
		UI_Print_Center(x, y + 24, w, FL_GetCurrentDisplay(fl), false);
		UI_Print_Center(x, y + 40, w, "(Y/N)", true);
		return;
	}
	#ifdef WITH_ZIP
	else if (fl->mode == FL_MODE_COMPRESS)
	{
		UI_Print_Center(x, y + 8,  w, "Are you sure you want to compress this file?", true);
		UI_Print_Center(x, y + 24, w, FL_GetCurrentDisplay(fl), false);
		UI_Print_Center(x, y + 40, w, "(Y/N)", true);
		return;
	}
	else if (fl->mode == FL_MODE_DECOMPRESS)
	{
		UI_Print_Center(x, y + 8,  w, "Are you sure you want to decompress this file?", true);
		UI_Print_Center(x, y + 24, w, FL_GetCurrentDisplay(fl), false);
		UI_Print_Center(x, y + 40, w, "(Y/N)", true);
		return;
	}
	#endif // WITH_ZIP

    fl->last_page_size = 0;

	fl->width = w;
	fl->height = h;

    // Calculate interline (The space between each row)
    interline = fl->interline->value;
    interline = max(interline, 0);
    interline = min(interline, 6);
    inter_up = interline / 2;
    inter_dn = interline - inter_up;
    rowh = 8 + inter_up + inter_dn;
    listsize = h / rowh;

    // Check screen boundaries and mimnimum size
    if (w < 160 || h < 80)
        return;
    if (x < 0 || y < 0 || x + w > vid.width || y + h > vid.height)
        return;

    if (fl->need_refresh)
	{
		#ifdef WITH_ZIP
		if (fl->in_zip)
		{
			FL_ReadZip (fl);
		}
		else
		#endif // WITH_ZIP
		{
			FL_ReadDir(fl);
		}
	}

    if (fl->need_resort)
	{
        FL_SortDir(fl);
	}

    if (fl->search_dirty)
	{
		FL_Search(fl);
	}

	// Print the current path.
	{
		char *curr_path = NULL;

		#ifdef WITH_ZIP
		if (fl->in_zip)
		{
			curr_path = fl->current_zip;
		}
		else
		#endif // WITH_ZIP
		{
			curr_path = fl->current_dir;
		}

		// Make the path fit on screen "c:\quake\bla\bla\bla" => "c:\quake...la\bla".
		COM_FitPath (line, sizeof(line), curr_path, w/8);
	}

    line[w/8] = 0;
    UI_Print(x, y + inter_up, line, true);
    listsize--;

    // Draw column titles.
    pos = w/8;
    memset(line, ' ', pos);
    line[pos] = 0;

    if (fl->show_time->value)
        Add_Column(line, &pos, "time", COL_TIME);
    if (fl->show_date->value)
        Add_Column(line, &pos, "date", COL_DATE);
    if (fl->show_size->value)
        Add_Column(line, &pos, "  kb", COL_SIZE);

    memcpy(line, "name", min(pos, 4));
    line[w/8] = 0;
    UI_Print_Center(x, y + rowh + inter_dn, w, line, true);
    listsize--;

	// Nothing to show.
    if (fl->num_entries <= 0)
    {
        UI_Print_Center(x, y + 2 * rowh + inter_dn + 4, w, "directory empty", false);
        return;
    }

	// Something went wrong when processing the directory.
    if (fl->error)
    {
        UI_Print_Center(x, y + 2 * rowh + inter_dn + 4, w, "error reading directory", false);
        return;
    }

	// If we're showing the status bar we have less room for the file list.
    if (fl->show_status->value)
	{
        listsize -= 3;
	}

    fl->last_page_size = listsize;  // Remember for PGUP/PGDN

    // Check / fix display position now
    FL_CheckPosition(fl);
    FL_CheckDisplayPosition(fl, listsize);

    for (i = 0; i < listsize; i++)
    {
        filedesc_t *entry;
        DWORD dwsize;
		char size[COL_SIZE+1], date[COL_DATE+1], time[COL_TIME+1];
		char name[MAX_PATH];
        int filenum = fl->display_entry + i;
		clrinfo_t clr[2]; // here we use _one_ color, at begining of the string
		static char	last_name[sizeof(name)]	= {0}; // this is for scroll, can't put it inside scroll code

		clr[0].c = int_white;
		clr[0].i = 0; // begin of the string

        if (filenum >= fl->num_entries)
            break;

        entry = &fl->entries[filenum];

        // Extract date & time.
        snprintf(date, sizeof(date), "%02d-%02d-%02d", entry->time.wYear % 100, entry->time.wMonth, entry->time.wDay);
        snprintf(time, sizeof(time), "%2d:%02d", entry->time.wHour, entry->time.wMinute);

        // Extract size.
        if (entry->is_directory)
        {
            strlcpy(size, "<-->", sizeof(size));
            if (filenum == fl->current_entry)
                strlcpy(ssize, "dir", sizeof(ssize));
        }
        else
        {
            dwsize = entry->size / 1024;
            if (dwsize > 9999)
            {
                dwsize /= 1024;
                dwsize = min(dwsize, 999);
                snprintf(size, sizeof(size), "%3dm", dwsize);
                if (filenum == fl->current_entry)
                    snprintf(ssize, sizeof(ssize), "%d mb", dwsize);
            }
            else
            {
                snprintf(size, sizeof(size), "%4d", dwsize);
                if (filenum == fl->current_entry)
                    snprintf(ssize, sizeof(ssize), "%d kb", dwsize);
            }
        }

        // Add the columns to the current row (starting from the right).
        pos = w/8;

		// Clear the line.
        memset(line, ' ', pos);
		line[pos] = 0;

		// Add columns.
        if (fl->show_time->value)
            Add_Column(line, &pos, time, COL_TIME);
        if (fl->show_date->value)
            Add_Column(line, &pos, date, COL_DATE);
        if (fl->show_size->value)
            Add_Column(line, &pos, size, COL_SIZE);

		// End of name, switch to white.
		clr[1].c = int_white;
		clr[1].i = pos;

		// Set the name.
		// (Add a space infront so that the cursor for the currently
		// selected file can fit infront)
		snprintf (name, sizeof(name) - 1, " %s", entry->display);

		//
		// Copy the display name of the entry into the space that's left on the row.
		//
		if (filenum == fl->current_entry && fl->scroll_names->value && strlen(name) > pos)
		{
			// We need to scroll the text since it doesn't fit.
			#define SCROLL_RIGHT	1
			#define SCROLL_LEFT		0

			double			t					= 0;
			static double	t_last_scroll		= 0;
			static qbool	wait				= false;
			static int		scroll_position		= 0;
			static int		scroll_direction	= SCROLL_RIGHT;
			static int		last_text_length	= 0;
			int				text_length			= strlen(name);
			float			scroll_delay		= 0.1;

			// If the text has changed since last time we scrolled
			// the scroll data will be invalid so reset it.
			if (last_text_length != text_length || !last_name[0] || strncmp(name, last_name, sizeof(last_name)))
			{
				strlcpy(last_name, name, sizeof(last_name));
				scroll_position = 0;
				scroll_direction = SCROLL_RIGHT;
				last_text_length = text_length;

				// Wait a second before start scroll plz.
				wait = true;
				t_last_scroll = Sys_DoubleTime();
			}

			// Get the current time.
			t = Sys_DoubleTime();

			// Time to scroll.
			if (!wait && (t - t_last_scroll) > scroll_delay)
			{
				// Save the current time as the last time we scrolled
				// and change the scroll position depending on what direction we're scrolling.
				t_last_scroll = t;
				scroll_position = (scroll_direction == SCROLL_RIGHT) ? scroll_position + 1 : scroll_position - 1;
			}

			// Set the scroll direction.
			if (text_length - scroll_position == pos)
			{
				// We've reached the end, go back.
				scroll_direction = SCROLL_LEFT;
				wait = true;
			}
			else if (scroll_position == 0)
			{
				// At the beginning, start over.
				scroll_direction = SCROLL_RIGHT;
				wait = true;
			}

			// Wait a second when changing direction.
			if (wait && (t - t_last_scroll) > 1.0)
			{
				wait = false;
			}

			memcpy(line, name + scroll_position, min(pos, text_length + scroll_position));
		}
		else
		{
			//
			// Fits in the name column, no need to scroll (or the user doesn't want us to scroll :~<)
			//

			// Did we just select a new entry? In that case reset the scrolling.
			if (filenum == fl->current_entry)
			{
				last_name[0] = 0;
			}

			// If it's not the selected directory/zip color it so that it stands out.
			if (filenum != fl->current_entry)
			{
				if (entry->is_directory)
				{
					// Set directory color.
					clr[0].c = (fl->dir_color == NULL) ? int_white : (int)fl->dir_color->value;
				}
				#ifdef WITH_ZIP
				else if (entry->is_zip)
				{
					// Set zip color.
					clr[0].c = (fl->zip_color == NULL) ? int_white : (int)fl->zip_color->value;
				}
				#endif // WITH_ZIP
				else
				{
					clr[0].c = (fl->file_color == NULL) ? int_white : (int)fl->file_color->value;
				}
			}

			memcpy(line, name, min(pos, strlen(name)));
		}

		// Draw a cursor character at the start of the line.
        if (filenum == fl->current_entry)
		{
            line[0] = 141;
			UI_DrawGrayBox(x, y + rowh * (i + 2) + inter_dn, w, rowh);
		}

		// Max amount of characters that fits on a line.
		line[w/8] = 0;

		// Color the selected entry.
		if (filenum == fl->current_entry)
		{
			clr[0].c = (fl->selected_color == NULL) ? int_white : (int)fl->selected_color->value;
			clr[0].i = 1; // Don't color the cursor (index 0).
		}

		// Print the line for the directory entry.
        UI_Print_Center3(x, y + rowh * (i + 2) + inter_dn, w, line, clr, 2, 0);

        // Remember the currently selected file for dispalying in the status bar
        if (filenum == fl->current_entry)
        {
			strlcpy (sname, line + 1, min(pos, sizeof(sname)));
            strlcpy (stime, time, sizeof(stime));
            snprintf (sdate, sizeof(sdate), "%02d-%02d-%02d", entry->time.wYear % 100, entry->time.wMonth, entry->time.wDay);
        }
    }

	// Show a statusbar displaying the currently selected entry.
    if (fl->show_status->value)
    {
		// Print a line to part the status bar from the file list.
        memset(line, '\x1E', w/8);
        line[0] = '\x1D';
        line[w/8 - 1] = '\x1F';
        line[w/8] = 0;
        UI_Print(x, y + h - 3 * rowh - inter_up, line, false);

		// Print the name.
        UI_Print_Center(x, y + h - 2 * rowh - inter_up, w, sname, false);

        if (fl->search_valid)
        {
			// Some weird but nice-looking string in Quake font perhaps
			strlcpy(line, "search for: ", sizeof(line));   // seach for:
            if (fl->search_error)
            {
                strlcat(line, "not found", sizeof(line));
                UI_Print_Center(x, y + h - rowh - inter_up, w, line, false);
            }
            else
            {
                strlcat(line, fl->search_string, sizeof(line));
                UI_Print_Center(x, y + h - rowh - inter_up, w, line, true);
            }
        }
        else
        {
            snprintf(line, sizeof(line), "%s \x8f modified: %s %s", ssize, sdate, stime);
            UI_Print_Center(x, y + h - rowh - inter_up, w, line, false);
        }
    }
}

// make ../backspace work fine (ala demoplayer)
// do not show hidden files
