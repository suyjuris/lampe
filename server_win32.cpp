
#include <cstring>
#include <initializer_list>
#include <io.h>

#include "server.hpp"

namespace jup {

/**
 * Finds the first file matching pattern, while containing none of the string in
 * antipattern, then append its name (including the zero) to the buffer. Returns
 * whether a file was found.
 */
bool find_file_not_containing(
        c_str pattern,
        std::initializer_list<c_str> antipattern,
        Buffer* into
) {
    assert(into);

    _finddata_t file;

    auto handle = _findfirst(pattern, &file);
    if (handle + 1 == 0) return 0;
    auto code = handle;
    while (true) {
        if (code + 1 == 0) return 0;

        bool flag = false;
        for (auto i: antipattern) {
            if (std::strstr(file.name, i)) {
                flag = true;
                break;
            }
        }
        if (!flag) break;
        
        code = _findnext(handle, &file);
    }

    into->append(file.name, std::strlen(file.name) + 1);
    _findclose(handle);
    return 1;
}

Server::Server(c_str directory, c_str config_par) {
    Buffer buffer {256};

    int package_pattern = buffer.size();
    buffer.append(directory);
    buffer.append("\\target\\agentcontest-*.jar");
    buffer.append("", 1);
               
    int package = buffer.size();
    buffer.append("..\\target\\");
    if (!find_file_not_containing(buffer.data() + package_pattern, {"sources", "javadoc"}, &buffer)) {
        jerr << "Could not find server jar\n";
        assert(false);
    }
    jerr << "Using package: " << buffer.data() + package << '\n';

    int config;
    if (config_par) {
        config = buffer.size();
        buffer.append(config_par);
    } else {
        int config_pattern = buffer.size();
        buffer.append(directory);
        buffer.append("\\scripts\\conf\\2015*");
        buffer.append("", 1);

        config = buffer.size();
        buffer.append("conf\\");
        if (!find_file_not_containing(buffer.data() + config_pattern, {"random"}, &buffer)) {
            jerr << "Could not find config file\n";
            assert(false);
        }
    }
    jerr << "Using configuration: " << buffer.data() + config << '\n';

    int cmdline = buffer.size();
    buffer.append("java -ea -Dcom.sun.management.jmxremote -Xss2000k -Xmx600M -DentityExpansionLimi"
                  "t=1000000 -DelementAttributeLimit=1000000 -Djava.rmi.server.hostname=TST -jar ");
    buffer.append(buffer.data() + package);
    buffer.append(" --conf ");
    buffer.append(buffer.data() + config);
    buffer.append("", 1);

    int dir = buffer.size();
    buffer.append(directory);
    buffer.append("\\scripts");
    buffer.append("", 1);
                  
    proc.write_to_stdout = true;
    proc.init(buffer.data() + cmdline, buffer.data() + dir);
    
	proc.waitFor("[ NORMAL ]  ##   InetSocketListener created");
}

void Server::start_sim() {
    proc.write_to_buffer = false;
    proc.write_to_stdout = true;
	proc.send("\n");
}

} /* end of namespace jup */


