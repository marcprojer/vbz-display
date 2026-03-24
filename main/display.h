#pragma once

#include <Arduino.h>
#include <SmartMatrix3.h>
#include <Adafruit_GFX.h>

// Basiskonfiguration
#define DISPLAY_COLOR_DEPTH 24
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_REFRESH_DEPTH 24
#define DISPLAY_DMA_BUFFER_ROWS 4
#define DISPLAY_PANEL_TYPE SMARTMATRIX_HUB75_64ROW_MOD32SCAN
#define DISPLAY_MATRIX_OPTIONS SMARTMATRIX_OPTIONS_NONE
#define DISPLAY_BACKGROUND_OPTIONS SM_BACKGROUND_OPTIONS_NONE

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_REFRESH_DEPTH, DISPLAY_DMA_BUFFER_ROWS, DISPLAY_PANEL_TYPE, DISPLAY_MATRIX_OPTIONS);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_COLOR_DEPTH, DISPLAY_BACKGROUND_OPTIONS);

struct DepartureDisplayRow {
  String line;
  String direction;
  String delay;
  String liveIn;
};

class DisplayView {
 public:
  void begin() {
    matrix.addLayer(&backgroundLayer);
    matrix.begin();
    matrix.setBrightness((25 * 255) / 100);
    backgroundLayer.enableColorCorrection(true);
    backgroundLayer.setFont(font6x10);
    backgroundLayer.fillScreen(kBlack);
    backgroundLayer.swapBuffers();
    matrixReady = true;
  }

  void showDeparturesHeader(const char* stationKey, size_t count) {
    rowIndex = 0;

    if (matrixReady) {
      backgroundLayer.fillScreen(kBlack);
      backgroundLayer.swapBuffers();
    }

    Serial.print("Abfahrten fuer ");
    Serial.print(stationKey);
    Serial.print(" (");
    Serial.print(count);
    Serial.println("):");
    Serial.println("Linie | Richtung | Delay | Live in");
  }

  void showDepartureRow(const DepartureDisplayRow& row) {
    if (matrixReady && rowIndex < 5) {
      String line = row.line;
      line.replace(" ", "");
      line.trim();
      
      String dir = cropDestination(row.direction);
      String delay = row.delay;
      String liveIn = row.liveIn;

      char lineBuf[20];
      char dirBuf[30];
      char delayBuf[12];
      char liveBuf[12];
      line.toCharArray(lineBuf, sizeof(lineBuf));
      dir.toCharArray(dirBuf, sizeof(dirBuf));
      delay.toCharArray(delayBuf, sizeof(delayBuf));
      liveIn.toCharArray(liveBuf, sizeof(liveBuf));

      int lineNumber = rowIndex * 13;
      
      // Line background (blue) - draw filled rectangle with lines
      for (int i = 0; i < 11; i++) {
        backgroundLayer.drawLine(0, lineNumber + i, 24, lineNumber + i, kBlue);
      }
      int xPos = getRightAlignStartingPoint(lineBuf, 23);
      backgroundLayer.drawString(xPos, lineNumber, kWhite, lineBuf);
      
      // Destination (left-aligned)
      backgroundLayer.drawString(27, lineNumber, kYellow, dirBuf);
      
      // Delay (right-aligned ~97)
      xPos = getRightAlignStartingPoint(delayBuf, 16);
      backgroundLayer.drawString(97 + xPos, lineNumber, kYellow, delayBuf);
      
      // TTA (right-aligned ~112)
      xPos = getRightAlignStartingPoint(liveBuf, 16);
      backgroundLayer.drawString(112 + xPos, lineNumber, kYellow, liveBuf);
      
      backgroundLayer.swapBuffers();
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
  bool matrixReady = false;
  uint8_t rowIndex = 0;

  const rgb24 kBlack = {0, 0, 0};
  const rgb24 kWhite = {255, 255, 255};
  const rgb24 kYellow = {252, 249, 110};
  const rgb24 kBlue = {255, 0, 0};  // SmartMatrix3 might use BGR order: B,G,R

  uint8_t getRightAlignStartingPoint(const char* str, int16_t width) {
    // Estimate text width: font6x10 is ~6px per character
    int textLen = strlen(str);
    int textWidth = textLen * 6;
    int xPos = width - textWidth;
    return (xPos > 0) ? xPos : 0;
  }

  String cropDestination(String destination) {
    // normalize umlauts first to keep later replacements predictable
    destination.replace("ä", "a");
    destination.replace("ö", "o");
    destination.replace("ü", "u");
    destination.replace("Ä", "A");
    destination.replace("Ö", "O");
    destination.replace("Ü", "U");

    // remove extra text / prefixes that are not relevant
    destination.replace("Zurich,", "");
    destination.replace("Zurich ", "");
    destination.replace("Zurich", "");
    destination.replace("zurich,", "");
    destination.replace("zurich", "");
    destination.replace("Winterthur,", "");
    destination.replace("Bahnhof ", "");

    while (destination.indexOf("  ") >= 0) {
      destination.replace("  ", " ");
    }
    
    destination.trim();

    // Calculate max width (70px for destination column)
    bool textWasTooLong = false;
    while (getTextUsedLength(destination) > 70 && destination.length() > 0) {
      destination = destination.substring(0, destination.length() - 1);
      textWasTooLong = true;
    }

    // Fallback guard for fonts where pixel width estimation can differ.
    while (destination.length() > 11) {
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
    canvas.setCursor(0, 0);
    canvas.print(text);
    return canvas.getCursorX() + 1;
  }
};
