/*
 * This application is based on the NodeMCU-Wixel code written by MrPsi
 * https://github.com/MrPsi/NodeMCU-Wixel
 * It has been ported from LUA into Arduino, so it was easier to modify, as LUA is hard (horrible!) :)
 * 
 * This version is capable of buffering 200 (or more/less) values, so if any client such as xDrip requires old data, it capable of providing it.
 * This version allows you to use DHCP or Static IP, and also allows you to easily define which 802.11 mode you want to use, based on your
 *   modem requirements, or the distance the signal needs to go. 802.11b will go further than 802.11n for example.
 * This was based on using the NodeMCU, however other versions of the ESP8266 microcontrollers could be used.
 * This version uses TX from the Wixel, into the RX of the NodeMCU - it does not need TX out of the NodeMCU. 
 * This version uses the standard Serial port, not Serial2 (Swapped Serial), and without TX being used the USB is free to be used for Debugging.
 * This can also be run on a 4D Systems gen4-IoD display module, so the wixel is wired to that, so the gen4-IoD acts in place of the NodeMCU, 
 *   but also provides a display if information is wanting to be displayed. This will be expanded on in the future, so BG readings can be 
 *   displayed. Possibly pulling the filtered/calibrated values via Nighscout, or incorporating the algorithm into here directly.
 * MrPsi's Monitor application for seeing which Nodes are online, and when they got the last data, works on this version also. When the Monitor
 *   requests information, it requests 200 values, and it also requests another parameter from the NodeMCU, which is Uptime. Requests from
 *   the monitor vs requests from xDrip, are easily discernible, and additional functionality can be added if required.
 *   
 * Big THANK YOU to Peter - Mr PSI, for doing the original, and also to Paul Clements for helping me port this code over and getting it working.
 * 
 * REQUIRED TICKER LIBRARY
 * 
 * VERSION 0.1 - Initial Beta - 14-JUNE-2017
 */

#include <Ticker.h>
#include <ESP8266WiFi.h>

// This is required so we can set the 802.11 mode, which currently does not have a built in arduino function
extern "C" {
#include "user_interface.h"
}

#define VERSION "0.1 Beta - 14-JUN-2017"

//#define DEBUG       // Uncomment this line if you want to see more information printed each transaction/request

#define LEDPIN   16
#define RESET     4

#define StaticIP    // Uncomment if you want a static IP, instead of DHCP allocated IP. Typically used, so you always know the IP of this node
#define PHY80211B   // Uncomment if you want 802.11b specifically
//#define PHY80211G   // Uncomment if you want 802.11g specifically
//#define PHY80211N   // Uncomment if you want 802.11n specifically - ALL commented, will leave it at default setting

//#define IOD         // Uncomment if using 4D Systems gen4-IoD instead of NodeMCU - WORK IN PROGRESS

#define MAXSTORAGE 200  // Defines the maximum number of historical records desired to be stored on the NodeMCU

#ifdef IOD
#include "GFX4d.h"
GFX4d gfx = GFX4d();
#endif

