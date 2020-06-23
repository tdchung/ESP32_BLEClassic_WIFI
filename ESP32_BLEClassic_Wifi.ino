/**
 * 
 * eps32 BLE classic Wifi config
 * tdchung.9@gmail.com
 * date: 23/06/2020
 * version:
 * Update:
 **/

#include "BluetoothSerial.h"
#include "WiFi.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

//--------------------------------------------------------------------------------------------------
// set debug mode
#define DEBUG_MODE 1
#if DEBUG_MODE
SemaphoreHandle_t mutex_v;
#endif

//--------------------------------------------------------------------------------------------------
// BLE message config
#define END_OF_DATA_CHAR '\r'
#define MAX_BLE_MSG_LENGTH 128
BluetoothSerial SerialBT;
typedef enum
{
    NONE = 0,
    WIFI_INFO,
    WIFI_CONNECT,
    WIFI_DISCONNECT,
    WIFI_NETWORK,
    WIFI_PASSWORD
} ble_mgs_t;
volatile bool isBleConnected = false;

//--------------------------------------------------------------------------------------------------
#define WIFI_NETWORK_STR "F**kingFPT"
#define WIFI_PASSWORD_STR "12345678@X"
#define WIFI_TIMEOUT_MS 20000
#define WIFI_RECOVER_TIME_MS 30000
#define MAX_WIFI_STRING 64
char wifi_network_str[MAX_WIFI_STRING] = WIFI_NETWORK_STR;
char wifi_password_str[MAX_WIFI_STRING] = WIFI_PASSWORD_STR;

//--------------------------------------------------------------------------------------------------
int debug(const char *fmt, ...)
{
    int rc = 0;
#if DEBUG_MODE
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    rc = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    xSemaphoreTake(mutex_v, portMAX_DELAY);
    Serial.println(buffer);
    xSemaphoreGive(mutex_v);
#endif
    return rc;
}

//--------------------------------------------------------------------------------------------------
/* FreeRTOS task denfine*/
void TaskBLE(void *pvParameters);
void keepWiFiAlive(void *pvParameters);

//--------------------------------------------------------------------------------------------------
void setup()
{
    // initialize serial communication at 115200 bits per second:
    Serial.begin(115200);

#if DEBUG_MODE
    mutex_v = xSemaphoreCreateMutex();
#endif

    // BLE setting
    SerialBT.register_callback(BLESerialCallBack);
    if (!SerialBT.begin("ESP32"))
    {
        debug("An error occurred initializing Bluetooth");
    }
    else
    {
        debug("Bluetooth initialized");
    }
    debug("The device started, now you can pair it with bluetooth!");

    // Wifi setting
    debug("Start connect wifi to %s", wifi_network_str);
    WiFi.onEvent(WiFiEvent);
    WiFi.begin((const char *)wifi_network_str, (const char *)wifi_password_str);

    unsigned long startAttemptTime = millis();
    while (WL_CONNECTED != WiFi.status() && WIFI_TIMEOUT_MS > millis() - startAttemptTime)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        debug("Connecting ...");
    }

    if (WL_CONNECTED != WiFi.status())
        debug("Cannot connect to wifi ...");
    else
    {
        IPAddress ip = WiFi.localIP();
        debug("Wifi connected IP %d.%d.%d.%d ...", ip[0], ip[1], ip[2], ip[3]);
    }

    // FreeRTOS tasks
    xTaskCreatePinnedToCore(
        TaskBLE,   //
        "TaskBLE", // A name just for humans
        10000,     // This stack size can be checked & adjusted by reading the Stack Highwater
        NULL,      //
        2,         // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
        NULL,
        ARDUINO_RUNNING_CORE);

    xTaskCreatePinnedToCore(
        keepWiFiAlive,
        "keepWiFiAlive", // Task name
        5000,            // Stack size (bytes)
        NULL,            // Parameter
        2,               // Task priority
        NULL,            // Task handle
        ARDUINO_RUNNING_CORE);
}

//--------------------------------------------------------------------------------------------------
void loop()
{
    // Empty. Things are done in Tasks.
}

//--------------------------------------------------------------------------------------------------
/**
 * CALLBACK
 */
//--------------------------------------------------------------------------------------------------
/* BLE callback */
void BLESerialCallBack(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_SRV_OPEN_EVT)
    {
        debug("[BLE event] Client Connected has address: %02X:%02X:%02X:%02X:%02X:%02X",
              param->srv_open.rem_bda[0],
              param->srv_open.rem_bda[1],
              param->srv_open.rem_bda[2],
              param->srv_open.rem_bda[3],
              param->srv_open.rem_bda[4],
              param->srv_open.rem_bda[5]);
        isBleConnected = true;
    }

    if (event == ESP_SPP_CLOSE_EVT)
    {
        debug("[BLE event] Client disconnected...");
        isBleConnected = false;
    }

    if (event == ESP_SPP_WRITE_EVT)
    {
        debug("[BLE event] Sent data to Client...");
        debug("ESP_SPP_WRITE_EVT: %u %s", param->write.len, param->write.cong ? "CONGESTED" : "FREE");
    }
}

/* Wifi callback */
void WiFiEvent(WiFiEvent_t event)
{
    debug("[WiFi-event] event: %d", event);
    IPAddress ip;
    switch (event)
    {
    case SYSTEM_EVENT_WIFI_READY:
        debug("[WiFi-event] WiFi interface ready");
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        debug("[WiFi-event] Completed scan for access points");
        break;
    case SYSTEM_EVENT_STA_START:
        debug("[WiFi-event] WiFi client started");
        break;
    case SYSTEM_EVENT_STA_STOP:
        debug("[WiFi-event] WiFi clients stopped");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        debug("[WiFi-event] Connected to access point");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        debug("[WiFi-event] Disconnected from WiFi access point");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ip = WiFi.localIP();
        debug("[WiFi-event] Obtained IP address: IP %d.%d.%d.%d ...", ip[0], ip[1], ip[2], ip[3]);
        break;
    case SYSTEM_EVENT_STA_LOST_IP:
        debug("[WiFi-event] Lost IP address and IP address is reset to 0");
        break;
    case SYSTEM_EVENT_AP_START:
        debug("[WiFi-event] WiFi access point started");
        break;
    case SYSTEM_EVENT_AP_STOP:
        debug("[WiFi-event] WiFi access point  stopped");
        break;
    default:
        break;
    }

    sendWfiEvt2Ble(event);
}

//--------------------------------------------------------------------------------------------------
/**
 * FREERTOS TASKS
 */
//--------------------------------------------------------------------------------------------------

/* BLE tasks */
void TaskBLE(void *pvParameters)
{
    (void)pvParameters;
    // char data[MAX_BLE_MSG_LENGTH] = {0};
    char *data = (char *)malloc(MAX_BLE_MSG_LENGTH);
    char inChar;
    int i = 0;
    ble_mgs_t ble_mode = NONE;

    for (;;)
    {
        if (SerialBT.available())
        {
            inChar = (char)SerialBT.read();
            // data[i] = inChar;
            // debug("%c", inChar);
            // i += 1;

            if ('\n' != inChar)
            {
                data[i] = inChar;
                i += 1;
            }

            if (END_OF_DATA_CHAR == inChar)
            {
                // data[i + 1] = '\0';
                data[i] = '\0';
                debug("BLE data received: %s", data);
                ble_mode = parseBleMsg((const char *)data);
                debug("TaskBLE BLE mode %d", (int)ble_mode);
                ble_handle_mode(ble_mode, (const char *)data);

                vTaskDelay(100 / portTICK_PERIOD_MS);

                i = 0;
                debug("TaskBLE uxTaskGetStackHighWaterMark %d", uxTaskGetStackHighWaterMark(NULL));
            }
        }
        else
            vTaskDelay(50 / portTICK_PERIOD_MS);

        // max length
        if (i == (MAX_BLE_MSG_LENGTH - 2))
        {
            data[0] = '\0';
            i = 0;
        }
    }

    free(data);
}

/* Wifi tasks */
void keepWiFiAlive(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        if (WL_CONNECTED == WiFi.status())
        {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        debug("keepWiFiAlive Connecting ...");
        debug("keepWiFiAlive uxTaskGetStackHighWaterMark %d", uxTaskGetStackHighWaterMark(NULL));
        // WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_network_str, wifi_password_str);
        vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);

        // unsigned long startAttemptTime = millis();

        // while (WL_CONNECTED != WiFi.status() && WIFI_TIMEOUT_MS > millis() - startAttemptTime)
        //     ;

        // if (WL_CONNECTED != WiFi.status())
        // {
        //     debug("keepWiFiAlive failed to connect to wifi ...");
        //     vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
        //     continue;
        // }

        // IPAddress ip = WiFi.localIP();
        // debug("keepWiFiAlive Wifi connected IP %d.%d.%d.%d ...", ip[0], ip[1], ip[2], ip[3]);
    }
}

//--------------------------------------------------------------------------------------------------

