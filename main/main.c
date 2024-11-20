#include <stdio.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS_ALS   5
#define PIN_NUM_CS_MIC   4

static const char *TAG = "SPI_Example";

void app_main(void) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = -1,          // No se usa MOSI
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

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

    spi_device_handle_t spiALS;
    spi_device_handle_t spiMIC;
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfgALS, &spiALS);
    spi_bus_add_device(SPI2_HOST, &devcfgMIC, &spiMIC);
    uint8_t data[2];
    uint16_t als_value;
    uint16_t mic_vol;
    uint16_t mic_freq;

    while (1) {
        
        spi_transaction_t t = {
            .length = 16,           // Lectura de 16 bits
            .rx_buffer = data
        };
        spi_device_transmit(spiALS, &t);  // Realiza la transacción
        als_value = (((data[0] << 8) | data[1]) >> 4);  // Valor de luz
        vTaskDelay(pdMS_TO_TICKS(100));

        spi_transaction_t u = {
            .length = 16,           // Lectura de 16 bits
            .rx_buffer = data
        };
        spi_device_transmit(spiMIC, &u);  // Realiza la transacción
        mic_vol = ((uint16_t)data[0] << 8 | data[0] );
        printf("ALS Value: %u MIC Vol: %u\n", als_value, mic_vol);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}