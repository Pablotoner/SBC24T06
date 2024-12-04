#include <stdio.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

//defines SPI
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS_ALS   5
#define PIN_NUM_CS_MIC   4

//defines wifi
/* STA Configuration */
#define EXAMPLE_ESP_WIFI_STA_SSID           "mywifissid"
#define EXAMPLE_ESP_WIFI_STA_PASSWD         "mypassword"
#define EXAMPLE_ESP_MAXIMUM_RETRY           5
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_PSK


/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID            "ESP3206"
#define EXAMPLE_ESP_WIFI_AP_PASSWD          "myssid"
#define EXAMPLE_ESP_WIFI_CHANNEL            1
#define EXAMPLE_MAX_STA_CONN                4


/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";
char parsedssid[32];
char parsepassword[64];
char token[64];
static const char *TAG_SPI = "SPI";
static const char *TAG_MQTT = "MQTT";

//variables wifi
static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT, "Last error %s: 0x%x", message, error_code);
    }
}
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG_MQTT, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG_MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }
}

esp_mqtt_client_handle_t mqtt_app_start(void)
    {
        esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://demo.thingsboard.io",
        .broker.address.port = 1883,
        .credentials.username = "5XwlQoQhVjWRRVQG4Bo2", //token
    };
    // Establecer la conexión
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    if(esp_mqtt_client_start(client) == ESP_OK) {
        return client;
    }else{
        return NULL;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_STA, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void save_wifi_credentials(const char* ssid, const char* password, const char* tokenL) {
    nvs_handle_t my_handle;
    nvs_open("storage", NVS_READWRITE, &my_handle);
    nvs_set_str(my_handle, "ssid", ssid);
    nvs_set_str(my_handle, "password", password);
    nvs_set_str(my_handle, "token", tokenL);
    nvs_commit(my_handle);
    nvs_close(my_handle);
}

bool load_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t password_len, nvs_handle_t my_handle) {
    //esp_err_t err;
    if (nvs_get_str(my_handle, "ssid", ssid, &ssid_len) != ESP_OK ||
        nvs_get_str(my_handle, "password", password, &password_len) != ESP_OK) {
        nvs_close(my_handle);
        return false;  // No hay credenciales guardadas
    }
    nvs_close(my_handle);
    return true;
}

/* Initialize soft AP */
esp_netif_t *wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASSWD, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}

/* Initialize wifi station */
esp_netif_t *wifi_init_sta(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READONLY, &my_handle);
    if(err != ESP_OK){
        return false;
    }
    size_t ssid_len;
    err = nvs_get_str(my_handle, "ssid", NULL, &ssid_len);
    if(err != ESP_OK){
        return false;
    }
    size_t password_len;
    nvs_get_str(my_handle, "password", NULL, &password_len);
        if(err != ESP_OK){
        return false;
    }
    char * ssid = malloc(ssid_len);
    char * password = malloc(password_len);


    if(!load_wifi_credentials(ssid, ssid_len, password, password_len, my_handle)){
        ssid = EXAMPLE_ESP_WIFI_STA_SSID;
        password = EXAMPLE_ESP_WIFI_STA_PASSWD;
        ESP_LOGI(TAG_STA, " saliendo de load SSID: %s, Pass: %s", ssid, password);
    }

    printf("Wifi load creds %s, lenth %i \n", ssid,ssid_len);
    printf("Wifi password %s lenth %i \n", password,password_len);

    ESP_LOGI(TAG_STA, "SSID: %s, Pass: %s", ssid, password);

    strcpy(parsedssid, ssid);
    strcpy(parsepassword, password);

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
            * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    for(int i = 0; i < 32; i++)
    {
     wifi_sta_config.sta.ssid[i] = parsedssid[i];
    }

    for(int i = 0; i < 64; i++)
    {
     wifi_sta_config.sta.password[i] = parsepassword[i];
    }

    ESP_LOGI(TAG_STA, "printing ssid %s \n", wifi_sta_config.sta.ssid );
    ESP_LOGI(TAG_STA, "printing ssid %s \n", wifi_sta_config.sta.password );

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}

esp_err_t send_web_page(httpd_req_t *req) {
    int response;
    char on_resp[] = "<html><head><title>Esp32</title><meta charset='UTF-8'></head><body><h1>Choose access point</h1><form method=\"POST\" action=\"/\"><br/><input type=\"text\" name=\"ssid\" placeholder=\"Wifi ssid\"/><br/><input type=\"text\" name=\"password\" placeholder=\"Wifi password\"/><br/><input type=\"hidden\" name=\"hidden\"/><br/><input type=\"submit\" value=\"Save\"/></form></body></html>";
    response = httpd_resp_send(req, on_resp, HTTPD_RESP_USE_STRLEN);
    return response;
}

esp_err_t get_req_handler(httpd_req_t *req)
{
    return send_web_page(req);
}

