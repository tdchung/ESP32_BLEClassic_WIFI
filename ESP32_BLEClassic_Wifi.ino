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
#include <EEPROM.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

//--------------------------------------------------------------------------------------------------
// CONFIG FEATURE
#define DEBUG_MODE 1
#define EEPROM_MODE 1

//--------------------------------------------------------------------------------------------------
// set debug mode
#if DEBUG_MODE
SemaphoreHandle_t mutex_debug;
#endif

//--------------------------------------------------------------------------------------------------
// define EEPROM
#define EEPROM_SIZE 130 // total size
#define EEPROM_MAX_PROFILE_LENGTH 15
#define EEPROM_TOTAL_PROFILE_LOCATION 0 // total profile \
                                        // 1-> 9: reserved
#define EEPROM_PROFILE_1 10
#define EEPROM_PROFILE_2 EEPROM_PROFILE_1 + EEPROM_MAX_PROFILE_LENGTH * 2
#define EEPROM_PROFILE_3 EEPROM_PROFILE_2 + EEPROM_MAX_PROFILE_LENGTH * 2
#define EEPROM_PROFILE_4 EEPROM_PROFILE_3 + EEPROM_MAX_PROFILE_LENGTH * 2
#if EEPROM_MODE
SemaphoreHandle_t mutex_eeprom;
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
    WIFI_PASSWORD,
    WIFI_READ_PROFILE,
    WIFI_WRITE_PROFILE
} ble_mgs_t;
volatile bool isBleConnected = false;

//--------------------------------------------------------------------------------------------------
#define WIFI_NETWORK_STR "F**kingFPT"
#define WIFI_PASSWORD_STR "12345678@X"
#define WIFI_TIMEOUT_MS 20000
#define WIFI_RECOVER_TIME_MS 30000
#define MAX_WIFI_STRING 20
char wifi_network_str[MAX_WIFI_STRING] = WIFI_NETWORK_STR;
char wifi_password_str[MAX_WIFI_STRING] = WIFI_PASSWORD_STR;
volatile bool isWifiConnected = false;

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

    xSemaphoreTake(mutex_debug, portMAX_DELAY);
    Serial.println(buffer);
    xSemaphoreGive(mutex_debug);
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
    vTaskDelay(200 / portTICK_PERIOD_MS);
    // initialize serial communication at 115200 bits per second:
    Serial.begin(115200);

#if DEBUG_MODE
    mutex_debug = xSemaphoreCreateMutex();
#endif

