using namespace ::std;

#include "es_server.hpp"

#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <ctime>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Initialize server with port and verbose settings
EventServer::EventServer(int port, bool verbose)
    : port_(port), verbose_(verbose) {
    ensure_data_dir();
    load_users();
    load_events();
    load_reservations();
}

// Clean up socket resources
EventServer::~EventServer() {
    if (udp_sock_ >= 0) ::close(udp_sock_);
    if (tcp_sock_ >= 0) ::close(tcp_sock_);
}

// Create data directory if it doesn't exist
void EventServer::ensure_data_dir() {
    struct stat st;
    if (stat("data", &st) != 0) {
        ::mkdir("data", 0755);
    }
}

// Persist users to disk
void EventServer::save_users() {
    ofstream ofs("data/users.txt", ios::trunc);
    if (!ofs) return;

    for (const auto& u : users_) {
        ofs << u.uid << " " << u.password << "\n";
    }
}

// Load users from disk and preserve login state
void EventServer::load_users() {
    vector<User> old = move(users_);
    users_.clear();

    ifstream ifs("data/users.txt");
    if (!ifs) return;

    string uid, pass;
    while (ifs >> uid >> pass) {
        User u;
        u.uid       = uid;
        u.password  = pass;
        u.loggedIn  = false;

        // Restore login state if user existed
        for (const auto& ou : old) {
            if (ou.uid == u.uid) {
                u.loggedIn = ou.loggedIn;
                break;
            }
        }

        users_.push_back(move(u));
    }
}

void EventServer::save_events() {
    ofstream ofs("data/events.txt", ios::trunc);
    if (!ofs) return;

    for (const auto& ev : events_) {
        ofs << ev.eid        << " "
            << ev.owner_uid  << " "
            << ev.name       << " "
            << ev.date       << " "
            << ev.time       << " "
            << ev.attendance << " "
            << ev.reserved   << " "
            << (ev.closed ? 1 : 0) << " "
            << ev.fname      << " "
            << ev.fsize      << "\n";
    }
}

void EventServer::save_reservations() {
    ofstream ofs("data/reservations.txt", ios::trunc);
    if (!ofs) return;

    for (const auto& r : reservations_) {
        ofs << r.uid << " "
            << r.eid << " "
            << r.seats << " "
            << r.timestamp << "\n";
    }
}

void EventServer::load_events() {
    events_.clear();

    ifstream ifs("data/events.txt");
    if (!ifs) return;

    Event ev;
    int closed_int = 0;
    while (ifs >> ev.eid
               >> ev.owner_uid
               >> ev.name
               >> ev.date
               >> ev.time
               >> ev.attendance
               >> ev.reserved
               >> closed_int
               >> ev.fname
               >> ev.fsize) {
        ev.closed = (closed_int != 0);
        events_.push_back(ev);
    }
}

void EventServer::load_reservations() {
    reservations_.clear();

    ifstream ifs("data/reservations.txt");
    if (!ifs) return;

    string line;
    while (getline(ifs, line)) {
        if (line.empty()) continue;

        istringstream iss(line);
        Reservation r;
        if (!(iss >> r.uid >> r.eid >> r.seats)) {
            continue;
        }

        string rest;
        getline(iss, rest);
        if (!rest.empty() && rest[0] == ' ')
            rest.erase(0, 1);

        r.timestamp = rest;
        reservations_.push_back(r);
    }
}

// Check if UID is 6 digits
bool EventServer::valid_uid(const string& uid) const {
    if (uid.size() != 6) return false;
    return all_of(uid.begin(), uid.end(),
                       [](unsigned char c){ return isdigit(c); });
}

// Check if password is 8 alphanumeric characters
bool EventServer::valid_password(const string& pass) const {
    if (pass.size() != 8) return false;
    return all_of(pass.begin(), pass.end(),
                       [](unsigned char c){ return isalnum(c); });
}

// Check if event name is alphanumeric and max 10 chars
bool EventServer::valid_event_name(const string& name) const {
    if (name.empty() || name.size() > 10) return false;
    return all_of(name.begin(), name.end(),
                       [](unsigned char c){ return isalnum(c); });
}

