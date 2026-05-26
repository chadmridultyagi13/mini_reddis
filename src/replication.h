#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
#include <algorithm>
#include <deque>
#include<queue>
#include<chrono>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
using namespace std;
#include<vector>
extern bool is_replica;
extern std::string master_host;
extern int master_port;
extern int master_fd;
extern int listening_port;
extern std::string master_replid;
extern long long master_repl_offset;
extern vector<int>replica_fds ;
void handle_info(const std::vector<std::string>& cmd, int client_fd);
void parse_replication_args(int argc, char** argv);
void connect_to_master();
void handle_psync(int client_fd) ; 
void propagate_to_replica(const vector<string>& cmd,int replica_fd) ;  
void listen_to_master();
void apply_set(const std::vector<std::string>& cmd, int client_fd, bool respond);