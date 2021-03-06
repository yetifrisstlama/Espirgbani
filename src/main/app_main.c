// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "wifi_startup.h"
#include "web_console.h"
#include "rgb_led_panel.h"
#include "sdcard.h"
#include "animations.h"
#include "frame_buffer.h"
#include "bmp.h"
#include "font.h"
#include "shaders.h"
#include "fast_hsv2rgb.h"
#include "app_main.h"
#include "json_settings.h"

static const char *T = "MAIN_APP";

int g_maxFnt = 0;
int g_fontNumberRequest = 0;

void seekToFrame(FILE *f, int byteOffset, int frameOffset) {
    byteOffset += DISPLAY_WIDTH * DISPLAY_HEIGHT * frameOffset / 2;
    fseek(f, byteOffset, SEEK_SET);
}

void playAni(FILE *f, headerEntry_t *h) {
    uint8_t r,g,b;
    fast_hsv2rgb_32bit(RAND_AB(0,HSV_HUE_MAX), HSV_SAT_MAX, HSV_VAL_MAX, &r, &g, &b);
    for(int i=0; i<h->nFrameEntries; i++) {
        frameHeaderEntry_t fh = h->frameHeader[i];
        if(fh.frameId == 0) {
            startDrawing(2);
            setAll(2, 0xFF000000);
        } else {
            seekToFrame(f, h->byteOffset+HEADER_SIZE, fh.frameId-1);
            startDrawing(2);
            setFromFile(f, 2, SRGBA(r,g,b,0xFF));
        }
        doneDrawing(2);
        // updateFrame();
        vTaskDelay(fh.frameDur / portTICK_PERIOD_MS);
    }
}

void manageBrightness(struct tm *timeinfo) {
    static int nBadPings = 0;
    cJSON *jPow = jGet(getSettings(), "power");
    cJSON *jDel = jGet(getSettings(), "delays");
    cJSON *jHi  = jGet(jPow, "hi");
    cJSON *jLo  = jGet(jPow, "lo");
    const char *pingIpStr;
    uint32_t pingRespTime;
    ip4_addr_t ip;
    pingIpStr = jGetS(jHi, "pingIp", "0.0.0.0");
    ip4addr_aton(pingIpStr, &ip);
    int maxBadPings = jGetI(jDel, "ping", 1);
    if (maxBadPings <= 0) maxBadPings = 1;
    int iHi = jGetI(jHi, "m", 0) + jGetI(jHi, "h", 9) * 60;
    int iLo = jGetI(jLo, "m", 45) + jGetI(jLo,"h", 22) * 60;
    int iCur = timeinfo->tm_min + timeinfo->tm_hour * 60;
    if (iCur >= iHi && iCur < iLo) {
        // Daylite mode
        brightNessState = BR_DAY;
        if (ip.addr > 0 && ip.addr < 0xFFFFFFFF) {
            if ((pingRespTime = isPingOk(&ip, 3000))) {
                nBadPings = 0;
                ESP_LOGD(T,"Ping response from %s in %d ms", ip4addr_ntoa(&ip), pingRespTime);
            } else {
                nBadPings++;
                ESP_LOGW(T,"Ping timeout %d on %s", nBadPings, ip4addr_ntoa(&ip));
            }
            if (nBadPings >= maxBadPings) {
                g_rgbLedBrightness = jGetI(jLo, "p", 1);
            } else {
                g_rgbLedBrightness = jGetI(jHi, "p", 20);
            }
        } else {
            g_rgbLedBrightness = jGetI(jHi, "p", 20);
        }
    } else {
        // Night mode
        brightNessState = BR_NIGHT;
        g_rgbLedBrightness = jGetI(jLo, "p", 1);
    }
}

