#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct lws;
struct lws_context;

class KalshiWS {
public:
    struct Config {
        std::string host           = "api.elections.kalshi.com";
        int         port           = 443;
        std::string path           = "/trade-api/ws/v2";
        std::string api_key_id;
        std::string private_key_pem;
        std::string ssl_ca_file;
    };

    KalshiWS(Config cfg, std::function<void(std::string_view)> onMessage);
    ~KalshiWS();

    void run();
    void stop();

    // returns the command id used
    int subscribe(std::string channel, std::vector<std::string> tickers = {});
    int unsubscribe(std::string channel, std::vector<std::string> tickers = {});

    // action: "add_markets" | "delete_markets"
    int update_subscription(int sid, std::string action, std::vector<std::string> tickers);

    // called by the lws protocol callback 
    int handle_event(lws* wsi, int reason, void* in, size_t len);

private:
    void enqueue(std::string msg);
    void sign_headers(std::string& ts_out, std::string& sig_out);

    Config    cfg_;
    std::function<void(std::string_view)> onMessage_;

    lws_context* ctx_ = nullptr;
    lws*         wsi_ = nullptr;

    std::mutex              mu_;
    std::deque<std::string> write_q_;
    std::string             rx_buf_;

    std::atomic<int>  next_id_{1};
    std::atomic<bool> running_{false};
};