#if EEPROM_MODE
    // initialize EEPROM with predefined size
    EEPROM.begin(EEPROM_SIZE);
    mutex_eeprom = xSemaphoreCreateMutex();
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
    WiFi.onEvent(WiFiEventCallback);
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
        7000,      // This stack size can be checked & adjusted by reading the Stack Highwater
        NULL,      //
        2,         // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
        NULL,
        ARDUINO_RUNNING_CORE);

    xTaskCreatePinnedToCore(
        keepWiFiAlive,
        "keepWiFiAlive", // Task name
        7000,            // Stack size (bytes)
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

//--------------------------------------------------------------------------------------------------
/* Wifi callback */
void WiFiEventCallback(WiFiEvent_t event)
{
    // https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/src/WiFiGeneric.cpp
    // Increased stack size of network event task to 7000
    debug("[WiFi-event] event: %d", event);
    debug("WiFiEvent uxTaskGetStackHighWaterMark %d", uxTaskGetStackHighWaterMark(NULL));
    IPAddress ip;
    switch (event)
    {
    case SYSTEM_EVENT_STA_CONNECTED:
        debug("[WiFi-event] Connected to access point");
        isWifiConnected = true;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        debug("[WiFi-event] Disconnected from WiFi access point");
        isWifiConnected = false;
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ip = WiFi.localIP();
        debug("[WiFi-event] Obtained IP address: IP %d.%d.%d.%d ...", ip[0], ip[1], ip[2], ip[3]);
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
                bleHandleMode(ble_mode, (const char *)data);

                vTaskDelay(100 / portTICK_PERIOD_MS);

                i = 0;
                debug("TaskBLE uxTaskGetStackHighWaterMark %d", uxTaskGetStackHighWaterMark(NULL));
            }
        }
        else
            vTaskDelay(100 / portTICK_PERIOD_MS);

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
        debug("keepWiFiAlive uxTaskGetStackHighWaterMark %d", uxTaskGetStackHighWaterMark(NULL));
        if (WL_CONNECTED == WiFi.status())
        {
            vTaskDelay(20000 / portTICK_PERIOD_MS);
            continue;
        }

        debug("keepWiFiAlive Connecting ...");
        // WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_network_str, wifi_password_str);
        vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
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
    else if (NULL != strstr(temp, "wifi ssid"))
        mode = WIFI_NETWORK;
    else if (NULL != strstr(temp, "wifi password"))
        mode = WIFI_PASSWORD;
    else if (NULL != strstr(temp, "wifi read"))
        mode = WIFI_READ_PROFILE;
    else if (NULL != strstr(temp, "wifi write"))
        mode = WIFI_WRITE_PROFILE;

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

//--------------------------------------------------------------------------------------------------
/* bleHandleMode */
void bleHandleMode(ble_mgs_t mode, const char *msg)
{
    int profile = 0;
    char ssid[EEPROM_MAX_PROFILE_LENGTH] = {0};
    char password[EEPROM_MAX_PROFILE_LENGTH] = {0};

    switch (mode)
    {
    case WIFI_INFO:
        sendWifiInfoToBleClient(msg);
        break;
    case WIFI_NETWORK:
    case WIFI_PASSWORD:
        break;
    case WIFI_DISCONNECT:
        if (isWifiConnected)
            WiFi.disconnect();
        else
            SerialBT.write((const uint8_t *)"Wifi already disconnected\r", strlen("Wifi already disconnected\r") + 1);
        break;
    case WIFI_CONNECT:
        if (!isWifiConnected)
            WiFi.begin(wifi_network_str, wifi_password_str);
        else
            SerialBT.write((const uint8_t *)"Wifi already connected\r", strlen("Wifi already connected\r") + 1);
        break;
    case WIFI_WRITE_PROFILE:
        if (1 == sscanf(strstr(msg, "wifi"), "wifi write %d", &profile))
        {
            if (writeWifiProfile2Eeprom((const char *)wifi_network_str, (const char *)wifi_password_str, profile))
                SerialBT.write((const uint8_t *)"Write complete\r", strlen("Write complete\r") + 1);
            else
                SerialBT.write((const uint8_t *)"Write failed\r", strlen("Write failed\r") + 1);
        }
        else
            debug("cannot read profile");
    case WIFI_READ_PROFILE:
        if (1 == sscanf(strstr(msg, "wifi"), "wifi read %d", &profile))
        {
            if (readWifiProfileFromEeprom(ssid, password, profile))
            {
                SerialBT.write((const uint8_t *)"Read complete \r", strlen("Read complete \r") + 1);
                SerialBT.write((const uint8_t *)ssid, strlen(ssid) + 1);
                SerialBT.write((const uint8_t *)" \r", strlen(" \r") + 1);
                SerialBT.write((const uint8_t *)password, strlen(password) + 1);
                SerialBT.write((const uint8_t *)" \r", strlen(" \r") + 1);
            }
            else
                SerialBT.write((const uint8_t *)"Read failed\r", strlen("Read failed\r") + 1);
        }
        else
            debug("cannot read profile");
        break;
    default:
        break;
    }
}

//--------------------------------------------------------------------------------------------------
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
            // snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "STATUS %s\r", (WL_CONNECTED == WiFi.status()) ? "Connected" : "Disconnected");
            snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "STATUS %s\r", (isWifiConnected) ? "Connected" : "Disconnected");
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
        snprintf(rsp, MAX_BLE_MSG_LENGTH - 1, "STATUS %s\r", (isWifiConnected) ? "Connected" : "Disconnected");
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

