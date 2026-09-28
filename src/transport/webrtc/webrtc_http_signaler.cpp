#include <magpie/transport/webrtc_http_signaler.hpp>

#include <magpie/utils/common.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace magpie {
namespace {

std::string encodeId(const std::string& value) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    for (std::size_t i = 0; i < value.size(); i += 3) {
        const auto a = static_cast<unsigned char>(value[i]);
        const auto b = i + 1 < value.size() ? static_cast<unsigned char>(value[i + 1]) : 0;
        const auto c = i + 2 < value.size() ? static_cast<unsigned char>(value[i + 2]) : 0;
        result += alphabet[a >> 2];
        result += alphabet[((a & 3) << 4) | (b >> 4)];
        if (i + 1 < value.size()) result += alphabet[((b & 15) << 2) | (c >> 6)];
        if (i + 2 < value.size()) result += alphabet[c & 63];
    }
    return result;
}

struct Response {
    long status{0};
    std::vector<std::uint8_t> body;
    std::string sequence;
};

size_t receiveBody(char* ptr, size_t size, size_t count, void* user) {
    const auto length = size * count;
    auto& body = static_cast<Response*>(user)->body;
    body.insert(body.end(), reinterpret_cast<std::uint8_t*>(ptr),
                reinterpret_cast<std::uint8_t*>(ptr) + length);
    return length;
}

size_t receiveHeader(char* ptr, size_t size, size_t count, void* user) {
    const auto length = size * count;
    constexpr char key[] = "X-Magpie-Sequence:";
    std::string line(ptr, length);
    if (line.size() >= sizeof(key) - 1 &&
        std::equal(line.begin(), line.begin() + sizeof(key) - 1, key,
                   [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                                std::tolower(static_cast<unsigned char>(b)); })) {
        auto value = line.substr(sizeof(key) - 1);
        const auto first = value.find_first_not_of(" \t");
        if (first != std::string::npos) {
            const auto last = value.find_last_not_of(" \t\r\n");
            static_cast<Response*>(user)->sequence = value.substr(first, last - first + 1);
        }
    }
    return length;
}

int progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return *static_cast<std::atomic<bool>*>(user) ? 1 : 0;
}

} // namespace

struct HttpSignaler::Impl {
    struct Pending {
        std::vector<std::uint8_t> body;
        std::string id;
    };

    std::string sessionId;
    std::string peerUrl;
    HttpSignalerOptions options;
    std::atomic<bool> stopped{false};
    bool closed{false};
    bool started{false};
    std::mutex mutex;
    std::mutex registerMutex;
    std::condition_variable wake;
    MessageCallback callback;
    std::deque<Pending> outgoing;
    std::thread sender;
    std::thread poller;

    Impl(std::string baseUrl, std::string session, HttpSignalerOptions opts)
        : sessionId(std::move(session)), options(std::move(opts)) {
        if (sessionId.empty() || (baseUrl.rfind("http://", 0) != 0 &&
                                  baseUrl.rfind("https://", 0) != 0) ||
            baseUrl.find_first_of("?#") != std::string::npos)
            throw std::invalid_argument("HttpSignaler: valid base URL and session ID required");
        if (options.pollWait <= 0 || options.requestTimeout <= 0)
            throw std::invalid_argument("HttpSignaler: timeouts must be positive");
        while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
        const auto peer = options.participantId.empty() ? getUniqueId() : options.participantId;
        peerUrl = baseUrl + "/sessions/" + encodeId(sessionId) + "/peers/" + encodeId(peer);
    }

