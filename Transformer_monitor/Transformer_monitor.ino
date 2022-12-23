#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h> //Version 1.1.2
#include "ThingSpeak.h" //Version 2.0.1
#include "RTClib.h" //Version 1.3.3
#include "FS.h"
#include "SD.h"
#include "SPI.h"

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

//Variables used for intertask communication
float pzemVoltage = 0; //in volts
float pzemCurrent = 0; //in amps
float pzemPower = 0; //in watts
float pzemEnergy = 0; //in kWh 

//Task handle(s)
TaskHandle_t wifiTaskHandle;
TaskHandle_t lcdTaskHandle;
TaskHandle_t dataLogTaskHandle;

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

/*
 * @brief Appends data to a file stored in an SD card
 * @param path: path to the file to be written
 * @param message: data to be appended to the file
 * @return None
*/
static void SD_AppendFile(const char* path,const char* message)
{
  File file = SD.open(path,FILE_APPEND);
  file.print(message);
  file.close();
}

/**
 * @brief Suspend all tasks (except application task) 
 * pinned to specified core.
*/
static void SuspendPinnedTasks(void)
{
  vTaskSuspend(wifiTaskHandle); 
  vTaskSuspend(lcdTaskHandle); 
  vTaskSuspend(dataLogTaskHandle); 
}

/**
 * @brief Resume all tasks (except application task) 
 * pinned to specified core.
*/
static void ResumePinnedTasks(void)
{
  vTaskResume(wifiTaskHandle); 
  vTaskResume(lcdTaskHandle);
  vTaskResume(dataLogTaskHandle); 
}

