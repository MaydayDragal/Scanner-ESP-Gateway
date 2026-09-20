#include "r3_boundary_sdk.h"
#include "scanner_button.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static const char *scenario;
static int64_t now;
static jmp_buf finished;
static void (*sample_task)(void *);
static scanner_button_event_t queue[8];
static unsigned count, queue_deletions, task_creations;

esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config->pin_bit_mask==1 && config->mode==GPIO_MODE_INPUT);
    assert(config->pull_up_en==GPIO_PULLUP_ENABLE && config->pull_down_en==GPIO_PULLDOWN_DISABLE);
    assert(config->intr_type==GPIO_INTR_DISABLE);
    return !strcmp(scenario,"gpio_error")?ESP_FAIL:ESP_OK;
}
int gpio_get_level(int pin)
{
    assert(pin==0);
    bool pressed=now<3000000 || (now>=3100000 && now<6500000) || now>=6700000;
    return pressed?0:1;
}
int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(TickType_t ticks)
{
    assert(ticks==10);
    now+=(int64_t)ticks*1000;
    if(now>=6600000 && now<6700000)scanner_button_set_context(false,true);
    if(now>=10000000)longjmp(finished,1);
}
QueueHandle_t xQueueCreate(unsigned length,unsigned size)
{
    assert(length==8 && size==sizeof(scanner_button_event_t));
    return !strcmp(scenario,"queue_error")?NULL:queue;
}
BaseType_t xQueueSend(QueueHandle_t handle,const void *item,TickType_t wait)
{
    assert(handle==queue && wait==0 && count<8);
    queue[count++]=*(const scanner_button_event_t *)item;
    if(queue[count-1]==SCANNER_BUTTON_WAKE)scanner_button_set_context(true,true);
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t handle,void *item,TickType_t wait)
{
    assert(handle==queue && wait==0);
    if(!count)return 0;
    *(scanner_button_event_t *)item=queue[0];
    memmove(queue,queue+1,--count*sizeof(*queue));return pdTRUE;
}
void vQueueDelete(QueueHandle_t handle) { assert(handle==queue);queue_deletions++; }
BaseType_t xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle)
{
    assert(!strcmp(name,"scanner_button") && stack>=2048 && priority==4 && !arg && !handle);
    task_creations++;sample_task=task;
    return !strcmp(scenario,"task_error")?0:pdPASS;
}

int main(int argc,char **argv)
{
    assert(argc==2);scenario=argv[1];
    scanner_button_set_context(true,true);
    esp_err_t result=scanner_button_start();
    if(!strcmp(scenario,"gpio_error")) { assert(result==ESP_FAIL && task_creations==0);return 0; }
    if(!strcmp(scenario,"queue_error")) { assert(result==ESP_ERR_NO_MEM && task_creations==0);return 0; }
    if(!strcmp(scenario,"task_error")) { assert(result==ESP_ERR_NO_MEM && queue_deletions==1);return 0; }
    assert(result==ESP_OK && scanner_button_start()==ESP_ERR_INVALID_STATE);
    if(setjmp(finished)==0)sample_task(NULL);
    assert(count==2); /* Boot hold ignored; awake hold once; asleep gesture wake only. */
    scanner_button_event_t event;
    assert(scanner_button_take_event(&event) && event==SCANNER_BUTTON_HOLD);
    assert(scanner_button_take_event(&event) && event==SCANNER_BUTTON_WAKE);
    assert(!scanner_button_take_event(&event) && !scanner_button_take_event(NULL));
    puts("Actual GPIO0 input driver passed");
}
