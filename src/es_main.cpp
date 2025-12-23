using namespace ::std;

#include "es_server.hpp"

#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // Default server configuration
    int port = 58000;
    bool verbose = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (arg == "-v") {
            verbose = true;
        } else {
            cerr << "Usage: " << argv[0] << " [-p ESport] [-v]\n";
            return 1;
        }
    }

    // Start event server
    EventServer server(port, verbose);
    server.run();
    return 0;
}