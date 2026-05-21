#include "replication.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
#include <algorithm>
#include <deque>
#include<queue>
#include<chrono>
using namespace std;
bool is_replica = false;
string master_replid ="8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb";

long long master_repl_offset = 0;
void parse_replication_args(int argc, char** argv){
    for(int i=1;i<argc;i++){
        if(string(argv[i])=="--replicaof"){
            is_replica = true;
        }
    }
}

void handle_info(const vector<string>& cmd, int client_fd) {
    if(cmd.size() >= 2 && cmd[1] == "replication") {
        string role = is_replica ? "slave" : "master";
        string info ="role:" + role + "\r\n"+ "master_repl_offset:" + to_string(master_repl_offset) + "\r\n"+ "master_replid:" + master_replid;
        string response ="$" + to_string(info.size()) + "\r\n"+ info+ "\r\n";
        send(client_fd, response.c_str(), response.size(), 0);
    }
}