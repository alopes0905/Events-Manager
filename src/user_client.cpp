using namespace ::std;

#include "user_client.hpp"
#include "protocol.hpp"

#include <iostream>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <vector>
#include <algorithm>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

// Read a line from TCP socket
static string tcp_read_line(int sockfd) {
    string line;
    char c;
    while (true) {
        ssize_t n = ::read(sockfd, &c, 1);
        if (n <= 0) break;
        line.push_back(c);
        if (c == '\n') break;
    }
    return line;
}

// Convert server state code to human-readable status
static string state_to_status(const string& state_token) {
    if (state_token == "1") return "Open";
    if (state_token == "3") return "Closed";
    if (state_token == "0") return "Past Event";
    if (state_token == "2") return "Sold out";
    return "Unknown";
}

UserClient::UserClient(const string& serverIp, int serverPort)
    : serverIp_(serverIp), serverPort_(serverPort) {}

void UserClient::print_help() const {
    cout << "Commands\n"
              << "  login <UID> <password>\n"
              << "  logout\n"
              << "  unregister\n"
              << "  mye / myevents\n"
              << "  myr / myreservations\n"
              << "  changePass <oldPassword> <newPassword>\n"
              << "  create <name> <event_fname> <dd-mm-yyyy> <hh:mm> <num_attendees>\n"
              << "  list\n"
              << "  close <EID>\n"
              << "  reserve <EID> <value>\n"
              << "  show <EID>\n"
              << "  help\n"
              << "  exit\n";
}

// Main client loop: read and process commands
void UserClient::run() {
    cout << "User client connecting to " << serverIp_
              << ":" << serverPort_ << endl;

    print_help();

    string line;
    while (true) {
        cout << "> ";
        if (!getline(cin, line)) break;

        if (line == "help") {
            print_help();
        } else if (line == "exit") {
            if (loggedIn_) {
                cout << "You must logout before exiting.\n";
                continue;
            }
            cout << "Exiting user client." << endl;
            break;
        } else {
            handle_command(line);
        }
    }
}

// Parse and dispatch commands to appropriate handlers
void UserClient::handle_command(const string& line) {
    istringstream iss(line);
    string cmd;
    iss >> cmd;

    if (cmd == "login") {
        string uid, pass;
        iss >> uid >> pass;
        if (uid.empty() || pass.empty()) {
            cout << "Usage: login <UID> <password>\n";
            return;
        }
        cmd_login(uid, pass);

    } else if (cmd == "logout") {
        cmd_logout();

    } else if (cmd == "unregister") {
        cmd_unregister();

    } else if (cmd == "mye" || cmd == "myevents") {
        cmd_myevents();

    } else if (cmd == "myr" || cmd == "myreservations") {
        cmd_myreservations();

    } else if (cmd == "changePass") {
        string oldPass, newPass;
        iss >> oldPass >> newPass;
        if (oldPass.empty() || newPass.empty()) {
            cout << "Usage: changePass <oldPassword> <newPassword>\n";
            return;
        }
        cmd_changePass(oldPass, newPass);

    } else if (cmd == "create") {
        string name, fname, date, time, attend_str;
        iss >> name >> fname >> date >> time >> attend_str;
        if (name.empty() || fname.empty() || date.empty() || time.empty() || attend_str.empty()) {
            cout << "Usage: create <name> <event_fname> <dd-mm-yyyy> <hh:mm> <num_attendees>\n";
            return;
        }
        cmd_create(name, fname, date, time, attend_str);

    } else if (cmd == "list") {
        cmd_list();

    } else if (cmd == "close") {
        string eid;
        iss >> eid;
        if (eid.empty()) {
            cout << "Usage: close <EID>\n";
            return;
        }
        cmd_close(eid);

    } else if (cmd == "reserve") {
        string eid, seats;
        iss >> eid >> seats;
        if (eid.empty() || seats.empty()) {
            cout << "Usage: reserve <EID> <value>\n";
            return;
        }
        cmd_reserve(eid, seats);

    } else if (cmd == "show") {
        string eid;
        iss >> eid;
        if (eid.empty()) {
            cout << "Usage: show <EID>\n";
            return;
        }
        cmd_show(eid);

    } else {
        cout << "Unknown command. Type 'help'.\n";
    }
}

