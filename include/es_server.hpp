#pragma once

#include <string>
#include <vector>
#include <map>

#include <sys/socket.h>
#include <netinet/in.h>

// Representa um utilizador
struct User {
    std::string uid;
    std::string password;
    bool loggedIn = false;
};

// Representa um evento
struct Event {
    std::string eid;
    std::string owner_uid;
    std::string name;
    std::string date;
    std::string time;
    int attendance = 0;
    unsigned long fsize = 0;
    std::string fname;
    int reserved = 0;
    bool closed = false;
};

struct Reservation {
    std::string uid;
    std::string eid;
    int seats = 0;
    std::string timestamp;
};

class EventServer {
public:
    EventServer(int port, bool verbose);
    ~EventServer();

    void run();

private:
    int port_;
    bool verbose_;

    int udp_sock_ = -1;
    int tcp_sock_ = -1;

    std::vector<User> users_;
    std::vector<Event> events_;
    std::vector<Reservation> reservations_;

    // --- sockets ---
    bool init_sockets();
    void main_loop();
    void handle_udp_request();
    void handle_tcp_client(int conn_fd);

    // --- helpers ---
    User* find_user(const std::string& uid);
    Event* find_event(const std::string& eid);
    bool valid_uid(const std::string& uid) const;
    bool valid_password(const std::string& pass) const;
    bool valid_event_name(const std::string& name) const;
    bool valid_event_datetime(const std::string& date, const std::string& time) const;
    int  compute_event_state(const Event& ev) const;

    std::string allocate_eid();

    void ensure_data_dir();
    void save_users();
    void load_users();
    void save_events();
    void load_events();
    void save_reservations();
    void load_reservations();



    // --- UDP handlers ---
    void handle_LIN(const std::string& uid,
                    const std::string& pass,
                    sockaddr_in& cliaddr,
                    socklen_t cli_len);

    void handle_LOU(const std::string& uid,
                    const std::string& pass,
                    sockaddr_in& cliaddr,
                    socklen_t cli_len);

    void handle_UNR(const std::string& uid,
                    const std::string& pass,
                    sockaddr_in& cliaddr,
                    socklen_t cli_len);

    void handle_LME(const std::string& uid,
                    const std::string& pass,
                    sockaddr_in& cliaddr,
                    socklen_t cli_len);

    void handle_LMR(const std::string& uid,
                    const std::string& pass,
                    sockaddr_in& cliaddr,
                    socklen_t cli_len);

    void send_udp_reply(const std::string& reply,
                        sockaddr_in& cliaddr,
                        socklen_t cli_len);


    // --- TCP token reader ---
    bool read_token(int fd, std::string& tok);

    // --- TCP handlers ---
    void handle_CPS(int fd); // changePass
    void handle_CRE(int fd); // create event
    void handle_LST(int fd); // list events
    void handle_CLS(int fd); // close event
    void handle_RID(int fd); // reserve
    void handle_SED(int fd); // show

    void send_tcp_line(int fd, const std::string& line);
};
