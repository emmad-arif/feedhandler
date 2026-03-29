#include "feed/kalshi_ws.h"

#include <libwebsockets.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static std::string b64_encode(const unsigned char* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string out(bptr->data, bptr->length);
    BIO_free_all(b64);
    return out;
}

static std::string rsa_pss_sign(const std::string& pem, const std::string& msg) {
    BIO* bio   = BIO_new_mem_buf(pem.c_str(), -1);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) throw std::runtime_error("Failed to load RSA private key");

    EVP_MD_CTX*   ctx  = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    EVP_DigestSignInit(ctx, &pctx, EVP_sha256(), nullptr, pkey);
    EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING);
    EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1); // -1 = digest length
    EVP_DigestSignUpdate(ctx, msg.c_str(), msg.size());

    size_t siglen = 0;
    EVP_DigestSignFinal(ctx, nullptr, &siglen);
    std::vector<unsigned char> sig(siglen);
    EVP_DigestSignFinal(ctx, sig.data(), &siglen);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return b64_encode(sig.data(), siglen);
}

static std::string json_str_array(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        out += '"'; out += v[i]; out += '"';
    }
    return out + ']';
}

static int protocol_cb(struct lws* wsi, enum lws_callback_reasons reason,
                        void* /*user*/, void* in, size_t len) {
    auto* self = static_cast<KalshiWS*>(lws_context_user(lws_get_context(wsi)));
    if (!self) return 0;
    return self->handle_event(wsi, static_cast<int>(reason), in, len);
}

int KalshiWS::handle_event(lws* wsi, int reason_int, void* in, size_t len) {
    switch (static_cast<lws_callback_reasons>(reason_int)) {

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
        std::string ts, sig;
        sign_headers(ts, sig);
        auto** p   = reinterpret_cast<unsigned char**>(in);
        auto*  end = *p + len;
        auto add   = [&](const char* name, const std::string& val) {
            if (lws_add_http_header_by_name(wsi,
                    reinterpret_cast<const unsigned char*>(name),
                    reinterpret_cast<const unsigned char*>(val.c_str()),
                    static_cast<int>(val.size()), p, end))
                std::cerr << "[kalshi] header buffer full adding " << name << "\n";
        };
        add("KALSHI-ACCESS-KEY:",       cfg_.api_key_id);
        add("KALSHI-ACCESS-TIMESTAMP:", ts);
        add("KALSHI-ACCESS-SIGNATURE:", sig);
        break;
    }

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        wsi_ = wsi;
        std::cout << "[kalshi] connected\n";
        lws_callback_on_writable(wsi); // drain any pre-queued commands
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        rx_buf_.append(static_cast<const char*>(in), len);
        if (lws_is_final_fragment(wsi)) {
            if (onMessage_) onMessage_(rx_buf_);
            rx_buf_.clear();
        }
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        std::string msg;
        bool has_more = false;
        {
            std::lock_guard lk(mu_);
            if (write_q_.empty()) break;
            msg      = std::move(write_q_.front());
            write_q_.pop_front();
            has_more = !write_q_.empty();
        }
        std::vector<unsigned char> buf(LWS_PRE + msg.size());
        std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());
        if (lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_TEXT) < 0)
            return -1;
        if (has_more) lws_callback_on_writable(wsi);
        break;
    }

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        if (wsi_) lws_callback_on_writable(wsi_);
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        wsi_ = nullptr;
        std::cerr << "[kalshi] disconnected\n";
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        wsi_ = nullptr;
        std::cerr << "[kalshi] connection error: "
                  << (in ? static_cast<const char*>(in) : "unknown") << "\n";
        break;

    default:
        break;
    }
    return 0;
}

KalshiWS::KalshiWS(Config cfg, std::function<void(std::string_view)> onMessage)
    : cfg_(std::move(cfg)), onMessage_(std::move(onMessage)) {}

KalshiWS::~KalshiWS() { stop(); }

void KalshiWS::run() {
    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    const lws_protocols protocols[] = {
        { "kalshi-ws", protocol_cb, 0, 65536, 0, nullptr, 0 },
        { nullptr,    nullptr,     0, 0,     0, nullptr, 0 }
    };

    lws_context_creation_info ctx_info{};
    ctx_info.port      = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = protocols;
    ctx_info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    ctx_info.user      = this;
    if (!cfg_.ssl_ca_file.empty())
        ctx_info.client_ssl_ca_filepath = cfg_.ssl_ca_file.c_str();

    ctx_ = lws_create_context(&ctx_info);
    if (!ctx_) throw std::runtime_error("Failed to create lws context");

    lws_client_connect_info ci{};
    ci.context      = ctx_;
    ci.address      = cfg_.host.c_str();
    ci.port         = cfg_.port;
    ci.path         = cfg_.path.c_str();
    ci.host         = cfg_.host.c_str();
    ci.origin       = cfg_.host.c_str();
    ci.protocol     = protocols[0].name;
    ci.ssl_connection = LCCSCF_USE_SSL;
    lws_client_connect_via_info(&ci);

    running_.store(true);
    while (running_.load(std::memory_order_acquire))
        lws_service(ctx_, 100);

    lws_context_destroy(ctx_);
    ctx_ = nullptr;
    wsi_ = nullptr;
}

void KalshiWS::stop() {
    running_.store(false, std::memory_order_release);
    if (ctx_) lws_cancel_service(ctx_);
}

void KalshiWS::enqueue(std::string msg) {
    { std::lock_guard lk(mu_); write_q_.push_back(std::move(msg)); }
    if (ctx_) lws_cancel_service(ctx_);
}

void KalshiWS::sign_headers(std::string& ts_out, std::string& sig_out) {
    using namespace std::chrono;
    ts_out  = std::to_string(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    sig_out = rsa_pss_sign(cfg_.private_key_pem, ts_out + "GET" + cfg_.path);
}

int KalshiWS::subscribe(std::string channel, std::vector<std::string> tickers) {
    const int id = next_id_.fetch_add(1);
    std::string j = R"({"id":)" + std::to_string(id)
                  + R"(,"cmd":"subscribe","params":{"channels":[")" + channel + R"("])";
    if (!tickers.empty()) j += R"(,"market_tickers":)" + json_str_array(tickers);
    j += "}}";
    enqueue(std::move(j));
    return id;
}

int KalshiWS::unsubscribe(std::string channel, std::vector<std::string> tickers) {
    const int id = next_id_.fetch_add(1);
    std::string j = R"({"id":)" + std::to_string(id)
                  + R"(,"cmd":"unsubscribe","params":{"channels":[")" + channel + R"("])";
    if (!tickers.empty()) j += R"(,"market_tickers":)" + json_str_array(tickers);
    j += "}}";
    enqueue(std::move(j));
    return id;
}

int KalshiWS::update_subscription(int sid, std::string action,
                                   std::vector<std::string> tickers) {
    const int id = next_id_.fetch_add(1);
    std::string j = R"({"id":)" + std::to_string(id)
                  + R"(,"cmd":"update_subscription","params":{"sids":[)" + std::to_string(sid)
                  + R"(],"action":")" + action
                  + R"(","market_tickers":)" + json_str_array(tickers) + "}}";
    enqueue(std::move(j));
    return id;
}