// ---------- UDP helper ----------

// Send UDP request and wait for response
string UserClient::send_udp_request(const string& msg) {
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "[user] socket() failed\n";
        return "";
    }

    // Set receive timeout for robustness
    struct timeval tv;
    tv.tv_sec = 5;   // 5 seconds timeout
    tv.tv_usec = 0;
    if (::setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        cerr << "[user] setsockopt timeout failed\n";
    }

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    string portStr = to_string(serverPort_);
    int err = ::getaddrinfo(serverIp_.c_str(), portStr.c_str(), &hints, &res);
    if (err != 0) {
        cerr << "[user] getaddrinfo: " << gai_strerror(err) << "\n";
        ::close(sockfd);
        return "";
    }

    // Send request
    ssize_t n = ::sendto(sockfd, msg.c_str(), msg.size(), 0,
                         res->ai_addr, res->ai_addrlen);
    if (n < 0) {
        cerr << "[user] sendto failed\n";
        ::freeaddrinfo(res);
        ::close(sockfd);
        return "";
    }

    // Receive response
    char buffer[1024];
    sockaddr_storage src_addr{};
    socklen_t src_len = sizeof(src_addr);
    n = ::recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                   reinterpret_cast<sockaddr*>(&src_addr), &src_len);
    if (n <= 0) {
        cerr << "[user] recvfrom failed or empty datagram\n";
        ::freeaddrinfo(res);
        ::close(sockfd);
        return "";
    }
    buffer[n] = '\0';

    ::freeaddrinfo(res);
    ::close(sockfd);

    return string(buffer);
}

// ---------- TCP helper ----------

// Send TCP request and read one line response
string UserClient::send_tcp_request(const string& msg) {
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        cerr << "[user] TCP socket() failed\n";
        return "";
    }

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    string portStr = to_string(serverPort_);
    int err = ::getaddrinfo(serverIp_.c_str(), portStr.c_str(), &hints, &res);
    if (err != 0) {
        cerr << "[user] getaddrinfo (TCP): " << gai_strerror(err) << "\n";
        ::close(sockfd);
        return "";
    }

    // Connect to server
    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        cerr << "[user] connect (TCP) failed\n";
        ::freeaddrinfo(res);
        ::close(sockfd);
        return "";
    }

    ::freeaddrinfo(res);

    // Send request
    ssize_t n = ::write(sockfd, msg.c_str(), msg.size());
    if (n < 0) {
        cerr << "[user] write (TCP) failed\n";
        ::close(sockfd);
        return "";
    }

    // Read response
    string line = tcp_read_line(sockfd);

    ::close(sockfd);

    return line;
}

// ---------- Commands: login/logout/unregister/mye/myr ----------

// Handle login command
void UserClient::cmd_login(const string& uid, const string& pass) {
    if (loggedIn_) {
        cout << "Already logged in as " << currentUid_ << "\n";
        return;
    }

    // Send login request via UDP
    string msg = protocol::build_login(uid, pass);
    string reply = send_udp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server.\n";
        return;
    }

    // Parse response
    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error: server replied ERR.\n";
        return;
    }

    if (r.type != "RLI") {
        cout << "Protocol error: expected RLI, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        loggedIn_    = true;
        currentUid_  = uid;
        currentPass_ = pass;
        cout << "Login successful (existing user).\n";
    } else if (r.status == "REG") {
        loggedIn_    = true;
        currentUid_  = uid;
        currentPass_ = pass;
        cout << "New user created and logged in.\n";
    } else if (r.status == "NOK") {
        cout << "Login failed: wrong password.\n";
    } else if (r.status == "ERR") {
        cout << "Login error: invalid syntax or parameter values.\n"
                  << "UID must be 6 digits, password must be 8 alphanumeric characters.\n";
    } else {
        cout << "Login failed: unexpected status '" << r.status << "'.\n";
    }
}