//--------------------------------------------------------------------------------------------------
/* static. check and print wifi state to BLE connection */
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

//--------------------------------------------------------------------------------------------------
// EEPROM
/* EEPROM write string */
bool writeString2Eeprom(const char *data, int len, int address)
{
#ifdef EEPROM_MODE
    // length failed
    if (EEPROM_MAX_PROFILE_LENGTH > len + 1)
        return false;

    int i = 0;
    for (i = 0; i < len; i++)
    {
        EEPROM.write(address + i, data[i]);
    }
    EEPROM.write(address + i, '\0');
    EEPROM.commit();
    return true;
#else
    return true;
#endif
}

/* EEPROM write wifi profile */
bool writeWifiProfile2Eeprom(const char *ssid, const char *password, int profile)
{
#ifdef EEPROM_MODE
    // if (!writeString2Eeprom(ssid, strlen(ssid), EEPROM_PROFILE_1 + ((profile-1)*2*EEPROM_MAX_PROFILE_LENGTH))) {
    //     debug("EEPROM: Failed to write ssid to profile %d", profile);
    //     return false;
    // }
    // if (!writeString2Eeprom(password, strlen(password), EEPROM_PROFILE_1 + EEPROM_MAX_PROFILE_LENGTH + ((profile-1)*2*EEPROM_MAX_PROFILE_LENGTH))) {
    //     debug("EEPROM: Failed to write password to profile %d", profile);
    //     return false;
    // }
    if (0 == EEPROM.writeString(EEPROM_PROFILE_1 + ((profile - 1) * 2 * EEPROM_MAX_PROFILE_LENGTH), ssid))
    {
        debug("EEPROM: Failed to write ssid to profile %d", profile);
        return false;
    }
    if (0 == EEPROM.writeString(EEPROM_PROFILE_1 + EEPROM_MAX_PROFILE_LENGTH + ((profile - 1) * 2 * EEPROM_MAX_PROFILE_LENGTH), password))
    {
        debug("EEPROM: Failed to write ssid to profile %d", profile);
        return false;
    }
    return true;
#else
    return true;
#endif
}

/* EEPROM read wifi profile */
bool readWifiProfileFromEeprom(char *ssid, char *password, int profile)
{
#ifdef EEPROM_MODE
    // if (!writeString2Eeprom(ssid, strlen(ssid), EEPROM_PROFILE_1 + ((profile-1)*2*EEPROM_MAX_PROFILE_LENGTH))) {
    //     debug("EEPROM: Failed to write ssid to profile %d", profile);
    //     return false;
    // }
    // if (!writeString2Eeprom(password, strlen(password), EEPROM_PROFILE_1 + EEPROM_MAX_PROFILE_LENGTH + ((profile-1)*2*EEPROM_MAX_PROFILE_LENGTH))) {
    //     debug("EEPROM: Failed to write password to profile %d", profile);
    //     return false;
    // }
    // return true;
    char *ssid_out = ssid;
    char *pwd_out = password;
    if (0 == EEPROM.readString(EEPROM_PROFILE_1 + ((profile - 1) * 2 * EEPROM_MAX_PROFILE_LENGTH), ssid_out, EEPROM_MAX_PROFILE_LENGTH))
    {
        debug("EEPROM: Failed to read ssid from profile %d", profile);
        return false;
    }
    if (0 == EEPROM.readString(EEPROM_PROFILE_1 + EEPROM_MAX_PROFILE_LENGTH + ((profile - 1) * 2 * EEPROM_MAX_PROFILE_LENGTH), pwd_out, EEPROM_MAX_PROFILE_LENGTH))
    {
        debug("EEPROM: Failed to read password from profile %d", profile);
        return false;
    }
    return true;
#else
    return true;
#endif
}
