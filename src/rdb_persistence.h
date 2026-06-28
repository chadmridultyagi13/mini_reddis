#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <deque>
#include <memory>
#include <atomic>
#include <chrono>
using namespace std;
extern string dir ;
extern string dbfilename ;
void save_to_rdb(int client_fd,vector<string>cmd) ;
extern vector<string> rdb_keys;
void load_rdb();
void handle_keys(int client_fd);