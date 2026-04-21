#include "streams.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
#include <chrono>
#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <climits>

bool check(string &str) ; 
void handle_incr(const vector<string>& cmd, int client_fd) ; 
void handle_multi(const vector<string>&cmd, int client_fd) ; 
void handle_exec(const vector<string>&cmd,int client_fd) ;
string execute_simple_command(const vector<string>& cmd) ; 
void handle_discard(const vector<string>& cmd, int client_fd) ;