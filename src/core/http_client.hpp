#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace wam {

struct HttpResponse {
    long status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
    // header lines like "x-api-key: xyz"
    static HttpResponse get(const std::string& url, const std::vector<std::string>& headers = {});

    // Streams the response body straight to destPath. Returns status only;
    // on failure the partially-written file is removed.
    static HttpResponse downloadToFile(const std::string& url,
                                        const std::filesystem::path& destPath,
                                        const std::vector<std::string>& headers = {});
};

} // namespace wam
