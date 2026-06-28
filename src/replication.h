#pragma once
#include <unordered_map>
#include <vector>
#include <string>

extern bool is_replica;
extern std::string master_host;
extern int master_port;
extern int master_fd;
extern int listening_port;

extern std::string master_replid;
extern long long master_repl_offset;
extern long long replica_offset;
extern std :: unordered_map<int, long long> replica_ack_offsets;
extern std::vector<int> replica_fds;
int get_command_size(const std::vector<std::string>& cmd);
void handle_info(const std::vector<std::string>& cmd, int client_fd);
void parse_replication_args(int argc, char** argv);
void connect_to_master();
void handle_psync(int client_fd);
void propagate_to_replica(const std::vector<std::string>& cmd, int replica_fd);
void listen_to_master();
void apply_set(const std::vector<std::string>& cmd, int client_fd, bool respond);
void handle_wait(int client_fd, const std::vector<std::string>& cmd); ; 