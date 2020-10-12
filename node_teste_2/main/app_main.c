#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "lora.h"
//#include "adcfix.h"
#include "DHT22.h"
#include "ssd1306.h"
#include "ssd1306_draw.h"
#include "ssd1306_font.h"
#include "ssd1306_default_if.h"
#include "driver/gpio.h"
#include "driver/timer.h"

#define  CNT_ON    gpio_set_level(GPIO_NUM_17, 1);
#define  CNT_OFF   gpio_set_level(GPIO_NUM_17, 0);


static const int I2CDisplayAddress = 0x3C;
static const int I2CDisplayWidth = 128;
static const int I2CDisplayHeight = 32;
static const int I2CResetPin = 16;

struct SSD1306_Device I2CDisplay;


SemaphoreHandle_t xMutex;
QueueHandle_t myQueue;

timer_idx_t timer;
timer_config_t timer_configurations= {
   .alarm_en = TIMER_ALARM_DIS,
   .counter_en = TIMER_START,
   .intr_type = TIMER_INTR_LEVEL,
   .counter_dir = TIMER_COUNT_UP,
   .auto_reload = TIMER_AUTORELOAD_DIS,
   .divider = 80,
};

EventGroupHandle_t lora_event;
static const uint32_t LORA_SENT = BIT0;
static const uint32_t LORA_ACK = BIT1;
static const uint32_t LORA_ALL = (BIT1|BIT0);


uint32_t gb_temp_ref;


bool DefaultBusInit( void ) {
   assert( SSD1306_I2CMasterInitDefault( ) == true );
   assert( SSD1306_I2CMasterAttachDisplayDefault( &I2CDisplay, I2CDisplayWidth, I2CDisplayHeight, I2CDisplayAddress, I2CResetPin ) == true );

   return true;
}

void SetupDemo( struct SSD1306_Device* DisplayHandle, const struct SSD1306_FontDef* Font ) {
   SSD1306_Clear( DisplayHandle, SSD_COLOR_BLACK );
   SSD1306_SetFont( DisplayHandle, Font );
}

void SayHello( struct SSD1306_Device* DisplayHandle, const char* HelloText ) {
   SSD1306_Clear( DisplayHandle, SSD_COLOR_BLACK );
   SSD1306_FontDrawAnchoredString( DisplayHandle, TextAnchor_Center, HelloText, SSD_COLOR_WHITE );
   SSD1306_Update( DisplayHandle );
}


void DecodeGateWayMessage( char* buf ){
   if(strstr(buf, "ON")){
      printf("RELE ON\n");
      gb_temp_ref = 1000;
   }
   else if(strstr(buf, "OFF")){
      printf("RELE OFF\n");
      gb_temp_ref = 0;
   }
   else if(strstr(buf, "ACK")){
      xEventGroupSetBits(lora_event, LORA_ACK);
   }
   else if(strlen( (char*) buf) == 2 ){
      gb_temp_ref = atoi(buf);
      printf("gb_temp_ref = %d\n", gb_temp_ref);
   }
}


void lora_send(void *p, char* mybuf){
   if( xSemaphoreTake(xMutex, (TickType_t)0xFFFFFFFF) == 1 ){
      lora_idle();
      lora_disable_invertiq();

      lora_send_packet( (uint8_t*)mybuf, 12 );
      printf("lora send: %s\n", mybuf);

      lora_enable_invertiq();
      lora_receive();
      
      xSemaphoreGive(xMutex);
   }
}



void DHT_task(void *pvParameter)
{

}

void app_main()
{
   setDHTgpio( 23 );
   char payload[16];
   char stemp[16];
   myQueue = xQueueCreate(3, sizeof(payload));
   float gb_temp = 0, gb_humi = 0;
   double ini_time;
   int cont = 0;

   uint8_t buf[32];
   int x;

   void *p = "b";
   char sendbuf[16];
   uint32_t lora_result; 

   lora_init();
   lora_set_frequency(915e6);
   lora_enable_crc();
   
   gpio_pad_select_gpio(GPIO_NUM_17); 
   gpio_set_direction(GPIO_NUM_17,GPIO_MODE_OUTPUT);

   xMutex = xSemaphoreCreateMutex();
   lora_event = xEventGroupCreate();
   timer_init(TIMER_GROUP_0, timer, &timer_configurations);

   if ( DefaultBusInit( ) == true ) {
      printf( "BUS Init lookin good...\n" );
      SetupDemo( &I2CDisplay, &Font_liberation_mono_17x30 );
   }


	while(1) {
      float temp = 0, humi = 0;
		
		while( readDHT() != DHT_OK);

      // for (int i = 0; i < 10; i++)
      // {
      //    temp += getTemperature();
      //    humi += getHumidity();
      //    ets_delay_us(1000);
      // }
      // temp = (temp/10);
      // humi = (humi/10);
      temp = getTemperature();
      humi = getHumidity();


      if ((abs(temp - gb_temp) >= 0.2 || abs(humi - gb_humi) >= 0.2) && temp >= 20)
      {
         gb_temp = temp;
         gb_humi = humi;

         sprintf(payload, "%.1f-%.1f\n", gb_temp, gb_humi);
         sprintf(stemp, "A=%.1f", gb_temp);
         xQueueSend(myQueue, (void*) payload, (TickType_t) 0);
         SayHello( &I2CDisplay, stemp );
      }

      if(gb_temp > gb_temp_ref + 0.2) CNT_OFF;
      if(gb_temp < gb_temp_ref - 0.2) CNT_ON;





      if( xSemaphoreTake(xMutex, (TickType_t)0xFFFFFFFF) ==1 ){
         lora_enable_invertiq();
         lora_receive();    // put into receive mode
         while(lora_received()) {
            x = lora_receive_packet(buf, sizeof(buf));
            buf[x] = 0;
            printf("Received: %s\n", buf);
            lora_receive();
            DecodeGateWayMessage( (char*)buf );
         }
         xSemaphoreGive(xMutex);
      }

      lora_result = xEventGroupWaitBits(lora_event, LORA_ALL, pdTRUE, pdTRUE, pdMS_TO_TICKS(10));
      if (lora_result == LORA_ALL){
      }
      else if (lora_result == LORA_SENT){
         lora_send(p, sendbuf);
         printf("SENT DATA AGAIN\n");
      }
      else if (lora_result == LORA_ACK){
         printf("ACK NOT EXPECTED\n");
         xEventGroupClearBits(lora_event, LORA_ACK);
      }
      else if (!(lora_result)){
         if(myQueue != 0){
            if(xQueueReceive(myQueue, (void*) sendbuf, (TickType_t) 5)){
               header_lora_msg( (uint8_t*)p, (uint8_t*) sendbuf );
               lora_send(p, sendbuf);
               xEventGroupSetBits(lora_event, LORA_SENT);
            }
         }
      }
      
      timer_get_counter_time_sec(TIMER_GROUP_0, timer, &ini_time);
      if (ini_time > 60){
         printf("AMOSTRAS: %d\n", cont);
         cont = 0;
         timer_set_counter_value( TIMER_GROUP_0, timer,  0 );
      }
      else cont++;
   }
}