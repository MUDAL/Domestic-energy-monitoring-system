#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
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
//Maximum number of characters for HiveMQ topic(s)
#define SIZE_TOPIC             30

//Shared resources
WiFiManagerParameter eventName("0","Event name","",SIZE_EVENT_NAME);
WiFiManagerParameter iftttKey("1","IFTTT key","",SIZE_IFTTT_KEY);
WiFiManagerParameter channelId("2","ThingSpeak channel ID","",SIZE_CHANNEL_ID);
WiFiManagerParameter apiKey("3","ThingSpeak API key","",SIZE_API_KEY);
WiFiManagerParameter pubTopic("4","HiveMQ Publish topic","",SIZE_TOPIC);
Preferences preferences; //for accessing ESP32 flash memory

//Global variables
bool isWifiTaskSuspended = false;
float pzemVoltage = 0; //in volts
float pzemCurrent = 0; //in amps
float pzemPower = 0; //in watts
float pzemEnergy = 0; //in kWh 
bool isThingSpeakOk = false;
bool isIftttOk = false;
uint8_t resetEnergyCounter = 0; //Set if data is received from MQTT broker to reset PZEM's energy counter
bool isEnergyReset = false; //Set if PZEM's energy counter was successfully reset

//Task handle(s)
TaskHandle_t wifiTaskHandle;
TaskHandle_t dispLogTaskHandle;
TaskHandle_t mqttTaskHandle;

/**
 * @brief Make an HTTP GET request to the specified server
*/
static void HttpGetRequest(const char* serverName,int* httpResponseCode) 
{ 
  HTTPClient http;
  http.begin(serverName);
  *httpResponseCode = http.GET();
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

/**
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
 * @brief Suspend WiFi task if it wasn't previously suspended.
*/
static void SuspendWiFiTask(void)
{
  if(!isWifiTaskSuspended)
  {
    vTaskSuspend(wifiTaskHandle);
    isWifiTaskSuspended = true;
  }  
}

/**
 * @brief Resume WiFi task if it was previously suspended.
*/
static void ResumeWiFiTask(void)
{
  if(isWifiTaskSuspended)
  {
    vTaskResume(wifiTaskHandle);
    isWifiTaskSuspended = false;
  }
}

/**
 * @brief Notify the user if connection to a server (ThingSpeak or IFTTT) is
 * OK i.e. data was successfully sent to the server.
*/
static void NotifyUserIfConnIsOk(LiquidCrystal_I2C& lcd,char* server,bool* isServerOk)
{
  if(*isServerOk)
  {
    TickType_t prevTick = xTaskGetTickCount();
    lcd.clear();
    lcd.print(server);
    lcd.setCursor(0,1);
    lcd.print("SUCCESS");
    *isServerOk = false;
    vTaskDelayUntil(&prevTick,pdMS_TO_TICKS(1000));
    lcd.clear();
  }
}

void setup() 
{
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  preferences.begin("T-Mon",false); 
  //Create tasks
  xTaskCreatePinnedToCore(WiFiManagementTask,"",7000,NULL,1,&wifiTaskHandle,1);
  xTaskCreatePinnedToCore(ApplicationTask,"",50000,NULL,1,NULL,1);
  xTaskCreatePinnedToCore(DisplayAndLogTask,"",40000,NULL,1,&dispLogTaskHandle,1);
  xTaskCreatePinnedToCore(MqttTask,"",7000,NULL,1,&mqttTaskHandle,1);
}

void loop() 
{
}

/**
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
  wm.addParameter(&pubTopic);
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
  int httpCode; //HTTP response code
  //Previously stored data (in ESP32's flash)
  char prevEventName[SIZE_EVENT_NAME] = {0};
  char prevIftttKey[SIZE_IFTTT_KEY] = {0};
  char prevChannelId[SIZE_CHANNEL_ID] = {0};
  char prevApiKey[SIZE_API_KEY] = {0};
  
  uint32_t prevPzemTime = millis();  
  uint32_t prevConnectTime = millis();
 
  while(1)
  {
    //Suspend WiFi Management task if the system is already.... 
    //connected to a Wi-Fi network
    if(WiFi.status() == WL_CONNECTED && !isWifiTaskSuspended)
    {
      Serial.println("WIFI TASK: SUSPENDED");
      vTaskSuspend(wifiTaskHandle);
      isWifiTaskSuspended = true;
    }
    else if(WiFi.status() != WL_CONNECTED && isWifiTaskSuspended)
    {
      Serial.println("WIFI TASK: RESUMED");
      vTaskResume(wifiTaskHandle);
      isWifiTaskSuspended = false;
    }
    
    //Reset PZEM's energy counter if MQTT command is received
    if(resetEnergyCounter)
    {
      isEnergyReset = pzem.resetEnergy();
      resetEnergyCounter = 0;
    }
    
    //Critical section [Get data from PZEM module periodically]
    if((millis() - prevPzemTime) >= 1000)
    {
      SuspendWiFiTask();
      vTaskSuspend(mqttTaskHandle);
      vTaskSuspend(dispLogTaskHandle);
      pzemVoltage = pzem.voltage(); //in volts
      pzemCurrent = pzem.current(); //in amps
      pzemPower = pzem.power(); //in watts
      pzemEnergy = pzem.energy(); //in kWh
      ResumeWiFiTask();
      vTaskResume(mqttTaskHandle);
      vTaskResume(dispLogTaskHandle);
      prevPzemTime = millis();
    }
    
    //Send data to the cloud [periodically]      
    if(WiFi.status() == WL_CONNECTED && ((millis() - prevConnectTime) >= 20000))
    {
      preferences.getBytes("0",prevEventName,SIZE_EVENT_NAME);
      preferences.getBytes("1",prevIftttKey,SIZE_IFTTT_KEY);  
      preferences.getBytes("2",prevChannelId,SIZE_CHANNEL_ID);
      preferences.getBytes("3",prevApiKey,SIZE_API_KEY); 
      
      //Encode PZEM data to be sent to ThingSpeak
      ThingSpeak.setField(1,pzemVoltage);
      ThingSpeak.setField(2,pzemCurrent); 
      ThingSpeak.setField(3,pzemPower); 
      ThingSpeak.setField(4,pzemEnergy);
      //Convert channel ID from string to integer
      String idString = String(prevChannelId);
      uint32_t idInteger = idString.toInt();
      
      //[Send data to ThingSpeak]
      httpCode = ThingSpeak.writeFields(idInteger,prevApiKey); 
      //Check HTTP response from ThingSpeak
      if(httpCode == HTTP_CODE_OK)
      {
        isThingSpeakOk = true;
      }
      else
      {
        isThingSpeakOk = false;
      }  
       
      String iftttServerPath = "http://maker.ifttt.com/trigger/" + String(prevEventName) + 
                               "/with/key/" + String(prevIftttKey) + 
                               "?value1=" + String(pzemVoltage,2) + "V" +
                               "&value2=" + String(pzemCurrent,2) + "A" +
                               "&value3=" + String(pzemPower,2) + "W_" +
                               String(pzemEnergy,3) + "kWh";  
      
      //[Send PZEM data to IFTTT (IFTTT sends the data to Google Sheets)]                                                      
      HttpGetRequest(iftttServerPath.c_str(),&httpCode);
      //Check HTTP response from IFTTT
      if(httpCode == HTTP_CODE_OK) 
      {
        isIftttOk = true;
      } 
      else 
      {
        isIftttOk = false;
      }     
      prevConnectTime = millis(); 
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief Displays PZEM-004T's readings on the LCD. 
 * Stores data on an SD card (with date and time).
*/
void DisplayAndLogTask(void* pvParameters)
{   
  static LiquidCrystal_I2C lcd(0x27,16,2); 
  static RTC_DS3231 rtc;  
  rtc.begin();
    
  //LCD init and startup message
  lcd.init();
  lcd.backlight();
  lcd.setCursor(1,0);
  lcd.print("Energy Monitor");
  vTaskDelay(pdMS_TO_TICKS(1500)); 
  lcd.clear();

  //SD Init
  //SD: Uses SPI pins 23(MOSI),19(MISO),18(CLK) and 5(CS)
  const uint8_t chipSelectPin = 5;
  pinMode(chipSelectPin,OUTPUT);
  digitalWrite(chipSelectPin,HIGH);
  if(SD.begin(chipSelectPin))
  {
    Serial.println("SD INIT: SUCCESS");
    lcd.print("SD INIT: ");
    lcd.setCursor(0,1);
    lcd.print("SUCCESS");
  }
  else
  {
    Serial.println("SD INIT: FAILURE");
    lcd.print("SD INIT: ");
    lcd.setCursor(0,1);
    lcd.print("FAILURE");  
  }
  vTaskDelay(pdMS_TO_TICKS(1500)); 
  lcd.clear();
  
  //Simple FSM to periodically change parameters being displayed.
  const uint8_t displayState1 = 0;
  const uint8_t displayState2 = 1;
  uint8_t displayState = displayState1;
  
  uint32_t prevTime = millis();
  uint32_t prevLogTime = millis();
  
  while(1)
  {
    //Display PZEM readings
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
    NotifyUserIfConnIsOk(lcd,"ThingSpeak",&isThingSpeakOk);
    NotifyUserIfConnIsOk(lcd,"IFTTT",&isIftttOk);
    //Log data to SD card periodically
    if((millis() - prevLogTime) >= 20000)
    {
      //Get current date and time and concatenate with PZEM readings
      DateTime dateTime = rtc.now();
      String sdCardData = String(dateTime.day()) + "/" + String(dateTime.month()) + "/" + 
                          String(dateTime.year()) + " " + String(dateTime.hour()) + ":" + 
                          String(dateTime.minute()) + " ---> " + String(pzemVoltage,2) + "V, " + 
                          String(pzemCurrent,2) + "A, " + String(pzemPower,2) + "W, " + 
                          String(pzemEnergy,3) + "kWh\n";   
      SD_AppendFile("/project_file.txt",sdCardData.c_str());
      TickType_t prevTick = xTaskGetTickCount();
      lcd.clear();
      lcd.print("LOGGING TO SD");
      prevLogTime = millis();
      vTaskDelayUntil(&prevTick,pdMS_TO_TICKS(1000));
      lcd.clear();      
    }     
    //Display message if PZEM's energy counter was successfully reset
    if(isEnergyReset)
    {
      TickType_t prevTick = xTaskGetTickCount();
      lcd.clear();
      lcd.print("ENERGY RESET:");
      lcd.setCursor(0,1);
      lcd.print("SUCCESS");
      isEnergyReset = false;
      vTaskDelayUntil(&prevTick,pdMS_TO_TICKS(1000));
      lcd.clear();
    }
  }
}

/**
 * @brief Handles communication with the HiveMQ broker.
*/
void MqttTask(void* pvParameters)
{
  static WiFiClient wifiClient;
  static PubSubClient mqttClient(wifiClient);
  char prevPubTopic[SIZE_TOPIC] = {0};
  const char *mqttBroker = "broker.hivemq.com";
  const uint16_t mqttPort = 1883;  
  
  while(1)
  {
    if(WiFi.status() == WL_CONNECTED)
    {       
      if(!mqttClient.connected())
      {
        preferences.getBytes("4",prevPubTopic,SIZE_TOPIC);  
        mqttClient.setServer(mqttBroker,mqttPort);
        mqttClient.setCallback(MqttCallback);
        while(!mqttClient.connected())
        {
          String clientID = String(WiFi.macAddress());
          Serial.print("MAC address = ");
          Serial.println(clientID.c_str());
          if(mqttClient.connect(clientID.c_str()))
          {
            Serial.println("Connected to HiveMQ broker");
            mqttClient.subscribe(prevPubTopic);
          }
        } 
      }
      else
      {
        mqttClient.loop(); //handles mqtt callback
      }
    }
  }
}

/**
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
  char prevPubTopic[SIZE_TOPIC] = {0};
  //Get previously stored data  
  preferences.getBytes("0",prevEventName,SIZE_EVENT_NAME);
  preferences.getBytes("1",prevIftttKey,SIZE_IFTTT_KEY);  
  preferences.getBytes("2",prevChannelId,SIZE_CHANNEL_ID);
  preferences.getBytes("3",prevApiKey,SIZE_API_KEY); 
  preferences.getBytes("4",prevPubTopic,SIZE_TOPIC);    
  //Store data in flash if the new ones are not the same as the old.  
  StoreNewFlashData("0",eventName.getValue(),prevEventName,SIZE_EVENT_NAME);
  StoreNewFlashData("1",iftttKey.getValue(),prevIftttKey,SIZE_IFTTT_KEY);
  StoreNewFlashData("2",channelId.getValue(),prevChannelId,SIZE_CHANNEL_ID);
  StoreNewFlashData("3",apiKey.getValue(),prevApiKey,SIZE_API_KEY);  
  StoreNewFlashData("4",pubTopic.getValue(),prevPubTopic,SIZE_TOPIC);
}

/**
 * @brief Callback function that is called whenever data is received
 * from the HiveMQ broker.  
*/
void MqttCallback(char *topic,byte *payload,uint32_t len) 
{
  Serial.print("MQTT receive:");
  resetEnergyCounter = (payload[0] - '0');
  Serial.println(resetEnergyCounter);
}
