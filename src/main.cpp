#include "feed/kalshi_ws.h"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

static KalshiWS* g_client = nullptr;
static void on_signal(int) { if (g_client) g_client->stop(); }

static std::string read_file(const char* path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error(std::string("cannot open: ") + path);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main() {
    const char* api_key  = std::getenv("KALSHI_API_KEY_ID");
    const char* key_file = std::getenv("KALSHI_PRIVATE_KEY_FILE");
    const char* ca_file  = std::getenv("SSL_CERT_FILE");

    if (!api_key || !key_file) {
        std::cerr << "Set KALSHI_API_KEY_ID and KALSHI_PRIVATE_KEY_FILE\n";
        return 1;
    }

    KalshiWS::Config cfg;
    cfg.api_key_id      = api_key;
    cfg.private_key_pem = read_file(key_file);
    if (ca_file) cfg.ssl_ca_file = ca_file;

    KalshiWS client(cfg, [](std::string_view msg) {
        std::cout << "callback: " << msg << "\n";
    });

    g_client = &client;
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // Queue commands before run() — they'll be sent once the connection is up.
    //client.subscribe("orderbook_delta", {"KXBTC15M-26MAR282200-00"});
    client.subscribe("ticker",          {"KXBTC15M-26MAR282200-00"});

    client.run();
    return 0;
}