void setup() 
{
  const uint8_t chipSelectPin = 5;
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  preferences.begin("T-Mon",false); //T-Mon : Transformer monitor 
  //SD: Uses SPI pins 23(MOSI),19(MISO),18(CLK) and 5(CS)
  pinMode(chipSelectPin,OUTPUT);
  digitalWrite(chipSelectPin,HIGH);
  if(SD.begin(chipSelectPin))
  {
    Serial.println("SD Init: SUCCESS");
  }
  else
  {
    Serial.println("SD Init: FAILURE");
  }  
  //Create tasks
  xTaskCreatePinnedToCore(WiFiManagementTask,"",7000,NULL,1,&wifiTaskHandle,1);
  xTaskCreatePinnedToCore(ApplicationTask,"",60000,NULL,1,NULL,1);
  xTaskCreatePinnedToCore(LcdTask,"",8000,NULL,1,&lcdTaskHandle,1);
  xTaskCreatePinnedToCore(LocalDataLogTask,"",15000,NULL,1,&dataLogTaskHandle,1);
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
 * @brief 
 * - Gets PZEM-004T data.
 * 
 * - Uploads PZEM-004T module's data to Google sheets and ThingSpeak
 *   via HTTP requests. IFTTT serves as an intermediary 
 *   between the ESP32 and Google sheets.  
*/
void ApplicationTask(void* pvParameters)
{
  static WiFiClient wifiClient;
  static PZEM004Tv30 pzem(&Serial2,16,17);
  ThingSpeak.begin(wifiClient);
  
  //Previously stored data (in ESP32's flash)
  char prevEventName[SIZE_EVENT_NAME] = {0};
  char prevIftttKey[SIZE_IFTTT_KEY] = {0};
  char prevChannelId[SIZE_CHANNEL_ID] = {0};
  char prevApiKey[SIZE_API_KEY] = {0};
  
  uint32_t prevPzemTime = millis();  
  uint32_t prevConnectTime = millis();
 
  while(1)
  {
    //Critical section [Get data from PZEM module periodically]
    if((millis() - prevPzemTime) >= 1000)
    {
      SuspendPinnedTasks();
      pzemVoltage = pzem.voltage(); //in volts
      pzemCurrent = pzem.current(); //in amps
      pzemPower = pzem.power(); //in watts
      pzemEnergy = pzem.energy(); //in kWh
      ResumePinnedTasks();
      prevPzemTime = millis();
    }
          
    if(WiFi.status() == WL_CONNECTED && ((millis() - prevConnectTime) >= 20000))
    {
      preferences.getBytes("0",prevEventName,SIZE_EVENT_NAME);
      preferences.getBytes("1",prevIftttKey,SIZE_IFTTT_KEY);  
      preferences.getBytes("2",prevChannelId,SIZE_CHANNEL_ID);
      preferences.getBytes("3",prevApiKey,SIZE_API_KEY); 

      //Debug PZEM
      Serial.print("Voltage: ");
      Serial.println(pzemVoltage);
      Serial.print("Current: ");
      Serial.println(pzemCurrent);
      Serial.print("Power: ");
      Serial.println(pzemPower);
      Serial.print("Energy: ");
      Serial.println(pzemEnergy,3); //3dp
      
      //Encode PZEM data to be sent to ThingSpeak
      ThingSpeak.setField(1,pzemVoltage);
      ThingSpeak.setField(2,pzemCurrent); 
      ThingSpeak.setField(3,pzemPower); 
      ThingSpeak.setField(4,pzemEnergy);
      
      //Convert channel ID from string to integer
      String idString = String(prevChannelId);
      uint32_t idInteger = idString.toInt();
      //Critical section [Send data to ThingSpeak]
      SuspendPinnedTasks();
      int httpCode = ThingSpeak.writeFields(idInteger,prevApiKey); 
      ResumePinnedTasks();
      
      //Check HTTP response from ThingSpeak
      if(httpCode == HTTP_CODE_OK)
      {
        Serial.println("THINGSPEAK: HTTP request successful");
      }
      else
      {
        Serial.println("THINGSPEAK: HTTP error");
      }
           
      String iftttServerPath = "http://maker.ifttt.com/trigger/" + String(prevEventName) + 
                        "/with/key/" + String(prevIftttKey) + "?value1=" + String(pzemVoltage) 
                        + "&value2=" + String(pzemCurrent) +"&value3=" + String(pzemPower)
                        + "&value4=" + String(pzemEnergy,3); //3dp for energy 
      //Critical section [Send PZEM data to IFTTT (IFTTT sends the data to Google Sheets)]                   
      SuspendPinnedTasks();                                    
      HttpGetRequest(iftttServerPath.c_str());
      ResumePinnedTasks();
   
      prevConnectTime = millis(); 
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief Displays PZEM-004T's readings on the LCD.
*/
void LcdTask(void* pvParameters)
{
  static LiquidCrystal_I2C lcd(0x27,16,2); 
  //LCD init and startup message
  lcd.init();
  lcd.backlight();
  lcd.setCursor(1,0);
  lcd.print("Energy Monitor");
  vTaskDelay(pdMS_TO_TICKS(1500)); 
  lcd.clear();
  //Simple FSM to periodically change parameters being displayed.
  const uint8_t displayState1 = 0;
  const uint8_t displayState2 = 1;
  uint8_t displayState = displayState1;

  uint32_t prevTime = millis();
  
  while(1)
  {
    lcd.setCursor(0,0);
    switch(displayState)
    {
      case displayState1: //Display voltage and current
        lcd.print("Volts: ");
        lcd.print(pzemVoltage,2);
        lcd.setCursor(0,1);
        lcd.print("Amps: ");
        lcd.print(pzemCurrent,2);
        if((millis() - prevTime) >= 4000)
        {
          displayState = displayState2;
          prevTime = millis();
          lcd.clear();
        }
        break;
        
      case displayState2: //Display power and energy
        lcd.print("Watts: ");
        lcd.print(pzemPower,2);
        lcd.setCursor(0,1);
        lcd.print("kWh: ");
        lcd.print(pzemEnergy,3);
        if((millis() - prevTime) >= 4000)
        {
          displayState = displayState1;
          prevTime = millis();
          lcd.clear();
        }
        break;
    }
  }
}

/**
 * @brief Handles offline data logging.  
 * Stores data on an SD card (with timestamp).
*/
void LocalDataLogTask(void* pvParameters)
{
  static RTC_DS3231 rtc;
  rtc.begin();
  uint32_t prevTime = millis();
  
  while(1)
  {
    if((millis() - prevTime) >= 10000)
    {
      Serial.println("Logging to SD card");
      /*TO-DO: Add code to store PZEM data (with timestamp) on SD card*/
      String sdCardData = ""; //Concatenate time,date and PZEM readings
      SD_AppendFile("/temp_file.txt",sdCardData.c_str());
      prevTime = millis();
    }
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

