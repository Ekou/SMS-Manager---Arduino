#include <SoftwareSerial.h>
#include <SIM900.h>
#include <sms.h>
#include <EEPROM.h>

#define OUT 4
#define SIGNAL_LED 5
#define ALARM_PIN 12
#define SIM900_PIN 9

#define SMS_SIZE 50

#define OUT_ADDRESS 11
#define HSTART_ADDRESS 1
#define MSTART_ADDRESS 2
#define HEND_ADDRESS 3
#define MEND_ADDRESS 4 
#define TIMEISPROGRAMMED_ADDRESS 5
#define TIMESET_ADDRESS 6
#define MITTENTE_START_ADDRESS 20
#define MITTENTE_CHECK_IF_EXIST 19

void SMSParse();
void SMSLedSetup();
void SMSLedSwitch();
void SMSSetTime();

char* ParseCCLK();
void ParseCSQ(char* bufferCSQ);
void ResetBuffer(char* buff);

void SaveNumber();      //it saves it in EEPROM but under char format!
void ReadNumber();      //to read as int, just subtract 48 from char value EDIT: char type fix it itself, no need to do any subtractions

boolean started = false;
boolean poweredON = false;
uint8_t retry = 0;
boolean startUpMessageSent = false;

uint8_t hours, minutes, seconds;
boolean timeIsProgrammed = false;
boolean timeSet = false;
boolean postMidNight = false;
uint8_t HStart, MStart;
uint8_t HEnd, MEnd;

bool lastCheckAlarm = false;

SMSGSM sms;
char smsBuffer[SMS_SIZE];
char mittente[13];    //3 + 10... [+391234567890]

uint8_t CSQ;

void setup ()
{
  Serial.begin(9600);
  Serial.println("-SMS MANAGER-test");
  
  pinMode(OUT, OUTPUT);
  digitalWrite(OUT, HIGH);
  pinMode(SIGNAL_LED, OUTPUT);
  digitalWrite(SIGNAL_LED,LOW);
  pinMode(ALARM_PIN, INPUT);
  digitalWrite(ALARM_PIN, HIGH);

  HStart = EEPROM.read(HSTART_ADDRESS);
  MStart = EEPROM.read(MSTART_ADDRESS);
  HEnd = EEPROM.read(HEND_ADDRESS);
  MEnd = EEPROM.read(MEND_ADDRESS);
  timeIsProgrammed = EEPROM.read(TIMEISPROGRAMMED_ADDRESS);
  timeSet = EEPROM.read(TIMESET_ADDRESS);

  pinMode(SIM900_PIN, OUTPUT);
  StartSIM900();
}

void loop()
{
  if(poweredON)
  {
    char CSQBuffer[10];
    gsm.SimpleWriteln("AT+CSQ");
    delay(500);
    gsm.WhileSimpleRead(CSQBuffer, 10, false);
    ParseCSQ(CSQBuffer);
    digitalWrite(SIGNAL_LED, started);
    if (CSQ >= 15)
    {
      started = true;
    }
    else if (CSQ <= 14 && CSQ != 0)
    {
      Serial.print("Low Signal: ");
      Serial.println(CSQ);
      started = false;
    }
    else if (CSQ == 0)
    {
      Serial.print("No response from GSM MODULE! Retrying to connect: attempt #");
      Serial.println(retry + 1);
      delay(3000);
      retry++;
      if (retry >= 5)
      {
        retry = 0;
        poweredON = false;
      }
    }
    if (started)
    {
      if (!startUpMessageSent && EEPROM.read(MITTENTE_CHECK_IF_EXIST) == 1)
      {
        sms.SendSMS(mittente, "SMS Manager avviato.");
      }
      startUpMessageSent = true;        //set to true even if it does not send because reasons
      SMSParse();
      if(timeIsProgrammed)
      {
        ParseCCLK();
        if(postMidNight && hours == 0 && minutes == 0)
        {
          HEnd -= 24;
          postMidNight = false;
        }
        Serial.println(HEnd);
        if(hours >= HStart && minutes >= MStart && !EEPROM.read(OUT_ADDRESS))
        {
          EEPROM.write(OUT_ADDRESS, 1);
          Serial.println("now on");
        }
        if (hours >= HEnd && minutes >= MEnd)
        {
          EEPROM.write(OUT_ADDRESS, 0);
          timeIsProgrammed = false;
          EEPROM.write(TIMEISPROGRAMMED_ADDRESS, 0);
          Serial.println("now off");
        }
      }
      
      if(digitalRead(ALARM_PIN) && digitalRead(ALARM_PIN) != lastCheckAlarm)
      {
        sms.SendSMS(mittente, "Allarme attivato.");
        Serial.println("allarme attivato.");
      }
      lastCheckAlarm = digitalRead(ALARM_PIN);
      
      digitalWrite(OUT, EEPROM.read(OUT_ADDRESS));
      Serial.print("OUT Status: ");
      Serial.println(!bitRead(PORTD, OUT));
    }
  }
  else
  {
    StartSIM900();
  }
}

