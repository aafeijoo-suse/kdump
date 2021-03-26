/*
 * (c) 2008, Bernhard Walle <bwalle@suse.de>, SUSE LINUX Products GmbH
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <libgen.h>
#include <dirent.h>
#include <sstream>
#include <algorithm>
#include <typeinfo>

#include <sys/vfs.h>
#include <sys/param.h>

#include "dataprovider.h"
#include "global.h"
#include "progress.h"
#include "debug.h"
#include "fileutil.h"
#include "process.h"
#include "stringutil.h"

using std::ostringstream;
using std::string;
using std::strcpy;
using std::free;


//{{{ FileDescriptor -----------------------------------------------------------

// -----------------------------------------------------------------------------
FileDescriptor::FileDescriptor(const char *path, int flags)
    : m_fd(-1)
{
    m_fd = open(path, flags);
    if (m_fd < 0)
        throw KSystemError(string("Cannot open ") + path, errno);
}

// -----------------------------------------------------------------------------
FileDescriptor::~FileDescriptor()
{
    if (m_fd >= 0)
        close(m_fd);
}

//}}}
//{{{ FileUtil -----------------------------------------------------------------

// -----------------------------------------------------------------------------
void FileUtil::nfsmount(const string &host,
                        const string &dir,
                        const string &mountpoint,
                        const StringVector &options)
{
    Debug::debug()->trace("FileUtil::nfsmount(%s, %s, %s, %s)",
        host.c_str(), dir.c_str(), mountpoint.c_str(),
        options.join(' ').c_str());

    mount(host + ":" + dir, mountpoint, "nfs", options);

}

// -----------------------------------------------------------------------------
void FileUtil::mount(const std::string &device, const std::string &mountpoint,
                     const std::string &fs, const StringVector &options)
{
    StringVector args;

    Debug::debug()->trace("FileUtil::mount(%s %s, %s, %s)",
        device.c_str(), mountpoint.c_str(), fs.c_str(),
        options.join(' ').c_str());

    for (StringVector::const_iterator it = options.begin();
            it != options.end(); ++it) {
        args.push_back("-o");
        args.push_back(*it);
    }

    args.push_back("-t");
    args.push_back(fs);

    args.push_back(device);
    args.push_back(mountpoint);

    ProcessFilter p;
    ostringstream stderrStream;
    p.setStderr(&stderrStream);

    int ret = p.execute("mount", args);
    Debug::debug()->dbg("Mount:%d", ret);
    if (ret != 0) {
        KString error = stderrStream.str();
        throw KError("mount failed: " + error.trim() + ".");
    }
}

// -----------------------------------------------------------------------------
void FileUtil::umount(const std::string &mountpoint)
{
    StringVector args;
    args.push_back(mountpoint);

    Debug::debug()->trace("FileUtil::umount(%s)", mountpoint.c_str());

    ProcessFilter p;
    ostringstream stderrStream;
    p.setStderr(&stderrStream);

    int ret = p.execute("umount", args);
    if (ret != 0) {
        KString error = stderrStream.str();
        throw KError("umount failed: " + error.trim());
    }
}

//}}}

//{{{ FilePath -----------------------------------------------------------------

const string FilePath::m_slash("/");

// -----------------------------------------------------------------------------
FilePath FilePath::getcwd(void)
{
    char *cwd = ::getcwd(NULL, 0);

    if (!cwd)
        throw KSystemError("getcwd failed", errno);

    FilePath ret(cwd);
    free(cwd);
    Debug::debug()->dbg("Current directory: " + ret);

    return ret;
}

// -----------------------------------------------------------------------------
string FilePath::baseName() const
{
    // modification of the arguments is allowed
    char *path = new char[length() + 1];
    strcpy(path, c_str());
    string ret(::basename(path));
    delete[] path;
    return ret;
}

// -----------------------------------------------------------------------------
string FilePath::dirName() const
{
    // modification of the arguments is allowed
    char *path = new char[length() + 1];
    strcpy(path, c_str());
    string ret(::dirname(path));
    delete[] path;
    return ret;
}

// -----------------------------------------------------------------------------
FilePath& FilePath::appendPath(const string &p)
{
    rtrim(PATH_SEPARATOR);
    append(PATH_SEPARATOR);
    size_type idx = p.find_first_not_of(PATH_SEPARATOR);
    if (idx != npos)
        append(p.substr(idx));
    return *this;
}

// -----------------------------------------------------------------------------
bool FilePath::exists() const
{
    return access(c_str(), F_OK) == 0;
}

// -----------------------------------------------------------------------------
bool FilePath::isSymlink() const
{
    struct stat mystat;

    int ret = lstat(c_str(), &mystat);
    if (ret < 0) {
        throw KSystemError("Stat failed on " + *this, errno);
    }

    return S_ISLNK(mystat.st_mode);
}

// -----------------------------------------------------------------------------
string FilePath::readLink() const
{
    char buffer[BUFSIZ];

    int ret = ::readlink(c_str(), buffer, BUFSIZ-1);
    if (ret < 0) {
        throw KSystemError("readlink() failed", errno);
    }

    buffer[ret] = '\0';
    return string(buffer);
}

// -----------------------------------------------------------------------------
FilePath FilePath::getCanonicalPath(const string &root) const
{
    Debug::debug()->trace("getCanonicalPathRoot(%s, %s)",
                          c_str(), root.c_str());

    if (empty())
        return *this;

    const string *rootp = root.empty() ? &m_slash : &root;

    // Use the current directory for relative paths
    FilePath ret;
    const_iterator p = begin();
    if (*p != '/') {
        ret = getcwd();

        if (ret.size() < rootp->size() ||
            ret.substr(0, rootp->size()) != *rootp)
            throw KSystemError("Cannot get current directory", ENOENT);
    } else
        ret = *rootp;

    string extra;
    int num_links = 0;
    const string *rpath = this;
    while (p != rpath->end()) {
        // Skip sequence of multiple path-separators.
        while (p != rpath->end() && *p == '/')
            ++p;

        // Find end of path component.
        const_iterator dirp = p;
        while (p != rpath->end() && *p != '/')
            ++p;
        string dir(dirp, p);

        // Handle the last component
        if (dir.empty())
            ;                   // extra slash(es) at end - ignore
        else if (dir == ".")
            ;                   // nothing
        else if (dir == "..") {
            // Back up to previous component
            if (ret.size() > rootp->size())
                ret.resize(ret.rfind('/'));
        } else {
            if (*ret.rbegin() != '/')
                ret += '/';
            ret += dir;

            struct stat st;
            if (lstat(ret.c_str(), &st) < 0) {
                if (errno == ENOENT)
                    ;           // non-existent elements will be created
                else
                    throw KSystemError("Stat failed on " + ret, errno);
            } else if (S_ISLNK(st.st_mode)) {
                if (rpath == &extra) {
                    extra.replace(0, p - extra.begin(), ret.readLink());
                } else {
                    extra = ret.readLink();
                    extra.append(p, rpath->end());
                    rpath = &extra;
                }

                if (++num_links > MAXSYMLINKS)
                    throw KSystemError("getCanonicalPath() failed", ELOOP);

                p = rpath->begin();
                ret.resize(*p == '/' ? rootp->size() : ret.rfind('/'));
            } else if (!S_ISDIR(st.st_mode) && p != rpath->end()) {
                throw KSystemError("getCanonicalPath() failed", ENOTDIR);
            }
        }
    }

    return ret;
}

// -----------------------------------------------------------------------------
StringVector FilePath::listDir(const ListDirFilter &filter) const
{
    StringVector v;

    Debug::debug()->trace("FileUtil::listdir(%s,%s)",
			  c_str(), typeid(filter).name());

    DIR *dirp = opendir(c_str());
    if (!dirp)
	throw KSystemError("Cannot open directory " + *this + ".", errno);

    try {
	struct dirent *d;

	errno = 0;
	while ( (d = readdir(dirp)) ) {
	    if (filter.test(dirfd(dirp), d))
		v.push_back(d->d_name);
	    errno = 0;
	}
	if (errno)
	    throw KSystemError("Cannot read directory " + *this + ".", errno);
    } catch(...) {
	closedir(dirp);
	throw;
    }
    closedir(dirp);

    sort(v.begin(), v.end());
    return v;
}

// -----------------------------------------------------------------------------
unsigned long long FilePath::freeDiskSize() const
{
    Debug::debug()->trace("FileUtil::freeDiskSize(%s)", c_str());

    struct statfs mystatfs;
    int ret = statfs(c_str(), &mystatfs);
    if (ret != 0)
        throw KSystemError("statfs() on " + *this + " failed.", errno);

    return (unsigned long long)mystatfs.f_bfree * mystatfs.f_bsize;
}

// -----------------------------------------------------------------------------
unsigned long long FilePath::fileSize() const
{
    Debug::debug()->trace("FileUtil::fileSize(%s)", c_str());

    struct stat mystat;
    int ret = ::stat(c_str(), &mystat);
    if (ret != 0) {
        throw KSystemError("stat() on " + *this + " failed.", errno);
    }

    return (unsigned long long)mystat.st_size;
}

// -----------------------------------------------------------------------------
void FilePath::mkdir(bool recursive)
{
    Debug::debug()->trace("mkdir(%s, %d)", c_str(), int(recursive));

    if (!recursive) {
        Debug::debug()->dbg("::mkdir(%s)", c_str());
        int ret = ::mkdir(c_str(), 0755);
        if (ret != 0 && errno != EEXIST)
            throw KSystemError("mkdir of " + *this + " failed.", errno);
    } else {
        FilePath directory = *this;

        // remove trailing '/' if there are any
        while (directory[directory.size()-1] == '/')
            directory = directory.substr(0, directory.size()-1);

        size_type current_slash = 0;

        while (true) {
            current_slash = directory.find('/', current_slash+1);
            if (current_slash == npos) {
                directory.mkdir(false);
                break;
            }

            FilePath fp = directory.substr(0, current_slash);
            fp.mkdir(false);
        }
    }
}

// -----------------------------------------------------------------------------
void FilePath::rmdir(bool recursive)
{
    Debug::debug()->trace("FileUtil::rmdir(%s, %d)", c_str(), recursive);

    if (recursive) {
        DIR *dirptr = opendir(c_str());
        if (!dirptr)
            throw KSystemError("Cannot opendir(" + *this + ").", errno);
        struct dirent *ptr;
        try {
            while ((ptr = readdir(dirptr)) != NULL) {
                FilePath fn = *this;
                if (strcmp(ptr->d_name, ".") == 0 ||
                        strcmp(ptr->d_name, "..") == 0)
                    continue;
                if (ptr->d_type == DT_DIR)
                    fn.appendPath(ptr->d_name).rmdir(true);
                else {
                    Debug::debug()->trace("Calling remove(%s)", ptr->d_name);
                    int ret = ::remove(fn.appendPath(ptr->d_name).c_str());
                    if (ret != 0)
                        throw KSystemError("Cannot remove " +
                            string(ptr->d_name) + ".", errno);
                }
            }
        } catch (...) {
            closedir(dirptr);
            throw;
        }
        closedir(dirptr);
    }
    int ret = ::rmdir(c_str());
    if (ret != 0)
        throw KSystemError("Cannot rmdir(" + *this + ").", errno);
}

//}}}

//{{{ FilterDots ---------------------------------------------------------------

// -----------------------------------------------------------------------------
bool FilterDots::test(int dirfd, const struct dirent *d) const
{
    if (strcmp(d->d_name, ".") == 0)
        return false;
    if (strcmp(d->d_name, "..") == 0)
        return false;
    else
        return true;
}
//}}}

//{{{ FilterDotsAndNondirs -----------------------------------------------------

// -----------------------------------------------------------------------------
bool FilterDotsAndNondirs::test(int dirfd, const struct dirent *d) const
{
    if (!FilterDots::test(dirfd, d))
        return false;
    if (d->d_type == DT_DIR)
        return true;
    if (d->d_type != DT_UNKNOWN)
        return false;

    struct stat mystat;
    if (fstatat(dirfd, d->d_name, &mystat, AT_SYMLINK_NOFOLLOW) != 0)
        return false;
    return S_ISDIR(mystat.st_mode);
}
//}}}

//{{{ FilterKdumpDirs ----------------------------------------------------------

// -----------------------------------------------------------------------------
bool FilterKdumpDirs::test(int dirfd, const struct dirent *d) const
{
    if (!FilterDotsAndNondirs::test(dirfd, d))
	return false;

    struct stat mystat;
    FilePath vmcore(d->d_name);
    vmcore.appendPath("vmcore");
    return fstatat(dirfd, vmcore.c_str(), &mystat, 0) == 0;
}
//}}}

// vim: set sw=4 ts=4 fdm=marker et: :collapseFolds=1:
