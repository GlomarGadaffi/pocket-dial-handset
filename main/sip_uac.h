#pragma once
#include <string>

// Where the peer wants its RTP — learned from the 200 OK SDP (c=/m=).
// With pocket-dial this is ext 102's OWN media endpoint, so RTP is peer-to-peer.
struct SipRemoteMedia {
    std::string ip;
    int         port = 0;
};

// Minimal SIP User Agent Client for the spike:
//   REGISTER <selfExt> (no auth) -> INVITE <calleeExt> via the server ->
//   200 OK -> ACK, then BYE. The server proxies signaling; media is P2P.
class SipUac {
public:
    SipUac(std::string localIp, int localSipPort, int localRtpPort,
           std::string serverIp, int serverPort,
           std::string selfExt, std::string calleeExt);

    // Bind the local socket and REGISTER our extension. Returns true on 200 OK.
    bool registerExt();

    // INVITE the callee through the server; on 200 OK fill `out` with the
    // peer's RTP endpoint and ACK. Call registerExt() first.
    bool placeCall(SipRemoteMedia &out);

    // Tear the dialog down (BYE through the server).
    void hangup();

private:
    bool openSocket();
    std::string buildRegister() const;
    std::string buildInvite()   const;
    std::string buildAck()      const;
    std::string buildBye()      const;

    std::string _localIp, _serverIp, _selfExt, _calleeExt;
    int _localSipPort, _localRtpPort, _serverPort;

    int         _sock = -1;
    std::string _callId, _fromTag, _remoteTag;
};