void UserClient::cmd_logout() {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot logout: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    string msg = protocol::build_logout(currentUid_, currentPass_);
    string reply = send_udp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server.\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error: server replied ERR.\n";
        return;
    }

    if (r.type != "RLO") {
        cout << "Protocol error: expected RLO, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        loggedIn_ = false;
        cout << "Logout successful.\n";
    } else if (r.status == "UNR") {
        loggedIn_ = false;
        currentUid_.clear();
        currentPass_.clear();
        cout << "Logout failed: user not registered on server (UNR).\n";
    } else if (r.status == "WRP") {
        cout << "Logout failed: wrong password (WRP).\n";
    } else if (r.status == "NOK") {
        loggedIn_ = false;
        cout << "Logout failed: user was not logged in on server (NOK).\n";
    } else if (r.status == "ERR") {
        cout << "Logout error: invalid syntax or parameter values.\n"
                  << "UID must be 6 digits, password must be 8 alphanumeric characters.\n";
    } else {
        cout << "Logout failed: unexpected status '" << r.status << "'.\n";
    }
}

void UserClient::cmd_unregister() {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot unregister: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    string msg = protocol::build_unregister(currentUid_, currentPass_);
    string reply = send_udp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server.\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error: server replied ERR.\n";
        return;
    }

    if (r.type != "RUR") {
        cout << "Protocol error: expected RUR, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        loggedIn_ = false;
        currentUid_.clear();
        currentPass_.clear();
        cout << "Unregister successful: user removed and logged out.\n";
    } else if (r.status == "UNR") {
        loggedIn_ = false;
        currentUid_.clear();
        currentPass_.clear();
        cout << "Unregister failed: unknown user on server (UNR).\n";
    } else if (r.status == "WRP") {
        cout << "Unregister failed: wrong password (WRP).\n";
    } else if (r.status == "NOK") {
        loggedIn_ = false;
        cout << "Unregister failed: user not logged in on server (NOK).\n";
    } else if (r.status == "ERR") {
        cout << "Unregister error: invalid syntax or parameter values.\n"
                  << "UID must be 6 digits, password must be 8 alphanumeric characters.\n";
    } else {
        cout << "Unregister failed: unexpected status '" << r.status << "'.\n";
    }
}

void UserClient::cmd_myevents() {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot list events: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    string msg = protocol::build_myevents(currentUid_, currentPass_);
    string reply = send_udp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server.\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error: server replied ERR.\n";
        return;
    }

    if (r.type != "RME") {
        cout << "Protocol error: expected RME, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        istringstream iss(r.rest);
        struct MyEv {
            string eid;
            string status;
        };
        vector<MyEv> myevents;
        string eid, st;

        while (iss >> eid >> st) {
            MyEv e;
            e.eid = eid;
            e.status = state_to_status(st);
            myevents.push_back(e);
        }

        if (myevents.empty()) {
            cout << "No events found for this user.\n";
            return;
        }

        sort(myevents.begin(), myevents.end(),
                  [](const MyEv& a, const MyEv& b) { return a.eid < b.eid; });

        cout << "My events:\n";
        for (const auto& e : myevents) {
            cout << "  EID " << e.eid << " - " << e.status << "\n";
        }
        cout << "Total events: " << myevents.size() << "\n";

    } else if (r.status == "NOK") {
        cout << "No events found for this user.\n";
    } else if (r.status == "UNR") {
        loggedIn_ = false;
        currentUid_.clear();
        currentPass_.clear();
        cout << "myevents failed: user not registered on server (UNR).\n";
    } else if (r.status == "WRP") {
        cout << "myevents failed: wrong password (WRP).\n";
    } else if (r.status == "NLG") {
        loggedIn_ = false;
        cout << "myevents failed: user not logged in on server (NLG).\n";
    } else if (r.status == "ERR") {
        cout << "myevents error: invalid syntax or parameter values.\n"
                  << "UID must be 6 digits, password must be 8 alphanumeric characters.\n";
    } else {
        cout << "myevents failed: unexpected status '" << r.status << "'.\n";
    }
}