// Validate date (dd-mm-yyyy) and time (hh:mm) format
bool EventServer::valid_event_datetime(const string& date,
                                       const string& time) const {
    if (date.size() != 10) return false;
    if (date[2] != '-' || date[5] != '-') return false;
    string dd = date.substr(0, 2);
    string mm = date.substr(3, 2);
    string yyyy = date.substr(6, 4);
    if (!all_of(dd.begin(), dd.end(), ::isdigit)) return false;
    if (!all_of(mm.begin(), mm.end(), ::isdigit)) return false;
    if (!all_of(yyyy.begin(), yyyy.end(), ::isdigit)) return false;

    int d = stoi(dd);
    int m = stoi(mm);
    int y = stoi(yyyy);
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > 31) return false;

    if (time.size() != 5 && time.size() != 8) return false;
    if (time[2] != ':') return false;
    string hh = time.substr(0, 2);
    string mi = time.substr(3, 2);
    if (!all_of(hh.begin(), hh.end(), ::isdigit)) return false;
    if (!all_of(mi.begin(), mi.end(), ::isdigit)) return false;
    int H = stoi(hh);
    int M = stoi(mi);
    if (H < 0 || H > 23) return false;
    if (M < 0 || M > 59) return false;
    (void)y;
    return true;
}

// Compute event state: 0=past, 1=open, 2=sold out, 3=closed
int EventServer::compute_event_state(const Event& ev) const {
    tm tm{};
    if (ev.date.size() != 10) return 0;
    string dd = ev.date.substr(0, 2);
    string mm = ev.date.substr(3, 2);
    string yyyy = ev.date.substr(6, 4);
    string hh = "00";
    string mi = "00";
    if (ev.time.size() >= 5) {
        hh = ev.time.substr(0, 2);
        mi = ev.time.substr(3, 2);
    }
    tm.tm_mday = stoi(dd);
    tm.tm_mon  = stoi(mm) - 1;
    tm.tm_year = stoi(yyyy) - 1900;
    tm.tm_hour = stoi(hh);
    tm.tm_min  = stoi(mi);
    tm.tm_sec  = 0;

    time_t event_time = timegm(&tm);

    time_t now = time(nullptr);
    if (event_time <= now) {
        return 0;
    }
    if (ev.closed) {
        return 3;
    }
    if (ev.reserved >= ev.attendance) {
        return 2;
    }
    return 1;
}

User* EventServer::find_user(const string& uid) {
    for (auto& u : users_) {
        if (u.uid == uid) return &u;
    }
    return nullptr;
}

Event* EventServer::find_event(const string& eid) {
    for (auto& ev : events_) {
        if (ev.eid == eid) return &ev;
    }
    return nullptr;
}

// Find next available event ID (001-999)
string EventServer::allocate_eid() {
    bool used[1000] = {false};
    for (const auto& ev : events_) {
        if (ev.eid.size() == 3 &&
            all_of(ev.eid.begin(), ev.eid.end(), ::isdigit)) {
            int x = stoi(ev.eid);
            if (x >= 1 && x <= 999) used[x] = true;
        }
    }
    for (int i = 1; i <= 999; ++i) {
        if (!used[i]) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%03d", i);
            return string(buf);
        }
    }
    return "";
}

