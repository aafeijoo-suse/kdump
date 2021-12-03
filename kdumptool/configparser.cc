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
#include <string>
#include <fstream>
#include <sstream>
#include <cerrno>

#include "configparser.h"
#include "debug.h"
#include "stringutil.h"
#include "process.h"
#include "quotedstring.h"
#include "stringvector.h"

using std::string;
using std::ifstream;
using std::stringstream;

//{{{ ConfigParser -------------------------------------------------------------

// -----------------------------------------------------------------------------
void ConfigParser::addVariable(const string &name, const string &defvalue)
{
    Debug::debug()->trace("ConfigParser: Adding %s to variable list"
	" (default: '%s')", name.c_str(), defvalue.c_str());

    // add the default value to the map
    m_variables[name] = defvalue;
}

// -----------------------------------------------------------------------------
string ConfigParser::getValue(const std::string &name) const
{
    StringStringMap::const_iterator loc = m_variables.find(name);

    if (loc == m_variables.end())
        throw KError("Variable " + name + " does not exist.");

    return loc->second;
}

//}}}

//{{{ ShellConfigParser --------------------------------------------------------

// -----------------------------------------------------------------------------
ShellConfigParser::ShellConfigParser(const string &filename)
    : ConfigParser(filename)
{}

// -----------------------------------------------------------------------------
void ShellConfigParser::parse()
{
    // check if the configuration file does exist
    ifstream fin(m_configFile.c_str());
    if (!fin)
        throw KSystemError("Cannot open config file " + m_configFile, errno);

    // build the shell snippet
    stringstream shell;
    shell << "#!/bin/sh\n";

    // set default values
    for (StringStringMap::const_iterator it = m_variables.begin();
            it != m_variables.end(); ++it) {
        const string name = it->first;
	ShellQuotedString value(it->second);

        shell << name << "=" << value.quoted() << "\n";
    }

    shell << fin.rdbuf();
    if (fin && !fin.tellg())
        shell.clear(shell.rdstate() & ~shell.failbit);
    fin.close();

    for (StringStringMap::const_iterator it = m_variables.begin();
            it != m_variables.end(); ++it) {
        const string name = it->first;

        shell << "echo '" << name << "='$" << name << "\n";
    }

    stringstream shelloutput;

    ProcessFilter p;
    p.setStdin(&shell);
    p.setStdout(&shelloutput);
    p.execute("/bin/sh", StringVector());

    string s;
    int no = 1;
    while (getline(shelloutput, s)) {
        Debug::debug()->trace("ShellConfigParser: Parsing line %s", s.c_str());

        string::size_type loc = s.find('=');
        if (loc == string::npos)
            throw KError("Parsing line number " +
                StringUtil::number2string(no) + " failed.");

        string name = s.substr(0, loc);
        string value = s.substr(loc+1);

        Debug::debug()->trace("ShellConfigParser: Setting %s to %s",
            name.c_str(), value.c_str());

        m_variables[name] = value;
    }
}

//}}}

// vim: set sw=4 ts=4 fdm=marker et: :collapseFolds=1:
