#include "sip_uac.h"
#include "poc_config.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "esp_log.h"

#include <cstdio>
#include <cstring>

// Vendored pocket-dial SIP layer (MIT) — used to PARSE responses + SDP.
#include "SipMessageFactory.hpp"
#include "SipSdpMessage.hpp"
#include "SipMessageTypes.h"
#include "IDGen.hpp"

static const char *TAG = "sip_uac";

// ── helpers ────────────────────────────────────────────────────────────────
static int statusCode(std::string_view type)
{
    if (type.rfind("SIP/2.0 ", 0) != 0) return -1;   // -1 => not a response
    int code = 0;
    for (size_t i = 8; i < type.size() && type[i] >= '0' && type[i] <= '9'; ++i)
        code = code * 10 + (type[i] - '0');
    return code;
}

static std::string extractParam(std::string_view header, const char *key)
{
    size_t p = header.find(key);
    if (p == std::string_view::npos) return {};
    p += std::strlen(key);
    size_t e = p;
    while (e < header.size() && header[e] != ';' && header[e] != '>' &&
           header[e] != '\r' && header[e] != '\n' && header[e] != ' ')
        ++e;
    return std::string(header.substr(p, e - p));
}

static std::string ipFromConnection(std::string_view conn)
{
    size_t sp = conn.rfind(' ');
    return (sp == std::string_view::npos) ? std::string(conn)
                                          : std::string(conn.substr(sp + 1));
}

static std::string randBranch() { return "z9hG4bK" + IDGen::GenerateID(10); }

// ── ctor ─────────────────────────────────────────────────────────────────--
SipUac::SipUac(std::string localIp, int localSipPort, int localRtpPort,
               std::string serverIp, int serverPort,
               std::string selfExt, std::string calleeExt)
    : _localIp(std::move(localIp)), _serverIp(std::move(serverIp)),
      _selfExt(std::move(selfExt)), _calleeExt(std::move(calleeExt)),
      _localSipPort(localSipPort), _localRtpPort(localRtpPort), _serverPort(serverPort)
{
    _callId  = IDGen::GenerateID(16);
    _fromTag = IDGen::GenerateID(8);
}

bool SipUac::openSocket()
{
    if (_sock >= 0) return true;
    _sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_sock < 0) { ESP_LOGE(TAG, "socket() failed"); return false; }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(_localSipPort);
    bind(_sock, (sockaddr *)&local, sizeof(local));

    struct timeval tv = { .tv_sec = 0, .tv_usec = 500 * 1000 };   // 500 ms poll
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return true;
}

// ── message builders (all routed at the server) ────────────────────────────
std::string SipUac::buildRegister() const
{
    char msg[768];
    std::snprintf(msg, sizeof(msg),
        "REGISTER sip:%s:%d SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: reg-%s@%s\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Contact: <sip:%s@%s:%d;transport=UDP>\r\n"
        "Expires: %d\r\n"
        "Content-Length: 0\r\n\r\n",
        _serverIp.c_str(), _serverPort,
        _localIp.c_str(), _localSipPort, randBranch().c_str(),
        _selfExt.c_str(), _serverIp.c_str(), _fromTag.c_str(),
        _selfExt.c_str(), _serverIp.c_str(),
        _callId.c_str(), _localIp.c_str(),
        _selfExt.c_str(), _localIp.c_str(), _localSipPort,
        POC_SIP_REG_EXPIRES);
    return std::string(msg);
}

std::string SipUac::buildInvite() const
{
    char sdp[512];
    int sdpLen = std::snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=%s 0 0 IN IP4 %s\r\n"
        "s=pocketdial-voice-poc\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio %d RTP/AVP %d\r\n"
        "a=rtpmap:%d PCMU/8000\r\n"
        "a=sendrecv\r\n",
        _selfExt.c_str(), _localIp.c_str(), _localIp.c_str(),
        _localRtpPort, POC_RTP_PAYLOAD_PCMU, POC_RTP_PAYLOAD_PCMU);

    char msg[1280];
    std::snprintf(msg, sizeof(msg),
        "INVITE sip:%s@%s:%d SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %s@%s\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:%s@%s:%d;transport=UDP>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n%s",
        _calleeExt.c_str(), _serverIp.c_str(), _serverPort,
        _localIp.c_str(), _localSipPort, randBranch().c_str(),
        _selfExt.c_str(), _serverIp.c_str(), _fromTag.c_str(),
        _calleeExt.c_str(), _serverIp.c_str(),
        _callId.c_str(), _localIp.c_str(),
        _selfExt.c_str(), _localIp.c_str(), _localSipPort,
        sdpLen, sdp);
    return std::string(msg);
}

std::string SipUac::buildAck() const
{
    char msg[768];
    std::snprintf(msg, sizeof(msg),
        "ACK sip:%s@%s:%d SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>;tag=%s\r\n"
        "Call-ID: %s@%s\r\n"
        "CSeq: 1 ACK\r\n"
        "Content-Length: 0\r\n\r\n",
        _calleeExt.c_str(), _serverIp.c_str(), _serverPort,
        _localIp.c_str(), _localSipPort, randBranch().c_str(),
        _selfExt.c_str(), _serverIp.c_str(), _fromTag.c_str(),
        _calleeExt.c_str(), _serverIp.c_str(), _remoteTag.c_str(),
        _callId.c_str(), _localIp.c_str());
    return std::string(msg);
}

