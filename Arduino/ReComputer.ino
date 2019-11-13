#include <Arduino.h>
#include <Wire.h>

#include "PID_v1.h"
#include "U8g2lib.h"
#include "bitmaps.h"
#include "PacketSerial.h"

//Hardware Pins
int fanCtrlPin = 13;
int fanPulseInPin = 12;

//PID parameters. I'm using defaults with quite nice results
double kp=2;   //proportional parameter
double ki=5;   //integral parameter
double kd=1;   //derivative parameter

//Constants
double desiredTemp = 45; // might need increase this during summer
double maxFanPwm = 255;  // the max value we can set is 255.
double minFanPwm = 0;    //set to 0 if you want the fan stop when the cpu's very cool
                         //otherwise set to a gate value that just maintain the fan spin, 
                         //e.g. 50 for an 8CM-12V fan.
#define CPU_LOAD_HISTORY_CNT  50

//Variables
double fanPwm = (minFanPwm + maxFanPwm) / 2;
double fanSpeed = 0;
double cpuTemp = 0, cpuLoad = 0, ram = 0;
String ip = "###.###.###.###";
int cpuLoadHistory[CPU_LOAD_HISTORY_CNT + 1] = {0};

double lastFanPwm = -1;
double lastFanSpeed = -1;
double lastCpuTemp = -1, lastCpuLoad = -1, lastRam = -1;
String lastIp = "###.###.###.###";

long lastRefreshTime;
long lastCommunicateTime;
bool cpuTempAvailable = false;
unsigned long pulseDuration;
char packetBuildBuffer[128];

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
PacketSerial myPacketSerial;
PID myPID(&cpuTemp, &fanPwm, &desiredTemp, kp, ki, kd, REVERSE);

#define DEBUGSER    Serial
#define COMMUSER    Serial1

void setup() {
  
  DEBUGSER.begin(115200);
  DEBUGSER.println("starting ...");

  pinMode(fanPulseInPin, INPUT);
  digitalWrite(fanPulseInPin,HIGH);
  pinMode(fanCtrlPin, OUTPUT);
  analogWrite(fanCtrlPin, fanPwm);
  
  u8g2.begin();
  u8g2.sendF("c", 0xAE);  //display off
  u8g2.sendF("c", 0xAF);  //display on
  u8g2.enableUTF8Print();
  
  //Display Seeed Logo at boot
  oledSplash();
  oledOneFrame();

  COMMUSER.begin(115200);
  myPacketSerial.setStream(&COMMUSER);
  myPacketSerial.setPacketHandler(&onPacketReceived);

  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(minFanPwm, maxFanPwm);

  DEBUGSER.println("setup done.");
  
}

void loop() {
  myPacketSerial.update();

  if (cpuTempAvailable) {
    //do PID
    myPID.Compute();  //has sample time control inside

    if (fanPwm != lastFanPwm) {
      DEBUGSER.print("fanPwm="); 
      DEBUGSER.println(fanPwm);
      analogWrite(fanCtrlPin, fanPwm);
      lastFanPwm = fanPwm;
    }
  }

  //calculate fast, but we refresh the screen slowly
  if (millis() - lastRefreshTime > 1000) {
    fanSpeed = getFanSpeed();
    DEBUGSER.print("fanSpeed="); 
    DEBUGSER.println(fanSpeed);
    sendFanSpeed();
    
    oledOneFrame();
    lastRefreshTime = millis();
  }
}

void oledSplash() {
  const char *text = "ReComputer";
  int offset = 127;
  int textWidth;
  
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 16, 128, 32, logo_2018_horizontal_bits);
  u8g2.sendBuffer();
  delay(1000);
//  u8g2.sendF("caaaaaac", 0x027, 0, /*startPage*/0, 0, /*endPage*/7, /*25frm*/7, 255, 0x2f); //scroll left
  delay(5000);  //scroll the logo off of screen
//  u8g2.sendF("c", 0x2e);  //stop scroll

  //draw text
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont22_tf);
  u8g2.setFontMode(0);  // enable transparent mode, which is faster
  textWidth = u8g2.getUTF8Width(text);  // calculate the pixel width of the text
  //scroll the text left
  do {
    u8g2.clearBuffer();
    u8g2.drawUTF8(offset, 40, text);
    u8g2.sendBuffer();
    delay(10);
    offset -= 5;
  } while (offset > (128 - textWidth) / 2);
  delay(5000);
}