void UserClient::cmd_myreservations() {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "You must be logged in to see your reservations.\n";
        return;
    }

    string msg   = protocol::build_myreservations(currentUid_, currentPass_);
    string reply = send_udp_request(msg);

    if (reply.empty()) {
        cout << "No reply from server.\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error: server replied ERR.\n";
        return;
    }

    if (r.type != "RMR") {
        cout << "Protocol error: expected RMR, got '" << r.type << "'.\n";
        return;
    }

    if (r.status == "NLG") {
        cout << "You are not logged in (NLG).\n";
        loggedIn_ = false;
        return;
    }

    if (r.status == "NOK") {
        cout << "You have no reservations.\n";
        return;
    }

    if (r.status != "OK") {
        cout << "Unexpected status in RMR reply: '" << r.status << "'.\n";
        return;
    }

    istringstream iss(r.rest);

    cout << "My reservations: \n (Event EID | Date | Reserved Seats)\n";

    int num_reservations = 0;

    while (true) {
        string eid, date, time, seatsStr;
        if (!(iss >> eid >> date >> time >> seatsStr)) {
            // No more complete groups of 4 tokens
            break;
        }

        int seats = 0;
        try {
            seats = stoi(seatsStr);
        } catch (...) {
            break;
        }

        string time_short = time.substr(0, 5);
        cout << "  " << eid << " | " << date << " | " << time_short
              << " | " << seats << "\n";
        ++num_reservations;
    }

    cout << "Total reservations: " << num_reservations << "\n";
}


// ---------- TCP: changePass ----------

void UserClient::cmd_changePass(const string& oldPass, const string& newPass) {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot change password: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    string msg = protocol::build_change_pass(currentUid_, oldPass, newPass);
    string reply = send_tcp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server (TCP).\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error (TCP): server replied ERR.\n";
        return;
    }

    if (r.type != "RCP") {
        cout << "Protocol error: expected RCP, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        currentPass_ = newPass;
        cout << "Password changed successfully.\n";
    } else if (r.status == "NID") {
        loggedIn_ = false;
        currentUid_.clear();
        currentPass_.clear();
        cout << "Password change failed: unknown user (NID).\n";
    } else if (r.status == "NLG") {
        loggedIn_ = false;
        cout << "Password change failed: user not logged in (NLG).\n";
    } else if (r.status == "NOK") {
        cout << "Password change failed: incorrect old password (NOK).\n";
    } else if (r.status == "ERR") {
        cout << "Password change error: invalid syntax or parameter values (ERR).\n"
                  << "UID must be 6 digits, passwords must be 8 alphanumeric characters.\n";
    } else {
        cout << "Password change failed: unexpected status '" << r.status << "'.\n";
    }
}

// ---------- TCP: create event ----------

