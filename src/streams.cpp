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

using namespace std;

/*
===============================================================================
                                DATA STRUCTURES
===============================================================================
*/

// Each entry inside a stream
// Format: (ID, fields)
// ID = "timestamp-sequence"
struct StreamEntry {
    string id;
    vector<pair<string,string>> fields;
};

/*
BlockedClient represents a client that issued:

XREAD BLOCK ...

and is now waiting for new data.

This object is SHARED between:
- XREAD thread (waiting)
- XADD thread (waking it)

IMPORTANT:
We use shared_ptr because both threads must access SAME object.
*/
struct BlockedClient {
    int client_fd;  // socket to send response

    // client wants entries strictly greater than this ID
    pair<long long,long long> last_seen_id;

    // absolute timeout time
    chrono::steady_clock::time_point timeout_time;

    // signal flag (communication between threads)
    atomic<bool> is_ready{false};

    // response prepared by XADD thread
    string response;
    bool infinite; // whether the client specified BLOCK 0 (wait indefinitely)
};


/*
Global Stores:

stream_store:
    key → list of entries (append-only)

blocked_clients:
    key → queue of clients waiting on that stream
*/
extern unordered_map<string, vector<StreamEntry>> stream_store;
extern mutex mtx;

unordered_map<string, deque<shared_ptr<BlockedClient>>> blocked_clients;


/*
===============================================================================
                            UTILITY FUNCTIONS
===============================================================================
*/

// Converts "T-S" → (T, S)
pair<long long,long long> parse_id(const string &id){
    int dash = id.find('-');
    long long t = stoll(id.substr(0,dash)); 
    long long s = stoll(id.substr(dash+1));
    return {t,s};
}

/*
Comparison logic for IDs

A > B if:
- A.timestamp > B.timestamp
OR
- same timestamp AND A.sequence > B.sequence
*/
bool is_greater(pair<long long,long long> a, pair<long long,long long> b){
    if(a.first > b.first) return true;
    if(a.first == b.first && a.second > b.second) return true;
    return false;
}

/*
Builds RESP response for XREAD

Structure:

[
  [key, [
      [id, [field value field value]],
      ...
  ]]
]
*/
string build_stream_response(const string &key, vector<StreamEntry> &entries){
    string resp = "*1\r\n";   // one stream
    resp += "*2\r\n";         // [key, entries]

    resp += "$" + to_string(key.size()) + "\r\n" + key + "\r\n";
    resp += "*" + to_string(entries.size()) + "\r\n";

    for(auto &entry : entries){

        resp += "*2\r\n"; // [id, fields]

        // ID
        resp += "$" + to_string(entry.id.size()) + "\r\n" + entry.id + "\r\n";

        // fields
        int field_count = entry.fields.size() * 2;
        resp += "*" + to_string(field_count) + "\r\n";

        for(auto &p : entry.fields){
            resp += "$" + to_string(p.first.size()) + "\r\n" + p.first + "\r\n";
            resp += "$" + to_string(p.second.size()) + "\r\n" + p.second + "\r\n";
        }
    }

    return resp;
}


/*
===============================================================================
                                XADD
===============================================================================
*/

