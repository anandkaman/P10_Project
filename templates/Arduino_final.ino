#include <SPI.h>
#include <DMD.h>
#include <TimerOne.h>
#include "Arial_Black_16.h"
#include "SystemFont5x7.h"

// --- DMD Configuration ---
#define DISPLAYS_ACROSS     7
#define DISPLAYS_DOWN       1
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

// --- Data Structures ---
struct ProductionSet {
  int prod_id;
  int plan_day;
  int actual_day;
  int gap_day;
  int plan_month;
  int actual_month;
  int gap_month;
  ProductionSet() : prod_id(0), plan_day(0), actual_day(0), gap_day(0),
                    plan_month(0), actual_month(0), gap_month(0) {}
};
ProductionSet production_data_sets[3]; 

// --- Timing ---
unsigned long lastDataDisplayTime = 0;
const long receivedMessageDuration = 2000;
const long cycleDuration = 10000;
unsigned long lastCycleTime = 0;
const long indicatorDuration = 1000;
unsigned long indicatorDisplayStartTime = 0;

// --- State ---
int currentDisplaySetIndex = 0;
int lastDrawnSetIndex = -1;
bool newDataReceivedFromESP = false;
bool showReceivedIndicator = false;

// --- Function Prototypes ---
void ScanDMD();
void parseAndStoreData(String dataString);
void updateProductionSet(const ProductionSet& new_data);
void drawAllData(const ProductionSet& currentSet);
void drawReceivedMessage(int prodId);
void drawWaitingMessage();
int calculateTextWidth(String text, DMD& display);

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Timer1.initialize(5000);              // Safer refresh rate: 5ms
  Timer1.attachInterrupt(ScanDMD);
  dmd.clearScreen(true);
  
  dmd.selectFont(Arial_Black_16);
  dmd.drawString(0, 0, "Booting...", 10, GRAPHICS_NORMAL);
  delay(1000);
  dmd.clearScreen(true);

  for (int i = 0; i < 3; ++i) {
    production_data_sets[i].prod_id = i + 1;
  }

  lastCycleTime = millis();
}

// --- Loop ---
void loop() {
  // --- Serial Reception ---
  if (Serial.available()) {
    String receivedString = Serial.readStringUntil('\n');
    receivedString.trim();
    if (receivedString.length() > 0) {
      parseAndStoreData(receivedString);
      newDataReceivedFromESP = true;
      showReceivedIndicator = true;
      indicatorDisplayStartTime = millis();
      lastDataDisplayTime = millis();
    }
    while (Serial.available()) Serial.read(); // clear buffer
  }

  // --- Display Logic ---
  if (newDataReceivedFromESP && (millis() - lastDataDisplayTime < receivedMessageDuration)) {
    drawReceivedMessage(production_data_sets[currentDisplaySetIndex].prod_id);
  } else {
    newDataReceivedFromESP = false;

    if (millis() - lastCycleTime >= cycleDuration) {
      currentDisplaySetIndex = (currentDisplaySetIndex + 1) % 3;
      lastCycleTime = millis();
    }

    if (production_data_sets[0].prod_id == 0 &&
        production_data_sets[1].prod_id == 0 &&
        production_data_sets[2].prod_id == 0) {
      drawWaitingMessage();
    } else {
      if (currentDisplaySetIndex != lastDrawnSetIndex) {
        drawAllData(production_data_sets[currentDisplaySetIndex]);
        lastDrawnSetIndex = currentDisplaySetIndex;
      }
    }

    if (showReceivedIndicator && (millis() - indicatorDisplayStartTime < indicatorDuration)) {
      dmd.drawBox(0, 0, 3, 3, true); // 2x2 LED indicator
    } else {
      showReceivedIndicator = false;
    }
  }
}

// --- Timer ISR ---
void ScanDMD() {
  dmd.scanDisplayBySPI();
}

