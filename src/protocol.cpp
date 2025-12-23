using namespace ::std;

#include "protocol.hpp"

#include <cstdio>
#include <sstream>

namespace protocol {

// ---------- UDP builders ----------

// Build login request message
string build_login(const string& uid, const string& pass) {
    char buf[64];
    snprintf(buf, sizeof(buf), "LIN %s %s\n", uid.c_str(), pass.c_str());
    return string(buf);
}

// Build logout request message
string build_logout(const string& uid, const string& pass) {
    char buf[64];
    snprintf(buf, sizeof(buf), "LOU %s %s\n", uid.c_str(), pass.c_str());
    return string(buf);
}

// Build unregister request message
string build_unregister(const string& uid, const string& pass) {
    char buf[64];
    snprintf(buf, sizeof(buf), "UNR %s %s\n", uid.c_str(), pass.c_str());
    return string(buf);
}

// Build list my events request message
string build_myevents(const string& uid, const string& pass) {
    char buf[64];
    snprintf(buf, sizeof(buf), "LME %s %s\n", uid.c_str(), pass.c_str());
    return string(buf);
}

// Build list my reservations request message
string build_myreservations(const string& uid, const string& pass) {
    char buf[64];
    snprintf(buf, sizeof(buf), "LMR %s %s\n", uid.c_str(), pass.c_str());
    return string(buf);
}

// ---------- TCP builders ----------

// Build change password request message
string build_change_pass(const string& uid,
                              const string& oldPass,
                              const string& newPass) {
    char buf[128];
    snprintf(buf, sizeof(buf),
                  "CPS %s %s %s\n",
                  uid.c_str(), oldPass.c_str(), newPass.c_str());
    return string(buf);
}

// Build create event header (without file data)
string build_create_header(const string& uid,
                                const string& pass,
                                const string& name,
                                const string& date,
                                const string& time,
                                int attendance,
                                const string& fname,
                                long fsize) {
    char buf[256];


    snprintf(buf, sizeof(buf),
                  "CRE %s %s %s %s %s %d %s %ld ",
                  uid.c_str(),
                  pass.c_str(),
                  name.c_str(),
                  date.c_str(),
                  time.c_str(),
                  attendance,
                  fname.c_str(),
                  fsize);

    return string(buf);
}



// Build list all events request message
string build_list() {
    return "LST\n";
}

// Build close event request message
string build_close(const string& uid,
                        const string& pass,
                        const string& eid) {
    char buf[64];
    snprintf(buf, sizeof(buf),
                  "CLS %s %s %s\n",
                  uid.c_str(), pass.c_str(), eid.c_str());
    return string(buf);
}

// Build reserve seats request message
string build_reserve(const string& uid,
                          const string& pass,
                          const string& eid,
                          int people) {
    char buf[128];
    snprintf(buf, sizeof(buf),
                  "RID %s %s %s %d\n",
                  uid.c_str(), pass.c_str(), eid.c_str(), people);
    return string(buf);
}

// Build show event details request message
string build_show(const string& eid) {
    char buf[32];
    snprintf(buf, sizeof(buf), "SED %s\n", eid.c_str());
    return string(buf);
}


// Parse server response into type, status and remaining data
ResponseLine parse_response_line(const string& line) {
    ResponseLine r;
    istringstream iss(line);
    iss >> r.type >> r.status;
    string rest;
    getline(iss, rest);
    // Remove leading space
    if (!rest.empty() && rest[0] == ' ') {
        rest.erase(0, 1);
    }
    r.rest = rest;
    return r;
}

}