// Handle create event command - upload event with file
void UserClient::cmd_create(const string& name,
                            const string& fname,
                            const string& date,
                            const string& time,
                            const string& attendees_str) {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot create event: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    int attendees = -1;
    try {
        attendees = stoi(attendees_str);
    } catch (...) {
        attendees = -1;
    }
    if (attendees < 10 || attendees > 999) {
        cout << "num_attendees must be between 10 and 999.\n";
        return;
    }

    // Read event file
    const long MAX_FILE_SIZE = 10000000L; // 10 MB
    FILE* fp = fopen(fname.c_str(), "rb");
    if (!fp) {
        cout << "Could not open file '" << fname << "'.\n";
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        cout << "Error seeking file.\n";
        return;
    }
    long fsize = ftell(fp);
    if (fsize < 0) {
        fclose(fp);
        cout << "Error determining file size.\n";
        return;
    }
    if (fsize == 0 || fsize > MAX_FILE_SIZE) {
        fclose(fp);
        cout << "File size must be > 0 and <= 10 MB.\n";
        return;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        cout << "Error seeking file.\n";
        return;
    }

    vector<char> data(static_cast<size_t>(fsize));
    size_t rd = fread(data.data(), 1, data.size(), fp);
    fclose(fp);
    if (rd != data.size()) {
        cout << "Error reading entire file.\n";
        return;
    }

    // Build request header
    string header = protocol::build_create_header(
        currentUid_, currentPass_, name,
        date, time,
        attendees, fname, fsize);

    // Connect to server
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        cerr << "[user] TCP socket() failed\n";
        return;
    }

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    string portStr = to_string(serverPort_);
    int err = ::getaddrinfo(serverIp_.c_str(), portStr.c_str(), &hints, &res);
    if (err != 0) {
        cerr << "[user] getaddrinfo (TCP): " << gai_strerror(err) << "\n";
        ::close(sockfd);
        return;
    }

    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        cerr << "[user] connect (TCP) failed\n";
        ::freeaddrinfo(res);
        ::close(sockfd);
        return;
    }

    ::freeaddrinfo(res);

    // Send header
    ssize_t n = ::write(sockfd, header.c_str(), header.size());
    if (n < 0) {
        cerr << "[user] write (TCP header) failed\n";
        ::close(sockfd);
        return;
    }

    // Send file data
    size_t remaining = data.size();
    const char* ptr = data.data();
    while (remaining > 0) {
        ssize_t sent = ::write(sockfd, ptr, remaining);
        if (sent <= 0) {
            cerr << "[user] write (TCP file data) failed\n";
            ::close(sockfd);
            return;
        }
        remaining -= static_cast<size_t>(sent);
        ptr += sent;
    }

    string reply = tcp_read_line(sockfd);

    ::close(sockfd);

    if (reply.empty()) {
        cout << "No reply from server (TCP).\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error (TCP): server replied ERR.\n";
        return;
    }

    if (r.type != "RCE") {
        cout << "Protocol error: expected RCE, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        string eid = r.rest;
        if (!eid.empty() && eid[0] == ' ') {
            eid.erase(0, 1);
        }
        cout << "Event created successfully with EID " << eid << ".\n";
    } else if (r.status == "NLG") {
        loggedIn_ = false;
        cout << "Event creation failed: user not logged in (NLG).\n";
    } else if (r.status == "WRP") {
        cout << "Event creation failed: incorrect password (WRP).\n";
    } else if (r.status == "NOK") {
        cout << "Event creation failed: could not create event (NOK).\n";
    } else if (r.status == "ERR") {
        cout << "Event creation error: invalid syntax or parameter values (ERR).\n"
                  << "Check UID, password, name, date/time, attendance size, Fname and file size.\n";
    } else {
        cout << "Event creation failed: unexpected status '" << r.status << "'.\n";
    }
}


// ---------- TCP: list events ----------

void UserClient::cmd_list() {
    string msg = protocol::build_list();
    string reply = send_tcp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server (TCP).\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error (TCP): server replied ERR.\n";
        return;
    }

    if (r.type != "RLS") {
        cout << "Protocol error: expected RLS, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        struct EvInfo {
            string eid;
            string name;
            string status;
            string datetime;
        };
        vector<EvInfo> events;

        istringstream iss(r.rest);
        string eid, name, st, date, time;

        while (iss >> eid >> name >> st >> date >> time) {
            EvInfo e;
            e.eid      = eid;
            e.name     = name;
            e.status   = state_to_status(st);
            e.datetime = date + " " + time;
            events.push_back(e);
        }

        if (events.empty()) {
            cout << "Events exist but payload is empty (protocol issue).\n";
            return;
        }

        cout << "Available events:\n";
        for (const auto& e : events) {
            cout << "  EID " << e.eid
                      << " | " << e.name
                      << " | " << e.status
                      << " | " << e.datetime << "\n";
        }
        cout << "Total events: " << events.size() << "\n";

    } else if (r.status == "NOK") {
        cout << "No events have been created yet.\n";
    } else if (r.status == "ERR") {
        cout << "List error: invalid syntax or parameter values (ERR).\n";
    } else {
        cout << "List failed: unexpected status '" << r.status << "'.\n";
    }
}

// ---------- TCP: close event ----------