// Initialize and bind UDP and TCP sockets
bool EventServer::init_sockets() {
    // UDP
    udp_sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock_ < 0) {
        perror("socket UDP");
        return false;
    }

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port_);

    if (::bind(udp_sock_, reinterpret_cast<sockaddr*>(&servaddr),
             sizeof(servaddr)) < 0) {
        perror("bind UDP");
        return false;
    }

    // TCP
    tcp_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock_ < 0) {
        perror("socket TCP");
        return false;
    }

    int opt = 1;
    ::setsockopt(tcp_sock_, SOL_SOCKET, SO_REUSEADDR,
                 &opt, sizeof(opt));

    sockaddr_in servaddr_tcp{};
    servaddr_tcp.sin_family = AF_INET;
    servaddr_tcp.sin_addr.s_addr = INADDR_ANY;
    servaddr_tcp.sin_port = htons(port_);

    if (::bind(tcp_sock_, reinterpret_cast<sockaddr*>(&servaddr_tcp),
             sizeof(servaddr_tcp)) < 0) {
        perror("bind TCP");
        return false;
    }

    if (listen(tcp_sock_, 5) < 0) {
        perror("listen TCP");
        return false;
    }

    if (verbose_) {
        cout << "[ES] UDP & TCP sockets bound on port " << port_ << "\n";
        cout << "[ES] Event Server running (UDP+TCP) on port " << port_ << "\n";
    }

    return true;
}

void EventServer::run() {
    if (!init_sockets()) {
        cerr << "Failed to init sockets\n";
        return;
    }
    main_loop();
}

// Main server loop: handle UDP and TCP requests using select
void EventServer::main_loop() {
    fd_set rfds;
    int maxfd = max(udp_sock_, tcp_sock_);

    while (true) {
        FD_ZERO(&rfds);
        FD_SET(udp_sock_, &rfds);
        FD_SET(tcp_sock_, &rfds);

        int ret = ::select(maxfd + 1, &rfds, nullptr, nullptr, nullptr);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(udp_sock_, &rfds)) {
            handle_udp_request();
        }

        if (FD_ISSET(tcp_sock_, &rfds)) {
            sockaddr_in cliaddr{};
            socklen_t len = sizeof(cliaddr);
            int conn_fd = ::accept(tcp_sock_,
                                   reinterpret_cast<sockaddr*>(&cliaddr),
                                   &len);
            if (conn_fd < 0) {
                perror("accept");
            } else {
                if (verbose_) {
                    cout << "[ES] New TCP connection accepted\n";
                }
                handle_tcp_client(conn_fd);
                ::close(conn_fd);
            }
        }
    }
}

// --- UDP ---

void EventServer::send_udp_reply(const string& reply,
                                 sockaddr_in& cliaddr,
                                 socklen_t cli_len) {
    if (verbose_) {
        cout << "[ES][UDP] Sent: \"" << reply << "\"\n";
    }
    ::sendto(udp_sock_, reply.c_str(), reply.size(), 0,
             reinterpret_cast<sockaddr*>(&cliaddr), cli_len);
}

// Process incoming UDP request and dispatch to handler
void EventServer::handle_udp_request() {
    char buf[1024];
    sockaddr_in cliaddr{};
    socklen_t len = sizeof(cliaddr);

    ssize_t n = ::recvfrom(udp_sock_, buf, sizeof(buf) - 1, 0,
                           reinterpret_cast<sockaddr*>(&cliaddr), &len);
    if (n <= 0) return;
    buf[n] = '\0';

    string msg(buf);
    if (verbose_) {
        cout << "[ES][UDP] Received: \"" << msg << "\"\n";
    }

    istringstream iss(msg);
    string cmd;
    iss >> cmd;

    if (cmd == "LIN") {
        string uid, pass;
        iss >> uid >> pass;
        handle_LIN(uid, pass, cliaddr, len);
    } else if (cmd == "LOU") {
        string uid, pass;
        iss >> uid >> pass;
        handle_LOU(uid, pass, cliaddr, len);
    } else if (cmd == "UNR") {
        string uid, pass;
        iss >> uid >> pass;
        handle_UNR(uid, pass, cliaddr, len);
    } else if (cmd == "LME") {
        string uid, pass;
        iss >> uid >> pass;
        handle_LME(uid, pass, cliaddr, len);
    } else if (cmd == "LMR") {
        string uid, pass;
        iss >> uid >> pass;
        handle_LMR(uid, pass, cliaddr, len);
    }
}

