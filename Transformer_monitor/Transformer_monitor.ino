#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h> //Version 1.1.2
#include "ThingSpeak.h" //Version 2.0.1

//Defines
//Maximum number of characters for IFTTT details
#define SIZE_EVENT_NAME        20
#define SIZE_IFTTT_KEY         50
//Maximum number of characters for ThingSpeak credentials
#define SIZE_CHANNEL_ID        30
#define SIZE_API_KEY           50

//Shared resources
//Define textboxes for IFTTT event name and key  
WiFiManagerParameter eventName("0","Event name","",SIZE_EVENT_NAME);
WiFiManagerParameter iftttKey("1","IFTTT key","",SIZE_IFTTT_KEY);
WiFiManagerParameter channelId("2","ThingSpeak channel ID","",SIZE_CHANNEL_ID);
WiFiManagerParameter apiKey("3","ThingSpeak API key","",SIZE_API_KEY);
Preferences preferences; //for accessing ESP32 flash memory

//Task handle(s)
TaskHandle_t wifiTaskHandle;

/**
 * @brief Make an HTTP GET request to the specified server
*/
static void HttpGetRequest(const char* serverName) 
{ 
  HTTPClient http;
  http.begin(serverName);
  int httpResponseCode = http.GET();
  if(httpResponseCode == HTTP_CODE_OK) 
  {
    Serial.println("Successful request ");
  } 
  else 
  {
    Serial.print("HTTP error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();  
}

/**
 * @brief Store new data (e.g. IFTTT and ThingSpeak credentials) to specified 
 * location in ESP32's flash memory if the new credentials are different from 
 * the previous.  
*/
static void StoreNewFlashData(const char* flashLoc,const char* newData,
                              const char* oldData,uint8_t dataSize)
{
  if(strcmp(newData,"") && strcmp(newData,oldData))
  {
    preferences.putBytes(flashLoc,newData,dataSize);
  }
}

void setup() 
{
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  preferences.begin("T-Mon",false); //T-Mon : Transformer monitor
  //Create tasks
  xTaskCreatePinnedToCore(WiFiManagementTask,"",7000,NULL,1,&wifiTaskHandle,1);
  xTaskCreatePinnedToCore(ApplicationTask,"",25000,NULL,1,NULL,1);
}

void loop() 
{
}

/*
 * @brief Manages WiFi configurations (STA and AP modes). Connects
 * to an existing/saved network if available, otherwise it acts as
 * an AP in order to receive new network credentials.
*/
void WiFiManagementTask(void* pvParameters)
{
  const uint16_t accessPointTimeout = 50000; //millisecs
  static WiFiManager wm;
  WiFi.mode(WIFI_STA);  
  wm.addParameter(&eventName);
  wm.addParameter(&iftttKey);
  wm.addParameter(&channelId);
  wm.addParameter(&apiKey);  
  wm.setConfigPortalBlocking(false);
  wm.setSaveParamsCallback(WiFiManagerCallback);   
  //Auto-connect to previous network if available.
  //If connection fails, ESP32 goes from being a station to being an access point.
  Serial.print(wm.autoConnect("TRANSFORMER")); 
  Serial.println("-->WiFi status");   
  bool accessPointMode = false;
  uint32_t startTime = 0;    
  
  while(1)
  {
    wm.process();
    if(WiFi.status() != WL_CONNECTED)
    {
      if(!accessPointMode)
      {
        if(!wm.getConfigPortalActive())
        {
          wm.autoConnect("TRANSFORMER"); 
        }
        accessPointMode = true; 
        startTime = millis(); 
      }
      else
      {
        //reset after a timeframe (device shouldn't spend too long as an access point)
        if((millis() - startTime) >= accessPointTimeout)
        {
          Serial.println("\nAP timeout reached, system will restart for better connection");
          vTaskDelay(pdMS_TO_TICKS(1000));
          esp_restart();
        }
      }
    }
    else
    {
      if(accessPointMode)
      {   
        accessPointMode = false;
        Serial.println("Successfully connected, system will restart now");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
      }
    }    
  }
}

/**
 * @brief Gets PZEM-004T data, and displays on an LCD.
 * Uploads PZEM-004T module's data to Google sheets and ThingSpeak
 * via HTTP requests. IFTTT serves as an intermediary 
 * between the ESP32 and Google sheets.  
*/
void ApplicationTask(void* pvParameters)
{
  static WiFiClient wifiClient;
  ThingSpeak.begin(wifiClient);
    
  static LiquidCrystal_I2C lcd(0x27,16,2);
  //LCD init and startup message
  lcd.init();
  lcd.backlight();
  lcd.print("Energy monitor");
  vTaskDelay(pdMS_TO_TICKS(1500));
  lcd.clear();  

  char prevEventName[SIZE_EVENT_NAME] = {0};
  char prevIftttKey[SIZE_IFTTT_KEY] = {0};
  char prevChannelId[SIZE_CHANNEL_ID] = {0};
  char prevApiKey[SIZE_API_KEY] = {0};
    
  uint32_t prevTime = millis();

  while(1)
  {
    if(WiFi.status() == WL_CONNECTED && ((millis() - prevTime) >= 20000))
    {
      preferences.getBytes("0",prevEventName,SIZE_EVENT_NAME);
      preferences.getBytes("1",prevIftttKey,SIZE_IFTTT_KEY);  
      preferences.getBytes("2",prevChannelId,SIZE_CHANNEL_ID);
      preferences.getBytes("3",prevApiKey,SIZE_API_KEY); 
      /*TO-DO: ADD CODE TO GET PZEM DATA*/

      /*TO-DO: ADD CODE TO DISPLAY DATA ON LCD*/

      //Encode PZEM data to be sent to ThingSpeak
      ThingSpeak.setField(1,112); //replace 2nd argument with PZEM data
      ThingSpeak.setField(2,225); //replace 2nd argument with PZEM data
      ThingSpeak.setField(3,777); //replace 2nd argument with PZEM data
      //Convert channel ID from string to integer
      String idString = String(prevChannelId);
      uint32_t idInteger = idString.toInt();
      
      //Critical section [Send data to ThingSpeak]
      vTaskSuspend(wifiTaskHandle);
      int httpCode = ThingSpeak.writeFields(idInteger,prevApiKey); 
      vTaskResume(wifiTaskHandle);
      
      if(httpCode == HTTP_CODE_OK)
      {
        Serial.println("THINGSPEAK: HTTP request successful");
      }
      else
      {
        Serial.println("THINGSPEAK: HTTP error");
      }
           
      String iftttServerPath = "http://maker.ifttt.com/trigger/" + String(prevEventName) + 
                         "/with/key/" + String(prevIftttKey) + "?value1=" + String(112) 
                         + "&value2="+String(225) +"&value3=" + String(777); 
      //Critical section [Send PZEM data to IFTTT (IFTTT sends the data to Google Sheets)]                   
      vTaskSuspend(wifiTaskHandle);                                      
      HttpGetRequest(iftttServerPath.c_str());
      vTaskResume(wifiTaskHandle);
      
      prevTime = millis(); 
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/*
 * @brief Callback function that is called whenever WiFi
 * manager parameters are received (in this case, IFTTT and ThingSpeak
 * credentials)
*/
void WiFiManagerCallback(void) 
{
  char prevEventName[SIZE_EVENT_NAME] = {0};
  char prevIftttKey[SIZE_IFTTT_KEY] = {0};
  char prevChannelId[SIZE_CHANNEL_ID] = {0};
  char prevApiKey[SIZE_API_KEY] = {0};
  //Get previously stored data  
  preferences.getBytes("0",prevEventName,SIZE_EVENT_NAME);
  preferences.getBytes("1",prevIftttKey,SIZE_IFTTT_KEY);  
  preferences.getBytes("2",prevChannelId,SIZE_CHANNEL_ID);
  preferences.getBytes("3",prevApiKey,SIZE_API_KEY);   
  //Store data in flash if the new ones are not the same as the old.  
  StoreNewFlashData("0",eventName.getValue(),prevEventName,SIZE_EVENT_NAME);
  StoreNewFlashData("1",iftttKey.getValue(),prevIftttKey,SIZE_IFTTT_KEY);
  StoreNewFlashData("2",channelId.getValue(),prevChannelId,SIZE_CHANNEL_ID);
  StoreNewFlashData("3",apiKey.getValue(),prevApiKey,SIZE_API_KEY);  
}