void UserClient::cmd_close(const string& eid) {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot close event: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    string msg = protocol::build_close(currentUid_, currentPass_, eid);
    string reply = send_tcp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server (TCP).\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error (TCP): server replied ERR.\n";
        return;
    }

    if (r.type != "RCL") {
        cout << "Protocol error: expected RCL, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "OK") {
        cout << "Event " << eid << " closed successfully.\n";
    } else if (r.status == "NOK") {
        cout << "Close failed: unknown user or incorrect password (NOK).\n";
    } else if (r.status == "NLG") {
        loggedIn_ = false;
        cout << "Close failed: user not logged in (NLG).\n";
    } else if (r.status == "NOE") {
        cout << "Close failed: event " << eid << " does not exist (NOE).\n";
    } else if (r.status == "EOW") {
        cout << "Close failed: event " << eid << " was not created by this user (EOW).\n";
    } else if (r.status == "SLD") {
        cout << "Close failed: event " << eid << " is already sold out (SLD).\n";
    } else if (r.status == "PST") {
        cout << "Close failed: event " << eid << " is already in the past (PST).\n";
    } else if (r.status == "CLO") {
        cout << "Close failed: event " << eid << " was already closed (CLO).\n";
    } else if (r.status == "ERR") {
        cout << "Close error: invalid syntax or parameter values (ERR).\n"
                  << "EID must be a 3-digit number.\n";
    } else {
        cout << "Close failed: unexpected status '" << r.status << "'.\n";
    }
}

// ---------- TCP: reserve seats ----------

void UserClient::cmd_reserve(const string& eid,
                             const string& seats_str) {
    if (currentUid_.empty() || currentPass_.empty()) {
        cout << "Cannot reserve: no known UID/password.\n"
                  << "Please login at least once first.\n";
        return;
    }

    int seats = -1;
    try {
        seats = stoi(seats_str);
    } catch (...) {
        seats = -1;
    }
    if (seats < 1 || seats > 999) {
        cout << "value must be between 1 and 999.\n";
        return;
    }

    if (eid.size() != 3 ||
        !isdigit(static_cast<unsigned char>(eid[0])) ||
        !isdigit(static_cast<unsigned char>(eid[1])) ||
        !isdigit(static_cast<unsigned char>(eid[2]))) {
        cout << "EID must be a 3-digit number (e.g., 001).\n";
        return;
    }

    string msg = protocol::build_reserve(currentUid_, currentPass_, eid, seats);
    string reply = send_tcp_request(msg);
    if (reply.empty()) {
        cout << "No reply from server (TCP).\n";
        return;
    }

    auto r = protocol::parse_response_line(reply);

    if (r.type == "ERR") {
        cout << "Protocol error (TCP): server replied ERR.\n";
        return;
    }

    if (r.type != "RRI") {
        cout << "Protocol error: expected RRI, got '" << r.type << "'\n";
        return;
    }

    if (r.status == "ACC") {
        cout << "Reservation accepted for event " << eid
                  << " (" << seats << " seat(s)).\n";
    } else if (r.status == "REJ") {
        string remaining_str = r.rest;
        if (!remaining_str.empty() && remaining_str[0] == ' ')
            remaining_str.erase(0, 1);
        cout << "Reservation rejected: only " << remaining_str
                  << " seat(s) remaining.\n";
    } else if (r.status == "SLD") {
        cout << "Reservation failed: event " << eid << " is sold out (SLD).\n";
    } else if (r.status == "CLS") {
        cout << "Reservation failed: event " << eid << " is closed (CLS).\n";
    } else if (r.status == "PST") {
        cout << "Reservation failed: event " << eid << " is already in the past (PST).\n";
    } else if (r.status == "NLG") {
        loggedIn_ = false;
        cout << "Reservation failed: user not logged in (NLG).\n";
    } else if (r.status == "WRP") {
        cout << "Reservation failed: wrong password (WRP).\n";
    } else if (r.status == "NOK") {
        cout << "Reservation failed: event not active or does not exist (NOK).\n";
    } else if (r.status == "ERR") {
        cout << "Reservation error: invalid syntax or parameter values (ERR).\n"
                  << "EID must be 3 digits, value must be between 1 and 999.\n";
    } else {
        cout << "Reservation failed: unexpected status '" << r.status << "'.\n";
    }
}

// ---------- TCP: show event (SED / RSE) ----------