// Handle login: register new user or authenticate existing
void EventServer::handle_LIN(const string& uid,
                             const string& pass,
                             sockaddr_in& cliaddr,
                             socklen_t cli_len) {
    load_users();

    if (!valid_uid(uid) || !valid_password(pass)) {
        send_udp_reply("RLI ERR\n", cliaddr, cli_len);
        return;
    }

    User* u = find_user(uid);
    if (!u) {
        // Register new user
        User nu;
        nu.uid       = uid;
        nu.password  = pass;
        nu.loggedIn  = true;
        users_.push_back(nu);
        save_users();
        if (verbose_) {
            cout << "[ES] LIN: new user " << uid
                      << " registered & logged in\n";
        }
        send_udp_reply("RLI REG\n", cliaddr, cli_len);
    } else {
        if (u->password != pass) {
            send_udp_reply("RLI NOK\n", cliaddr, cli_len);
        } else {
            u->loggedIn = true;
            send_udp_reply("RLI OK\n", cliaddr, cli_len);
        }
    }
}

// Handle logout request
void EventServer::handle_LOU(const string& uid,
                             const string& pass,
                             sockaddr_in& cliaddr,
                             socklen_t cli_len) {
    User* u = find_user(uid);
    if (!u || u->password != pass) {
        send_udp_reply("RLO ERR\n", cliaddr, cli_len);
        return;
    }
    if (!u->loggedIn) {
        send_udp_reply("RLO NOK\n", cliaddr, cli_len);
        return;
    }
    u->loggedIn = false;
    send_udp_reply("RLO OK\n", cliaddr, cli_len);
}

// Handle user unregister request
void EventServer::handle_UNR(const string& uid,
                             const string& pass,
                             sockaddr_in& cliaddr,
                             socklen_t cli_len) {
    User* u = find_user(uid);
    if (!u) {
        send_udp_reply("RUR UNR\n", cliaddr, cli_len);
        return;
    }
    if (u->password != pass) {
        send_udp_reply("RUR WRP\n", cliaddr, cli_len);
        return;
    }
    if (!u->loggedIn) {
        send_udp_reply("RUR NOK\n", cliaddr, cli_len);
        return;
    }
    // Remove user from list
    users_.erase(remove_if(users_.begin(), users_.end(),
                                [&](const User& usr){ return usr.uid == uid; }),
                 users_.end());
    save_users();
    send_udp_reply("RUR OK\n", cliaddr, cli_len);
}

// LME: myevents
void EventServer::handle_LME(const string& uid,
                             const string& pass,
                             sockaddr_in& cliaddr,
                             socklen_t cli_len) {
    load_events();

    User* u = find_user(uid);
    if (!u || u->password != pass || !u->loggedIn) {
        send_udp_reply("RME NLG\n", cliaddr, cli_len);
        return;
    }

    vector<const Event*> mine;
    for (const auto& ev : events_) {
        if (ev.owner_uid == uid) mine.push_back(&ev);
    }

    if (mine.empty()) {
        send_udp_reply("RME NOK\n", cliaddr, cli_len);
        return;
    }

    sort(mine.begin(), mine.end(),
              [](const Event* a, const Event* b) {
                  return a->eid < b->eid;
              });

    string reply = "RME OK";
    for (const Event* ev : mine) {
        int st = compute_event_state(*ev);
        reply += " " + ev->eid + " " + to_string(st);
    }
    reply += "\n";
    send_udp_reply(reply, cliaddr, cli_len);
}

// LMR: myreservations — RMR OK [EID date time seats]
void EventServer::handle_LMR(const string& uid,
                             const string& pass,
                             sockaddr_in& cliaddr,
                             socklen_t cli_len) {
    load_reservations();

    User* u = find_user(uid);
    if (!u || u->password != pass || !u->loggedIn) {
        send_udp_reply("RMR NLG\n", cliaddr, cli_len);
        return;
    }

    vector<const Reservation*> mine;
    for (const auto& r : reservations_) {
        if (r.uid == uid) mine.push_back(&r);
    }

    if (mine.empty()) {
        send_udp_reply("RMR NOK\n", cliaddr, cli_len);
        return;
    }

    sort(mine.begin(), mine.end(),
              [](const Reservation* a, const Reservation* b) {
                  if (a->eid != b->eid) return a->eid < b->eid;
                  return a->timestamp < b->timestamp;
              });

    string reply = "RMR OK";
    for (const Reservation* r : mine) {
        string date = "00-00-0000";
        string time = "00:00:00";
        if (r->timestamp.size() >= 19) {
            date = r->timestamp.substr(0, 10);
            time = r->timestamp.substr(11, 8);
        }
        reply += " " + r->eid + " " + date + " " + time + " " + to_string(r->seats);
    }
    reply += "\n";

    send_udp_reply(reply, cliaddr, cli_len);
}