esp_err_t index_post_handler(httpd_req_t *req)
{
    uint8_t buffer[100];
    httpd_req_recv(req, (char *) buffer, 100);
    wifi_config_t wifi_sta_config = {};
    if (httpd_query_key_value((char *) buffer, "ssid", (char *) wifi_sta_config.sta.ssid, 32) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, "SSID required", -1);
        return ESP_OK;
    }
    if (httpd_query_key_value((char *) buffer, "password", (char *) wifi_sta_config.sta.password, 32) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, "Password is required", -1);

        
        return ESP_OK;
    }
    if (httpd_query_key_value((char *) buffer, "token", (char *) token, 64) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, "Token is required", -1);

        
        return ESP_OK;
    }
 
    if (strlen((char *) wifi_sta_config.sta.ssid) < 1) {
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, "<p>Invalid ssid</p>", -1);
        return ESP_OK;
    }
    
    strcpy(parsedssid,(char *) wifi_sta_config.sta.ssid);
    strcpy(parsepassword,(char *) wifi_sta_config.sta.password);

    int encontrado;
    encontrado = 0;
    int i = 0;
    int j = 0;
    while(i < strlen(parsedssid)){
        if((int)parsedssid[i] != 37 ){
            if(!encontrado)wifi_sta_config.sta.ssid[j] = parsedssid[i];
            else wifi_sta_config.sta.ssid[j] = '\0';
        }
        else {
            wifi_sta_config.sta.ssid[j] = 36;
            encontrado = 1;
        }
        i++;
        j++;
    }

    i = 0;
    j = 0;
    encontrado = 0;
    while(i < strlen(parsepassword)){
        if((int)parsepassword[i] != 37){
            if(!encontrado)wifi_sta_config.sta.password[j] = parsepassword[i];
            else wifi_sta_config.sta.password[j] = '\0';
        }
        else {
            wifi_sta_config.sta.password[j] = 36;
            encontrado = 1;
        }
        i++;
        j++;
    }

    save_wifi_credentials((char *) wifi_sta_config.sta.ssid,(char *) wifi_sta_config.sta.password, (char *) token);
 
    ESP_LOGI(TAG_STA, "SSID: %s, Pass: %s", wifi_sta_config.sta.ssid, wifi_sta_config.sta.password);
    httpd_resp_send(req, "<h1>OK</h1>", -1);
 
    //ESP_ERROR_CHECK(esp_wifi_restore());
    //ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //wifi_sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    //wifi_sta_config.sta.threshold.authmode  = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
    //wifi_sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    
    //ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    //ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );
    //ESP_ERROR_CHECK(esp_wifi_start());
    //ESP_ERROR_CHECK(esp_wifi_connect());
    //esp_restart();
    /* Start WiFi */
    // ESP_LOGI(TAG_STA, "ESP_WIFI_MODE_STA");
    return ESP_OK;
}

httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_req_handler,
    .user_ctx = NULL};

httpd_uri_t uri_post = {
    .uri = "/",
    .method = HTTP_POST,
    .handler = index_post_handler,
    .user_ctx = NULL};

httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
    }

    return server;
}

float medidaMic (spi_device_handle_t spiMIC) {
    //Lectura MIC
    int sumaRMS = 0;
    int freq = 100;
    float decibelios = 0.0;
    float RMS;
    uint8_t dataMic[2];
    uint16_t mic_vol;

    spi_transaction_t u = {
        .length = 16,           // Lectura de 16 bits
        .rx_buffer = dataMic
    };

    for(int j = 0; j < freq; j++) {
        spi_device_transmit(spiMIC, &u);  // Realiza la transacción
        mic_vol = ((uint16_t)dataMic[0] << 8 | (uint16_t)dataMic[1] );
        mic_vol = abs(mic_vol - 2048);
        //printf("Mic vol: %d\n", mic_vol);
        sumaRMS = sumaRMS + pow(mic_vol, 2);
        vTaskDelay(pdMS_TO_TICKS((1/freq)*1000)); //delay para hacer freq medidas en 1 segundo
    }
    //Hacer RMS
    RMS = sqrt(sumaRMS/ freq);
    decibelios = (20 * log10f(RMS/2047)) + 94; //+94 por ser el valor de referencia de mediciones de sonido

    return decibelios;
}

uint16_t medidaALS (spi_device_handle_t spiALS) {
    uint8_t data[2];
    uint16_t als_value;
    spi_transaction_t t = {
            .length = 16,           // Lectura de 16 bits
            .rx_buffer = data
    };

    //Lectura ALS
    spi_device_transmit(spiALS, &t);  // Realiza la transacción
    als_value = (((data[0] << 8) | data[1]) >> 4);  // Valor de luz
    return als_value;
}

void app_main(void) {
    //inicializacion de bus SPI
    spi_bus_config_t buscfg = {
        .mosi_io_num = -1,          // No se usa MOSI
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    //configuracion de dispositivos SPI
    spi_device_interface_config_t devcfgALS = {
        .clock_speed_hz = 1000000,  // 1 MHz
        .mode = 0,                  // Modo SPI 0
        .spics_io_num = PIN_NUM_CS_ALS,
        .queue_size = 1,
    };

    spi_device_interface_config_t devcfgMIC = {
        .clock_speed_hz = 1000000,  // 1 MHz
        .mode = 0,                  // Modo SPI 0
        .spics_io_num = PIN_NUM_CS_MIC,
        .queue_size = 1,
    };
    //inicializar bus y añadir dispositivos
    spi_device_handle_t spiALS;
    spi_device_handle_t spiMIC;
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfgALS, &spiALS);
    spi_bus_add_device(SPI2_HOST, &devcfgMIC, &spiMIC);

    while (1) {
        printf("ALS Value: %u MIC dB: %f \n", medidaALS(spiALS), medidaMic(spiMIC));
    }
}