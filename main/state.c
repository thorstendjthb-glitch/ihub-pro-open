// state.c — Zentraler Zustands-Cache
#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static SemaphoreHandle_t s_mtx;
static sensor_data_t   s_sensors;
static power_data_t    s_power;
static climate_status_t s_climate;
static water_data_t    s_water;

esp_err_t state_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    return s_mtx ? ESP_OK : ESP_ERR_NO_MEM;
}

#define LOCK()   xSemaphoreTake(s_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mtx)

void state_set_sensors(const sensor_data_t *d){ LOCK(); s_sensors = *d; UNLOCK(); }
void state_set_power(const power_data_t *p)   { LOCK(); s_power   = *p; UNLOCK(); }
void state_set_climate(const climate_status_t *c){ LOCK(); s_climate = *c; UNLOCK(); }
void state_set_water(const water_data_t *w){ LOCK(); s_water = *w; UNLOCK(); }

void state_get_sensors(sensor_data_t *o){ LOCK(); *o = s_sensors; UNLOCK(); }
void state_get_power(power_data_t *o)   { LOCK(); *o = s_power;   UNLOCK(); }
void state_get_climate(climate_status_t *o){ LOCK(); *o = s_climate; UNLOCK(); }
void state_get_water(water_data_t *o)   { LOCK(); *o = s_water;   UNLOCK(); }