// --- Parse and Store Data ---
void parseAndStoreData(String dataString) {
  ProductionSet new_data;
  int currentPos = 0;
  int nextPos;

  nextPos = dataString.indexOf(',', currentPos);
  if (nextPos == -1) return;
  new_data.prod_id = dataString.substring(currentPos, nextPos).toInt();
  currentPos = nextPos + 1;

  if (new_data.prod_id < 1 || new_data.prod_id > 3) return;

  nextPos = dataString.indexOf(',', currentPos);
  if (nextPos == -1) return;
  new_data.plan_day = dataString.substring(currentPos, nextPos).toInt(); currentPos = nextPos + 1;

  nextPos = dataString.indexOf(',', currentPos);
  if (nextPos == -1) return;
  new_data.actual_day = dataString.substring(currentPos, nextPos).toInt(); currentPos = nextPos + 1;

  nextPos = dataString.indexOf(',', currentPos);
  if (nextPos == -1) return;
  new_data.gap_day = dataString.substring(currentPos, nextPos).toInt(); currentPos = nextPos + 1;

  nextPos = dataString.indexOf(',', currentPos);
  if (nextPos == -1) return;
  new_data.plan_month = dataString.substring(currentPos, nextPos).toInt(); currentPos = nextPos + 1;

  nextPos = dataString.indexOf(',', currentPos);
  if (nextPos == -1) return;
  new_data.actual_month = dataString.substring(currentPos, nextPos).toInt(); currentPos = nextPos + 1;

  new_data.gap_month = dataString.substring(currentPos).toInt();

  updateProductionSet(new_data);
}

// --- Update Production Set ---
void updateProductionSet(const ProductionSet& new_data) {
  if (new_data.prod_id >= 1 && new_data.prod_id <= 3) {
    production_data_sets[new_data.prod_id - 1] = new_data;
  }
}

// --- Draw All Values Across Panels ---
void drawAllData(const ProductionSet& currentSet) {
  dmd.clearScreen(true);
  dmd.selectFont(Arial_Black_16);

  const int panelWidth = 32;
  const int panelHeight = 16;
  const int y_offset = 0; // Top-aligned text

  String vals[7] = {
    String(currentSet.gap_day),    // gap_day
    String(currentSet.gap_month),  // gap_month
    String(currentSet.actual_day), // actual_day
    String(currentSet.actual_month),// actual_month
    String(currentSet.plan_day),   // plan_day
    String(currentSet.plan_month), // plan_month
    String(currentSet.prod_id)     // prod_id
  };

  for (int i = 0; i < 7; ++i) {
    // Truncate string if too wide for panel
    String displayVal = vals[i];
    while (calculateTextWidth(displayVal, dmd) > panelWidth && displayVal.length() > 1) {
      displayVal = displayVal.substring(0, displayVal.length() - 1);
    }

    // Center text horizontally
    int textWidth = calculateTextWidth(displayVal, dmd);
    int x_offset = (panelWidth - textWidth) / 2;
    if (x_offset < 0) x_offset = 0;

    // Draw text in the i-th panel
    dmd.drawString(i * panelWidth + x_offset, y_offset, displayVal.c_str(), displayVal.length(), GRAPHICS_NORMAL);
  }
}
// --- "Rcvd:X" Message ---
void drawReceivedMessage(int prodId) {
  dmd.clearScreen(true);
  dmd.selectFont(Arial_Black_16);
  String msg = "Rcvd:" + String(prodId);
  int x_offset = (32 - calculateTextWidth(msg, dmd)) / 2;
  if (x_offset < 0) x_offset = 0;
  dmd.drawString(x_offset, 0, msg.c_str(), msg.length(), GRAPHICS_NORMAL);
}

// --- Waiting Message ---
void drawWaitingMessage() {
  dmd.clearScreen(true);
  dmd.selectFont(SystemFont5x7);
  String msg = "Waiting for data...";
  int x_offset = (DISPLAYS_ACROSS * 32 - calculateTextWidth(msg, dmd)) / 2;
  if (x_offset < 0) x_offset = 0;
  dmd.drawString(x_offset, (16 - 7) / 2, msg.c_str(), msg.length(), GRAPHICS_NORMAL);
}

// --- Text Width Helper ---
int calculateTextWidth(String text, DMD& display) {
  int width = 0;
  for (int i = 0; i < text.length(); ++i) {
    width += display.charWidth(text.charAt(i));
  }
  return width;
}
