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

//Maximum number of characters
#define SIZE_EVENT_NAME        20
#define SIZE_IFTTT_KEY         50
#define SIZE_CHANNEL_ID        30
#define SIZE_API_KEY           50
#define SIZE_TOPIC             30
#define SIZE_VOLTAGE            7
#define SIZE_CURRENT            7
#define SIZE_POWER             11
#define SIZE_ENERGY            11 

//Shared resources
WiFiManagerParameter eventName("0","Event name","",SIZE_EVENT_NAME);
WiFiManagerParameter iftttKey("1","IFTTT key","",SIZE_IFTTT_KEY);
WiFiManagerParameter channelId("2","ThingSpeak channel ID","",SIZE_CHANNEL_ID);
WiFiManagerParameter apiKey("3","ThingSpeak API key","",SIZE_API_KEY);
WiFiManagerParameter pubTopic("4","HiveMQ Publish topic","",SIZE_TOPIC);
WiFiManagerParameter voltageLimit("5","Voltage limit (V)","",SIZE_VOLTAGE);
WiFiManagerParameter currentLimit("6","Current limit (A)","",SIZE_CURRENT);
WiFiManagerParameter powerLimit("7","Power limit (W)","",SIZE_POWER);
WiFiManagerParameter energyLimit("8","Energy limit (kWh)","",SIZE_ENERGY);
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
    lcd.clear();
    lcd.print(server);
    lcd.setCursor(0,1);
    lcd.print("SUCCESS");
    *isServerOk = false;
    vTaskDelay(pdMS_TO_TICKS(1500));
    lcd.clear();
  }
}

/**
 * @brief Converts an integer to a string.
*/
static void IntegerToString(uint32_t integer,char* stringPtr)
{
  if(integer == 0)
  {  
    stringPtr[0] = '0';
    return;
  }
  uint32_t integerCopy = integer;
  uint8_t numOfDigits = 0;

  while(integerCopy > 0)
  {
    integerCopy /= 10;
    numOfDigits++;
  }
  while(integer > 0)
  {
    stringPtr[numOfDigits - 1] = '0' + (integer % 10);
    integer /= 10;
    numOfDigits--;
  }
}

/**
 * @brief Converts a float to a string.
*/
static void FloatToString(float floatPt,char* stringPtr,uint32_t multiplier)
{
  uint32_t floatAsInt = lround(floatPt * multiplier);
  char quotientBuff[20] = {0};
  char remainderBuff[20] = {0};
  IntegerToString((floatAsInt / multiplier),quotientBuff);
  IntegerToString((floatAsInt % multiplier),remainderBuff);
  strcat(stringPtr,quotientBuff);
  strcat(stringPtr,".");
  strcat(stringPtr,remainderBuff);
}

/**
 * @brief Converts a string to an integer.
 * e.g.
 * "932" to 932:
 * '9' - '0' = 9
 * '3' - '0' = 3
 * '2' - '0' = 2
 * 9 x 10^2 + 3 x 10^1 + 2 x 10^0 = 932.
*/
static void StringToInteger(char* stringPtr,uint32_t* integerPtr)
{
  *integerPtr = 0;
  uint8_t len = strlen(stringPtr);
  uint32_t j = 1;
  for(uint8_t i = 0; i < len; i++)
  {
    *integerPtr += ((stringPtr[len - i - 1] - '0') * j);
    j *= 10;
  }
}

