#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "vbzfont.h"
#include "config.h"

// HUB75 panel configuration for 128x64 (2x 64x64 chained)
#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 2

// ESP32 HUB75 default wiring
#define R1_PIN 2
#define G1_PIN 15
#define B1_PIN 4
#define R2_PIN 16
#define G2_PIN 27
#define B2_PIN 17
#define A_PIN 5
#define B_PIN 18
#define C_PIN 19
#define D_PIN 21
#define E_PIN 12
#define LAT_PIN 26
#define OE_PIN 25
#define CLK_PIN 22

struct DepartureDisplayRow {
  String line;
  String direction;
  String delay;
  String liveIn;
};

class DisplayView {
 public:
  void begin() {
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    mxconfig.clkphase = false;
    mxconfig.min_refresh_rate = 90;
    mxconfig.gpio.r1 = G1_PIN;
    mxconfig.gpio.g1 = B1_PIN;
    mxconfig.gpio.b1 = R1_PIN;
    mxconfig.gpio.r2 = G2_PIN;
    mxconfig.gpio.g2 = B2_PIN;
    mxconfig.gpio.b2 = R2_PIN;
    mxconfig.gpio.a = A_PIN;
    mxconfig.gpio.b = B_PIN;
    mxconfig.gpio.c = C_PIN;
    mxconfig.gpio.d = D_PIN;
    mxconfig.gpio.e = E_PIN;
    mxconfig.gpio.lat = LAT_PIN;
    mxconfig.gpio.oe = OE_PIN;
    mxconfig.gpio.clk = CLK_PIN;

    dmaDisplay = new MatrixPanel_I2S_DMA(mxconfig);
    dmaDisplay->begin();
    dmaDisplay->setTextWrap(false);
    dmaDisplay->setLatBlanking(2);
    dmaDisplay->setBrightness8(DISPLAY_BRIGHTNESS);

    kBlack = dmaDisplay->color565(0, 0, 0);
    kWhite = dmaDisplay->color565(255, 255, 255);
    kYellow = dmaDisplay->color565(252, 249, 110);
    kBlue = dmaDisplay->color565(0, 102, 255);

    dmaDisplay->clearScreen();
    dmaDisplay->fillScreen(kBlack);
    dmaDisplay->setFont(&vbzfont);
    dmaDisplay->setTextSize(1);
    normalBrightness = DISPLAY_BRIGHTNESS;
    panelEnabled = true;
    matrixReady = true;
  }

  void setPanelAwake(bool awake) {
    if (!matrixReady || dmaDisplay == nullptr) {
      return;
    }

    panelEnabled = awake;
    if (awake) {
      dmaDisplay->setBrightness8(normalBrightness);
    } else {
      dmaDisplay->fillScreen(kBlack);
      dmaDisplay->setBrightness8(0);
    }
  }

  void showDeparturesHeader(const char* stationKey, size_t count) {
    rowIndex = 0;
    // Store total count for scroll boundary checking
    totalRows = count;

    if (matrixReady) {
      dmaDisplay->fillScreen(kBlack);
    }

    Serial.print("Abfahrten fuer ");
    Serial.print(stationKey);
    Serial.print(" (");
    Serial.print(count);
    Serial.println("):");
    Serial.println("Linie | Richtung | Delay | Live in");
  }

  void scrollUp() {
    if (scrollOffset > 0) {
      scrollOffset--;
      // Uncomment for 3-row scrolling:
      // if (scrollOffset >= 3) scrollOffset -= 3;
      // else scrollOffset = 0;
    }
  }

  void scrollDown() {
    // Max offset = totalRows - 5 (to keep 5 rows visible)
    int maxOffset = (totalRows > 5) ? (totalRows - 5) : 0;
    if (scrollOffset < maxOffset) {
      scrollOffset++;
      // Uncomment for 3-row scrolling:
      // if (scrollOffset + 3 <= maxOffset) scrollOffset += 3;
      // else scrollOffset = maxOffset;
    }
  }

  int getScrollOffset() const {
    return scrollOffset;
  }

  size_t getTotalRows() const {
    return totalRows;
  }

  void scrollReset() {
    scrollOffset = 0;
  }
  void showDepartureRow(const DepartureDisplayRow& row) {
    // Only render if row is within visible range
    if (matrixReady && panelEnabled && rowIndex >= scrollOffset && rowIndex < (scrollOffset + 5)) {
      String line = row.line;
      line.replace(" ", "");
      line.trim();
      
      String dir = cropDestination(row.direction);
      String liveIn = row.liveIn;
      if (liveIn == "0") {
        liveIn = "\x1E";  // Convert '0' to VBZ "sofort" glyph for panel
      }

      char lineBuf[20];
      char dirBuf[30];
      char liveBuf[12];
      line.toCharArray(lineBuf, sizeof(lineBuf));
      dir.toCharArray(dirBuf, sizeof(dirBuf));
      liveIn.toCharArray(liveBuf, sizeof(liveBuf));

      // Calculate Y position relative to viewport (0-based within visible area)
      int viewportRow = rowIndex - scrollOffset;
      int lineNumber = viewportRow * 13;
      uint16_t badgeBg = getLineBadgeBackground(line);
      uint16_t badgeFg = getLineBadgeTextColor(line);

      // Clear the whole row first to avoid glyph leftovers/ghost pixels.
      dmaDisplay->fillRect(0, lineNumber, 128, 11, kBlack);
      dmaDisplay->fillRect(0, lineNumber, 24, 11, badgeBg);

      int xPos = getRightAlignStartingPoint(lineBuf, 23);
      dmaDisplay->setTextColor(badgeFg);
      dmaDisplay->setCursor(xPos, lineNumber);
      dmaDisplay->print(lineBuf);

      dmaDisplay->setTextColor(kYellow);
      dmaDisplay->setCursor(27, lineNumber);
      dmaDisplay->print(dirBuf);

      xPos = getRightAlignStartingPoint(liveBuf, 16);
      dmaDisplay->setCursor(112 + xPos, lineNumber);
      dmaDisplay->print(liveBuf);

      rowIndex++;
    }

    Serial.print(row.line);
    Serial.print(" | ");
    Serial.print(row.direction);
    Serial.print(" | ");
    Serial.print(row.delay);
    Serial.print(" | ");
    Serial.println(row.liveIn);
  }

