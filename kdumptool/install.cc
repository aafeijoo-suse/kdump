/*
 * (c) 2021, Petr Tesarik <ptesarik@suse.de>, SUSE Linux Software Solutions GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <cctype>
#include <string>
#include <sstream>
#include <fstream>

#include "global.h"
#include "install.h"
#include "process.h"
#include "stringutil.h"
#include "cpio.h"
#include "debug.h"

using std::ifstream;
using std::ostringstream;
using std::string;
using std::make_shared;

//{{{ SharedDependencies -------------------------------------------------------

// -----------------------------------------------------------------------------
SharedDependencies::SharedDependencies(string const &path)
{
    Debug::debug()->trace("SharedDependencies(%s)", path.c_str());

    ProcessFilter p;

    StringVector args;
    args.push_back(path);

    ostringstream stdoutStream, stderrStream;
    p.setStdout(&stdoutStream);
    p.setStderr(&stderrStream);
    int ret = p.execute("ldd", args);
    if (ret == 0) {
        KString out = stdoutStream.str();
        size_t pos = 0;
        while (pos < out.length()) {
            size_t end = out.find_first_of("\r\n", pos);
            size_t slash = out.find('/', pos);
            if (slash < end) {
                auto start(out.begin() + slash), it(start);
                while (it != out.end() && !std::isspace(*it))
                    ++it;
                string path(start, it);
                m_list.emplace_back(start, it);
            }
            pos = out.find_first_not_of("\r\n", end);
        }
    } else {
        KString error = stderrStream.str();
        if (!error.empty())
            throw KError("Cannot get shared dependencies: " + error.trim());
    }
}

//}}}
//{{{ Initrd -------------------------------------------------------------------

const char Initrd::DATA_DIRECTORY[] = "/usr/lib/kdump";

// -----------------------------------------------------------------------------
bool Initrd::installFile(FilePath const &path, const char *destdir)
{
    FilePath dst(destdir);
    dst.appendPath(path.baseName());
    return addPath(make_shared<CPIOFile>(dst, path));
}

// -----------------------------------------------------------------------------
bool Initrd::installProgram(FilePath const &path, const char *destdir)
{
    if (!installFile(path, destdir))
        return false;

    FilePath const *binary = &path;
    FilePath interp;

    char c, marker[2];
    ifstream fin(path);
    if (fin && fin.read(marker, 2) &&
        marker[0] == '#' && marker[1] == '!') {
        while (fin.get(c) && (c == ' ' || c == '\t'));
        do {
            interp.push_back(c);
        } while (fin.get(c) && !(c == ' ' || c == '\t' || c == '\n'));
        if (fin.bad() || interp.empty())
            return true;
        if (!installFile(interp))
            return true;

        binary = &interp;
    }

    SharedDependencies deps(*binary);
    for (auto const &lib : deps)
        addPath(make_shared<CPIOFile>(lib));

    return true;
}

// -----------------------------------------------------------------------------
bool Initrd::installData(const char *name, const char *destdir)
{
    FilePath src(DATA_DIRECTORY);
    FilePath dst(destdir);

    src.appendPath(name);
    dst.appendPath(name);

    return addPath(make_shared<CPIOFile>(dst, src));
}

//}}}

// vim: set sw=4 ts=4 fdm=marker et: :collapseFolds=1:
