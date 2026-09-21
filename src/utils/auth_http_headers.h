#pragma once

#include <string>
#include <curl/curl.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "libavutil/dict.h"

#ifdef __cplusplus
}
#endif

struct curl_slist;

namespace pag {
namespace auth_headers {

//CAUTION: replace the cookie string periodically
inline std::string getFullCookieString() {
    return "bd_ticket_guard_client_web_domain=2; "
           "n_mh=U86sJaoBrIABfaJ-bnpFZRgNLneYTbvNWUNsYmwjjbk; "
           "uid_tt=dea6205236f71a5d2db5f18e82655cdc; "
           "uid_tt_ss=dea6205236f71a5d2db5f18e82655cdc; "
           "is_hit_partitioned_cookie_canary_ss=true; "
           "sid_tt=42b822315a47ccaf413f73ac3d3d38c3; "
           "sessionid=42b822315a47ccaf413f73ac3d3d38c3; "
           "sessionid_ss=42b822315a47ccaf413f73ac3d3d38c3; "
           "is_staff_user=false; "
           "has_biz_token=false; "
           "is_hit_partitioned_cookie_canary=true; "
           "_bd_ticket_crypt_doamin=3; "
           "__security_server_data_status=1; "
           "gr_user_id=1d7bebc2-0bde-41d8-aedc-d12a827fc7f5; "
           "grwng_uid=9c7cc934-2aab-4e2a-baed-dd3b4cd983f2; "
           "passport_csrf_token=1953324aae857de6c314fed1155e8653; "
           "passport_csrf_token_default=1953324aae857de6c314fed1155e8653; "
           "__security_mc_61_s_sdk_crypt_sdk=64f33261-43bd-a3cd; "
           "_bd_ticket_crypt_cookie=f15c64c594f7747ad9cc7f5794a1d106; "
           "__security_mc_61_s_sdk_sign_data_key_web_protect=adbcbff5-41d8-9595; "
           "bd_ticket_guard_client_data=eyJiZC10aWNrZXQtZ3VhcmQtdmVyc2lvbiI6MiwiYmQtdGlja2V0LWd1YXJkLWl0ZXJhdGlvbi12ZXJzaW9uIjoxLCJiZC10aWNrZXQtZ3VhcmQtcmVlLXB1YmxpYy1rZXkiOiJCQXhOWTR0bzV6cnY2UjlpVnR3TUlEN0hVY3VHajdibHhPajhwZTZKdkhIcW9KT1JvV2hCRTZoZmY5dmN3NDBWOHpxdnFyeXM0NUs2SEVubm1JNHdlRk09IiwiYmQtdGlja2V0LWd1YXJkLXdlYi12ZXJzaW9uIjoyfQ%3D%3D; "
           "odin_tt=895cd782c044a7a1c2858b296131a939f443d7f16fb8bf63b46c6fb105498bef92c37117f048867c50547b76f433e197fc51f1b71d8cab9bb401859d4c30db49; "
           "sid_guard=42b822315a47ccaf413f73ac3d3d38c3%7C1786948582%7C5184000%7CFri%2C+16-Oct-2026+06%3A36%3A22+GMT; "
           "session_tlb_tag=sttt%7C6%7CQrgiMVpHzK9BP3OsPT04w_________-kr4ngZteb-a2XAJWzyp5UPpWjjpgJuR7lMCJmLTe0RiU%3D; "
           "sid_ucp_v1=1.0.0-KDlkMjg0Y2E2ZTRiZWI5NTI4OGM3ZTJlZDUwZWVjZmIzNjQ2ZWUzMmQKHAi6_vCQ4c2bAhDm14rUBhjkzTQgDDgGQPQHSAQaAmxmIiA0MmI4MjIzMTVhNDdjY2FmNDEzZjczYWMzZDNkMzhjMw; "
           "ssid_ucp_v1=1.0.0-KDlkMjg0Y2E2ZTRiZWI5NTI4OGM3ZTJlZDUwZWVjZmIzNjQ2ZWUzMmQKHAi6_vCQ4c2bAhDm14rUBhjkzTQgDDgGQPQHSAQaAmxmIiA0MmI4MjIzMTVhNDdjY2FmNDEzZjczYWMzZDNkMzhjMw; "
           "trace_log_adv_id=; "
           "trace_log_user_id=undefined; "
           "aefa4e5d2593305f_gr_last_sent_cs1=1874099548921305; "
           "aefa4e5d2593305f_gr_cs1=1874099548921305; "
           "ttwid=1%7CiiMAXaWXt_pefb-0lfhQTDlbe5a6-BWzH9GomHvXrFk%7C1789379428%7Ca6dfd65159a817559c1690b9f2a3f413cc374fa3b412e74e714d2a7c515c02c9";
}

inline const std::string& getChromeUserAgent() {
    static const std::string ua = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36";
    return ua;
}

inline struct curl_slist* buildLightHttpHeadersCurl() {
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("User-Agent: " + getChromeUserAgent()).c_str());
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    return headers;
}

inline struct curl_slist* buildFullHttpHeadersCurl(const std::string& url) {
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    headers = curl_slist_append(headers, "Pragma: no-cache");
    headers = curl_slist_append(headers, "Range: bytes=0-");
    headers = curl_slist_append(headers, ("Referer: " + url).c_str());
    headers = curl_slist_append(headers, "Sec-Fetch-Dest: video");
    headers = curl_slist_append(headers, "Sec-Fetch-Mode: cors");
    headers = curl_slist_append(headers, "Sec-Fetch-Site: same-origin");
    headers = curl_slist_append(headers, ("User-Agent: " + getChromeUserAgent()).c_str());
    headers = curl_slist_append(headers, "sec-ch-ua: \"Chromium\";v=\"152\", \"Not?A_Brand\";v=\"24\", \"Google Chrome\";v=\"152\"");
    headers = curl_slist_append(headers, "sec-ch-ua-mobile: ?0");
    headers = curl_slist_append(headers, "sec-ch-ua-platform: \"macOS\"");
    return headers;
}

inline void addFFmpegStableOptions(AVDictionary** opts) {
    av_dict_set(opts, "seekable", "1", 0);
    av_dict_set(opts, "multiple_requests", "1", 0);
    av_dict_set(opts, "reconnect", "1", 0);
    av_dict_set(opts, "reconnect_streamed", "1", 0);
    av_dict_set(opts, "reconnect_on_network_error", "1", 0);
    av_dict_set(opts, "reconnect_on_http_error", "4xx,5xx", 0);
    av_dict_set(opts, "reconnect_delay_max", "3", 0);
    av_dict_set(opts, "rw_timeout", "30000000", 0);
}

inline void buildLightFFmpegOptions(AVDictionary** opts) {
    av_dict_set(opts, "user_agent", getChromeUserAgent().c_str(), 0);
    av_dict_set(opts, "headers",
        "Accept: */*\r\n"
        "Accept-Language: zh-CN,zh;q=0.9\r\n"
        "Connection: keep-alive\r\n", 0);
    addFFmpegStableOptions(opts);
}

inline void buildFullFFmpegOptions(const std::string& url, AVDictionary** opts) {
    std::string headers;
    headers += "Accept: */*\r\n";
    headers += "Accept-Language: zh-CN,zh;q=0.9\r\n";
    headers += "Cache-Control: no-cache\r\n";
    headers += "Connection: keep-alive\r\n";
    headers += "Pragma: no-cache\r\n";
    headers += "Range: bytes=0-\r\n";
    headers += "Referer: " + url + "\r\n";
    headers += "Sec-Fetch-Dest: video\r\n";
    headers += "Sec-Fetch-Mode: cors\r\n";
    headers += "Sec-Fetch-Site: same-origin\r\n";
    headers += "sec-ch-ua: \"Chromium\";v=\"152\", \"Not?A_Brand\";v=\"24\", \"Google Chrome\";v=\"152\"\r\n";
    headers += "sec-ch-ua-mobile: ?0\r\n";
    headers += "sec-ch-ua-platform: \"macOS\"\r\n";
    headers += "Cookie: " + getFullCookieString() + "\r\n";

    av_dict_set(opts, "user_agent", getChromeUserAgent().c_str(), 0);
    av_dict_set(opts, "headers", headers.c_str(), 0);
    addFFmpegStableOptions(opts);
}

}  // namespace auth_headers
}  // namespace pag