/* parse BLE classic message */
ble_mgs_t parseBleMsg(const char *msg)
{
    // char temp[MAX_BLE_MSG_LENGTH + 1] = {0};
#ifdef __cplusplus
    char *temp = (char *)malloc(MAX_BLE_MSG_LENGTH);
#else
    char *temp = malloc(MAX_BLE_MSG_LENGTH);
#endif
    memset(temp, 0, MAX_BLE_MSG_LENGTH);
    strncpy(temp, msg, MAX_BLE_MSG_LENGTH);

    ble_mgs_t mode = NONE;

    if (NULL != strstr(temp, "wifi info"))
        mode = WIFI_INFO;
    else if (NULL != strstr(temp, "wifi connect"))
        mode = WIFI_CONNECT;
    else if (NULL != strstr(temp, "wifi disconnect"))
        mode = WIFI_DISCONNECT;

    // TODO: update network and password
    else if (NULL != strstr(temp, "wifi network"))
        mode = WIFI_NETWORK;
    else if (NULL != strstr(temp, "wifi password"))
        mode = WIFI_PASSWORD;

    else
    {
        debug("parseBleMsg not match anything ...");
    }

    // send ACK
    if (mode)
        SerialBT.write((const uint8_t *)"ok\r", strlen("ok\r") + 1);
    else
        SerialBT.write((const uint8_t *)"error\r", strlen("error\r") + 1);
    free(temp);

    return mode;
}

/* ble_handle_mode */
void ble_handle_mode(ble_mgs_t mode, const char *msg)
{
    switch (mode)
    {
    case WIFI_INFO:
        sendWifiInfoToBleClient(msg);
        break;
    case WIFI_NETWORK:
    case WIFI_PASSWORD:
    case WIFI_DISCONNECT:
        WiFi.disconnect();
        break;
    case WIFI_CONNECT:
        WiFi.begin(wifi_network_str, wifi_password_str);
        break;
    default:
        break;
    }
}

/* BLE Classic send Wifi information */
bool sendWifiInfoToBleClient(const char *msg)
{
    // char rsp[MAX_BLE_MSG_LENGTH];
#ifdef __cplusplus
    char *rsp = (char *)malloc(MAX_BLE_MSG_LENGTH);
    char *temp = (char *)malloc(MAX_BLE_MSG_LENGTH);
    char *str_temp2 = (char *)malloc(20);
#else
    char *rsp = malloc(MAX_BLE_MSG_LENGTH);
    char *temp = malloc(MAX_BLE_MSG_LENGTH);
    char *str_temp2 = malloc(20);
#endif
    memset(rsp, 0, MAX_BLE_MSG_LENGTH);

    memset(temp, 0, MAX_BLE_MSG_LENGTH);
    strncpy(temp, msg, MAX_BLE_MSG_LENGTH);

    memset(str_temp2, 0, 20);

    IPAddress ip;

    // char str_temp2[20];
    if (1 == sscanf(strstr(temp, "wifi"), "wifi info %s", str_temp2))
    {
        debug("Info request: %s", str_temp2);
        if (NULL != strstr(str_temp2, "status"))
            snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "STATUS %s\r", (WL_CONNECTED == WiFi.status()) ? "Connected" : "Disconnected");
        else if (NULL != strstr(str_temp2, "ssid"))
            snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "SSID %s\r", wifi_network_str);
        else if (NULL != strstr(str_temp2, "password"))
            snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "PW %s\r", wifi_password_str);
        else if (NULL != strstr(str_temp2, "ip"))
        {
            if (WL_CONNECTED == WiFi.status())
            {
                ip = WiFi.localIP();
                snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "IP %d.%d.%d.%d\r", ip[0], ip[1], ip[2], ip[3]);
            }
            else
                snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "IP %s\r", "None");
        }

        debug("Sending: %d %s", strlen(rsp) + 1, rsp);

        SerialBT.write((const uint8_t *)rsp, strlen(rsp) + 1);
    }
    else
    {
        snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "STATUS %s\r", (WL_CONNECTED == WiFi.status()) ? "Connected" : "Disconnected");
        SerialBT.write((const uint8_t *)rsp, strlen(rsp) + 1);
        snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "SSID %s\r", wifi_network_str);
        SerialBT.write((const uint8_t *)rsp, strlen(rsp) + 1);
        snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "PW %s\r", wifi_password_str);
        SerialBT.write((const uint8_t *)rsp, strlen(rsp) + 1);
        if (WL_CONNECTED == WiFi.status())
        {
            ip = WiFi.localIP();
            snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "IP %d.%d.%d.%d\r", ip[0], ip[1], ip[2], ip[3]);
        }
        else
            snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "IP %s\r", "None");
        SerialBT.write((const uint8_t *)rsp, strlen(rsp) + 1);
    }
    free(rsp);
    free(temp);
    free(str_temp2);
}

static void sendWfiEvt2Ble(WiFiEvent_t event)
{
    if (isBleConnected)
    {
        switch (event)
        {
        case SYSTEM_EVENT_STA_CONNECTED:
            SerialBT.write((const uint8_t *)"Wifi connected\r", strlen("Wifi connected\r") + 1);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            SerialBT.write((const uint8_t *)"Wifi disconnected\r", strlen("Wifi disconnected\r") + 1);
            break;
        default:
            break;
        }
    }
}