void oledOneFrame() {
  if (fanSpeed != lastFanSpeed || cpuTemp != lastCpuTemp || cpuLoad != lastCpuLoad ||
      ram != lastRam || ip != lastIp) 
  {
    int xoffset = 0, yoffset=16;
    int xoffsetGraph, xGraph, yGraph, hGraph, wGraph, graphWidth;
    int stepGraph;
    const char *numStr;
    u8g2.clearBuffer();
    //cpu temp & fan
    u8g2.setFont(u8g2_font_profont15_tf);
    u8g2.setFontMode(0);
    u8g2.setCursor(xoffset, yoffset);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "CPU: ");
    numStr = u8x8_u8toa(cpuTemp, 2); //2 digits
    xoffset += u8g2.drawUTF8(xoffset, yoffset, numStr);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "°C ");
    numStr = u8x8_u16toa(fanSpeed, 4);  //4 digits
    xoffset += u8g2.drawUTF8(xoffset, yoffset, numStr);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, " RPM");

    //cpu load
    xoffset = 0; yoffset += 20;
    u8g2.setCursor(xoffset, yoffset);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "LOAD:");
    xoffsetGraph = xoffset;
    graphWidth = 50;
    stepGraph = 50 / CPU_LOAD_HISTORY_CNT;
    xoffset += (graphWidth + 10);
    numStr = u8x8_u8toa(cpuLoad, 2); //2 digits
    xoffset += u8g2.drawUTF8(xoffset, yoffset, numStr);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "%");
    u8g2.drawFrame(xoffsetGraph, yoffset - 18, graphWidth, 18);
    xGraph = xoffsetGraph;
    yGraph = yoffset - 18;
    for (int i = 0; i < CPU_LOAD_HISTORY_CNT; i++) {
      hGraph = map(cpuLoadHistory[i], 0, 100, 0, 18);
      if (hGraph > 0) {
        u8g2.drawBox(xGraph, yGraph + (18 - hGraph), stepGraph, hGraph);
      }
      xGraph += stepGraph;
    }

    //ram
    xoffset = 0; yoffset += 16;
    u8g2.setCursor(xoffset, yoffset);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "RAM: ");
    xoffsetGraph = xoffset;
    xoffset += (graphWidth + 10);
    numStr = u8x8_u8toa(ram, 2); //2 digits
    xoffset += u8g2.drawUTF8(xoffset, yoffset, numStr);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "%");
    u8g2.drawFrame(xoffsetGraph, yoffset - 10, graphWidth, 10);
    wGraph = map(ram, 0, 100, 0, graphWidth);
    u8g2.drawBox(xoffsetGraph, yoffset - 10, wGraph, 10);

    //IP
    xoffset = 0; yoffset += 12;
    u8g2.setFont(u8g2_font_profont11_tf);
    u8g2.setFontMode(0);
    u8g2.setCursor(xoffset, yoffset);
    xoffset += u8g2.drawUTF8(xoffset, yoffset, "IP:   ");
    xoffset += u8g2.drawUTF8(xoffset, yoffset, ip.c_str());

    //send to OLED display
    u8g2.sendBuffer();
    
    //save the history
    lastFanSpeed = fanSpeed;
    lastCpuTemp = cpuTemp;
    lastCpuLoad = cpuLoad;
    lastRam = ram;
    lastIp = String(ip);
  }  
}

/**
 * packet structure: 
 * packetType | packetPayload
 *   1byte    |     4+byte
 * 4byte int = realValue * 1000
 * packet types:
 * 1: cpuTemp(4byte int), refresh every 5s
 * 2: cpuLoad(4byte int), refresh every 10s
 * 3: ram(4byte int), refresh every 30s
 * 4: ip(to the end), refresh every 60s
 */
void onPacketReceived(const uint8_t* buffer, size_t size)
{
  // Make a temporary buffer.
  uint8_t tempBuffer[size + 1];
  uint8_t type;
  uint32_t tempValue;
  int x = 0;

  if (size < 5) return;

  // Copy the packet into our temporary buffer.
  memcpy(tempBuffer, buffer, size);
  tempBuffer[size] = 0;

  memcpy(&type, tempBuffer + x, 1);
  x += 1;

  switch(type) {
    case 1:
      memcpy(&tempValue, tempBuffer + x, 4);
      cpuTemp = tempValue / 1000.0f;
      cpuTempAvailable = true;
      DEBUGSER.print("cpuTemp="); 
      DEBUGSER.println(cpuTemp);
      break;
    case 2:
      memcpy(&tempValue, tempBuffer + x, 4);
      cpuLoad = tempValue / 1000.0f;
      pushCpuLoad(cpuLoad);
      break;
    case 3:
      memcpy(&tempValue, tempBuffer + x, 4);
      ram = tempValue / 1000.0f;
      break;
    case 4:
      ip = (const char *)(tempBuffer + x);
      break;
    default:
      break;
  }
}

void pushCpuLoad(double load) {
  cpuLoadHistory[CPU_LOAD_HISTORY_CNT] = int(load);
  for (int i = 0; i < CPU_LOAD_HISTORY_CNT; i++) {
    cpuLoadHistory[i] = cpuLoadHistory[i + 1];
  }
}

//get current fan speed
double getFanSpeed() {
  if (fanPwm < 50 ) return 0;
  pulseDuration = pulseIn(fanPulseInPin, LOW);
  DEBUGSER.print("pulseDuration="); 
  DEBUGSER.println(pulseDuration);
  if (pulseDuration < 1000) pulseDuration = 100000000;
  double frequency = 1000000/pulseDuration;
  double rpm = frequency / 4 * 60;
  return   rpm;
}

void sendFanSpeed() {
  int fs = int(fanSpeed);
  int len;
  len = sprintf(packetBuildBuffer, "{\"fanSpeed\": %d}", fs);
  myPacketSerial.send((uint8_t *)packetBuildBuffer, len);
}
