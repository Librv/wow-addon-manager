#include "core/http_client.hpp"
#include <curl/curl.h>
#include <fstream>
#include <stdexcept>

namespace wam {

namespace {

size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t writeToStream(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

curl_slist* buildHeaders(const std::vector<std::string>& headers) {
    curl_slist* list = nullptr;
    for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
    return list;
}

} // namespace

HttpResponse HttpClient::get(const std::string& url, const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to init curl handle");

    HttpResponse resp;
    curl_slist* hdrs = buildHeaders(headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wow-addon-manager/0.1");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status = code;
    } else {
        resp.status = 0;
        resp.body = curl_easy_strerror(res);
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body,
                               const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to init curl handle");

    HttpResponse resp;
    curl_slist* hdrs = buildHeaders(headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wow-addon-manager/0.1");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status = code;
    } else {
        resp.status = 0;
        resp.body = curl_easy_strerror(res);
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse HttpClient::downloadToFile(const std::string& url,
                                         const std::filesystem::path& destPath,
                                         const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to init curl handle");

    std::filesystem::create_directories(destPath.parent_path());
    std::ofstream out(destPath, std::ios::binary);

    HttpResponse resp;
    curl_slist* hdrs = buildHeaders(headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wow-addon-manager/0.1");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    out.close();

    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status = code;
    } else {
        resp.status = 0;
        resp.body = curl_easy_strerror(res);
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (!resp.ok()) std::filesystem::remove(destPath);
    return resp;
}

} // namespace wam