// --- TCP utils ---

void EventServer::send_tcp_line(int fd, const string& line) {
    ::write(fd, line.c_str(), line.size());
}

bool EventServer::read_token(int fd, string& tok) {
    tok.clear();
    char c;
    while (true) {
        ssize_t r = ::read(fd, &c, 1);
        if (r <= 0) return false;
        if (!isspace(static_cast<unsigned char>(c))) {
            tok.push_back(c);
            break;
        }
    }
    while (true) {
        ssize_t r = ::read(fd, &c, 1);
        if (r <= 0) break;
        if (isspace(static_cast<unsigned char>(c))) break;
        tok.push_back(c);
    }
    return true;
}

// Process TCP client request and dispatch to handler
void EventServer::handle_tcp_client(int fd) {
    string cmd;
    if (!read_token(fd, cmd)) {
        return;
    }

    if (verbose_) {
        cout << "[ES][TCP] Command: " << cmd << "\n";
    }

    if (cmd == "CPS") {
        handle_CPS(fd);
    } else if (cmd == "CRE") {
        handle_CRE(fd);
    } else if (cmd == "LST") {
        handle_LST(fd);
    } else if (cmd == "CLS") {
        handle_CLS(fd);
    } else if (cmd == "RID") {
        handle_RID(fd);
    } else if (cmd == "SED") {
        handle_SED(fd);
    } else {
        send_tcp_line(fd, "ERR\n");
    }
}

// Handle change password request
void EventServer::handle_CPS(int fd) {
    string uid, oldp, newp;
    if (!read_token(fd, uid) ||
        !read_token(fd, oldp) ||
        !read_token(fd, newp)) {
        send_tcp_line(fd, "RCP ERR\n");
        return;
    }

    User* u = find_user(uid);
    if (!u) {
        send_tcp_line(fd, "RCP NID\n");
        return;
    }
    if (!u->loggedIn) {
        send_tcp_line(fd, "RCP NLG\n");
        return;
    }
    if (u->password != oldp) {
        send_tcp_line(fd, "RCP NOK\n");
        return;
    }
    if (!valid_password(newp)) {
        send_tcp_line(fd, "RCP ERR\n");
        return;
    }
    // Update password
    u->password = newp;
    save_users();
    send_tcp_line(fd, "RCP OK\n");
}

