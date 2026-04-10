#pragma once

#include <Arduino.h>
#include "display.h"
#include "homeassistant.h"

inline String fakeApiSafeString(const char* value) {
  if (value && strlen(value) > 0) {
    return String(value);
  }
  return String("-");
}

inline bool fakeApiCategoryMatchesMode(const char* category, VehicleFilterMode mode) {
  if (mode == VehicleFilterMode::All) {
    return true;
  }

  String cat = fakeApiSafeString(category);
  cat.toLowerCase();

  bool isTram = (cat == "t" || cat == "tram");
  bool isBus = (cat == "b" || cat == "bus");

  if (mode == VehicleFilterMode::Tram) {
    return isTram;
  }
  if (mode == VehicleFilterMode::Bus) {
    return isBus;
  }

  return true;
}

inline void fakeApiAddRow(
    DepartureDisplayRow rows[],
    size_t maxRows,
    size_t& count,
    const char* category,
    const char* line,
    const char* destination,
    const char* liveIn,
    const char* delay
) {
  if (count >= maxRows) {
    return;
  }

  DepartureDisplayRow row;
  row.id = String(line) + ":" + String(destination) + ":" + String(count);
  row.line = fakeApiSafeString(line);
  row.direction = fakeApiSafeString(destination);
  row.delay = fakeApiSafeString(delay);
  row.liveIn = fakeApiSafeString(liveIn);
  row.category = fakeApiSafeString(category);
  rows[count++] = row;
}

inline size_t buildFakeDepartures(
    DepartureDisplayRow rows[],
    size_t maxRows,
    uint8_t scenario,
    VehicleFilterMode mode
) {
  size_t count = 0;

  auto addFiltered = [&](const char* category, const char* line, const char* destination, const char* liveIn, const char* delay) {
    if (fakeApiCategoryMatchesMode(category, mode)) {
      fakeApiAddRow(rows, maxRows, count, category, line, destination, liveIn, delay);
    }
  };

  switch (scenario) {
    case 1:  // Night lines focus
      addFiltered("T", "8", "Central", "0", "2 min");
      addFiltered("BN", "N4", "Klusplatz", "3`", "0 min");
      addFiltered("B", "72", "AlbisriedenAlbisrieden", ">6`", "3 min");
      addFiltered("BN", "N8", "Werdhölzli", "9'", "-");
      addFiltered("T", "50", "AltstettenAltstetten", ">14'", "-");
      addFiltered("B", "72", "MorgentalMorgental", ">18`", "0 min");
      break;

    case 2:  // Tram focus
      addFiltered("T", "4", "Tiefenbrunnen", "1`", "0 min");
      addFiltered("T", "8", "Hardturm", "4`", "0 min");
      addFiltered("T", "13", "Albisguetli", ">7`", "2 min");
      addFiltered("T", "17", "Werdhölzli", "10'", "-");
      addFiltered("T", "6", "Zoo", "13'", "-");
      break;

    case 3:  // Bus focus
      addFiltered("B", "31", "Kienastenwies", "2`", "0 min");
      addFiltered("B", "32", "Holzerhurd", "5`", "0 min");
      addFiltered("B", "33", "Triemli", ">8`", "2 min");
      addFiltered("B", "46", "Ruetihof", "11'", "-");
      addFiltered("B", "83", "Milchbuck", "16'", "-");
      break;

    default:  // Mixed default
      addFiltered("T", "4", "Tiefenbrunnen", "1`", "0 min");
      addFiltered("B", "72", "Morgental", "4`", "0 min");
      addFiltered("T", "8", "Hardturm", "6'", "-");
      addFiltered("BN", "N1", "Central", ">9`", "4 min");
      addFiltered("B", "31", "Kienastenwies", "12'", "-");
      addFiltered("T", "13", "Albisguetli", "15'", "-");
      break;
  }

  if (count == 0) {
    fakeApiAddRow(rows, maxRows, count, "B", "--", "Keine passenden Eintraege", "-", "-");
  }

  return count;
}
