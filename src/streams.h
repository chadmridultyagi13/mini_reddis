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
/*
==================== UTILITY FUNCTIONS ====================
*/

pair<long long,long long> parse_id(const string &id);
bool is_greater(pair<long long,long long> a, pair<long long,long long> b);

/*
==================== HANDLERS ====================
*/

void handle_xadd(const vector<string>& cmd, int client_fd);

void handle_xrange(const vector<string>& cmd, int client_fd);

void handle_xread(const vector<string>& cmd, int client_fd);