// Handle create event request with file upload
void EventServer::handle_CRE(int fd) {
    load_events();
    string uid, pass, name, date, time;
    string attendance_str, fname, fsize_str;

    // Read request parameters
    if (!read_token(fd, uid) ||
        !read_token(fd, pass) ||
        !read_token(fd, name) ||
        !read_token(fd, date) ||
        !read_token(fd, time) ||
        !read_token(fd, attendance_str) ||
        !read_token(fd, fname) ||
        !read_token(fd, fsize_str)) {
        send_tcp_line(fd, "RCE ERR\n");
        return;
    }

    User* u = find_user(uid);
    if (!u) {
        send_tcp_line(fd, "RCE NLG\n");
        return;
    }
    if (u->password != pass) {
        send_tcp_line(fd, "RCE WRP\n");
        return;
    }
    if (!u->loggedIn) {
        send_tcp_line(fd, "RCE NLG\n");
        return;
    }

    int  attendance = -1;
    long fsize      = -1;
    try {
        attendance = stoi(attendance_str);
        fsize      = stol(fsize_str);
    } catch (...) {
        send_tcp_line(fd, "RCE ERR\n");
        return;
    }

    const long MAX_FILE_SIZE = 10000000L; // 10 MB

    // Validate parameters
    if (!valid_event_name(name) ||
        !valid_event_datetime(date, time) ||
        attendance < 10 || attendance > 999 ||
        fsize <= 0 || fsize > MAX_FILE_SIZE) {
        send_tcp_line(fd, "RCE ERR\n");
        return;
    }

    // Read file data from client
    vector<char> data(static_cast<size_t>(fsize));
    long   remaining = fsize;
    size_t offset    = 0;

    while (remaining > 0) {
        ssize_t got = ::read(fd, data.data() + offset,
                             static_cast<size_t>(remaining));
        if (got <= 0) {
            send_tcp_line(fd, "RCE NOK\n");
            return;
        }
        remaining -= got;
        offset    += static_cast<size_t>(got);
    }

    /*
    // Check event is not in the past
    Event tmp;
    tmp.date = date;
    tmp.time = time;
    if (compute_event_state(tmp) == 0) {
        send_tcp_line(fd, "RCE ERR\n");
        return;
    }
    */

    string eid = allocate_eid();
    if (eid.empty()) {
        send_tcp_line(fd, "RCE NOK\n");
        return;
    }

    // Save file to disk
    FILE* fp = fopen(fname.c_str(), "wb");
    if (!fp) {
        send_tcp_line(fd, "RCE NOK\n");
        return;
    }
    size_t written = fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);
    if (written != data.size()) {
        send_tcp_line(fd, "RCE NOK\n");
        return;
    }

    // Create and save event
    Event ev;
    ev.eid        = eid;
    ev.owner_uid  = uid;
    ev.name       = name;
    ev.date       = date;
    ev.time       = time;
    ev.attendance = attendance;
    ev.fname      = fname;
    ev.fsize      = static_cast<unsigned long>(fsize);
    ev.reserved   = 0;
    ev.closed     = false;

    events_.push_back(ev);
    save_events();

    ostringstream oss;
    oss << "RCE OK " << eid << "\n";
    send_tcp_line(fd, oss.str());
}

// Handle list all events request
void EventServer::handle_LST(int fd) {
    load_events();

    if (events_.empty()) {
        send_tcp_line(fd, "RLS NOK\n");
        return;
    }

    // Sort events by EID
    vector<const Event*> vec;
    for (const auto& ev : events_) {
        vec.push_back(&ev);
    }

    sort(vec.begin(), vec.end(),
              [](const Event* a, const Event* b) {
                  return a->eid < b->eid;
              });

    // Build response with event details
    ostringstream oss;
    oss << "RLS OK";
    for (const Event* ev : vec) {
        int st = compute_event_state(*ev);
        oss << " " << ev->eid
            << " " << ev->name
            << " " << st
            << " " << ev->date
            << " " << ev->time;
    }
    oss << "\n";
    send_tcp_line(fd, oss.str());
}

// CLS UID password EID
void EventServer::handle_CLS(int fd) {
    load_events();

    string uid, pass, eid;
    if (!read_token(fd, uid) ||
        !read_token(fd, pass) ||
        !read_token(fd, eid)) {
        send_tcp_line(fd, "RCL ERR\n");
        return;
    }

    User* u = find_user(uid);
    if (!u) {
        send_tcp_line(fd, "RCL NOK\n");
        return;
    }
    if (u->password != pass) {
        send_tcp_line(fd, "RCL NOK\n");
        return;
    }
    if (!u->loggedIn) {
        send_tcp_line(fd, "RCL NLG\n");
        return;
    }

    Event* ev = find_event(eid);
    if (!ev) {
        send_tcp_line(fd, "RCL NOE\n");
        return;
    }
    if (ev->owner_uid != uid) {
        send_tcp_line(fd, "RCL EOW\n");
        return;
    }
    int st = compute_event_state(*ev);
    if (st == 0) {
        send_tcp_line(fd, "RCL PST\n");
        return;
    }
    if (ev->closed) {
        send_tcp_line(fd, "RCL CLO\n");
        return;
    }
    if (ev->reserved >= ev->attendance) {
        send_tcp_line(fd, "RCL SLD\n");
        return;
    }

    ev->closed = true;
    save_events();

    send_tcp_line(fd, "RCL OK\n");
}

