using namespace ::std;

#include "user_client.hpp"

#include <iostream>
#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    // Default client configuration
    string serverIp = "127.0.0.1";
    int port = 58000;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-n" && i + 1 < argc) {
            serverIp = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else {
            cerr << "Usage: " << argv[0]
                      << " [-n ESIP] [-p ESport]\n";
            return 1;
        }
    }

    // Start user client
    UserClient client(serverIp, port);
    client.run();
    return 0;
}