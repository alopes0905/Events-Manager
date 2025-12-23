#pragma once

#include <string>

class UserClient {
public:
    UserClient(const std::string& serverIp, int serverPort);
    void run();

private:
    std::string serverIp_;
    int         serverPort_;

    bool        loggedIn_   = false;
    std::string currentUid_;
    std::string currentPass_;

    void print_help() const;
    void handle_command(const std::string& line);

    // UDP helper
    std::string send_udp_request(const std::string& msg);

    // TCP helper (send, then read one line, then close)
    std::string send_tcp_request(const std::string& msg);

    // Commands
    void cmd_login        (const std::string& uid, const std::string& pass);
    void cmd_logout       ();
    void cmd_unregister   ();
    void cmd_myevents     ();
    void cmd_myreservations();
    void cmd_changePass   (const std::string& oldPass,
                           const std::string& newPass);
    void cmd_create       (const std::string& name,
                           const std::string& fname,
                           const std::string& date,
                           const std::string& time,
                           const std::string& attendees_str);
    void cmd_list         ();
    void cmd_close        (const std::string& eid);
    void cmd_reserve      (const std::string& eid,
                           const std::string& seats_str);
    void cmd_show         (const std::string& eid);
};
