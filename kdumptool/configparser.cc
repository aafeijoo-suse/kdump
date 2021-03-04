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
                Stringutil::number2string(no) + " failed.");

        string name = s.substr(0, loc);
        string value = s.substr(loc+1);

        Debug::debug()->trace("ShellConfigParser: Setting %s to %s",
            name.c_str(), value.c_str());

        m_variables[name] = value;
    }
}

//}}}

//{{{ KernelConfigParser -------------------------------------------------------

// -----------------------------------------------------------------------------
static bool is_kernel_space(const char c)
{
    // follow the definition from lib/ctype.c in the Linux kernel tree
    return (c >= 9 && c <= 13) || c == 32 || c == (char)160;
}

// -----------------------------------------------------------------------------
KernelConfigParser::KernelConfigParser(const string &filename)
    : ConfigParser(filename)
{}

// -----------------------------------------------------------------------------
void KernelConfigParser::parse()
{
    // check if the configuration file does exist
    ifstream fin(m_configFile.c_str());
    if (!fin)
        throw KSystemError("Cannot open config file " + m_configFile, errno);

    string name, value, *outp = &name;
    bool inquote = false;
    char c;
    while (fin.get(c)) {
        if (c == '\"') {
            inquote = !inquote;
            continue;
        }
        if (c == '=' && outp == &name) {
            outp = &value;
            continue;
        }
        if ((is_kernel_space(c) && !inquote) || fin.peek() == EOF) {
            if (name.empty())
                continue;

            Debug::debug()->trace("KernelConfigParser: Setting %s to %s",
                name.c_str(), value.c_str());

            m_variables[name] = value;

            name.clear();
            value.clear();
            outp = &name;
        } else if (c == '\\') {
            if (fin.peek() == '\\' && fin.get(c)) {
                outp->push_back(c);
                continue;
            }

            char seq[3];
            int i;
            for (i = 0; i < 3; ++i) {
                c = fin.peek();
                if (c < '0' || c > '7')
                    break;
                if (!fin.get(seq[i]))
                    break;
            }
            if (i < 3) {
		outp->push_back('\\');
                outp->append(seq, i);
            } else {
                c = ((seq[0] - '0') << 6) +
                    ((seq[1] - '0') << 3) +
                    (seq[2] - '0');
                outp->push_back(c);
            }
        } else {
            outp->push_back(c);
        }
    }
}

//}}}

// vim: set sw=4 ts=4 fdm=marker et: :collapseFolds=1:
