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

#ifndef X265_MLUTIL_H
#define X265_MLUTIL_H

#ifdef ENABLE_MLCTUPRED

#include "common.h"
#include <string>

/* Paths are UTF-8; converted to UTF-16 for Windows APIs */
#define ML_MAX_PATH 4096

namespace X265_NS {
// private x265 namespace

/* Joins with the native separator; name may use '/' */
bool joinPath(char* out, size_t size, const char* dir, const char* name);

/* Environment variable as UTF-8 */
bool getEnvPath(const char* name, char* out, size_t size);

/* Directory of the x265 executable or shared library */
bool getModuleDir(char* dir, size_t size);

#if defined(_WIN32)
/* UTF-8 to UTF-16 for wide-char Windows APIs; empty on failure */
std::wstring utf8ToWide(const char* s);
#endif

}

#endif // ENABLE_MLCTUPRED
#endif // X265_MLUTIL_H
