#include "nvs_flash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *DEMO_TAG = "BLSCAN";

#define IBEACON_SENDER 0
#define IBEACON_RECEIVER 1
#define IBEACON_MODE IBEACON_RECEIVER



/* (BLE stack tracking removed) */

// Wi-Fi credentials (change these)
#define WIFI_SSID "YOUR_ESSID"
#define WIFI_PASS "YOUR_PASS"
#define SERVER "http://you_ip:9999/store"
// --- BLE device table ---
#define BLE_DEVICE_TABLE_MAX 256

#define BLE_DEVICE_NAME_MAX_LEN 32
typedef struct
{
    esp_bd_addr_t addr;
    int rssi;
    char name[BLE_DEVICE_NAME_MAX_LEN];
    bool used;
} ble_device_entry_t;
static ble_device_entry_t ble_device_table[BLE_DEVICE_TABLE_MAX];

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;
/* The event group allows multiple bits for each event, but we only care about one event - are we connected? */
#define WIFI_CONNECTED_BIT BIT0

static void wifi_connect_and_post_task(void *pvParameter);
static void led_task(void *pvParameter);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            ESP_LOGI(DEMO_TAG, "wifi disconnected, retrying...");
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
    else if (event_base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            ESP_LOGI(DEMO_TAG, "got ip event");
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(DEMO_TAG, "wifi_init finished (not connected). SSID:%s", WIFI_SSID);
}

static void wifi_connect(void)
{
    ESP_LOGI(DEMO_TAG, "Starting Wi-Fi connect");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void post_device_table(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return;
    cJSON *data = cJSON_CreateArray();
    if (!data)
    {
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "data", data);

    for (int i = 0; i < BLE_DEVICE_TABLE_MAX; ++i)
    {
        if (!ble_device_table[i].used)
            continue;
        cJSON *entry = cJSON_CreateArray();
        if (!entry)
            continue;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 ble_device_table[i].addr[0], ble_device_table[i].addr[1], ble_device_table[i].addr[2],
                 ble_device_table[i].addr[3], ble_device_table[i].addr[4], ble_device_table[i].addr[5]);
        cJSON_AddItemToArray(entry, cJSON_CreateString(mac_str));
        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%d", ble_device_table[i].rssi);
        cJSON_AddItemToArray(entry, cJSON_CreateString(rssi_str));
        cJSON_AddItemToArray(entry, cJSON_CreateString(ble_device_table[i].name));
        cJSON_AddItemToArray(data, entry);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str)
    {
        cJSON_Delete(root);
        return;
    }

    esp_http_client_config_t config = {
        .url = SERVER,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client)
    {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_str, strlen(json_str));
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK)
        {
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(DEMO_TAG, "POST status = %d", status);
        }
        else
        {
            ESP_LOGE(DEMO_TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }

    cJSON_free(json_str);
    cJSON_Delete(root);
}