void SMSParse ()
{
  char pos;
  pos = sms.IsSMSPresent(SMS_ALL);
  if (pos)
  {
    sms.GetSMS(pos, mittente, smsBuffer, SMS_SIZE);
    sms.DeleteSMS(pos);
    delay(500);
    SaveNumber();     //store mittente in EEPROM
    Serial.println("Comando ricevuto [tel. "+String(mittente)+String("]: "+ String(smsBuffer)));
    char temp[SMS_SIZE];
    strcpy(temp, smsBuffer);
    char* buffer = strtok(temp, " ");
    int i = 0;
    char c;
    if (!strcmp(buffer, "Out"))
    {
      do
      {
        c = smsBuffer[i];
        if (c == ':')
        {
          SMSLedSetup(temp);
          break;
        }
        else if (c == '\0')
        {
          SMSLedSwitch();
          break;
        }
        i++;
      }while(true);
    }
    else if (!strcmp(buffer, "Status"))
    {
      char text[50];
      strcpy(text, "Orario: ");
      strcat(text, ParseCCLK());
      if(bitRead(PORTD, OUT))
      {
        strcat(text, "\nOut: OFF\n");
      }
      else
      {
        strcat(text, "\nOut: ON\n");
      }
      strcat(text, "Out programmato:\n");
      if (!timeSet)
      {
        strcat(text, "--:--:-- / --:--:--");
      }
      else
      {
        char buff[3];
        itoa(HStart, buff, 10);
        if (HStart <10)
          strcat(text, "0");
        strcat(text, buff);
        strcat(text, ":");
        itoa(MStart, buff, 10);
        if (MStart <10)
          strcat(text, "0");
        strcat(text, buff);
        strcat(text, " - ");
        itoa(HEnd, buff, 10);
        if (HEnd <10)
          strcat(text, "0");
        strcat(text, buff);
        strcat(text, ":");
        itoa(MEnd, buff, 10);
        if (MEnd <10)
          strcat(text, "0");
        strcat(text, buff);
      }
      sms.SendSMS(mittente, text);
    }
    else if (!strcmp(buffer, "Orario"))
    {
      SMSSetTime();
    }
    else if (!strcmp(buffer, "?") || !strcmp(buffer, "Info"))
    {
      sms.SendSMS(mittente, "Elenco comandi:\n- Out\n- Status\n- Orario\n- ?\n- Info");
    }
    else
    {
      Serial.println("Comando non riconosciuto");
      sms.SendSMS(mittente, "Comando non riconosciuto");
    }
    ResetBuffer(buffer);
    delay(1000);
  }
}

void SaveNumber()
{
  for (int i = 0; i < 13; i++)
  {
    EEPROM.write(MITTENTE_START_ADDRESS + i, mittente[i]);
  }
  EEPROM.write(MITTENTE_CHECK_IF_EXIST, 1);
}

void ReadNumber()
{
  mittente[0] = '+';
  for (int i = 1; i < 13; i++)
  {
    mittente[i] = EEPROM.read(MITTENTE_START_ADDRESS + i);
  }
}

void SMSSetTime()
{
  int h, m, s;
  char command[50];
  strcpy(command, "AT+CCLK=\"00/01/01,");
  char* buffer = strtok(smsBuffer, " ");
  buffer = strtok(NULL, ":");
  h = atoi(buffer);
  if (strlen(buffer) == 1)
  {
    strcat(command, "0");
  }
  strcat(command, buffer);
  strcat(command, ":");
  buffer = strtok(NULL, ":");
  m = atoi(buffer);
  if (strlen(buffer) == 1)
  {
    strcat(command, "0");
  }
  strcat(command, buffer);
  strcat(command, ":");
  buffer = strtok(NULL, "\0");
  s = atoi(buffer);
  if (strlen(buffer) == 1)
  {
    strcat(command, "0");
  }
  strcat(command, buffer);
  strcat(command, "+00\"");
  if(h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 || strlen(command) != 30)
  {
    sms.SendSMS(mittente, "Orario non valido\nFormato orario:\n24h - hh:mm:ss\n00:00:00 - 23:59:59");
    Serial.println(strlen(command));
    Serial.println(command);
  }
  else
  {
    Serial.println(command);
    gsm.SimpleWriteln(command);
    delay(1000);
    sms.SendSMS(mittente, "Orario impostato");
  }
}