 private:
  MatrixPanel_I2S_DMA* dmaDisplay = nullptr;
  bool matrixReady = false;
  bool panelEnabled = true;
  uint8_t rowIndex = 0;
    int scrollOffset = 0;
    size_t totalRows = 0;
  uint8_t normalBrightness = DISPLAY_BRIGHTNESS;

  uint16_t kBlack = 0;
  uint16_t kWhite = 0;
  uint16_t kYellow = 0;
  uint16_t kBlue = 0;

  int extractLineNumber(const String& line) {
    String digits = "";
    for (size_t i = 0; i < line.length(); i++) {
      char c = line.charAt(i);
      if (c >= '0' && c <= '9') {
        digits += c;
      }
    }
    if (digits.length() == 0) {
      return -1;
    }
    return digits.toInt();
  }

  uint16_t getLineBadgeBackground(const String& line) {
    int lineNo = extractLineNumber(line);
    switch (lineNo) {
      case 3: return dmaDisplay->color565(0, 137, 47);
      case 4: return dmaDisplay->color565(17, 41, 111);
      case 6: return dmaDisplay->color565(202, 125, 60);
      case 7: return dmaDisplay->color565(0, 0, 0);
      case 8: return dmaDisplay->color565(138, 181, 31);
      case 9: return dmaDisplay->color565(17, 41, 111);
      case 11: return dmaDisplay->color565(0, 137, 47);
      case 13: return dmaDisplay->color565(255, 193, 0);
      case 14: return dmaDisplay->color565(0, 141, 197);
      case 17: return dmaDisplay->color565(142, 34, 77);
      case 50: return dmaDisplay->color565(0, 0, 0);
      case 51: return dmaDisplay->color565(0, 0, 0);
      case 31: return dmaDisplay->color565(165, 162, 198);
      case 32: return dmaDisplay->color565(204, 178, 209);
      case 33: return dmaDisplay->color565(218, 214, 156);
      case 46: return dmaDisplay->color565(193, 213, 159);
      case 72: return dmaDisplay->color565(198, 166, 147);
      case 83: return dmaDisplay->color565(255, 255, 255);
      default: return kBlue;
    }
  }

  uint16_t getLineBadgeTextColor(const String& line) {
    int lineNo = extractLineNumber(line);
    switch (lineNo) {
      // Tram lines
      case 3: return kWhite;
      case 4: return kWhite;
      case 6: return kBlack;
      case 7: return kWhite;
      case 8: return kBlack;
      case 9: return kWhite;
      case 11: return kWhite;
      case 13: return kBlack;
      case 14: return kWhite;
      case 17: return kWhite;
      case 50: return kWhite;
      case 51: return kWhite;

      // Bus lines
      case 31: return kWhite;
      case 32: return kBlack;
      case 33: return kBlack;
      case 46: return kBlack;
      case 72: return kBlack;
      case 83: return kBlack;

      default: return kWhite;
    }
  }

  uint8_t getRightAlignStartingPoint(const char* str, int16_t width) {
    GFXcanvas1 canvas(width, 16);
    canvas.setFont(&vbzfont);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setCursor(0, 0);
    canvas.print(str);
    int advance = canvas.getCursorX() + 1;
    int xPos = width - advance;
    return (xPos > 0) ? xPos : 0;
  }

  String cropDestination(String destination) {
    // remove extra text / prefixes that are not relevant
    destination.replace("Zürich,", "");
    destination.replace("Zürich ", "");
    destination.replace("Zurich,", "");    
    destination.replace("Zurich", "");
    destination.replace("Winterthur,", "");
    destination.replace("Bahnhof ", "");

    while (destination.indexOf("  ") >= 0) {
      destination.replace("  ", " ");
    }
    
    destination.trim();

    // Map umlauts to vbzfont glyph codes (after city filtering)
    destination.replace("ä", "\x7B");
    destination.replace("ö", "\x7C");
    destination.replace("ü", "\x7D");

    // Delay is no longer shown on panel, so destination can use more width.
    // Keep some gap before the right-aligned live-in column at x=112.
    const int maxDestinationWidthPx = 84;
    bool textWasTooLong = false;
    while (getTextUsedLength(destination) > maxDestinationWidthPx && destination.length() > 0) {
      destination = destination.substring(0, destination.length() - 1);
      textWasTooLong = true;
    }

    // Fallback guard for fonts where pixel width estimation can differ.
    while (destination.length() > 14) {
      destination = destination.substring(0, destination.length() - 1);
      textWasTooLong = true;
    }

    if (textWasTooLong && destination.length() > 0) {
      destination = destination + ".";
    }

    return destination;
  }

  int getTextUsedLength(String text) {
    GFXcanvas1 canvas(128, 16);
    canvas.setFont(&vbzfont);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setCursor(0, 0);
    canvas.print(text);
    return canvas.getCursorX() + 1;
  }
};
