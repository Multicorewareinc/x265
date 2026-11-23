/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * Authors: Dash Santosh <dash.sathyanarayanan@multicorewareinc.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#ifdef ENABLE_MLCTUPRED

#include "common.h"
#include "mlutil.h"

#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#if defined(_WIN32)
#define ML_PATH_SEP '\\'
#else
#define ML_PATH_SEP '/'
#endif

namespace X265_NS {
// private x265 namespace

#if defined(_WIN32)
std::wstring utf8ToWide(const char* s)
{
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (len <= 1)
        return std::wstring();
    std::wstring w(len, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, &w[0], len))
        return std::wstring();
    w.resize(len - 1);
    return w;
}
#endif

/* Its address identifies the binary containing this code */
static const char s_moduleAnchor = 0;

static bool isPathSep(char c)
{
#if defined(_WIN32)
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

bool joinPath(char* out, size_t size, const char* dir, const char* name)
{
    const size_t len = strlen(dir);
    const char sep[2] = { ML_PATH_SEP, 0 };
    int n = snprintf(out, size, "%s%s%s", dir, (len && isPathSep(dir[len - 1])) ? "" : sep, name);
    if (n < 0 || (size_t)n >= size)
        return false;
#if defined(_WIN32)
    for (char* p = out + len; *p; p++)
        if (*p == '/')
            *p = '\\';
#endif
    return true;
}

bool getEnvPath(const char* name, char* out, size_t size)
{
#if defined(_WIN32)
    const std::wstring wname = utf8ToWide(name);
    const wchar_t* value = wname.empty() ? NULL : _wgetenv(wname.c_str());
    return value && value[0] && WideCharToMultiByte(CP_UTF8, 0, value, -1, out, (int)size, NULL, NULL) > 0;
#else
    const char* value = getenv(name);
    return value && value[0] && snprintf(out, size, "%s", value) < (int)size;
#endif
}

/* Removes the last path component */
static bool stripFileName(char* path)
{
    char* sep = NULL;
    for (char* p = path; *p; p++)
        if (isPathSep(*p))
            sep = p;
    if (!sep)
        return false;
    *sep = 0;
    return true;
}

bool getModuleDir(char* dir, size_t size)
{
#if defined(_WIN32)
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&s_moduleAnchor), &module))
        return false;

    /* Long paths may exceed MAX_PATH */
    std::wstring wpath(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD len = GetModuleFileNameW(module, &wpath[0], (DWORD)wpath.size());
        if (!len)
            return false;
        if (len < wpath.size())
        {
            wpath.resize(len);
            break;
        }
        if (wpath.size() >= 32768)
            return false;
        wpath.resize(wpath.size() * 2);
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, dir, (int)size, NULL, NULL))
        return false;
#else
    Dl_info info;
    if (!dladdr(&s_moduleAnchor, &info) || !info.dli_fname || !info.dli_fname[0])
        return false;

    char path[ML_MAX_PATH];
    if (strchr(info.dli_fname, '/'))
        snprintf(path, sizeof(path), "%s", info.dli_fname);
    else
    {
        /* dladdr() reported only argv[0] */
#if defined(__APPLE__)
        uint32_t len = sizeof(path);
        if (_NSGetExecutablePath(path, &len))
            return false;
#elif defined(__linux__) || defined(__CYGWIN__)
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (n <= 0)
            return false;
        path[n] = 0;
#else
        return false;
#endif
    }

    /* Resolve relative paths and symlinks */
    char* resolved = realpath(path, NULL);
    if (resolved)
    {
        snprintf(dir, size, "%s", resolved);
        free(resolved);
    }
    else
        snprintf(dir, size, "%s", path);
#endif
    return stripFileName(dir);
}

}

#endif // ENABLE_MLCTUPRED
