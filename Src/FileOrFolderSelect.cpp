/////////////////////////////////////////////////////////////////////////////
//    WinMerge:  an interactive diff/merge utility
//    Copyright (C) 1997-2000  Thingamahoochie Software
//    Author: Dean Grimm
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
/////////////////////////////////////////////////////////////////////////////
/** 
 * @file  FileOrFolderSelect.cpp
 *
 * @brief Implementation of the file and folder selection routines.
 */

#include <windows.h>
#include "FileOrFolderSelect.h"
#include <shlobj.h>
#include <sys/stat.h>
#include "Environment.h"
#include "paths.h"
#include "MergeApp.h"

static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam,
		LPARAM lpData);
static void ConvertFilter(LPTSTR filterStr);

/** @brief Last selected folder for folder selection dialog. */
static String LastSelectedFolder;

/**
 * @brief Helper function for selecting folder or file.
 * This function shows standard Windows file selection dialog for selecting
 * file or folder to open or file to save. The last parameter @p is_open selects
 * between open or save modes. Biggest difference is that in save-mode Windows
 * asks if user wants to override existing file.
 * @param [in] parent Handle to parent window. Can be a NULL, but then
 *     CMainFrame is used which can cause modality problems.
 * @param [out] path Selected path is returned in this string
 * @param [in] initialPath Initial path (and file) shown when dialog is opened
 * @param [in] titleid Resource string ID for dialog title.
 * @param [in] filterid 0 or STRING ID for filter string
 *     - 0 means "All files (*.*)". Note the string formatting!
 * @param [in] is_open Selects Open/Save -dialog (mode).
 * @note Be careful when setting @p parent to NULL as there are potential
 * modality problems with this. Dialog can be lost behind other windows!
 * @param [in] defaultExtension Extension to append if user doesn't provide one
 */
BOOL SelectFile(HWND parent, String& path, LPCTSTR initialPath /*=NULL*/,
		const String& stitle /*=_T("")*/, const String& sfilter /*=_T("")*/,
		BOOL is_open /*=TRUE*/, LPCTSTR defaultExtension /*=NULL*/)
{
	path.clear(); // Clear output param

	// This will tell common file dialog what to show
	// and also this will hold its return value
	TCHAR sSelectedFile[MAX_PATH] = {0};

	// check if specified path is a file
	if (initialPath && initialPath[0])
	{
		// If initial path info includes a file
		// we put the bare filename into sSelectedFile
		// so the common file dialog will start up with that file selected
		if (paths::DoesPathExist(initialPath) == paths::IS_EXISTING_FILE)
		{
			String temp;
			paths::SplitFilename(initialPath, 0, &temp, 0);
			lstrcpy(sSelectedFile, temp.c_str());
		}
	}

	String filters = sfilter, title = stitle;
	if (sfilter.empty())
		filters = _("All Files (*.*)|*.*||");
	if (stitle.empty())
		title = _("Open");

	// Convert extension mask from MFC style separators ('|')
	//  to Win32 style separators ('\0')
	LPTSTR filtersStr = &*filters.begin();
	ConvertFilter(filtersStr);

	OPENFILENAME_NT4 ofn = { sizeof OPENFILENAME_NT4 };
	ofn.hwndOwner = parent;
	ofn.lpstrFilter = filtersStr;
	ofn.lpstrCustomFilter = NULL;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = sSelectedFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrInitialDir = initialPath;
	ofn.lpstrTitle = title.c_str();
	ofn.lpstrFileTitle = NULL;
	if (defaultExtension)
		ofn.lpstrDefExt = defaultExtension;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	BOOL bRetVal = FALSE;
	if (is_open)
		bRetVal = GetOpenFileName((OPENFILENAME *)&ofn);
	else
		bRetVal = GetSaveFileName((OPENFILENAME *)&ofn);
	// common file dialog populated sSelectedFile variable's buffer

	if (bRetVal)
		path = sSelectedFile;
	
	return bRetVal;
}

/** 
 * @brief Helper function for selecting directory
 * @param [out] path Selected path is returned in this string
 * @param [in] root_path Initial path shown when dialog is opened
 * @param [in] titleid Resource string ID for dialog title.
 * @param [in] hwndOwner Handle to owner window or NULL
 * @return TRUE if valid folder selected (not cancelled)
 */
