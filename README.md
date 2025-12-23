Events Manager
===================

**1. What this project is**


Simple event management system with:

- ES   – Event Server (UDP + TCP)
- user – User client

They communicate using the project protocol:
- UDP: login / logout / unregister / myevents / myreservations
- TCP: change password / create / list / close / reserve / show (with file transfer)

------------------------
**2. Files and directories**


Top-level:
- Makefile            – builds both binaries
- readme.txt          – this file

Headers (include/):
- es_server.hpp       – EventServer class
- user_client.hpp     – UserClient class
- protocol.hpp        – protocol helpers (build/parse)
- common.hpp          – shared utilities (if used)

Sources (src/):
- es_main.cpp         – main() for ES
- es_server.cpp       – EventServer implementation
- user_main.cpp       – main() for user
- user_client.cpp     – UserClient implementation
- protocol.cpp        – protocol build/parse implementation
- common.cpp          – shared utilities (if used)

Data (created at runtime):
- data/users.txt         – UID + password (persistent users)
- data/events.txt        – events (EID, owner, date/time, status, file info)
- data/reservations.txt  – reservations (UID, EID, seats, timestamp)

Event description files:
- Stored in the current working directory with the filename given in 'create'.
- Used later by 'show'.

------------------------
**3. Building**


Requirements: C++17 compiler and POSIX sockets.

To build:

    make

This creates:

    ES
    user

To clean:

    make clean

------------------------
**4. Running the server**

Default (port from code, e.g. 58000):

    ./ES

With options:

    ./ES -p <port> [-v]

- '-p <port>': UDP/TCP port to bind.
- '-v'       : verbose logging.

On startup the server:
- Ensures 'data/' exists.
- Loads users, events and reservations from 'data/*.txt' if present.
- Listens on UDP and TCP on the same port.

------------------------
**5. Running the client**


Usage:

    ./user -n <server_ip> -p <server_port>

Example:

    ./user -n 127.0.0.1 -p 58000

Available commands in the prompt:

- login <UID> <password>
- logout
- unregister
- mye / myevents
- myr / myreservations
- changePass <oldPass> <newPass>
- create <name> <event_fname> <dd-mm-yyyy> <hh:mm> <num_attendees>
- list
- close <EID>
- reserve <EID> <value>
- show <EID>
- help
- exit

------------------------
**6. Persistence / reset**


The server keeps state across restarts in:

- data/users.txt
- data/events.txt
- data/reservations.txt

To reset everything:

1. Stop the server.
2. Remove the data directory:

       rm -rf data

3. Start the server again; it will recreate 'data/' and start from an empty state.