static void wifi_connect_and_post_task(void *pvParameter)
{
    // Start and connect
    wifi_connect();
    // Wait for IP with timeout (30s)
    ESP_LOGI(DEMO_TAG, "Waiting for Wi-Fi to obtain IP...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    if ((bits & WIFI_CONNECTED_BIT) != 0)
    {
        ESP_LOGI(DEMO_TAG, "Wi-Fi ready, posting device table");
        // ensure LED shows connected (led_task will respect this too)
        gpio_set_level(GPIO_NUM_2, 1);
        post_device_table();
    }
    else
    {
        ESP_LOGE(DEMO_TAG, "Wi-Fi connect timeout");
        gpio_set_level(GPIO_NUM_2, 0);
    }
    vTaskDelete(NULL);
}

static void wifi_disconnect(void)
{
    ESP_LOGI(DEMO_TAG, "Stopping Wi-Fi and disconnecting");
    esp_wifi_disconnect();
    esp_wifi_stop();
    if (s_wifi_event_group)
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/// Declare static functions
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

#if (IBEACON_MODE == IBEACON_RECEIVER)
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = ESP_BLE_GAP_SCAN_ITVL_MS(100),
    .scan_window = ESP_BLE_GAP_SCAN_WIN_MS(100),
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};

static void ble_device_table_clear(void)
{
    memset(ble_device_table, 0, sizeof(ble_device_table));
}

static void ble_device_table_add(const esp_bd_addr_t addr, int rssi, const char *name)
{
    // Check for duplicate
    for (int i = 0; i < BLE_DEVICE_TABLE_MAX; ++i)
    {
        if (ble_device_table[i].used && memcmp(ble_device_table[i].addr, addr, sizeof(esp_bd_addr_t)) == 0)
        {
            // Update RSSI if already present
            ble_device_table[i].rssi = rssi;
            if (name && name[0])
            {
                strncpy(ble_device_table[i].name, name, BLE_DEVICE_NAME_MAX_LEN - 1);
                ble_device_table[i].name[BLE_DEVICE_NAME_MAX_LEN - 1] = '\0';
            }
            return;
        }
    }
    // Add new entry if space
    for (int i = 0; i < BLE_DEVICE_TABLE_MAX; ++i)
    {
        if (!ble_device_table[i].used)
        {
            memcpy(ble_device_table[i].addr, addr, sizeof(esp_bd_addr_t));
            ble_device_table[i].rssi = rssi;
            if (name && name[0])
            {
                strncpy(ble_device_table[i].name, name, BLE_DEVICE_NAME_MAX_LEN - 1);
                ble_device_table[i].name[BLE_DEVICE_NAME_MAX_LEN - 1] = '\0';
            }
            else
            {
                ble_device_table[i].name[0] = '\0';
            }
            ble_device_table[i].used = true;
            return;
        }
    }
    // Table full, do nothing
}

static void ble_device_table_print(void)
{
    ESP_LOGI(DEMO_TAG, "--- BLE Device Table ---");
    for (int i = 0; i < BLE_DEVICE_TABLE_MAX; ++i)
    {
        if (ble_device_table[i].used)
        {
            ESP_LOGI(DEMO_TAG, "[%3d] MAC: %02X:%02X:%02X:%02X:%02X:%02X, RSSI: %d, NAME: %s",
                     i,
                     ble_device_table[i].addr[0], ble_device_table[i].addr[1], ble_device_table[i].addr[2],
                     ble_device_table[i].addr[3], ble_device_table[i].addr[4], ble_device_table[i].addr[5],
                     ble_device_table[i].rssi,
                     ble_device_table[i].name);
        }
    }
}

#elif (IBEACON_MODE == IBEACON_SENDER)
static esp_ble_adv_params_t ble_adv_params = {
    .adv_int_min = ESP_BLE_GAP_ADV_ITVL_MS(20),
    .adv_int_max = ESP_BLE_GAP_ADV_ITVL_MS(40),
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
#endif

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;

    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    {
#if (IBEACON_MODE == IBEACON_SENDER)
        esp_ble_gap_start_advertising(&ble_adv_params);
#endif
        break;
    }
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    {
#if (IBEACON_MODE == IBEACON_RECEIVER)
        // Do not start scanning here, scanning will be managed by a task
#endif
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        // scan start complete event to indicate scan start successfully or failed
        if ((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(DEMO_TAG, "Scanning start failed, error %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(DEMO_TAG, "Scanning start successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        // adv start complete event to indicate adv start successfully or failed
        if ((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(DEMO_TAG, "Advertising start failed, error %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(DEMO_TAG, "Advertising start successfully");
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt)
        {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
        {
            ESP_LOGI(DEMO_TAG, "ESP_GAP_SEARCH_INQ_RES_EVT");

            // Extract device name from advertisement data using new API
            char dev_name[BLE_DEVICE_NAME_MAX_LEN] = {0};
            uint8_t adv_name_len = 0;
            uint8_t *adv_ptr = esp_ble_resolve_adv_data_by_type(scan_result->scan_rst.ble_adv,
                                                                scan_result->scan_rst.adv_data_len,
                                                                ESP_BLE_AD_TYPE_NAME_CMPL,
                                                                &adv_name_len);
            if (adv_ptr == NULL || adv_name_len == 0)
            {
                adv_ptr = esp_ble_resolve_adv_data_by_type(scan_result->scan_rst.ble_adv,
                                                           scan_result->scan_rst.adv_data_len,
                                                           ESP_BLE_AD_TYPE_NAME_SHORT,
                                                           &adv_name_len);
            }

            /* If name not present in advertising packet, check scan response (if present).
               The stack places scan response bytes after adv_data_len in ble_adv buffer. */
            if ((adv_ptr == NULL || adv_name_len == 0) && scan_result->scan_rst.scan_rsp_len > 0)
            {
                uint8_t *scan_rsp_ptr = scan_result->scan_rst.ble_adv + scan_result->scan_rst.adv_data_len;
                uint8_t scan_rsp_len = scan_result->scan_rst.scan_rsp_len;
                adv_ptr = esp_ble_resolve_adv_data_by_type(scan_rsp_ptr, scan_rsp_len, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
                if (adv_ptr == NULL || adv_name_len == 0)
                {
                    adv_ptr = esp_ble_resolve_adv_data_by_type(scan_rsp_ptr, scan_rsp_len, ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
                }
            }

            if (adv_ptr && adv_name_len)
            {
                int copy_len = adv_name_len < (BLE_DEVICE_NAME_MAX_LEN - 1) ? adv_name_len : (BLE_DEVICE_NAME_MAX_LEN - 1);
                memcpy(dev_name, adv_ptr, copy_len);
                dev_name[copy_len] = '\0';
            }
            else
            {
                dev_name[0] = '\0';
            }

            // print macs, rssi, and name
            ESP_LOGI(DEMO_TAG, "Device address: " ESP_BD_ADDR_STR ", RSSI: %d, NAME: %s",
                     ESP_BD_ADDR_HEX(scan_result->scan_rst.bda), scan_result->scan_rst.rssi, dev_name);

            // Store MAC, RSSI, and name in table
            ble_device_table_add(scan_result->scan_rst.bda, scan_result->scan_rst.rssi, dev_name);

            break;
        }
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(DEMO_TAG, "Scanning stop failed, error %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(DEMO_TAG, "Scanning stop successfully");
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if ((err = param->adv_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(DEMO_TAG, "Advertising stop failed, error %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(DEMO_TAG, "Advertising stop successfully");
        }
        break;

    default:
        break;
    }
}

// --- Periodic scan/stop logic ---
static TimerHandle_t scan_timer = NULL;
static bool scanning = false;

static void scan_timer_callback(TimerHandle_t xTimer)
{
    if (scanning)
    {
        ESP_LOGI(DEMO_TAG, "Stopping scan");
        esp_ble_gap_stop_scanning();
        scanning = false;
        // Print device table after scan stops
        ble_device_table_print();
        // Spawn task to connect to Wi-Fi and POST device table after stopping BLE scanning
        if (xTaskCreate(wifi_connect_and_post_task, "wifi_post", 4096, NULL, 5, NULL) != pdPASS)
        {
            ESP_LOGE(DEMO_TAG, "Failed to create wifi_post task");
        }
        // Change timer to 1 minute for stopped period
        xTimerChangePeriod(scan_timer, pdMS_TO_TICKS(1 * 60 * 1000), 0);
    }
    else
    {
        ESP_LOGI(DEMO_TAG, "Starting scan");
        // Disconnect from Wi-Fi before starting BLE scanning
        wifi_disconnect();
        // (No BLE reinitialization required)
        // Clear device table before each scan period
        ble_device_table_clear();
        esp_ble_gap_start_scanning(5 * 60); // 5 minutes
        scanning = true;
        // Change timer to 5 minutes for scanning period
        xTimerChangePeriod(scan_timer, pdMS_TO_TICKS(5 * 60 * 1000), 0);
    }
}

static void scan_control_task(void *pvParameter)
{
    // Wait for BLE stack to be ready
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Set scan params
    esp_ble_gap_set_scan_params(&ble_scan_params);
    // Start LED task
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
    // Create and start timer (5 minutes)
    scan_timer = xTimerCreate("ScanTimer", pdMS_TO_TICKS(5 * 60 * 1000), pdFALSE, NULL, scan_timer_callback);
    if (scan_timer != NULL)
    {
        scanning = false;
        xTimerStart(scan_timer, 0); // Start with scan stopped, timer will start scan after 5 min
    }
    vTaskDelete(NULL);
}

static void led_task(void *pvParameter)
{
    // Configure GPIO2 as output
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 0);

    for (;;)
    {
        if (scanning)
        {
            // Blink fast while scanning
            gpio_set_level(GPIO_NUM_2, 1);
            vTaskDelay(pdMS_TO_TICKS(150));
            gpio_set_level(GPIO_NUM_2, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        // When not scanning, reflect Wi-Fi connected state: on if connected, off otherwise
        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
        if (bits & WIFI_CONNECTED_BIT)
        {
            gpio_set_level(GPIO_NUM_2, 1);
        }
        else
        {
            gpio_set_level(GPIO_NUM_2, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void ble_ibeacon_appRegister(void)
{
    esp_err_t status;

    ESP_LOGI(DEMO_TAG, "register callback");

    // register the scan callback function to the gap module
    if ((status = esp_ble_gap_register_callback(esp_gap_cb)) != ESP_OK)
    {
        ESP_LOGE(DEMO_TAG, "gap register error: %s", esp_err_to_name(status));
        return;
    }
}

void ble_ibeacon_init(void)
{
    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    esp_bluedroid_init_with_cfg(&cfg);
    esp_bluedroid_enable();
    ble_ibeacon_appRegister();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    // Initialize Wi-Fi (setup only). Connect/disconnect will be managed with scanning.
    wifi_init();

    ble_ibeacon_init();

    /* set scan parameters */

    // Start scan control task for periodic scan/stop
    xTaskCreate(scan_control_task, "scan_control_task", 4096, NULL, 5, NULL);


}