    Response request(const std::string& method, const std::string& url,
                     const std::vector<std::uint8_t>* body = nullptr,
                     const std::string& messageId = {}, double timeout = 0) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("HttpSignaler: curl_easy_init failed");
        curl_slist* headers = nullptr;
        Response response;
        try {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                             static_cast<long>(std::ceil((timeout > 0 ? timeout : options.requestTimeout) * 1000)));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receiveHeader);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopped);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            if (!options.proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, options.proxy.c_str());
            if (!options.caBundle.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, options.caBundle.c_str());
            auto fields = options.headers;
            if (options.headersProvider) {
                for (auto& item : options.headersProvider()) fields[item.first] = item.second;
            }
            for (const auto& item : fields)
                headers = curl_slist_append(headers, (item.first + ": " + item.second).c_str());
            if (!messageId.empty())
                headers = curl_slist_append(headers, ("X-Magpie-Message-Id: " + messageId).c_str());
            if (body) {
                headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->empty() ? "" : reinterpret_cast<const char*>(body->data()));
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body->size()));
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            const auto code = curl_easy_perform(curl);
            if (code != CURLE_OK) throw std::runtime_error(std::string("HttpSignaler: ") + curl_easy_strerror(code));
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
        } catch (...) {
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            throw;
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    void registerPeer() {
        std::lock_guard<std::mutex> lock(registerMutex);
        auto response = request("PUT", peerUrl);
        if (response.status == 409) throw std::runtime_error("HttpSignaler: session is full (409)");
        if (response.status != 204) throw std::runtime_error("HttpSignaler: registration failed (HTTP " + std::to_string(response.status) + ")");
    }

    void sendLoop() {
        while (!stopped) {
            Pending pending;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopped || !outgoing.empty(); });
                if (stopped) break;
                pending = outgoing.front();
            }
            try {
                auto response = request("POST", peerUrl + "/messages", &pending.body, pending.id);
                if (response.status == 404) registerPeer();
                else if (response.status == 204) {
                    std::lock_guard<std::mutex> lock(mutex);
                    outgoing.pop_front();
                    continue;
                }
            } catch (...) {}
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(500), [this] { return stopped.load(); });
        }
    }

    void pollLoop() {
        std::uint64_t cursor = 0;
        while (!stopped) {
            try {
                const auto url = peerUrl + "/messages?after=" + std::to_string(cursor) +
                                 "&wait=" + std::to_string(options.pollWait);
                auto response = request("GET", url, nullptr, {},
                                        std::max(options.requestTimeout, options.pollWait + 5.0));
                if (response.status == 404) { registerPeer(); cursor = 0; continue; }
                if (response.status == 204) continue;
                if (response.status != 200 || response.sequence.empty())
                    throw std::runtime_error("HttpSignaler: invalid poll response");
                const auto sequence = std::stoull(response.sequence);
                if (sequence <= cursor) continue;
                MessageCallback receiver;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    receiver = callback;
                }
                if (receiver) receiver(response.body.data(), response.body.size());
                cursor = sequence;
                continue;
            } catch (...) {}
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(500), [this] { return stopped.load(); });
        }
    }
};

HttpSignaler::HttpSignaler(std::string baseUrl, std::string sessionId, HttpSignalerOptions options)
    : impl_(std::make_unique<Impl>(std::move(baseUrl), std::move(sessionId), std::move(options))) {
    static std::once_flag init;
    std::call_once(init, [] { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("HttpSignaler: curl_global_init failed"); });
}

HttpSignaler::~HttpSignaler() { disconnect(); }
const std::string& HttpSignaler::sessionId() const noexcept { return impl_->sessionId; }

void HttpSignaler::publish(const std::uint8_t* data, std::size_t size) {
    if (!data && size) throw std::invalid_argument("HttpSignaler: null payload");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->started || impl_->closed) throw std::runtime_error("HttpSignaler: not connected");
    std::vector<std::uint8_t> bytes;
    if (size) bytes.assign(data, data + size);
    impl_->outgoing.push_back({std::move(bytes), getUniqueId()});
    impl_->wake.notify_one();
}

void HttpSignaler::subscribe(MessageCallback callback) {
    if (!callback) throw std::invalid_argument("HttpSignaler: callback is empty");
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->closed) throw std::runtime_error("HttpSignaler: disconnected");
        if (impl_->started) { impl_->callback = std::move(callback); return; }
    }
    impl_->registerPeer();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->callback = std::move(callback);
        impl_->started = true;
    }
    impl_->sender = std::thread([this] { impl_->sendLoop(); });
    impl_->poller = std::thread([this] { impl_->pollLoop(); });
}

void HttpSignaler::unsubscribe() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->callback = {};
}

void HttpSignaler::disconnect() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->closed) return;
        impl_->closed = true;
        impl_->stopped = true;
        impl_->callback = {};
    }
    impl_->wake.notify_all();
    if (impl_->sender.joinable()) impl_->sender.join();
    if (impl_->poller.joinable()) impl_->poller.join();
    if (impl_->started) {
        impl_->stopped = false; // permit the final best-effort DELETE request
        try { impl_->request("DELETE", impl_->peerUrl); } catch (...) {}
    }
}

} // namespace magpie