void aniClockTask(void *pvParameters) {
    // takes care of things which need to happen
    // every minute (like redrawing the clock)
    time_t now = 0;
    struct tm timeinfo;
    timeinfo.tm_min =  0;
    timeinfo.tm_hour= 18;
    char strftime_buf[64];

    unsigned col = rand();
    unsigned ticks_font=0, ticks_color=0;  // for counting ticks [min]
    cJSON *jDelay = jGet(getSettings(), "delays");
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    ESP_LOGI(T,"aniClockTask started");

    // count font files and choose a random one
    g_maxFnt = cntFntFiles("/sd/fnt") - 1;
    if (g_maxFnt < 0) {
        ESP_LOGE(T, "no fonts found on SD card");
        g_fontNumberRequest = -1;
    } else {
        ESP_LOGI(T, "max. font file: /sd/fnt/%d.fnt", g_maxFnt);
        g_fontNumberRequest = RAND_AB(0, g_maxFnt);
    }

    while(1) {
        ESP_LOGD(T, " ticks_font: %u, max: %d", ticks_font, jGetI(jDelay, "font", 60));
        if (g_maxFnt > 0 && ticks_font > jGetI(jDelay, "font", 60)) {
            // every delays.font minutes
            g_fontNumberRequest = RAND_AB(0, g_maxFnt);
            ticks_font = 0;
        }
        if (ticks_color > jGetI(jDelay, "color", 10)) {
            col = 0xFF000000 | rand();
            ticks_color = 0;
        }
        // every minute
        if(g_maxFnt > 0 && g_fontNumberRequest >= 0 && g_fontNumberRequest <= g_maxFnt) {
            sprintf(strftime_buf, "/sd/fnt/%d", g_fontNumberRequest);
            initFont(strftime_buf);
            g_fontNumberRequest = -1;
        }
        // Redraw the clock
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%H:%M", &timeinfo);
        drawStrCentered(strftime_buf, 1, col, 0xFF000000);
        manageBrightness(&timeinfo);
        ticks_font++;
        ticks_color++;

        // wait for the next minute
        vTaskDelayUntil(&xLastWakeTime, 1000 * 60 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    wifiState = WIFI_START_HOTSPOT_MODE;
}

void tp_stripes(unsigned width, unsigned offset, bool isY)
{
    for (unsigned y=0; y<DISPLAY_HEIGHT; y++) {
        for (unsigned x=0; x<DISPLAY_WIDTH; x++) {
            unsigned var = isY ? x : y;
            unsigned col = (var + offset) % width == 0 ? 0xFFFFFFFF : 0xFF000000;
            setPixel(2, x, y, col);
        }
    }
    updateFrame();
}

void tp_stripes_sequence(bool isY)
{
    for (unsigned i=0; i<8; i++) {
        ESP_LOGI(T, "stripes %d / 8", i + 1);
        tp_stripes(8, i, isY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    for (unsigned i=0; i<4; i++) {
        ESP_LOGI(T, "stripes %d / 2", (i % 2) + 1);
        tp_stripes(2, i % 2, isY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main() {
    //------------------------------
    // Enable RAM log file
    //------------------------------
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_set_vprintf(wsDebugPrintf);
    srand(esp_random());

    //------------------------------
    // Init filesystems
    //------------------------------
    initSpiffs();
    initSd();
    setLogLevel(*jGetS(getSettings(), "log_level", "W"));

    //------------------------------
    // Init rgb tiles
    //------------------------------
    // with .json settings, display HelloWrld
    g_rgbLedBrightness = 10;
    init_rgb();
    if (!initFont("/sd/fnt/2")) {
        // TODO: try to fallback on font in spiffs
        initFont("/spiffs/fnt");
    }
    setAll(2, 0x00000000);
    drawStrCentered("HelloWrld", 1, 0xFF222222, 0xFF000000);
    updateFrame();

    //------------------------------
    // Pushing flash button triggers access point mode
    //------------------------------
    gpio_config_t io_conf;
    //interrupt of falling edge
    io_conf.intr_type = GPIO_PIN_INTR_NEGEDGE;
    //bit mask of the pins, use GPIO0 here
    io_conf.pin_bit_mask = (1<<0);
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    //install gpio isr service
    gpio_install_isr_service(0);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(0, gpio_isr_handler, 0);

    //------------------------------
    // Startup wifi & webserver
    //------------------------------
    ESP_LOGI(T,"Starting network infrastructure ...");
    wifi_conn_init();

    //------------------------------
    // display test-pattern
    //------------------------------
    // only if enabled in json
    cJSON *jPanel = jGet(getSettings(), "panel");
    if (jGetB(jPanel, "test_pattern", false)) {
        g_rgbLedBrightness = jGetI(jPanel, "tp_brightness", 10);
        if (g_rgbLedBrightness < 1) g_rgbLedBrightness = 1;
        while(1) {
            ESP_LOGI(T, "Test-pattern mode!!!");
            setAll(0, 0xFF000000);
            setAll(1, 0xFF000000);
            setAll(2, 0xFF000000);
            updateFrame();

            ESP_LOGI(T, "Diagonal");
            for (unsigned y=0; y<DISPLAY_HEIGHT; y++)
                for (unsigned x=0; x<DISPLAY_WIDTH; x++)
                    setPixel(2, x, y, (x - y) % DISPLAY_HEIGHT == 0 ? 0xFFFFFFFF : 0xFF000000);
            updateFrame();
            vTaskDelay(5000 / portTICK_PERIOD_MS);

            ESP_LOGI(T, "Vertical stripes ...");
            tp_stripes_sequence(true);

            ESP_LOGI(T, "Horizontal stripes ...");
            tp_stripes_sequence(false);

            ESP_LOGI(T, "All red");
            setAll(2, 0xFF0000FF);
            updateFrame();
            vTaskDelay(1000 / portTICK_PERIOD_MS);

            ESP_LOGI(T, "All green");
            setAll(2, 0xFF00FF00);
            updateFrame();
            vTaskDelay(1000 / portTICK_PERIOD_MS);

            ESP_LOGI(T, "All blue");
            setAll(2, 0xFFFF0000);
            updateFrame();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 0, 20000 / portTICK_PERIOD_MS);
    updateFrame();
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    //------------------------------
    // Set the clock / print time
    //------------------------------
    // Set timezone to Eastern Standard Time and print local time
    const char *tz_str = jGetS(getSettings(), "timezone", "PST8PDT");
    ESP_LOGI(T,"Setting timezone to TZ = %s", tz_str);
    setenv("TZ", tz_str, 1);
    tzset();
    time_t now = 0;
    struct tm timeinfo = { 0 };
    char strftime_buf[64];
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(T, "Local Time: %s (%ld)", strftime_buf, time(NULL));
    srand(time(NULL));

    //------------------------------
    // Startup animated background layer
    //------------------------------
    xTaskCreate(&aniBackgroundTask, "aniBackground", 4096, NULL, 0, NULL);

    //------------------------------
    // Startup clock layer
    //------------------------------
    xTaskCreate(&aniClockTask, "aniClock", 4096, NULL, 0, NULL);

    //------------------------------
    // Read animation file from SD
    //------------------------------
    FILE *f = fopen(ANIMATION_FILE, "r");
    if (!f) {
        ESP_LOGE(T, "fopen(%s, rb) failed: %s", ANIMATION_FILE, strerror(errno));
        ESP_LOGE(T, "Will not show animations!");
        goto exit_main_task;
    }

    fileHeader_t fh;
    getFileHeader(f, &fh);
    printf("Build: %s, Found %d animations\n\n", fh.buildStr, fh.nAnimations);
    fseek(f, HEADER_OFFS, SEEK_SET);
    headerEntry_t myHeader;

    int aniId;
    cJSON *jDelay = jGet(getSettings(), "delays");

    while (1) {
        vTaskDelay(jGetI(jDelay, "ani", 15) * 1000 / portTICK_PERIOD_MS);

        aniId = RAND_AB(0, fh.nAnimations-1);
        readHeaderEntry(f, &myHeader, aniId);
        ESP_LOGD(T, "%s", myHeader.name);
        ESP_LOGD(T, "--------------------------------");
        ESP_LOGD(T, "animationId:       0x%04X", myHeader.animationId);
        ESP_LOGD(T, "nStoredFrames:         %d", myHeader.nStoredFrames);
        ESP_LOGD(T, "byteOffset:    0x%08X", myHeader.byteOffset);
        ESP_LOGD(T, "nFrameEntries:         %d", myHeader.nFrameEntries);
        ESP_LOGD(T, "width:               0x%02X", myHeader.width);
        ESP_LOGD(T, "height:              0x%02X", myHeader.height);
        ESP_LOGD(T, "unknowns:            0x%02X", myHeader.unknown0);
        ESP_LOG_BUFFER_HEX_LEVEL(T, myHeader.unknown1, sizeof(myHeader.unknown1), ESP_LOG_DEBUG);
        ESP_LOGD(T, "frametimes:");
        ESP_LOG_BUFFER_HEX_LEVEL(T, myHeader.frameHeader, myHeader.nFrameEntries*2, ESP_LOG_DEBUG);
        playAni(f, &myHeader);
        free(myHeader.frameHeader);
        myHeader.frameHeader = NULL;

        // Keep a single frame displayed for a bit
        if (myHeader.nStoredFrames <= 3 || myHeader.nFrameEntries <= 3)
            vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Fade out the frame
        uint32_t nTouched = 1;
        while (nTouched) {
            startDrawing(2);
            nTouched = fadeOut(2, 10);
            doneDrawing(2);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        startDrawing(2);
        setAll(2, 0x00000000);    //Make layer fully transparent
        doneDrawing(2);
    }
    fclose(f);

exit_main_task:
    vTaskDelete(NULL);
}
