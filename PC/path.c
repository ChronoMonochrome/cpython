/*
 * Copyright 2018 Nikolay Sivov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include "pathcch.h"
#include "strsafe.h"

static BOOL is_prefixed_unc(const WCHAR *string)
{
    static const WCHAR prefixed_unc[] = {'\\', '\\', '?', '\\', 'U', 'N', 'C', '\\'};
    return !strncmpiW(string, prefixed_unc, ARRAY_SIZE(prefixed_unc));
}

static BOOL is_prefixed_disk(const WCHAR *string)
{
    static const WCHAR prefix[] = {'\\', '\\', '?', '\\'};
    return !strncmpW(string, prefix, ARRAY_SIZE(prefix)) && isalphaW(string[4]) && string[5] == ':';
}

static BOOL is_prefixed_volume(const WCHAR *string)
{
    static const WCHAR prefixed_volume[] = {'\\', '\\', '?', '\\', 'V', 'o', 'l', 'u', 'm', 'e'};
    const WCHAR *guid;
    INT i = 0;

    if (strncmpiW(string, prefixed_volume, ARRAY_SIZE(prefixed_volume))) return FALSE;

    guid = string + ARRAY_SIZE(prefixed_volume);

    while (i <= 37)
    {
        switch (i)
        {
        case 0:
            if (guid[i] != '{') return FALSE;
            break;
        case 9:
        case 14:
        case 19:
        case 24:
            if (guid[i] != '-') return FALSE;
            break;
        case 37:
            if (guid[i] != '}') return FALSE;
            break;
        default:
            if (!isalnumW(guid[i])) return FALSE;
            break;
        }
        i++;
    }

    return TRUE;
}

/* Get the next character beyond end of the segment.
   Return TRUE if the last segment ends with a backslash */
static BOOL get_next_segment(const WCHAR *next, const WCHAR **next_segment)
{
    while (*next && *next != '\\') next++;
    if (*next == '\\')
    {
        *next_segment = next + 1;
        return TRUE;
    }
    else
    {
        *next_segment = next;
        return FALSE;
    }
}

/* Find the last character of the root in a path, if there is one, without any segments */
static const WCHAR *get_root_end(const WCHAR *path)
{
    /* Find path root */
    if (is_prefixed_volume(path))
        return path[48] == '\\' ? path + 48 : path + 47;
    else if (is_prefixed_unc(path))
        return path + 7;
    else if (is_prefixed_disk(path))
        return path[6] == '\\' ? path + 6 : path + 5;
    /* \\ */
    else if (path[0] == '\\' && path[1] == '\\')
        return path + 1;
    /* \ */
    else if (path[0] == '\\')
        return path;
    /* X:\ */
    else if (isalphaW(path[0]) && path[1] == ':')
        return path[2] == '\\' ? path + 2 : path + 1;
    else
        return NULL;
}

HRESULT WINAPI PathCchAddBackslash(WCHAR *path, SIZE_T size)
{
    return PathCchAddBackslashEx(path, size, NULL, NULL);
}

HRESULT WINAPI PathCchAddBackslashEx(WCHAR *path, SIZE_T size, WCHAR **endptr, SIZE_T *remaining)
{
    BOOL needs_termination;
    SIZE_T length;

    TRACE("%s, %lu, %p, %p\n", debugstr_w(path), size, endptr, remaining);

    length = strlenW(path);
    needs_termination = size && length && path[length - 1] != '\\';

    if (length >= (needs_termination ? size - 1 : size))
    {
        if (endptr) *endptr = NULL;
        if (remaining) *remaining = 0;
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    if (!needs_termination)
    {
        if (endptr) *endptr = path + length;
        if (remaining) *remaining = size - length;
        return S_FALSE;
    }

    path[length++] = '\\';
    path[length] = 0;

    if (endptr) *endptr = path + length;
    if (remaining) *remaining = size - length;

    return S_OK;
}

HRESULT WINAPI PathCchSkipRoot(const WCHAR *path, const WCHAR **root_end)
{
    static const WCHAR unc_prefix[] = {'\\', '\\', '?'};

    TRACE("%s %p\n", debugstr_w(path), root_end);

    if (!path || !path[0] || !root_end
        || (!memicmpW(unc_prefix, path, ARRAY_SIZE(unc_prefix)) && !is_prefixed_volume(path) && !is_prefixed_unc(path)
            && !is_prefixed_disk(path)))
        return E_INVALIDARG;

    *root_end = get_root_end(path);
    if (*root_end)
    {
        (*root_end)++;
        if (is_prefixed_unc(path))
        {
            get_next_segment(*root_end, root_end);
            get_next_segment(*root_end, root_end);
        }
        else if (path[0] == '\\' && path[1] == '\\' && path[2] != '?')
        {
            /* Skip share server */
            get_next_segment(*root_end, root_end);
            /* If mount point is empty, don't skip over mount point */
            if (**root_end != '\\') get_next_segment(*root_end, root_end);
        }
    }

    return *root_end ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI PathCchStripPrefix(WCHAR *path, SIZE_T size)
{
    TRACE("%s %lu\n", wine_dbgstr_w(path), size);

    if (!path || !size || size > PATHCCH_MAX_CCH) return E_INVALIDARG;

    if (is_prefixed_unc(path))
    {
        /* \\?\UNC\a -> \\a */
        if (size < strlenW(path + 8) + 3) return E_INVALIDARG;
        strcpyW(path + 2, path + 8);
        return S_OK;
    }
    else if (is_prefixed_disk(path))
    {
        /* \\?\C:\ -> C:\ */
        if (size < strlenW(path + 4) + 1) return E_INVALIDARG;
        strcpyW(path, path + 4);
        return S_OK;
    }
    else
        return S_FALSE;
}