std::string SipUac::buildBye() const
{
    char msg[768];
    std::snprintf(msg, sizeof(msg),
        "BYE sip:%s@%s:%d SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>;tag=%s\r\n"
        "Call-ID: %s@%s\r\n"
        "CSeq: 2 BYE\r\n"
        "Content-Length: 0\r\n\r\n",
        _calleeExt.c_str(), _serverIp.c_str(), _serverPort,
        _localIp.c_str(), _localSipPort, randBranch().c_str(),
        _selfExt.c_str(), _serverIp.c_str(), _fromTag.c_str(),
        _calleeExt.c_str(), _serverIp.c_str(), _remoteTag.c_str(),
        _callId.c_str(), _localIp.c_str());
    return std::string(msg);
}

// ── REGISTER ─────────────────────────────────────────────────────────────--
bool SipUac::registerExt()
{
    if (!openSocket()) return false;

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(_serverIp.c_str());
    srv.sin_port = htons(_serverPort);

    const std::string reg = buildRegister();
    ESP_LOGI(TAG, "REGISTER %s -> %s:%d", _selfExt.c_str(), _serverIp.c_str(), _serverPort);

    char rbuf[2048];
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (attempt % 2 == 0)
            sendto(_sock, reg.data(), reg.size(), 0, (sockaddr *)&srv, sizeof(srv));

        sockaddr_in src{};
        socklen_t sl = sizeof(src);
        int n = recvfrom(_sock, rbuf, sizeof(rbuf) - 1, 0, (sockaddr *)&src, &sl);
        if (n <= 0) continue;
        rbuf[n] = '\0';

        SipMessageFactory factory;
        auto parsed = factory.createMessage(std::string(rbuf, n), src);
        if (!parsed.has_value()) continue;
        int code = statusCode(parsed.value()->getType());
        if (code == 200) { ESP_LOGI(TAG, "registered as %s", _selfExt.c_str()); return true; }
        if (code >= 400) { ESP_LOGE(TAG, "REGISTER rejected (%d)", code); return false; }
    }
    ESP_LOGW(TAG, "no REGISTER response from %s:%d", _serverIp.c_str(), _serverPort);
    return false;
}

// ── INVITE (via server) ─────────────────────────────────────────────────--
bool SipUac::placeCall(SipRemoteMedia &out)
{
    if (!openSocket()) return false;

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(_serverIp.c_str());
    srv.sin_port = htons(_serverPort);

    const std::string invite = buildInvite();
    ESP_LOGI(TAG, "INVITE %s via %s:%d", _calleeExt.c_str(), _serverIp.c_str(), _serverPort);

    char rbuf[2048];
    for (int attempt = 0; attempt < 24; ++attempt) {
        if (attempt % 2 == 0)
            sendto(_sock, invite.data(), invite.size(), 0, (sockaddr *)&srv, sizeof(srv));

        sockaddr_in src{};
        socklen_t sl = sizeof(src);
        int n = recvfrom(_sock, rbuf, sizeof(rbuf) - 1, 0, (sockaddr *)&src, &sl);
        if (n <= 0) continue;
        rbuf[n] = '\0';

        SipMessageFactory factory;
        auto parsed = factory.createMessage(std::string(rbuf, n), src);
        if (!parsed.has_value()) continue;
        auto msg = parsed.value();

        int code = statusCode(msg->getType());
        ESP_LOGI(TAG, "<- %.*s", (int)msg->getType().size(), msg->getType().data());

        if (code == 100 || code == 180 || code == 183) continue;   // provisional
        if (code == 200) {
            _remoteTag = extractParam(msg->getTo(), ";tag=");
            if (msg->hasSdp()) {
                auto *sdp = static_cast<SipSdpMessage *>(msg.get());   // RTTI off
                out.port = sdp->getRtpPort();
                out.ip   = ipFromConnection(sdp->getConnectionInformation());
            }
            std::string ack = buildAck();
            sendto(_sock, ack.data(), ack.size(), 0, (sockaddr *)&srv, sizeof(srv));
            ESP_LOGI(TAG, "200 OK -> ACK; P2P media to %s:%d", out.ip.c_str(), out.port);
            return out.port > 0 && !out.ip.empty();
        }
        if (code >= 400) {
            ESP_LOGW(TAG, "call to %s rejected (%d)", _calleeExt.c_str(), code);
            return false;
        }
    }
    ESP_LOGW(TAG, "no final response to INVITE (is %s registered?)", _calleeExt.c_str());
    return false;
}

void SipUac::hangup()
{
    if (_sock < 0) return;
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(_serverIp.c_str());
    srv.sin_port = htons(_serverPort);
    std::string bye = buildBye();
    sendto(_sock, bye.data(), bye.size(), 0, (sockaddr *)&srv, sizeof(srv));
    ESP_LOGI(TAG, "BYE sent");
    close(_sock);
    _sock = -1;
}