// Handle reserve seats request
void EventServer::handle_RID(int fd) {
    load_events();
    load_reservations();
    string uid, pass, eid, peopleStr;
    if (!read_token(fd, uid) ||
        !read_token(fd, pass) ||
        !read_token(fd, eid) ||
        !read_token(fd, peopleStr)) {
        send_tcp_line(fd, "RRI ERR\n");
        return;
    }

    User* u = find_user(uid);
    if (!u) {
        send_tcp_line(fd, "RRI NLG\n");
        return;
    }
    if (u->password != pass) {
        send_tcp_line(fd, "RRI WRP\n");
        return;
    }
    if (!u->loggedIn) {
        send_tcp_line(fd, "RRI NLG\n");
        return;
    }

    Event* ev = find_event(eid);
    if (!ev) {
        send_tcp_line(fd, "RRI NOK\n");
        return;
    }

    // Check event state
    int st = compute_event_state(*ev);
    if (st == 0) {
        send_tcp_line(fd, "RRI PST\n");
        return;
    }
    if (ev->closed) {
        send_tcp_line(fd, "RRI CLS\n");
        return;
    }
    if (ev->reserved >= ev->attendance) {
        send_tcp_line(fd, "RRI SLD\n");
        return;
    }

    int people = 0;
    try {
        people = stoi(peopleStr);
    } catch (...) {
        send_tcp_line(fd, "RRI ERR\n");
        return;
    }
    if (people < 1 || people > 999) {
        send_tcp_line(fd, "RRI ERR\n");
        return;
    }

    // Check available seats
    int available = ev->attendance - ev->reserved;
    if (people > available) {
        ostringstream oss;
        oss << "RRI REJ " << available << "\n";
        send_tcp_line(fd, oss.str());
        return;
    }

    // Accept reservation
    ev->reserved += people;

    Reservation r;
    r.uid = uid;
    r.eid = eid;
    r.seats = people;

    // Generate timestamp
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%d-%m-%Y %H:%M:%S", t);
    r.timestamp = tbuf;

    reservations_.push_back(r);
    save_reservations();
    save_events();

    ostringstream oss;
    oss << "RRI ACC\n";
    send_tcp_line(fd, oss.str());
}

// Handle show event details request - send event info and file
void EventServer::handle_SED(int fd) {
    load_events();

    string eid;
    if (!read_token(fd, eid)) {
        send_tcp_line(fd, "RSE NOK\n");
        return;
    }

    Event* ev = find_event(eid);
    if (!ev) {
        send_tcp_line(fd, "RSE NOK\n");
        return;
    }

    // Read event file from disk
    FILE* fp = fopen(ev->fname.c_str(), "rb");
    if (!fp) {
        send_tcp_line(fd, "RSE NOK\n");
        return;
    }
    vector<char> data(ev->fsize);
    size_t rd = fread(data.data(), 1, data.size(), fp);
    fclose(fp);
    if (rd != data.size()) {
        send_tcp_line(fd, "RSE NOK\n");
        return;
    }

    // Send event metadata and file data
    ostringstream oss;
    oss << "RSE OK "
        << ev->owner_uid << " "
        << ev->name      << " "
        << ev->date      << " "
        << ev->time      << " "
        << ev->attendance << " "
        << ev->reserved   << " "
        << ev->fname      << " "
        << ev->fsize      << " ";

    string header = oss.str();
    ::write(fd, header.c_str(), header.size());

    // Send file data
    long remaining = static_cast<long>(ev->fsize);
    size_t offset  = 0;
    while (remaining > 0) {
        ssize_t sent = ::write(fd, data.data() + offset,
                               static_cast<size_t>(remaining));
        if (sent <= 0) break;
        remaining -= sent;
        offset    += static_cast<size_t>(sent);
    }

    const char nl = '\n';
    ::write(fd, &nl, 1);
}
