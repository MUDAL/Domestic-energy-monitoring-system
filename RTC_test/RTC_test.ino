#include <Wire.h>
#include "RTClib.h" //Version 1.3.3

static RTC_DS3231 rtc;

void setup() 
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  if(!rtc.begin())
  {
    Serial.println("Could not find RTC");
  }
  //Set date and time: year,month,day,hour,minute,second
  rtc.adjust(DateTime(2022,12,25,23,36,0)); 
}

void loop() 
{
  // put your main code here, to run repeatedly:
  DateTime dateTime = rtc.now();

  Serial.print("day = ");
  Serial.println(dateTime.day());

  Serial.print("month = ");
  Serial.println(dateTime.month());
  
  Serial.print("year = ");
  Serial.println(dateTime.year());
  
  Serial.print("hour = ");
  Serial.println(dateTime.hour());
  
  Serial.print("minute = ");
  Serial.println(dateTime.minute());
  delay(2000);
}