BOOL SelectFolder(String& path, LPCTSTR root_path /*=NULL*/, 
			const String& stitle /*=_T("")*/, 
			HWND hwndOwner /*=NULL*/) 
{
	BROWSEINFO bi;
	LPITEMIDLIST pidl;
	TCHAR szPath[MAX_PATH] = {0};
	BOOL bRet = FALSE;
	String title = stitle;
	if (root_path == NULL)
		LastSelectedFolder.clear();
	else
		LastSelectedFolder = root_path;

	bi.hwndOwner = hwndOwner;
	bi.pidlRoot = NULL;  // Start from desktop folder
	bi.pszDisplayName = szPath;
	bi.lpszTitle = title.c_str();
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_VALIDATE;
	bi.lpfn = BrowseCallbackProc;
	bi.lParam = (LPARAM)root_path;

	pidl = SHBrowseForFolder(&bi);
	if (pidl)
	{
		if (SHGetPathFromIDList(pidl, szPath))
		{
			path = szPath;
			bRet = TRUE;
		}
		CoTaskMemFree(pidl);
	}
	return bRet;
}

/**
 * @brief Callback function for setting selected folder for folder dialog.
 */
static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam,
		LPARAM lpData)
{
	// Look for BFFM_INITIALIZED
	if (uMsg == BFFM_INITIALIZED)
	{
		if (lpData)
			SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
		else
			SendMessage(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)LastSelectedFolder.c_str());
	}
	else if (uMsg == BFFM_VALIDATEFAILED)
	{
		String strMessage = (TCHAR *)lParam;
		strMessage += _T("フォルダは存在しません。作成しますか?");
		int answer = MessageBox(hwnd, strMessage.c_str(), _T("フォルダの作成"), MB_YESNO);
		if (answer == IDYES)
		{
			if (!paths::CreateIfNeeded((TCHAR*)lParam))
			{
				MessageBox(hwnd, _T("フォルダの作成に失敗しました"), _T("フォルダの作成"), MB_OK | MB_ICONWARNING);
			}
		}
		return 1;
	}
	return 0;
}

/** 
 * @brief Shows file/folder selection dialog.
 *
 * We need this custom function so we can select files and folders with the
 * same dialog.
 * - If existing filename is selected return it
 * - If filename in (CFileDialog) editbox and current folder doesn't form
 * a valid path to file, return current folder.
 * @param [in] parent Handle to parent window. Can be a NULL, but then
 *     CMainFrame is used which can cause modality problems.
 * @param [out] path Selected folder/filename
 * @param [in] initialPath Initial file or folder shown/selected.
 * @return TRUE if user choosed a file/folder, FALSE if user canceled dialog.
 */
BOOL SelectFileOrFolder(HWND parent, String& path, LPCTSTR initialPath /*=NULL*/)
{
	String title = _("Open");

	// This will tell common file dialog what to show
	// and also this will hold its return value
	TCHAR sSelectedFile[MAX_PATH];

	// check if specified path is a file
	if (initialPath && initialPath[0])
	{
		// If initial path info includes a file
		// we put the bare filename into sSelectedFile
		// so the common file dialog will start up with that file selected
		if (paths::DoesPathExist(initialPath) == paths::IS_EXISTING_FILE)
		{
			String temp;
			paths::SplitFilename(initialPath, 0, &temp, 0);
			lstrcpy(sSelectedFile, temp.c_str());
		}
	}

	String filters = _("All Files (*.*)|*.*||");

	// Convert extension mask from MFC style separators ('|')
	//  to Win32 style separators ('\0')
	LPTSTR filtersStr = &*filters.begin();
	ConvertFilter(filtersStr);

	String dirSelTag = _("Folder Selection");

	// Set initial filename to folder selection tag
	dirSelTag += _T("."); // Treat it as filename
	lstrcpy(sSelectedFile, dirSelTag.c_str()); // What is assignment above good for?

	OPENFILENAME_NT4 ofn = { sizeof OPENFILENAME_NT4 };
	ofn.hwndOwner = parent;
	ofn.lpstrFilter = filtersStr;
	ofn.lpstrCustomFilter = NULL;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = sSelectedFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrInitialDir = initialPath;
	ofn.lpstrTitle = title.c_str();
	ofn.lpstrFileTitle = NULL;
	ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOTESTFILECREATE | OFN_NOCHANGEDIR;

	BOOL bRetVal = GetOpenFileName((OPENFILENAME *)&ofn);

	if (bRetVal)
	{
		path = sSelectedFile;
		if (paths::DoesPathExist(path) == paths::DOES_NOT_EXIST)
		{
			// We have a valid folder name, but propably garbage as a filename.
			// Return folder name
			String folder = paths::GetPathOnly(sSelectedFile);
			path = paths::AddTrailingSlash(folder);
		}
	}
	return bRetVal;
}


/** 
 * @brief Helper function for converting filter format.
 *
 * MFC functions separate filter strings with | char which is also
 * good choice to safe into resource. But WinAPI32 functions we use
 * needs '\0' as separator. This function replaces '|'s with '\0's.
 *
 * @param [in,out] filterStr
 * - in Mask string to convert
 * - out Converted string
 */
static void ConvertFilter(LPTSTR filterStr)
{
	while (TCHAR *ch = _tcschr(filterStr, '|'))
	{
		filterStr = ch + 1;
		*ch = '\0';
	}
}