#ifdef StaticIP
IPAddress ip(192, 168, 178, 48);           // SET THESE IF YOU WANT MANUAL IP ADDRESS.
IPAddress gateway(192, 168, 178, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(8, 8, 8, 8);                // Use google Primary DNS (or modify it to suit) - Not critical
IPAddress dns2(8, 8, 4, 4);                // Use google Secondary DNS (or modify it to suit) - Not critical
#endif

const char* wifiSsid = "Smelly";          // SET YOUR WIFI SSID
const char* wifiPassword = "Socks";       // SET YOUR WIFI PASSWORD

WiFiServer server(50005);
WiFiClient serverClient;

String NullString = "{}\n";
String currentRecordString;
long valuenum[6];
String value[10];
String timestring;
String requesttimestring;
String getmsg;
unsigned long wixeltime;
int LED = 0;
boolean mon;
boolean flash = false;
boolean hasBeenConnected = false;
int LedCounter = 0;
int IdleError = 0;
boolean attemptingConnection = false;
int listsize = 0;
String TxID[MAXSTORAGE + 1];
uint32_t FilteredValue[MAXSTORAGE + 1];
uint32_t RawValue[MAXSTORAGE + 1];
uint16_t BatteryLife[MAXSTORAGE + 1];
int ReceivedSignalStrength[MAXSTORAGE + 1];
uint16_t TransmissionId[MAXSTORAGE + 1];
unsigned long wixelTime[MAXSTORAGE + 1];
boolean firstValue;

Ticker ticker;

void setup()
{
  WiFi.persistent(false); // Prevents ESP8266 from writing/reading persistent SSID/Pass data to/from flash

#ifdef IOD
  gfx.begin();
  gfx.Cls();
  gfx.ScrollEnable(true);
  gfx.BacklightOn(true);
  gfx.Orientation(PORTRAIT);
  gfx.SmoothScrollSpeed(5);
  gfx.TextColor(WHITE); gfx.Font(2);  gfx.TextSize(1);
#endif

  pinMode(LEDPIN, OUTPUT);    // LEDPIN is defined above, uses the NodeMCU LED by default
  digitalWrite(LEDPIN, HIGH); // HIGH = OFF when using GPIO16
  pinMode(RESET, OUTPUT);     // RESET is the reset of the Wixel, held in reset at startup by pulldown resistor
  digitalWrite(RESET, HIGH);  // HIGH = DISABLED, This activated the Wixel now that the NodeMCU is alive

  Serial.begin(9600);
  //Serial.swap(); // Swap to the Serial2 pins (D7/GPIO13 and D8/GPIO15) - Untested
  delay(100);
  Println("Starting Up");
  Println(VERSION);

  server.begin(); // Start the server so clients can request information from this Node
  server.setNoDelay(true);

  // Trigger LEDcontrol every 100ms
  ticker.attach(0.1, LEDcontrol);

  // Attempt connection to WiFi
  WifiConnect();
}

void loop()
{
  // Attempt to reconnect WiFi after it dropped out, allowing 30s to show error LED flashing
  if ((WiFi.status() != WL_CONNECTED) && IdleError >= 300) // 10 = 1 second
  {
    IdleError = 0; // Reset flag
    Println("Retry Connection");
    WifiConnect();
  }
  if (Serial.available() > 0)
  {
#ifdef DEBUG    
    Println("Serial Data from Wixel Received");
#endif
    GetWixelData();
  }

  serverClient = server.available();
  if (serverClient)
  {
    if (serverClient.connected())
    {
      delay(5);
      //Println("Connected to client");
    }
    if (serverClient.available() > 0)
    {
      getmsg = "";
      char getchr;

      while (serverClient.available())
      {
        getchr = serverClient.read();
        getmsg = getmsg + char(getchr);
        //Write(getchr);
      }
#ifdef DEBUG
      int newlinemsg = getmsg.indexOf("\n"); // Find the newline character in the string at the end
      getmsg.remove(newlinemsg); // remove trailing newlines from the string so it prints nicely on the terminal
      Print("Message from Client: ");
      Println(getmsg);
#endif

      int recnopos = getmsg.indexOf("cords"); // index of where the string "cords" is in the request string: 'numberOfRecords'
      int monitorreq = getmsg.indexOf("includeInfo"); // index of where the string "includeInfo" is in the request string
      if (monitorreq > 0)
      {
        mon = true;
        firstValue = true; // Set flag for when values are sent, only Uptime is sent one time for Monitor request packets
#ifdef DEBUG        
        Println("Request has come from NodeMCU-Wixel Monitor, not xDrip+");
#endif        
      }
      else
      {
        mon = false;
#ifdef DEBUG
        Println("Request has likely come from xDrip+");
#endif
      }
      if (recnopos > 0)
      {
        String nofr = ""; // nofr is the Number Of Records
        for (int n = 0; n < 3; n ++)
        {
          if ((getmsg.charAt(recnopos + n + 7) != 44) && (getmsg.charAt(recnopos + n + 7) != 34)) // 44 = ',' and 34 = '"'
          {
            nofr = nofr + getmsg.charAt(recnopos + n + 7);
          }
        } // at this point, nofr may be 200, or 1 for example.

        int noOfrec = nofr.toInt();
        if (noOfrec > listsize) // && mon == false) // if the number of requested records is more than the number we have in our list
        {
          noOfrec = listsize; // set the request size to the number of records we actually have
        }
        if (noOfrec > 0) // Request is for 1 or more values
        {
          // Do this if the list is NOT empty
          if (noOfrec > MAXSTORAGE)
          {
            noOfrec = MAXSTORAGE;
          }
          int beglp = listsize - noOfrec + 1;
          int endlp = listsize + 1;
          if (noOfrec > 1) // 2 or more
          {
            long tempTime = millis();
            for (int n = 0; n < noOfrec; n ++)
            {
              SendPacket(MAXSTORAGE - n); // Send newest first
              serverClient.print(currentRecordString);
            }
            tempTime = millis() - tempTime;
#ifdef DEBUG
            Print("Sent ");
            Print((String)noOfrec);
            Print(" values");
            if (mon)
            {
              Print(" WITH Uptime");
            }
            else
            {
              Print(" WITHOUT Uptime");
            }
            Print(" - Took ");
            Print((String)tempTime);
            Println("ms");
#endif
          }
          else // 1
          {
            SendPacket(MAXSTORAGE); // newest value is stored in 200, oldest in 1
            serverClient.print(currentRecordString);
#ifdef DEBUG            
            Print("Sent 1 value");
            if (mon)
            {
              Println(" WITH Uptime");
            }
            else
            {
              Println(" WITHOUT Uptime");
            }
#endif
          }
          yield();
        }
        else // Request is for 0, typically from first request after poweron
        {
          SendPacket(0); // null string is sent in this case
          serverClient.print(currentRecordString);
#ifdef DEBUG
          Print("Sent NULL value");
          if (mon)
          {
            Println(" WITH Uptime");
          }
          else
          {
            Println(" WITHOUT Uptime");
          }
#endif          
        }
        serverClient.flush();
        serverClient.stop();
      }
    }
  }
  yield();
}

void LEDcontrol()
{
  // Each ISR triggers this function, so count the number of triggers
  LedCounter ++;

  // 100ms Flash = WiFi connect issue, 1000ms Flash = Attempting to connect to Wifi
  if ((LED == 100) || (LED == 1000 && LedCounter == 10))
  {
    flash = !flash;
    digitalWrite(LEDPIN, flash); // Toggle on/off
  }

  // reset the counter once 10 iterations have happened
  if (LedCounter >= 10)
  {
    LedCounter = 0;
  }

  // If the WiFi failed to connect, increment IdleError each 100ms trigger
  if (!hasBeenConnected && !attemptingConnection)
  {
    IdleError++;
  }
}

void WifiConnect()
{
  int retries = 0;

#ifdef StaticIP
  WiFi.config(ip, gateway, subnet, dns1, dns2);
#endif

  Print("Attempting Connection using ");
#ifdef PHY80211B
  Println("802.11b");
  wifi_set_phy_mode(PHY_MODE_11B);
#endif
#ifdef PHY80211G
  Println("802.11g");
  wifi_set_phy_mode(PHY_MODE_11G);
#endif
#ifdef PHY80211N
  Println("802.11n");
  wifi_set_phy_mode(PHY_MODE_11N);
#endif
#if ! defined PHY80211B && ! defined PHY80211G && ! defined PHY80211N
  Println("default 802.11 mode");
#endif

  // Attempt connection to WiFi
  WiFi.begin(wifiSsid, wifiPassword);
  LED = 1000; // set LED blink rate to 1s to indicate attempting connection

  // Loop until max retries met, not WiFi connects
  while ((WiFi.status() != WL_CONNECTED) && (retries < 22))
  {
    attemptingConnection = true;
    retries++;
    delay(1000);
  }
  attemptingConnection = false;

  // If the WiFi has connected
  if (WiFi.status() == WL_CONNECTED)
  {
    hasBeenConnected = true;
    LED = 0; // set LED blink rate to off to indicate all OK
    digitalWrite(LEDPIN, HIGH); // Turn off LED
    Print("Connected - ");
    Print(ipToString(WiFi.localIP()));
    long rssi = WiFi.RSSI();
    Print(", Signal strength (RSSI): ");
    Print((String)rssi);
    Println(" dBm");
  }
  // If the WiFi failed to connect
  else
  {
    hasBeenConnected = false;
    LED = 100; // set LED blink rate to 100ms to indicate an issue
    Println("Connection Failed");
  }
}

void GetWixelData()
{
  int strglen = 0;
  int valpos = 0;
  char ipchar;
  String tempnum = "";
  while (Serial.available() > 0)
  {
    ipchar = Serial.read();
#ifdef DEBUG    
    Write(ipchar);
#endif
    strglen++;
    if (ipchar != 32 && ipchar != 13)
    {
      tempnum = tempnum + ipchar;
      delay(5);
    }
    if (ipchar == 32)
    {
      value[valpos] = tempnum;
      valpos ++;
      tempnum = "";
    }
  }
  if (strglen < 13)
  {
    for (int n = 0; n < 6; n++)
    {
      value[n] = "";
    }
  }
  else
  {
    wixeltime = millis();
    addtoList();
  }
}

void SendPacket(int index)
{
  yield();
  if (serverClient.connected())
  {
    String uptimeString = "";
    if (mon == true && firstValue == true) // Request from Monitor
    {
      long uptimeSec = millis() / 1000;
      uptimeString = (String)uptimeSec;
      uptimeString = "{\"Uptime\":" + uptimeString + "}\n";
      firstValue = false; // switch flag off, so Uptime is only sent once per group of values
    }
    getfromList(index);
    currentRecordString = uptimeString + currentRecordString; // Append Uptime for Monitor if required
  }
}

void addtoList()
{
  listsize ++;
  if (listsize > MAXSTORAGE)
  {
    listsize = MAXSTORAGE;
  }
  moveList();
  
#ifdef DEBUG
  Print("Wixel Data added to Storage, Records in storage now: ");
  Println((String)listsize);
#endif

  TxID[MAXSTORAGE] = value[0];
  FilteredValue[MAXSTORAGE] = toLong(value[1]);
  RawValue[MAXSTORAGE] = toLong(value[2]);
  BatteryLife[MAXSTORAGE] = value[3].toInt();
  ReceivedSignalStrength[MAXSTORAGE] = value[4].toInt();
  TransmissionId[MAXSTORAGE] = value[5].toInt();
  wixelTime[MAXSTORAGE] = wixeltime; //Add when the Wixel received the data, to start with
  /*
    Print("TxID: ");
    Println((String)TxID[listsize]);
    Print("FilteredValue: ");
    Println((String)FilteredValue[listsize]);
    Print("RawValue: ");
    Println((String)RawValue[listsize]);
    Print("BatteryLife: ");
    Println((String)BatteryLife[listsize]);
    Print("ReceivedSignalStrength: ");
    Println((String)ReceivedSignalStrength[listsize]);
    Print("TransmissionId: ");
    Println((String)TransmissionId[listsize]);
    Print("Wixel Time: ");
    Println((String)wixelTime[listsize]);
    Print("CaptureDateTime: ");
    Println((String)CaptureDateTime[listsize]);
  */
}

void getfromList(int index)
{
  if (index > 0)
  {
    unsigned long currentTime;
    currentTime = millis();
    
    // Update the relative time only, this is now the differece between wixel received time and now, leave all other data static
    String relativeTimeString = (String)((currentTime - wixelTime[index]) / 1000); // Current time (ms) minus the time the wixel received the packet (ms), SECONDS
    relativeTimeString = relativeTimeString + "000"; // Append 3 zeros for some reason.

    /* DETAILS
     * BatteryLife - This is the battery life of G4 Transmitter
     * TransmitterId - This is the G4 Transmitter ID
     * RelativeTime - This is the difference in milliseconds between now, and when a record was saved from the Wixel, in seconds but formatted to ms.
     * TransmissionId - This is a rolling Transmittion ID number
     * ReceivedSignalStrength - Signal Strength from G4 Transmitter to Wixel
     * UploaderBatteryLife - This is not used - But would be the battery life if this was battery powered
     * Uploaded - This is not used
     * CaptureDateTime - This is not used - But would be the EPOCH time of the record
     * FilteredValue - This is the Blood Glucose Value
     * RawValue - This is the Raw Blood Glucose Value
     */
    
    currentRecordString = "{\"BatteryLife\":" + (String)BatteryLife[index] + ",\"TransmitterId\":\"" + TxID[index] + "\",\"RelativeTime\":" + relativeTimeString;
    currentRecordString = currentRecordString + ",\"TransmissionId\":" + (String)TransmissionId[index] + ",\"ReceivedSignalStrength\":" + (String)ReceivedSignalStrength[index] + ",\"UploaderBatteryLife\":0";
    currentRecordString = currentRecordString + ",\"Uploaded\":0,\"UploadAttempts\":0,\"CaptureDateTime\":0";
    currentRecordString = currentRecordString + ",\"FilteredValue\":" + (String)FilteredValue[index] + ",\"RawValue\":" + (String)RawValue[index] + "}\n";
  }
  else
  {
    currentRecordString = NullString;
  }
}

void moveList()
{
  // Position at MAXSTORAGE of arrays is the current value.
  // Position at 1 of arrays is the oldest value, assuming the arrays are full.
  for (int n = 1; n < MAXSTORAGE; n ++)
  {
    TxID[n] = TxID[n + 1];
    FilteredValue[n] = FilteredValue[n + 1];
    RawValue[n] = RawValue[n + 1];
    BatteryLife[n] = BatteryLife[n + 1];
    ReceivedSignalStrength[n] = ReceivedSignalStrength[n + 1];
    TransmissionId[n] = TransmissionId[n + 1];
    wixelTime[n] = wixelTime[n + 1];
  }
}

uint32_t toLong(String tempstr)
{
  int l = tempstr.length();
  int m = 1;
  uint32_t t = 0;
  String s = "";
  for (int n = 0; n < l; n++)
  {
    s = tempstr.charAt((l - 1) - n);
    t = t + (s.toInt() * m);
    m = m * 10;
  }
  return t;
}

String ipToString(IPAddress ip)
{
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

void Print(String prtstrg)
{
#ifdef IOD
  gfx.print(prtstrg);         // Prints to the gen4-IoD display when defined
#else
  Serial.print(prtstrg);      // Prints to the serial port
#endif
}

void Println(String prtstrg)
{
#ifdef IOD
  gfx.println(prtstrg);       // Prints to the gen4-IoD display when defined
#else
  Serial.println(prtstrg);    // Prints to the serial port
#endif
}

void Write(char prtstrg)
{
#ifdef IOD
  gfx.write(prtstrg);         // Prints to the gen4-IoD display when defined
#else
  Serial.write(prtstrg);      // Prints to the serial port
#endif
}
