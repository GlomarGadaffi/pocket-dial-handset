// ─────────────────────────────────────────────────────────────────────────
//  pocketdial-voice-poc  —  app_main
//
//  Flow: Wi-Fi -> bring up mic+speaker -> (motor "actuation") -> SIP INVITE to
//  a desktop softphone -> on 200 OK, run a FULL-DUPLEX G.711 media engine
//  (see media_tasks.cpp): mic->RTP and RTP->speaker run concurrently on
//  separate tasks, the whole call, no PTT gating.
//
//  Full duplex without acoustic echo cancellation WILL produce audible echo
//  (the far end hears themselves) — AEC is tracked separately (see README
//  roadmap / issue #2). This stage is the concurrency/jitter-buffer
//  foundation AEC plugs into, not the final echo-free state.
// ─────────────────────────────────────────────────────────────────────────
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cstring>

#include "poc_config.h"
#include "board_mvsr.h"
#include "net_wifi.h"
#include "audio_io.h"
#include "sip_uac.h"
#include "media_tasks.h"

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
    struct timeval rtv = { 0, POC_RTP_RX_TIMEOUT_MS * 1000 };   // bounds RX task's shutdown-poll interval
    setsockopt(rtp_sock, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(remote.ip.c_str());
    dst.sin_port = htons(remote.port);

    motor_buzz(60);                              // call answered — short confirm buzz
    audio_amp_enable(true);                      // power the speaker amp only for the live call
    media_run_full_duplex(uac, rtp_sock, dst);   // returns when the peer hangs up (BYE)
    audio_amp_enable(false);                     // amp off -> silent on idle (no underrun click)

    close(rtp_sock);
    motor_buzz(120);                  // call-ended haptic
    ESP_LOGI(TAG, "call done — idle (reset the board to place another call)");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