/**
 * @brief Converts a string to a float
*/
static void StringToFloat(char* stringPtr,float* floatPtr)
{
  uint8_t decimalPointIndex = 255; //Invalid index
  uint8_t len = strlen(stringPtr);
  
  for(uint8_t i = 0; i < len; i++)
  {
    if(stringPtr[i] == '.')
    {
      decimalPointIndex = i;
      break;
    }
  }
  if(decimalPointIndex == 255)
  {
    uint32_t integer = 0;
    StringToInteger(stringPtr,&integer);
    *floatPtr = (float)integer;
  }
  else
  {
    char quotientBuff[20] = {0};
    char remainderBuff[20] = {0};
    uint32_t quotient = 0;
    uint32_t remainder = 0;
    uint32_t multiplier = 1;
    uint8_t multiplierLen = len - 1 - decimalPointIndex;

    for(uint8_t i = 0; i < multiplierLen; i++)
    {
      multiplier *= 10;
    }
    for(uint8_t i = 0; i < decimalPointIndex; i++)
    {
      quotientBuff[i] = stringPtr[i];
    }
    for(uint8_t i = decimalPointIndex + 1; i < len; i++)
    {
      remainderBuff[i - decimalPointIndex - 1] = stringPtr[i];
    }

    StringToInteger(quotientBuff,&quotient);
    StringToInteger(remainderBuff,&remainder);
    uint32_t floatAsInt = ((quotient * multiplier) + remainder);
    *floatPtr = (float)floatAsInt / multiplier;
  }
}