void handle_xadd(const vector<string>& cmd, int client_fd) {

    string key = cmd[1];
    string input_id = cmd[2];

    /*
    Parse fields
    Format:
    XADD key id field value field value ...
    */
    vector<pair<string,string>> fields;
    for(int i = 3; i < cmd.size(); i += 2){
        fields.push_back({cmd[i], cmd[i+1]});
    }

    long long timestamp, sequence;

    /*
    Lock because:
    - modifying stream_store
    - accessing blocked_clients
    */
    {
        lock_guard<mutex> lock(mtx);

        /*
        ===================== CASE 1: FULL AUTO (*) =====================
        Server decides both timestamp and sequence
        */
        if(input_id == "*"){

            timestamp = chrono::duration_cast<chrono::milliseconds>(
                chrono::system_clock::now().time_since_epoch()
            ).count();

            sequence = 0;

            // handle same-millisecond case
            if(!stream_store[key].empty()){
                auto last = parse_id(stream_store[key].back().id);
                if(last.first == timestamp){
                    sequence = last.second + 1;
                }
            }
        }

        /*
        ===================== CASE 2: PARTIAL AUTO (T-*) =====================
        Client decides timestamp
        Server decides sequence
        */
        else if(input_id.back() == '*'){

            timestamp = stoll(input_id.substr(0, input_id.size() - 2));

            if(stream_store[key].empty()){
                // special rule: 0-0 is invalid
                sequence = (timestamp == 0 ? 1 : 0);
            }
            else{
                auto last = parse_id(stream_store[key].back().id);

                if(last.first == timestamp){
                    sequence = last.second + 1;
                }
                else{
                    sequence = (timestamp == 0 ? 1 : 0);
                }
            }
        }

        /*
        ===================== CASE 3: EXPLICIT ID =====================
        Client provides full ID → validate only
        */
        else{

            if(input_id == "0-0"){
                string err = "-ERR The ID specified in XADD must be greater than 0-0\r\n";
                send(client_fd, err.c_str(), err.size(), 0);
                return;
            }

            auto new_id = parse_id(input_id);

            if(!stream_store[key].empty()){
                auto last = parse_id(stream_store[key].back().id);

                if(!is_greater(new_id, last)){
                    string err = "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n";
                    send(client_fd, err.c_str(), err.size(), 0);
                    return;
                }
            }

            stream_store[key].push_back({input_id, fields});
            goto WAKE_CLIENTS;
        }

        // construct final ID
        input_id = to_string(timestamp) + "-" + to_string(sequence);

        // append entry
        stream_store[key].push_back({input_id, fields});

        /*
        ===================== WAKE BLOCKED CLIENTS =====================
        */

        WAKE_CLIENTS:

        auto &queue = blocked_clients[key];

        while(!queue.empty()){

            auto bc = queue.front();

            vector<StreamEntry> result;

            // find entries > last_seen_id
            for(auto &entry : stream_store[key]){
                if(is_greater(parse_id(entry.id), bc->last_seen_id)){
                    result.push_back(entry);
                }
            }

            if(!result.empty()){
                bc->response = build_stream_response(key, result);
                bc->is_ready = true;
                queue.pop_front(); // remove client
            }
            else break;
        }
    }

    // send final ID
    string resp = "$" + to_string(input_id.size()) + "\r\n" + input_id + "\r\n";
    send(client_fd, resp.c_str(), resp.size(), 0);
}


/*
===============================================================================
                                XRANGE
===============================================================================
*/

void handle_xrange(const vector<string>& cmd, int client_fd) {

    string key = cmd[1];
    string start = cmd[2];
    string end = cmd[3];

    /*
    Normalize range boundaries
    */
    pair<long long,long long> left, right;

    if(start == "-") left = {LLONG_MIN, LLONG_MIN};
    else if(start.find('-') == string::npos) left = {stoll(start), 0};
    else left = parse_id(start);

    if(end == "+") right = {LLONG_MAX, LLONG_MAX};
    else if(end.find('-') == string::npos) right = {stoll(end), LLONG_MAX};
    else right = parse_id(end);

    vector<StreamEntry> result;

    {
        lock_guard<mutex> lock(mtx);

        for(auto &entry : stream_store[key]){
            auto curr = parse_id(entry.id);

            // check: left ≤ curr ≤ right
            if(!is_greater(left, curr) && !is_greater(curr, right)){
                result.push_back(entry);
            }
        }
    }

    /*
    Build RESP
    */
    string resp = "*" + to_string(result.size()) + "\r\n";

    for(auto &entry : result){
        resp += "*2\r\n";
        resp += "$" + to_string(entry.id.size()) + "\r\n" + entry.id + "\r\n";

        int field_count = entry.fields.size()*2;
        resp += "*" + to_string(field_count) + "\r\n";

        for(auto &p : entry.fields){
            resp += "$" + to_string(p.first.size()) + "\r\n" + p.first + "\r\n";
            resp += "$" + to_string(p.second.size()) + "\r\n" + p.second + "\r\n";
        }
    }

    send(client_fd, resp.c_str(), resp.size(), 0);
}


