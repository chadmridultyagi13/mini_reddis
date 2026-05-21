#pragma once
#include <vector>
#include <string>
extern bool is_replica;
extern std::string master_replid;
extern long long master_repl_offset;
void handle_info(const std::vector<std::string>& cmd, int client_fd);
void parse_replication_args(int argc, char** argv);