#include "sdk/include/network.h"

#include "httplib.h"

namespace nyx {
namespace sdk {
namespace net {

namespace {

bool starts_with(const std::string& text, const char* prefix) {
    const std::string view(prefix);
    return text.size() >= view.size() && text.compare(0, view.size(), view) == 0;
}

std::string path_or_root(const std::string& endpoint) {
    if (endpoint.empty()) {
        return "/";
    }

    if (endpoint.front() == '/') {
        return endpoint;
    }

    return "/" + endpoint;
}

std::string redacted_url(const std::string& url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return "<invalid-url>";
    }

    const std::size_t authority_start = scheme_end + 3;
    const std::size_t authority_end = url.find_first_of("/?#", authority_start);
    std::string authority = url.substr(
        authority_start,
        authority_end == std::string::npos ? std::string::npos : authority_end - authority_start
    );
    const std::size_t credentials = authority.find('@');
    if (credentials != std::string::npos) {
        authority = "<redacted>@" + authority.substr(credentials + 1);
    }

    return url.substr(0, authority_start) + authority;
}

std::string redacted_endpoint(const std::string& endpoint) {
    const std::string path = path_or_root(endpoint);
    const std::size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos || last_slash + 1 >= path.size()) {
        return path;
    }

    return path.substr(0, last_slash + 1) + "<redacted>";
}

int timeout_or_default(int value, int fallback) {
    return value > 0 ? value : fallback;
}

void configure_client(httplib::Client& client, int conn_timeout, int read_timeout, CaBundle ca) {
    if (ca.pem != nullptr && ca.size > 0) {
        client.load_ca_cert_store(ca.pem, ca.size);
    }
    client.set_ca_cert_path("", "/system/etc/security/cacerts");
    client.enable_system_ca(true);
    client.set_connection_timeout(timeout_or_default(conn_timeout, 3), 0);
    client.set_read_timeout(timeout_or_default(read_timeout, 5), 0);
    client.set_follow_location(true);
    client.enable_server_certificate_verification(true);
    client.enable_server_hostname_verification(true);
}

bool has_valid_url(const std::string& url, HttpResponse* response, const char* tag) {
    if (utils::IsMain()) {
        response->error_code = kMainThreadRequest;
        NYX_LOGE("%s rejected a main-thread request", tag);
        return false;
    }

    if (url.empty()) {
        response->error_code = kInvalidUrl;
        NYX_LOGE("%s rejected an empty url", tag);
        return false;
    }

    if (!starts_with(url, "https://")) {
        response->error_code = kInsecureUrl;
        NYX_LOGE("%s requires https url: %s", tag, redacted_url(url).c_str());
        return false;
    }

    return true;
}

bool has_valid_client(httplib::Client& client, const std::string& url, HttpResponse* response, const char* tag) {
    if (!client.is_valid()) {
        response->error_code = kInvalidClient;
        NYX_LOGE("%s failed to create client: %s", tag, redacted_url(url).c_str());
        return false;
    }

    return true;
}

} // namespace

HttpResponse GetConfig(
    const std::string& url,
    const std::string& endpoint,
    int conn_timeout,
    int read_timeout,
    CaBundle ca
) {
    HttpResponse response;

    if (!has_valid_url(url, &response, "GetConfig")) {
        return response;
    }

    httplib::Client client(url);
    if (!has_valid_client(client, url, &response, "GetConfig")) {
        return response;
    }

    configure_client(client, conn_timeout, read_timeout, ca);

    const auto result = client.Get(path_or_root(endpoint));
    if (!result) {
        const auto error = result.error();
        const std::string safe_url = redacted_url(url);
        const std::string safe_endpoint = redacted_endpoint(endpoint);
        response.error_code = static_cast<int>(error);
        NYX_LOGW(
            "GetConfig failed url=%s endpoint=%s error=%d(%s) ssl=%d backend=%llu",
            safe_url.c_str(),
            safe_endpoint.c_str(),
            response.error_code,
            httplib::to_string(error).c_str(),
            result.ssl_error(),
            static_cast<unsigned long long>(result.ssl_backend_error())
        );
        return response;
    }

    response.status = result->status;
    response.body = result->body;
    if (response.status < 200 || response.status >= 300) {
        response.error_code = response.status;
    }

    return response;
}

HttpResponse Post(
    const std::string& url,
    const std::string& endpoint,
    const std::string& body,
    int conn_timeout,
    int read_timeout,
    std::size_t max_body_len,
    CaBundle ca
) {
    HttpResponse response;

    if (!has_valid_url(url, &response, "Post")) {
        return response;
    }

    if (max_body_len > 0 && body.size() > max_body_len) {
        response.error_code = kBodyTooLarge;
        NYX_LOGE("Post rejected an oversized body: len=%zu limit=%zu", body.size(), max_body_len);
        return response;
    }

    httplib::Client client(url);
    if (!has_valid_client(client, url, &response, "Post")) {
        return response;
    }

    configure_client(client, conn_timeout, read_timeout, ca);
    client.set_payload_max_length(max_body_len > 0 ? max_body_len : 65536);

    httplib::Headers headers;
    headers.emplace("Accept", "text/plain, application/json, */*");
    headers.emplace("Connection", "close");
    headers.emplace("User-Agent", "nyx-auth/1.0");

    const auto result = client.Post(
        path_or_root(endpoint),
        headers,
        body,
        "application/x-www-form-urlencoded"
    );
    if (!result) {
        const auto error = result.error();
        const std::string safe_url = redacted_url(url);
        const std::string safe_endpoint = redacted_endpoint(endpoint);
        response.error_code = static_cast<int>(error);
        NYX_LOGW(
            "Post failed url=%s endpoint=%s body_len=%zu error=%d(%s) ssl=%d backend=%llu",
            safe_url.c_str(),
            safe_endpoint.c_str(),
            body.size(),
            response.error_code,
            httplib::to_string(error).c_str(),
            result.ssl_error(),
            static_cast<unsigned long long>(result.ssl_backend_error())
        );
        return response;
    }

    response.status = result->status;
    response.body = result->body;
    if (response.status < 200 || response.status >= 300) {
        response.error_code = response.status;
    }

    return response;
}

} // namespace net
} // namespace sdk
} // namespace nyx