/**
 * @brief Set float to zero if it is not a number.
*/
static void SetToZeroIfNaN(float* floatPtr)
{
  if(isnan(*floatPtr))
  {
    *floatPtr = 0.0;
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
  wm.addParameter(&voltageLimit);
  wm.addParameter(&currentLimit);
  wm.addParameter(&powerLimit);
  wm.addParameter(&energyLimit);    
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
  static char iftttServerPath[300];
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
      
      SetToZeroIfNaN(&pzemVoltage);
      SetToZeroIfNaN(&pzemCurrent);
      SetToZeroIfNaN(&pzemPower);
      SetToZeroIfNaN(&pzemEnergy);
      
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
      
      uint32_t idInteger = 0;
      StringToInteger(prevChannelId,&idInteger); //Convert channel ID from string to integer
      httpCode = ThingSpeak.writeFields(idInteger,prevApiKey); //Send data to ThingSpeak
      if(httpCode == HTTP_CODE_OK)
      {
        isThingSpeakOk = true;
      }
      else
      {
        isThingSpeakOk = false;
      }  

      char voltageBuff[SIZE_VOLTAGE] = {0};
      char currentBuff[SIZE_CURRENT] = {0};
      char powerBuff[SIZE_POWER] = {0};
      char energyBuff[SIZE_ENERGY] = {0};
      
      FloatToString(pzemVoltage,voltageBuff,100);
      FloatToString(pzemCurrent,currentBuff,100);
      FloatToString(pzemPower,powerBuff,100);
      FloatToString(pzemEnergy,energyBuff,1000);

      strcat(iftttServerPath,"http://maker.ifttt.com/trigger/");
      strcat(iftttServerPath,prevEventName);
      strcat(iftttServerPath,"/with/key/");
      strcat(iftttServerPath,prevIftttKey);
      strcat(iftttServerPath,"?value1=");
      strcat(iftttServerPath,voltageBuff);
      strcat(iftttServerPath,"V&value2=");
      strcat(iftttServerPath,currentBuff);
      strcat(iftttServerPath,"A&value3=");
      strcat(iftttServerPath,powerBuff);
      strcat(iftttServerPath,"W_");
      strcat(iftttServerPath,energyBuff);
      strcat(iftttServerPath,"kWh");
      
      HttpGetRequest(iftttServerPath,&httpCode); //Send PZEM data to IFTTT
      uint32_t iftttServerPathLen = strlen(iftttServerPath);
      memset(iftttServerPath,'\0',iftttServerPathLen);
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
  static char sdCardData[120];
  static DateTime dateTime;
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
      dateTime = rtc.now(); 
                          
      char dayBuff[3] = {0};
      char monthBuff[3] = {0};
      char yearBuff[5] = {0};
      char hourBuff[3] = {0};
      char minuteBuff[3] = {0};
      char voltageBuff[SIZE_VOLTAGE] = {0};
      char currentBuff[SIZE_CURRENT] = {0};
      char powerBuff[SIZE_POWER] = {0};
      char energyBuff[SIZE_ENERGY] = {0};
      
      IntegerToString(dateTime.day(),dayBuff);
      IntegerToString(dateTime.month(),monthBuff);
      IntegerToString(dateTime.year(),yearBuff);
      IntegerToString(dateTime.hour(),hourBuff);
      IntegerToString(dateTime.minute(),minuteBuff);
      FloatToString(pzemVoltage,voltageBuff,100);
      FloatToString(pzemCurrent,currentBuff,100);
      FloatToString(pzemPower,powerBuff,100);
      FloatToString(pzemEnergy,energyBuff,1000);

      strcat(sdCardData,dayBuff);
      strcat(sdCardData,"/");
      strcat(sdCardData,monthBuff);
      strcat(sdCardData,"/");
      strcat(sdCardData,yearBuff);
      strcat(sdCardData," ");
      strcat(sdCardData,hourBuff);
      strcat(sdCardData,":");
      strcat(sdCardData,minuteBuff);
      strcat(sdCardData," ---> ");
      strcat(sdCardData,voltageBuff);
      strcat(sdCardData,"V, ");
      strcat(sdCardData,currentBuff);
      strcat(sdCardData,"A, ");
      strcat(sdCardData,powerBuff);
      strcat(sdCardData,"W, ");
      strcat(sdCardData,energyBuff);
      strcat(sdCardData,"kWh\n");
      
      SD_AppendFile("/project_file.txt",sdCardData);
      uint32_t sdCardDataLen = strlen(sdCardData);
      memset(sdCardData,'\0',sdCardDataLen);
      
      lcd.clear();
      lcd.print("LOGGING TO SD");
      prevLogTime = millis();
      vTaskDelay(pdMS_TO_TICKS(1500));
      lcd.clear();      
    }     
    //Display message if PZEM's energy counter was successfully reset
    if(isEnergyReset)
    {
      lcd.clear();
      lcd.print("ENERGY RESET:");
      lcd.setCursor(0,1);
      lcd.print("SUCCESS");
      isEnergyReset = false;
      vTaskDelay(pdMS_TO_TICKS(1500));
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
  static char dataToPublish[600];
  
  char prevPubTopic[SIZE_TOPIC] = {0};
  const char *mqttBroker = "broker.hivemq.com";
  const char* brokerSubTopic = "Limits";
  const uint16_t mqttPort = 1883;  
  uint32_t prevTime = millis();
            
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
        if((millis() - prevTime) >= 1000)
        {
          char prevVoltageLimit[SIZE_VOLTAGE] = {0};
          char prevCurrentLimit[SIZE_CURRENT] = {0};
          char prevPowerLimit[SIZE_POWER] = {0};
          char prevEnergyLimit[SIZE_ENERGY] = {0};  
           
          preferences.getBytes("5",prevVoltageLimit,SIZE_VOLTAGE);
          preferences.getBytes("6",prevCurrentLimit,SIZE_CURRENT);
          preferences.getBytes("7",prevPowerLimit,SIZE_POWER);
          preferences.getBytes("8",prevEnergyLimit,SIZE_ENERGY); 
        
          strcat(dataToPublish,"LIMITS: ");
          strcat(dataToPublish,prevVoltageLimit);
          strcat(dataToPublish,"V\n");
          strcat(dataToPublish,prevCurrentLimit);
          strcat(dataToPublish,"A\n");          
          strcat(dataToPublish,prevPowerLimit);
          strcat(dataToPublish,"W\n"); 
          strcat(dataToPublish,prevEnergyLimit);
          strcat(dataToPublish,"kWh\n\n"); 

          float voltageLim = 0;
          float currentLim = 0;
          float powerLim = 0;
          float energyLim = 0;
          
          StringToFloat(prevVoltageLimit,&voltageLim);
          StringToFloat(prevCurrentLimit,&currentLim);
          StringToFloat(prevPowerLimit,&powerLim);
          StringToFloat(prevEnergyLimit,&energyLim);

          bool isVoltageAbnormal = lround(pzemVoltage * 100) > lround(voltageLim * 100);
          bool isCurrentAbnormal = lround(pzemCurrent * 100) > lround(currentLim * 100);
          bool isPowerAbnormal = lround(pzemPower * 100) > lround(powerLim * 100);
          bool isEnergyAbnormal = lround(pzemEnergy * 1000) > lround(energyLim * 1000);
          
          if(isVoltageAbnormal)
          {
            char voltageBuff[SIZE_VOLTAGE] = {0};
            FloatToString(pzemVoltage,voltageBuff,100);
            strcat(dataToPublish,"Voltage: ");
            strcat(dataToPublish,voltageBuff);
            strcat(dataToPublish,"V (LIMIT EXCEEDED!!!)\n");
          }
          if(isCurrentAbnormal)
          {
            char currentBuff[SIZE_CURRENT] = {0};
            FloatToString(pzemCurrent,currentBuff,100);
            strcat(dataToPublish,"Current: ");
            strcat(dataToPublish,currentBuff);
            strcat(dataToPublish,"A (LIMIT EXCEEDED!!!)\n");
          }
          if(isPowerAbnormal)
          {
            char powerBuff[SIZE_POWER] = {0};
            FloatToString(pzemPower,powerBuff,100);
            strcat(dataToPublish,"Power: ");
            strcat(dataToPublish,powerBuff);
            strcat(dataToPublish,"W (LIMIT EXCEEDED!!!)\n");
          }
          if(isEnergyAbnormal)
          {
            char energyBuff[SIZE_ENERGY] = {0};
            FloatToString(pzemEnergy,energyBuff,1000);
            strcat(dataToPublish,"Energy: ");
            strcat(dataToPublish,energyBuff);
            strcat(dataToPublish,"kWh (LIMIT EXCEEDED!!!)\n");
          }

          if(isVoltageAbnormal || isCurrentAbnormal || isPowerAbnormal || isEnergyAbnormal)
          {
            mqttClient.publish(brokerSubTopic,dataToPublish); 
          }
          memset(dataToPublish,'\0',strlen(dataToPublish));
          prevTime = millis();
        }   
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
  char prevVoltageLimit[SIZE_VOLTAGE] = {0};
  char prevCurrentLimit[SIZE_CURRENT] = {0};
  char prevPowerLimit[SIZE_POWER] = {0};
  char prevEnergyLimit[SIZE_ENERGY] = {0};
  //Get previously stored data  
  preferences.getBytes("0",prevEventName,SIZE_EVENT_NAME);
  preferences.getBytes("1",prevIftttKey,SIZE_IFTTT_KEY);  
  preferences.getBytes("2",prevChannelId,SIZE_CHANNEL_ID);
  preferences.getBytes("3",prevApiKey,SIZE_API_KEY); 
  preferences.getBytes("4",prevPubTopic,SIZE_TOPIC);    
  preferences.getBytes("5",prevVoltageLimit,SIZE_VOLTAGE);
  preferences.getBytes("6",prevCurrentLimit,SIZE_CURRENT);
  preferences.getBytes("7",prevPowerLimit,SIZE_POWER);
  preferences.getBytes("8",prevEnergyLimit,SIZE_ENERGY);
  //Store data in flash if the new ones are not the same as the old.  
  StoreNewFlashData("0",eventName.getValue(),prevEventName,SIZE_EVENT_NAME);
  StoreNewFlashData("1",iftttKey.getValue(),prevIftttKey,SIZE_IFTTT_KEY);
  StoreNewFlashData("2",channelId.getValue(),prevChannelId,SIZE_CHANNEL_ID);
  StoreNewFlashData("3",apiKey.getValue(),prevApiKey,SIZE_API_KEY);  
  StoreNewFlashData("4",pubTopic.getValue(),prevPubTopic,SIZE_TOPIC);
  StoreNewFlashData("5",voltageLimit.getValue(),prevVoltageLimit,SIZE_VOLTAGE);
  StoreNewFlashData("6",currentLimit.getValue(),prevCurrentLimit,SIZE_CURRENT);
  StoreNewFlashData("7",powerLimit.getValue(),prevPowerLimit,SIZE_POWER);
  StoreNewFlashData("8",energyLimit.getValue(),prevEnergyLimit,SIZE_ENERGY);
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
