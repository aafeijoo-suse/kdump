/*
 * (c) 2014, Petr Tesarik <ptesarik@suse.de>, SUSE LINUX Products GmbH
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
#include <iostream>
#include <memory>

#include <unistd.h>

#include "global.h"
#include "kdumptool.h"
#include "process.h"
#include "debug.h"
#include "stringvector.h"

using std::cout;
using std::cerr;
using std::endl;
using std::make_shared;

// -----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    static const char hello_world[] = "Hello, world!\n";
    static const char another_line[] = "This line is not shown.\n";

    SubProcess p;
    int fd;
    int status;
    int errors = 0;

    Debug::debug()->setStderrLevel(Debug::DL_TRACE);
    try {
        try {
            // This must throw an out_of_range exception
            cout << "Child fd 0 is " << typeid(p.getChildFD(0)).name() << endl;
            ++errors;
        } catch(std::out_of_range &e) {
            cout << "not yet initialized." << endl;
        }

        cout << "Checking ParentToChildPipe" << endl;
        p.setChildFD(0, make_shared<ParentToChildPipe>());
        SubProcessFD const &found = p.getChildFD(0);
        if (typeid(found) != typeid(ParentToChildPipe)) {
            cerr << "Unexpected type " << typeid(found).name() << endl;
            ++errors;
        }

        cout << "Checking ChildToParentPipe" << endl;
        p.setChildFD(0, make_shared<ChildToParentPipe>());
        SubProcessFD const &found2 = p.getChildFD(0);
        if (typeid(found2) != typeid(ChildToParentPipe)) {
            cerr << "Unexpected type " << typeid(found2).name() << endl;
            ++errors;
        }

        cout << "Checking no pipe" << endl;
	p.setChildFD(0, nullptr);
        try {
            // This must throw an out_of_range exception
            SubProcessFD const &found3 = p.getChildFD(0);
            cout << "Child fd 0 is " << typeid(found3).name() << endl;
            ++errors;
        } catch(const std::out_of_range &e) {
            cout << "OK, pipe 0 de-initialized again" << endl;
        }

	StringVector v;
	p.spawn("true", v);
	Debug::debug()->info("Spawned process 'true' with PID %d",
			     p.getChildPID());
	status = p.wait();
	Debug::debug()->info("Child exited with status %d", status);

        auto pipe = make_shared<ParentToChildPipe>();
	p.setChildFD(0, pipe);
	p.spawn("cat", v);
	Debug::debug()->info("Spawned process 'cat' with PID %d",
			     p.getChildPID());
        fd = pipe->writeEnd();
	int res = write(fd, hello_world, sizeof(hello_world) - 1);
	if (res != sizeof(hello_world) - 1) {
	    cerr << "Partial write to 'cat': " << res << " bytes" << endl;
	    ++errors;
	}
	pipe->close();
	status = p.wait();
	Debug::debug()->info("Child exited with status %d", status);

	// Redirect the output from one command to another
	SubProcess p2;
        auto pipe2 = make_shared<ChildToParentPipe>();
        p.setChildFD(1, pipe2);
	p.spawn("cat", v);
	Debug::debug()->info("Spawned process 'cat' with PID %d",
			     p.getChildPID());
        auto redir = make_shared<SubProcessRedirect>(pipe2->readEnd());
	p2.setChildFD(0, redir);
	v.push_back("^Hello");
	p2.spawn("grep", v);
	Debug::debug()->info("Spawned process 'grep' with PID %d",
			     p2.getChildPID());
	pipe2->close();

        fd = pipe->writeEnd();
	res = write(fd, another_line, sizeof(another_line) - 1);
	if (res != sizeof(another_line) - 1) {
	    cerr << "Partial write to 'cat': " << res << " bytes" << endl;
	    ++errors;
	}
	res = write(fd, hello_world, sizeof(hello_world) - 1);
	if (res != sizeof(hello_world) - 1) {
	    cerr << "Partial write to 'cat': " << res << " bytes" << endl;
	    ++errors;
	}
	pipe->close();

	status = p.wait();
	Debug::debug()->info("'cat' exited with status %d", status);
	status = p2.wait();
	Debug::debug()->info("'grep' exited with status %d", status);

    } catch(const std::exception &ex) {
	cerr << "Fatal exception: " << ex.what() << endl;
	++errors;
    }

    return errors;
}
