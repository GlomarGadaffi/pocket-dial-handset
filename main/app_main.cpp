// ─────────────────────────────────────────────────────────────────────────
//  pocketdial-voice-poc  —  app_main
//
//  Flow: Wi-Fi -> bring up mic+speaker -> (motor "actuation") -> SIP INVITE to
//  a desktop softphone -> on 200 OK, run a PUSH-TO-TALK half-duplex G.711 loop:
//     PTT held   -> mic -> G.711 -> RTP  (talk)
//     PTT released-> RTP -> G.711 -> speaker (listen)
//  Half-duplex by design => no acoustic echo cancellation needed for the spike.
// ─────────────────────────────────────────────────────────────────────────
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/gpio.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cstring>

#include "poc_config.h"
#include "board_mvsr.h"
#include "net_wifi.h"
#include "audio_io.h"
#include "g711.h"
#include "rtp.h"
#include "sip_uac.h"

static const char *TAG = "app";

static void gpio_init(void)
{
    gpio_set_direction(MVSR_MOTOR, GPIO_MODE_OUTPUT);
    gpio_set_level(MVSR_MOTOR, 0);
    gpio_set_direction(MVSR_PTT_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(MVSR_PTT_BUTTON, GPIO_PULLUP_ONLY);
}

static void motor_buzz(int ms)
{
    gpio_set_level(MVSR_MOTOR, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(MVSR_MOTOR, 0);
}

// Half-duplex push-to-talk RTP loop. Never returns (reset to end the demo).
static void media_loop(int rtp_sock, const sockaddr_in &dst)
{
    int16_t pcm[POC_FRAME_SAMPLES];
    uint8_t pkt[RTP_HEADER_LEN + POC_FRAME_SAMPLES];
    uint8_t rx[RTP_HEADER_LEN + POC_FRAME_SAMPLES * 2];

    rtp_tx_t tx = { (uint16_t)esp_random(), esp_random(), esp_random() };
    bool talking_prev = false;

    ESP_LOGI(TAG, "media loop up — hold PTT (BOOT btn) to talk, release to listen");

    for (;;) {
        bool ptt = (gpio_get_level(MVSR_PTT_BUTTON) == 0);   // pressed = LOW

        if (ptt) {
            size_t got = audio_read_mic(pcm, POC_FRAME_SAMPLES);   // ~20 ms of audio
            if (got > 0) {
                g711_ulaw_encode_buf(pcm, pkt + RTP_HEADER_LEN, got);
                rtp_write_header(pkt, POC_RTP_PAYLOAD_PCMU, talking_prev ? 0 : 1,
                                 tx.seq, tx.timestamp, tx.ssrc);
                sendto(rtp_sock, pkt, RTP_HEADER_LEN + got, 0,
                       (const sockaddr *)&dst, sizeof(dst));
                tx.seq++;
                tx.timestamp += got;
            }
            talking_prev = true;
        } else {
            int n = recvfrom(rtp_sock, rx, sizeof(rx), 0, NULL, NULL);
            if (n > (int)RTP_HEADER_LEN) {
                size_t plen = n - RTP_HEADER_LEN;
                if (plen > POC_FRAME_SAMPLES) plen = POC_FRAME_SAMPLES;
                g711_ulaw_decode_buf(rx + RTP_HEADER_LEN, pcm, plen);
                audio_write_spk(pcm, plen);
            }
            talking_prev = false;
        }
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    gpio_init();

    // Bring up the PDM mic + speaker FIRST, so hardware init is validated and
    // logged on boot — before the (blocking) Wi-Fi join.
    ESP_ERROR_CHECK(audio_init());

    ESP_ERROR_CHECK(wifi_sta_connect(POC_WIFI_SSID, POC_WIFI_PASS));

    ESP_LOGI(TAG, "local IP %s — registering ext %s with server %s:%d",
             wifi_local_ip(), POC_SIP_EXT_SELF, POC_SIP_SERVER_IP, POC_SIP_SERVER_PORT);

    SipUac uac(wifi_local_ip(), POC_SIP_LOCAL_PORT, POC_RTP_LOCAL_PORT,
               POC_SIP_SERVER_IP, POC_SIP_SERVER_PORT,
               POC_SIP_EXT_SELF, POC_SIP_EXT_CALLEE);

    if (!uac.registerExt()) {
        ESP_LOGE(TAG, "registration failed — check server IP and network");
        return;
    }

    motor_buzz(150);   // "actuation": ring -> place the call to ext 102
    ESP_LOGI(TAG, "calling ext %s through the server ...", POC_SIP_EXT_CALLEE);

    SipRemoteMedia remote;   // filled with ext 102's OWN media endpoint (P2P)
    if (!uac.placeCall(remote)) {
        ESP_LOGE(TAG, "call to ext %s failed (is it registered/online?)", POC_SIP_EXT_CALLEE);
        return;
    }

    // RTP socket bound to our advertised media port.
    int rtp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(POC_RTP_LOCAL_PORT);
    bind(rtp_sock, (sockaddr *)&local, sizeof(local));
    struct timeval rtv = { 0, 20 * 1000 };   // 20 ms recv timeout (listen pacing)
    setsockopt(rtp_sock, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(remote.ip.c_str());
    dst.sin_port = htons(remote.port);

    motor_buzz(60);              // call answered — short confirm buzz
    media_loop(rtp_sock, dst);   // never returns

    // (unreached in the spike) uac.hangup();
}