void UserClient::cmd_show(const string& eid) {
    if (eid.empty()) {
        cout << "Usage: show EID\n";
        return;
    }

    // --- Open TCP connection to ES ---
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        cerr << "[user] TCP socket() failed\n";
        return;
    }

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    string portStr = to_string(serverPort_);
    int err = ::getaddrinfo(serverIp_.c_str(), portStr.c_str(), &hints, &res);
    if (err != 0) {
        cerr << "[user] getaddrinfo (TCP): " << gai_strerror(err) << "\n";
        ::close(sockfd);
        return;
    }

    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        cerr << "[user] connect (TCP) failed\n";
        ::freeaddrinfo(res);
        ::close(sockfd);
        return;
    }

    ::freeaddrinfo(res);

    // --- Send SED request to ES ---
    string header = protocol::build_show(eid);

    ssize_t n = ::write(sockfd, header.c_str(), header.size());
    if (n < 0) {
        cerr << "[user] write (TCP SED) failed\n";
        ::close(sockfd);
        return;
    }

    auto read_token = [&](string &tok) -> bool {
        tok.clear();
        char c;
        while (true) {
            ssize_t r = ::read(sockfd, &c, 1);
            if (r <= 0) {
                return false;
            }
            if (!isspace(static_cast<unsigned char>(c))) {
                tok.push_back(c);
                break;
            }
        }
        while (true) {
            ssize_t r = ::read(sockfd, &c, 1);
            if (r <= 0) {
                break;
            }
            if (isspace(static_cast<unsigned char>(c))) {
                break;
            }
            tok.push_back(c);
        }
        return true;
    };

    string type, status;

    if (!read_token(type) || !read_token(status)) {
        cout << "Show failed: could not read server reply header.\n";
        ::close(sockfd);
        return;
    }

    if (type != "RSE") {
        cout << "Protocol error: expected RSE, got '" << type << "'.\n";
        ::close(sockfd);
        return;
    }

    if (status == "NOK") {
        cout << "Show failed: event does not exist or no file to send (NOK).\n";
        ::close(sockfd);
        return;
    }

    if (status != "OK") {
        cout << "Show failed: unexpected status '" << status << "'.\n";
        ::close(sockfd);
        return;
    }

    string ownerUid, name, date, time_str;
    string attendanceStr, reservedStr, fname, fsizeStr;

    if (!read_token(ownerUid) ||
        !read_token(name)      ||
        !read_token(date)      ||
        !read_token(time_str)  ||
        !read_token(attendanceStr) ||
        !read_token(reservedStr)   ||
        !read_token(fname)     ||
        !read_token(fsizeStr)) {
        cout << "Show failed: incomplete RSE header from server.\n";
        ::close(sockfd);
        return;
    }

    long fsize = -1;
    try {
        fsize = stol(fsizeStr);
    } catch (...) {
        fsize = -1;
    }

    if (fsize <= 0) {
        cout << "Show failed: invalid file size in server reply.\n";
        ::close(sockfd);
        return;
    }

    cout << "Event " << eid << " details:\n";
    cout << "  Owner UID:      " << ownerUid    << "\n";
    cout << "  Name:           " << name        << "\n";
    cout << "  Date & time:    " << date << " " << time_str << "\n";
    cout << "  Total seats:    " << attendanceStr << "\n";
    cout << "  Reserved seats: " << reservedStr   << "\n";

    vector<char> data(static_cast<size_t>(fsize));
    long remaining = fsize;
    size_t offset  = 0;

    while (remaining > 0) {
        ssize_t got = ::read(sockfd, data.data() + offset,
                             static_cast<size_t>(remaining));
        if (got <= 0) {
            cout << "Show failed: could not read all file data from server.\n";
            ::close(sockfd);
            return;
        }
        remaining -= got;
        offset    += static_cast<size_t>(got);
    }

    ::close(sockfd);


    FILE *fp = fopen(fname.c_str(), "wb");
    if (!fp) {
        cout << "Show succeeded but could not save file '" << fname << "'.\n";
        return;
    }

    size_t written = fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);

    if (written != data.size()) {
        cout << "Show succeeded but error writing local file '" << fname << "'.\n";
        return;
    }

    cout << "File '" << fname << "' (" << fsize << " bytes) saved successfully.\n";
}