/*
===============================================================================
                                XREAD
===============================================================================
*/

void handle_xread(const vector<string>&cmd, int client_fd){

    string op = cmd[1];
    for(char &c : op) c = tolower(c);

    /*
    ===================== BLOCK MODE =====================
    */

    if(op == "block"){

        long long timeout = stoll(cmd[2]);
        string key = cmd[4];

        // 🔥 NEW: handle "$"
        pair<long long,long long> threshold;
        {
            lock_guard<mutex> lock(mtx);
            if(cmd[5] == "$"){
                if(stream_store[key].empty()){
                    threshold = {0,0};
                } else {
                    threshold = parse_id(stream_store[key].back().id);
                }
            } else {
                threshold = parse_id(cmd[5]);
            }
        }

        // check immediately
        {
            lock_guard<mutex> lock(mtx);

            vector<StreamEntry> result;

            for(auto &entry : stream_store[key]){
                if(is_greater(parse_id(entry.id), threshold)){
                    result.push_back(entry);
                }
            }

            if(!result.empty()){
                string resp = build_stream_response(key, result);
                send(client_fd, resp.c_str(), resp.size(), 0);
                return;
            }
        }

        // register client
        auto bc = make_shared<BlockedClient>();
        bc->client_fd = client_fd;
        bc->last_seen_id = threshold;

        // 🔥 existing BLOCK logic (unchanged)
        if(timeout == 0){
            bc->infinite = true;
        } else {
            bc->infinite = false;
            bc->timeout_time = chrono::steady_clock::now() + chrono::milliseconds(timeout);
        }

        {
            lock_guard<mutex> lock(mtx);
            blocked_clients[key].push_back(bc);
        }

        /*
        WAIT LOOP (polling)
        */
        while(true){

            if(bc->is_ready){
                send(client_fd, bc->response.c_str(), bc->response.size(), 0);
                break;
            }

            if(!bc->infinite){
                if(chrono::steady_clock::now() > bc->timeout_time){
                    string resp = "*-1\r\n";
                    send(client_fd, resp.c_str(), resp.size(), 0);
                    break;
                }
            }

            this_thread::sleep_for(chrono::milliseconds(10));
        }

        return;
    }

    /*
    ===================== MULTI STREAM XREAD =====================
    */

    int k = (cmd.size() - 2) / 2;

    vector<string> keys, ids;

    for(int i = 0; i < k; i++) keys.push_back(cmd[2+i]);
    for(int i = 0; i < k; i++) ids.push_back(cmd[2+k+i]);

    vector<pair<string, vector<StreamEntry>>> final_result;

    {
        lock_guard<mutex> lock(mtx);

        for(int i = 0; i < k; i++){

            auto threshold = parse_id(ids[i]);

            vector<StreamEntry> curr;

            for(auto &entry : stream_store[keys[i]]){
                if(is_greater(parse_id(entry.id), threshold)){
                    curr.push_back(entry);
                }
            }

            if(!curr.empty()){
                final_result.push_back({keys[i], curr});
            }
        }
    }

    if(final_result.empty()){
        string resp="*0\r\n";
        send(client_fd, resp.c_str(), resp.size(), 0);
        return;
    }

    string resp="*"+to_string(final_result.size())+"\r\n";

    for(auto &p : final_result){
        resp += "*2\r\n";
        resp += "$"+to_string(p.first.size())+"\r\n"+p.first+"\r\n";
        resp += "*"+to_string(p.second.size())+"\r\n";

        for(auto &e : p.second){
            resp += "*2\r\n";
            resp += "$"+to_string(e.id.size())+"\r\n"+e.id+"\r\n";

            int fc = e.fields.size()*2;
            resp += "*"+to_string(fc)+"\r\n";

            for(auto &f : e.fields){
                resp += "$"+to_string(f.first.size())+"\r\n"+f.first+"\r\n";
                resp += "$"+to_string(f.second.size())+"\r\n"+f.second+"\r\n";
            }
        }
    }

    send(client_fd, resp.c_str(), resp.size(), 0);
}