void SMSLedSetup(char* command)
{
  char* buffer = strtok(NULL, ":");

  HStart = atoi(buffer);
  buffer = strtok(NULL, " ");
  MStart = atoi(buffer);
  
  buffer = strtok(NULL, ":");
  HEnd = atoi(buffer);
  buffer = strtok(NULL, "\0");
  MEnd = atoi(buffer);

  if(HStart < 24 && HStart >= 0 && MStart < 60 && MStart >= 0 && HEnd >= 0 && HEnd < 24 && MEnd < 60 && MEnd >= 0)
  {
    if(HStart > HEnd)
    {
      HEnd += 24;
      postMidNight = true;
    }
  
    timeIsProgrammed = true;
    timeSet = true;
    
    EEPROM.write(HSTART_ADDRESS, HStart);
    EEPROM.write(MSTART_ADDRESS, MStart);
    EEPROM.write(HEND_ADDRESS, HEnd);
    EEPROM.write(MEND_ADDRESS, MEnd);
    EEPROM.write(TIMEISPROGRAMMED_ADDRESS, 1);
    EEPROM.write(TIMESET_ADDRESS, 1);
    
    sms.SendSMS(mittente, "Out programmato");
  }
  else
  {
    sms.SendSMS(mittente, "Formato out errato\nFormato orario out:\n24h - hh:mm hh:mm\nes. \"00:00 - 23:59\"");
  }
}

void SMSLedSwitch()
{
  char* buffer = strtok(NULL, "\0");
  if(!strcmp(buffer, "off"))
  {
    EEPROM.write(OUT_ADDRESS, 1);
    sms.SendSMS(mittente, smsBuffer);
  }
  else if(!strcmp(buffer, "on"))
  {
    EEPROM.write(OUT_ADDRESS, 0);
    sms.SendSMS(mittente, smsBuffer);
  }
  else if(!strcmp(buffer,"reset"))
  {
    EEPROM.write(OUT_ADDRESS, 1);
    timeIsProgrammed = false;
    timeSet = false;
    EEPROM.write(TIMESET_ADDRESS, 0);
    sms.SendSMS(mittente, smsBuffer);
  }
  else
  {
    Serial.println("Comando Out non valido");
    char text[100];
    strcpy(text, "Comando Out \"");
    strcat(text, buffer);
    strcat(text, "\" non valido.\nLista comandi:\n- Out on\n- Out off\n- Out hh:mm hh:mm [orario inizio / orario fine]\n- Out reset");
    sms.SendSMS(mittente, text);
  }
  delay(500);
}

char* ParseCCLK()
{
  char bufferCCLK[30];
  gsm.SimpleWriteln("AT+CCLK?");      //TODO: to call when sms arrive (not in loop)
  delay(500);
  gsm.WhileSimpleRead(bufferCCLK, 30, false);     //store char[], sizeof array, echoes ON/OFF (best function evar)
  
  char* temp = strtok(bufferCCLK, ",");
  temp = strtok(NULL, ":");
  hours = atoi(temp);
  temp = strtok(NULL,  ":");
  minutes = atoi(temp);
  temp = strtok(NULL, "+");
  seconds = atoi(temp);
  
  char temp_hours[3];
  char temp_minutes[3];
  char temp_seconds[3];
  itoa(hours, temp_hours, 10);
  itoa(minutes, temp_minutes, 10);
  itoa(seconds, temp_seconds, 10);
  char text[10];
  if (hours < 10)
  {
    strcpy(text, "0");
    strcat(text, temp_hours);
  }
  else
    strcpy(text, temp_hours);
  strcat(text, ":");
  if (minutes < 10)
    strcat(text, "0");
  strcat(text, temp_minutes);
  strcat(text, ":");
  if (seconds < 10)
    strcat(text, "0");
  strcat(text, temp_seconds);
  Serial.println(text);
  return (char*)text;
}

void ParseCSQ (char* bufferCSQ)
{
  char* temp = strtok(bufferCSQ, " ");
  temp = strtok(NULL, ",");
  CSQ = atoi(temp);
}

void StartSIM900()
{
  Serial.println("Turning ON GSM MODULE. . .");
  digitalWrite(SIM900_PIN, LOW);
  delay(1000);
  digitalWrite(SIM900_PIN, HIGH);
  delay(1000);
  digitalWrite(SIM900_PIN, LOW);
  delay(5000);
  
  if (gsm.begin(9600))
  {
    Serial.println("GSM READY");
    if (EEPROM.read(MITTENTE_CHECK_IF_EXIST) == 1)
    {
      ReadNumber();
    }
    poweredON = true;
  }
  else
  {
    Serial.println("GSM IDLE");
  }
}

void ResetBuffer(char* buff) 
{
  memset(buff, 0, sizeof(buff));
}
