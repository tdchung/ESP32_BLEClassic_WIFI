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


//--------------------------------------------------------------------------------------------------
#define WIFI_NETWORK_STR "12345678"
#define WIFI_PASSWORD_STR "12345678"
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

    // WIFI setting
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_network_str, wifi_password_str);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime > WIFI_TIMEOUT_MS) ;
    if (WiFi.status() != WL_CONNECTED) debug("Cannot connect to wifi ...");
    else debug("Wifi connected ...");

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
        "keepWiFiAlive",  // Task name
        5000,             // Stack size (bytes)
        NULL,             // Parameter
        2,                // Task priority
        NULL,             // Task handle
	    ARDUINO_RUNNING_CORE
    );
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
        debug("Client Connected has address: %02X:%02X:%02X:%02X:%02X:%02X",
              param->srv_open.rem_bda[0],
              param->srv_open.rem_bda[1],
              param->srv_open.rem_bda[2],
              param->srv_open.rem_bda[3],
              param->srv_open.rem_bda[4],
              param->srv_open.rem_bda[5]);
    }

    if (event == ESP_SPP_CLOSE_EVT)
    {
        debug("Client disconnected...");
    }

    if (event == ESP_SPP_WRITE_EVT)
    {
        debug("Sent data to Client...");
    }
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
                // TODO: to be implementation
                // parse_lte_msg((const char *)data);
                i = 0;
                vTaskDelay(100 / portTICK_PERIOD_MS);
                debug("TaskBLE uxTaskGetStackHighWaterMark %d", uxTaskGetStackHighWaterMark(NULL));
            }
        }
        else vTaskDelay(50 / portTICK_PERIOD_MS);

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
    for(;;){
        if(WL_CONNECTED == WiFi.status()){
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        debug("keepWiFiAlive Connecting ...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_network_str, wifi_password_str);

        unsigned long startAttemptTime = millis();

        while (WL_CONNECTED != WiFi.status() && WIFI_TIMEOUT_MS > millis() - startAttemptTime);

        if(WL_CONNECTED != WiFi.status()){
            debug("keepWiFiAlive failed to connect to wifi ...");
            vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
            continue;
        }

        Serial.println("keepWiFiAlive Connected: " + WiFi.localIP());
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

    if (NULL != strstr(temp, "wifi info")) mode = WIFI_INFO;
    else if (NULL != strstr(temp, "wifi connect")) mode = WIFI_CONNECT;
    else if (NULL != strstr(temp, "wifi disconnect")) mode = WIFI_DISCONNECT;

    // TODO: update network and password
    else if (NULL != strstr(temp, "wifi network")) mode = WIFI_NETWORK;
    else if (NULL != strstr(temp, "wifi password")) mode = WIFI_PASSWORD;

    else
    {
        debug("parseBleMsg not match anything ...");
    }

    free(temp);
}


/* BLE Classic send Wifi information */
bool sendWifiInfoToBleClient(void) {
    if(WL_CONNECTED == WiFi.status()) {
        SerialBT.write((const uint8_t *)"ESP32: Wifi connected\r",
                        sizeof("ESP32: Wifi is connected\r"));
        // IP
        // network
    }
    else SerialBT.write((const uint8_t *)"ESP32: Wifi is not connected\r",
                         sizeof("ESP32: Wifi is not connected\r"));
}