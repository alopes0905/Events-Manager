#pragma once

#include <string>

namespace protocol {

// UDP builders
std::string build_login         (const std::string& uid, const std::string& pass);
std::string build_logout        (const std::string& uid, const std::string& pass);
std::string build_unregister    (const std::string& uid, const std::string& pass);
std::string build_myevents      (const std::string& uid, const std::string& pass);
std::string build_myreservations(const std::string& uid, const std::string& pass);

// TCP builders
std::string build_change_pass   (const std::string& uid,
                                 const std::string& oldPass,
                                 const std::string& newPass);

// protocol.hpp
std::string build_create_header (const std::string& uid,
                                 const std::string& pass,
                                 const std::string& name,
                                 const std::string& date,
                                 const std::string& time,
                                 int attendance,
                                 const std::string& fname,
                                 long fsize);


std::string build_list();

std::string build_close         (const std::string& uid,
                                 const std::string& pass,
                                 const std::string& eid);

std::string build_reserve       (const std::string& uid,
                                 const std::string& pass,
                                 const std::string& eid,
                                 int people);

std::string build_show          (const std::string& eid);

// Generic response line parser
struct ResponseLine {
    std::string type;   // e.g. RLI, RLO, RUR, RCP, RCE, RLS, RME, RMR, RCL, RRI, RSE, ERR
    std::string status; // e.g. OK, NOK, ERR, ...
    std::string rest;   // remaining tokens (if any)
};

ResponseLine parse_response_line(const std::string& line);

}
