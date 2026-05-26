#include "replication.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
#include <algorithm>
#include <deque>
#include <queue>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

using namespace std;
extern void apply_set(const vector<string>& cmd, int client_fd, bool respond);
vector<int> replica_fds;
int listening_port = 6379;
bool is_replica = false;
string master_host = "";
int master_port = 0;
string master_replid = "8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb";
long long master_repl_offset = 0;
int master_fd = -1;
void connect_to_master() {
    if (!is_replica) return;
    master_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (master_fd < 0) {
        cerr << "Failed to create master socket\n";
        return;
    }
    sockaddr_in master_addr{};
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(master_port);
    string resolved_host = master_host;
    if (master_host == "localhost") {
        resolved_host = "127.0.0.1";
    }
    if (inet_pton(AF_INET, resolved_host.c_str(), &master_addr.sin_addr) <= 0) {
        cerr << "Invalid master address\n";
        close(master_fd);
        master_fd = -1;
        return;
    }
    if (connect(master_fd, (sockaddr*)&master_addr, sizeof(master_addr)) < 0) {
        cerr << "Failed to connect to master\n";
        close(master_fd);
        master_fd = -1;
        return;
    }
    char buffer[1024];
    // STEP 1: PING
    string ping =
        "*1\r\n"
        "$4\r\n"
        "PING\r\n";
    send(master_fd, ping.c_str(), ping.size(), 0);
    recv(master_fd, buffer, sizeof(buffer), 0);\
    // STEP 2: REPLCONF listening-port
    string port_str = to_string(listening_port);
    string replconf1 =
        "*3\r\n"
        "$8\r\n"
        "REPLCONF\r\n"
        "$14\r\n"
        "listening-port\r\n"
        "$" + to_string(port_str.size()) + "\r\n" +
        port_str + "\r\n";
    send(master_fd, replconf1.c_str(), replconf1.size(), 0);
    recv(master_fd, buffer, sizeof(buffer), 0);

    // STEP 3: REPLCONF capa psync2
    string replconf2 =
        "*3\r\n"
        "$8\r\n"
        "REPLCONF\r\n"
        "$4\r\n"
        "capa\r\n"
        "$6\r\n"
        "psync2\r\n";

    send(master_fd, replconf2.c_str(), replconf2.size(), 0);
    recv(master_fd, buffer, sizeof(buffer), 0);

    // STEP 4: PSYNC
    string psync =
        "*3\r\n"
        "$5\r\n"
        "PSYNC\r\n"
        "$1\r\n"
        "?\r\n"
        "$2\r\n"
        "-1\r\n";

    send(master_fd, psync.c_str(), psync.size(), 0);

    // Read FULLRESYNC
    recv(master_fd, buffer, sizeof(buffer), 0);

    // Read RDB header
    string rdb_header;
    char ch;

    while (true) {
        int n = recv(master_fd, &ch, 1, 0);

        if (n <= 0) {
            break;
        }

        rdb_header += ch;

        if (rdb_header.size() >= 2 &&
            rdb_header.substr(rdb_header.size() - 2) == "\r\n") {
            break;
        }
    }

    int rdb_size = stoi(rdb_header.substr(1, rdb_header.size() - 3));

    vector<char> rdb_data(rdb_size);
    int received = 0;

    while (received < rdb_size) {
        int n = recv(master_fd,
                     rdb_data.data() + received,
                     rdb_size - received,
                     0);

        if (n <= 0) {
            break;
        }

        received += n;
    }
}

void parse_replication_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (string(argv[i]) == "--port") {
            if (i + 1 < argc) {
                listening_port = stoi(argv[i + 1]);
            }
        }

        if (string(argv[i]) == "--replicaof") {
            is_replica = true;
            string replicaof = argv[i + 1];
            size_t space_pos = replicaof.find(' ');

            master_host = replicaof.substr(0, space_pos);
            master_port = stoi(replicaof.substr(space_pos + 1));
        }
    }
}

