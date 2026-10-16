#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace wam {

struct HttpResponse {
    long status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

// What a request asked for, as the test transport sees it.
// method is "GET", "POST" or "DOWNLOAD".
struct HttpRequest {
    std::string method;
    std::string url;
    std::vector<std::string> headers;
    std::string body;
};

// Every request is HTTPS only. get() and post() never follow redirects: they
// carry the API key, and curl would send it on to whatever host a redirect
// names. downloadToFile() follows redirects (CDN links do), so it must never
// be given the key.
class HttpClient {
public:
    // header lines like "x-api-key: xyz"
    static HttpResponse get(const std::string& url, const std::vector<std::string>& headers = {});

    // POST with a request body (e.g. JSON; pass the Content-Type header yourself).
    static HttpResponse post(const std::string& url, const std::string& body,
                              const std::vector<std::string>& headers = {});

    // Streams the response body straight to destPath. Returns status only;
    // on failure the partially-written file is removed.
    static HttpResponse downloadToFile(const std::string& url,
                                        const std::filesystem::path& destPath,
                                        const std::vector<std::string>& headers = {});

    // Tests set this to answer requests without the network. A DOWNLOAD's
    // response body is written to the file. Unset means real HTTP.
    static inline std::function<HttpResponse(const HttpRequest&)> transportForTesting;
};

} // namespace wam
