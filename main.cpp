#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using json = nlohmann::json;


size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *buffer = static_cast<std::string *>(userdata);
    buffer->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string UrlEncode(const std::string &value) {
    CURL *curl = curl_easy_init();
    if (!curl) return "";
    char *out = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string encoded = out ? out : "";
    if (out) curl_free(out);
    curl_easy_cleanup(curl);
    return encoded;
}

std::string HttpGet(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) return "";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "simple-telegram-bot/1.0");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return "";
    return response;
}

void SendMessage(const std::string &token, long long chatId, const std::string &text) {
    std::string url = "https://api.telegram.org/bot" + token +
                      "/sendMessage?chat_id=" + std::to_string(chatId) +
                      "&text=" + UrlEncode(text) +
                      "&disable_web_page_preview=true";
    HttpGet(url);
}

bool IsAllowed(long long chatId, long long adminId) {
    return (adminId == 0) || (chatId == adminId);
}

void LoadEnvFile(const std::string &path) {
    std::ifstream file(path);
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (!key.empty() && key.back() == '\r') key.pop_back();
        if (!value.empty() && value.back() == '\r') value.pop_back();
        if (!key.empty() && key.front() == '\xEF') key = key.substr(3);
#ifdef _WIN32
        for (auto &ch : value) {
            if (ch == '\t') ch = ' ';
        }
#endif
        #ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }
}

int main() {
    LoadEnvFile(".env");
    const char *envToken = std::getenv("TELEGRAM_BOT_TOKEN");
    if (!envToken || std::string(envToken).empty()) {
        std::cout << "Set TELEGRAM_BOT_TOKEN env var.\n";
        return 1;
    }
    std::string kBotToken = envToken;
    long long adminId = 0;
    if (const char *envAdmin = std::getenv("TELEGRAM_ADMIN_ID")) {
        adminId = std::atoll(envAdmin);
    }
    std::cout << "Bot started. adminId=" << adminId << "\n";

    curl_global_init(CURL_GLOBAL_DEFAULT);

    long long offset = 0;
    while (true) {
        std::string url = "https://api.telegram.org/bot" + kBotToken +
                          "/getUpdates?timeout=25&offset=" + std::to_string(offset);
        std::string body = HttpGet(url);
        if (body.empty()) {
            std::cout << "HTTP empty response\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            continue;
        }

        json data = json::parse(body, nullptr, false);
        if (data.is_discarded() || !data.value("ok", false)) {
            std::cout << "Bad JSON or ok=false. Body: ";
            std::cout << body.substr(0, 300) << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            continue;
        }

        for (const auto &item : data["result"]) {
            offset = item.value("update_id", 0LL) + 1;
            if (!item.contains("message")) continue;
            const auto &msg = item["message"];
            if (!msg.contains("chat")) continue;
            long long chatId = msg["chat"].value("id", 0LL);
            if (!msg.contains("text")) continue;

            std::string text = msg.value("text", "");
            std::cout << "Message from " << chatId << ": " << text << "\n";
            if (!IsAllowed(chatId, adminId)) {
                SendMessage(kBotToken, chatId, "Access denied.");
                continue;
            }

            if (text == "/start") {
                SendMessage(kBotToken, chatId, "Bot is online. Commands: /ping /help");
            } else if (text == "/ping") {
                SendMessage(kBotToken, chatId, "pong");
            } else if (text == "/help") {
                SendMessage(kBotToken, chatId, "Commands: /ping /help");
            } else {
                SendMessage(kBotToken, chatId, "Echo: " + text);
            }
        }
    }

    curl_global_cleanup();
    return 0;
}