void handle_info(const vector<string>& cmd, int client_fd) {
    if (cmd.size() >= 2 && cmd[1] == "replication") {
        string role = is_replica ? "slave" : "master";

        string info =
            "role:" + role + "\r\n" +
            "master_repl_offset:" + to_string(master_repl_offset) + "\r\n" +
            "master_replid:" + master_replid;

        string response =
            "$" + to_string(info.size()) + "\r\n" +
            info + "\r\n";

        send(client_fd, response.c_str(), response.size(), 0);
    }
}

void handle_psync(int client_fd) {
    string response = "+FULLRESYNC " + master_replid + " 0\r\n";
    send(client_fd, response.c_str(), response.size(), 0);

    replica_fds.push_back(client_fd);

    static const unsigned char empty_rdb[] = {
        0x52,0x45,0x44,0x49,0x53,0x30,0x30,0x31,0x31,
        0xfa,0x09,0x72,0x65,0x64,0x69,0x73,0x2d,0x76,0x65,0x72,
        0x05,0x37,0x2e,0x32,0x2e,0x30,
        0xfa,0x0a,0x72,0x65,0x64,0x69,0x73,0x2d,0x62,0x69,0x74,0x73,
        0xc0,0x40,
        0xfa,0x05,0x63,0x74,0x69,0x6d,0x65,
        0xc2,0x6d,0x08,0xbc,0x65,
        0xfa,0x08,0x75,0x73,0x65,0x64,0x2d,0x6d,0x65,0x6d,
        0xc2,0xb0,0xc4,0x10,0x00,
        0xfa,0x08,0x61,0x6f,0x66,0x2d,0x62,0x61,0x73,0x65,
        0xc0,0x00,
        0xff,0xf0,0x6e,0x3b,0xfe,0xc0,0xff,0x5a,0xa2
    };

    size_t rdb_size = sizeof(empty_rdb);

    string rdb_header = "$" + to_string(rdb_size) + "\r\n";

    send(client_fd, rdb_header.c_str(), rdb_header.size(), 0);
    send(client_fd, empty_rdb, rdb_size, 0);
}

void propagate_to_replica(const vector<string>& cmd, int replica_fd) {
    if (replica_fd == -1) return;

    string resp = "*" + to_string(cmd.size()) + "\r\n";

    for (const auto& arg : cmd) {
        resp += "$" + to_string(arg.size()) + "\r\n";
        resp += arg + "\r\n";
    }

    send(replica_fd, resp.c_str(), resp.size(), 0);
}

void listen_to_master() {
    if (master_fd == -1) return;

    char buffer[4096];
    string pending;

    while (true) {
        int bytes = recv(master_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;
        }
        pending.append(buffer, bytes);
        while (true) {
            if (pending.empty()) break;
            if (pending[0] != '*') break;
            size_t pos = pending.find("\r\n");
            if (pos == string::npos) break;
            int argc = stoi(pending.substr(1, pos - 1));
            size_t idx = pos + 2;
            vector<string> cmd;
            bool incomplete = false;
            for (int i = 0; i < argc; i++) {
                if (idx >= pending.size() || pending[idx] != '$') {
                    incomplete = true;
                    break;
                }
                size_t len_end = pending.find("\r\n", idx);
                if (len_end == string::npos) {
                    incomplete = true;
                    break;
                }
                int len = stoi(pending.substr(idx + 1, len_end - idx - 1));
                idx = len_end + 2;
                if (idx + len + 2 > pending.size()) {
                    incomplete = true;
                    break;
                }
                cmd.push_back(pending.substr(idx, len));
                idx += len + 2;
            }
            if(incomplete){
                break;
            }
            pending.erase(0, idx);
            if (!cmd.empty() && cmd[0] == "SET") {
                apply_set(cmd, -1, false);
            }
        }
    }
}