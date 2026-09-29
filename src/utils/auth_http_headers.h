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
           "is_hit_partitioned_cookie_canary_ss=true; "
           "is_staff_user=false; "
           "has_biz_token=false; "
           "is_hit_partitioned_cookie_canary=true; "
           "_tea_utm_cache_481911={%22utm_source%22:%2251%22}; "
           "_tea_utm_cache_1192={%22utm_source%22:%2251%22}; "
           "_tea_utm_cache_579056={%22utm_source%22:%2251%22}; "
           "_tea_utm_cache_3229={%22utm_source%22:%2251%22}; "
           "_bd_ticket_crypt_doamin=3; "
           "__security_server_data_status=1; "
           "gr_user_id=1d7bebc2-0bde-41d8-aedc-d12a827fc7f5; "
           "grwng_uid=9c7cc934-2aab-4e2a-baed-dd3b4cd983f2; "
           "passport_csrf_token=1953324aae857de6c314fed1155e8653; "
           "passport_csrf_token_default=1953324aae857de6c314fed1155e8653; "
           "_bd_ticket_crypt_cookie=f15c64c594f7747ad9cc7f5794a1d106; "
           "_tea_utm_cache_1574={%22utm_source%22:%2251%22}; "
           "_tea_utm_cache_592637={%22utm_source%22:%2251%22}; "
           "_tea_utm_cache_5474={%22utm_source%22:%2251%22}; "
           "tt_scid=SxId8LUmgdutcri6ELw-.eg5pMBuZCAZZnfMS4Q.RMTIoD0uuEjyZ3QOOb3Fhg2O42f5; "
           "x-web-secsdk-uid=a2287174-360e-4cbb-b1e3-d9772f300792; "
           "csrftoken=ik4tPekM8fDaS_j08MFDWH1t; "
           "s_v_web_id=verify_muciptvc_XllX1pmr_73nJ_4CdE_94ic_I4uziahX5TXf; "
           "trace_log_adv_id=; "
           "trace_log_user_id=undefined; "
           "aefa4e5d2593305f_gr_last_sent_cs1=1866413587468427; "
           "aefa4e5d2593305f_gr_cs1=1866413587468427; "
           "__security_mc_61_s_sdk_crypt_sdk=dc1a75c1-4fb1-a3b9; "
           "__security_mc_61_s_sdk_sign_data_key_web_protect=c9bf9aa5-4962-a4f6; "
           "bd_ticket_guard_client_data=eyJiZC10aWNrZXQtZ3VhcmQtdmVyc2lvbiI6MiwiYmQtdGlja2V0LWd1YXJkLWl0ZXJhdGlvbi12ZXJzaW9uIjoxLCJiZC10aWNrZXQtZ3VhcmQtcmVlLXB1YmxpYy1rZXkiOiJCR0daTWZHdXFZeXM2dWduRWVScDBhZVRwYkdMSk90QVBUakZMRFpBcnNnWllOWmFlK2cwN2JlYUFTQXlyR2tHbFlPVVB1UnJ1N2xmRlY1VVE1elNON0E9IiwiYmQtdGlja2V0LWd1YXJkLXdlYi12ZXJzaW9uIjoyfQ%3D%3D; "
           "ttwid=1%7CiiMAXaWXt_pefb-0lfhQTDlbe5a6-BWzH9GomHvXrFk%7C1790596288%7C9576d977cc92e1d292543ec90a09a34c64b3a5b17a4b84be545073c10e2848ac; "
           "sso_uid_tt=44bbfdc8fa972459d62420a47ea9608a; "
           "sso_uid_tt_ss=44bbfdc8fa972459d62420a47ea9608a; "
           "toutiao_sso_user=fa3314b72adcfd40d78e6d8421d8cc4a; "
           "toutiao_sso_user_ss=fa3314b72adcfd40d78e6d8421d8cc4a; "
           "sid_ucp_sso_v1=1.0.0-KDQxZTNiMWVkMzlkNWUyOGIwZWY3NjBjYjEzMmNiNWIxOTE3YzMzNjEKIgi6_vCQ4c2bAhDHqenVBhjkzTQgDDCXw4S_BjgGQPQHSAYaAmxmIiBmYTMzMTRiNzJhZGNmZDQwZDc4ZTZkODQyMWQ4Y2M0YQ; "
           "ssid_ucp_sso_v1=1.0.0-KDQxZTNiMWVkMzlkNWUyOGIwZWY3NjBjYjEzMmNiNWIxOTE3YzMzNjEKIgi6_vCQ4c2bAhDHqenVBhjkzTQgDDCXw4S_BjgGQPQHSAYaAmxmIiBmYTMzMTRiNzJhZGNmZDQwZDc4ZTZkODQyMWQ4Y2M0YQ; "
           "__security_mc_61_s_sdk_sign_data_key_sso=0dde0562-45e7-9ab5; "
           "__security_mc_61_s_sdk_cert_key=379a78ab-4434-905f; "
           "odin_tt=2028bd46129f3a0e781068b93d491b56556525aa00378da0799709b1abadb54077c040256828ecbec0d7c3d39f77d856a716b167c73e667785963bda6332410b; "
           "passport_auth_status=0eff53aa3ad5fdd7b934902e0f156ef4%2C; "
           "passport_auth_status_ss=0eff53aa3ad5fdd7b934902e0f156ef4%2C; "
           "bd_ticket_guard_server_data=eyJ0aWNrZXQiOiJoYXNoLmJOVUUzZHp2MytwdkJxcWpNS3A4SXRvTm1yUHRORnNJbUZZbktkR1RKTVU9IiwidHNfc2lnbiI6InRzLjIuYWMxMTdhZmJiYWZjMTYxMjc4ODA2Zjc3MWQ2YWVkMjgxOWY1YjQ3OWFkOTAwZDdiNmFmZmE2YzQ1YWEwMTRkN2M0ZmJlODdkMjMxOWNmMDUzMTg2MjRjZWRhMTQ5MTFjYTQwNmRlZGJlYmVkZGIyZTMwZmNlOGQ0ZmEwMjU3NWQiLCJjbGllbnRfY2VydCI6InB1Yi5CR0daTWZHdXFZeXM2dWduRWVScDBhZVRwYkdMSk90QVBUakZMRFpBcnNnWllOWmFlK2cwN2JlYUFTQXlyR2tHbFlPVVB1UnJ1N2xmRlY1VVE1elNON0E9IiwibG9nX2lkIjoiMjAyNjA5MjgxOTUxMzVFOTU5QTFGQTg0OTYyMkNGMkNCNyIsImNyZWF0ZV90aW1lIjoxNzkwNTk2Mjk1fQ%3D%3D; "
           "bd_ticket_guard_web_domain=3; "
           "sid_guard=4678e63719c215563c95d4831ea8a545%7C1790596295%7C5184002%7CFri%2C+27-Nov-2026+11%3A51%3A37+GMT; "
           "uid_tt=a4fb50c3aa936c36a217cf8f37b8b282; "
           "uid_tt_ss=a4fb50c3aa936c36a217cf8f37b8b282; "
           "sid_tt=4678e63719c215563c95d4831ea8a545; "
           "sessionid=4678e63719c215563c95d4831ea8a545; "
           "sessionid_ss=4678e63719c215563c95d4831ea8a545; "
           "session_tlb_tag=sttt%7C5%7CRnjmNxnCFVY8ldSDHqilRf_________LLqIiBYzXOn0pMNZ77wB5iTCAn_5MIRwZKhjV7bAF6UE%3D; "
           "sid_ucp_v1=1.0.0-KDc3MTEwMjQwYjZkMTE2ODg2Y2NlMDI0MjJkN2QzZWM1YWZmM2I4OGYKHAi6_vCQ4c2bAhDHqenVBhjkzTQgDDgGQPQHSAQaAmxxIiA0Njc4ZTYzNzE5YzIxNTU2M2M5NWQ0ODMxZWE4YTU0NQ; "
           "ssid_ucp_v1=1.0.0-KDc3MTEwMjQwYjZkMTE2ODg2Y2NlMDI0MjJkN2QzZWM1YWZmM2I4OGYKHAi6_vCQ4c2bAhDHqenVBhjkzTQgDDgGQPQHSAQaAmxxIiA0Njc4ZTYzNzE5YzIxNTU2M2M5NWQ0ODMxZWE4YTU0NQ";
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
