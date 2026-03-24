#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "display.h"

unsigned long lastPollMs = 0;
bool clockSynced = false;
DisplayView displayView;

String buildStationboardUrl() {
	String url = "https://transport.opendata.ch/v1/stationboard";
	url += "?id=" + String(STATION_ID);
	url += "&station=" + String(STATION_KEY);
	url += "&limit=" + String(RESULT_LIMIT);
	url += "&transportations%5B%5D=tram";
	url += "&transportations%5B%5D=bus";
	url += "&fields%5B%5D=stationboard%2Fcategory";
	url += "&fields%5B%5D=stationboard%2Fname";
	url += "&fields%5B%5D=stationboard%2Fnumber";
	url += "&fields%5B%5D=stationboard%2Fto";
	url += "&fields%5B%5D=stationboard%2Fstop%2FdepartureTimestamp";
	url += "&fields%5B%5D=stationboard%2Fstop%2Fprognosis%2Fdeparture";
	url += "&fields%5B%5D=stationboard%2Fstop%2Fdelay";
	return url;
}

void ensureClockSync() {
	if (clockSynced) {
		return;
	}

	setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
	tzset();

	time_t now = time(nullptr);
	if (now > 1700000000) {
		clockSynced = true;
		return;
	}

	Serial.println("Synchronisiere Uhrzeit via NTP...");
	configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

	for (uint8_t i = 0; i < 30; i++) {
		delay(250);
		now = time(nullptr);
		if (now > 1700000000) {
			clockSynced = true;
			Serial.println("Uhrzeit synchronisiert.");
			return;
		}
	}

	Serial.println("NTP-Sync fehlgeschlagen, versuche spaeter erneut.");
}

int minutesUntilDeparture(time_t departureTs) {
	time_t now = time(nullptr);
	if (now < 100000 || departureTs <= 0) {
		return -1;
	}

	long deltaSec = (long)(departureTs - now);
	if (deltaSec <= 0) {
		return 0;
	}

	return (deltaSec + 59) / 60;
}

time_t parseIsoDateTimeToEpoch(const char* iso) {
	if (!iso || strlen(iso) < 19) {
		return 0;
	}

	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	char tzSign = 'Z';
	int tzHour = 0;
	int tzMinute = 0;

	int parsed = sscanf(
			iso,
			"%4d-%2d-%2dT%2d:%2d:%2d%c%2d:%2d",
			&year,
			&month,
			&day,
			&hour,
			&minute,
			&second,
			&tzSign,
			&tzHour,
			&tzMinute
	);

	if (parsed < 7) {
		return 0;
	}

	// Convert calendar date/time (UTC basis) to Unix epoch without timegm().
	int y = year;
	unsigned m = (unsigned)month;
	unsigned d = (unsigned)day;
	y -= (m <= 2) ? 1 : 0;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : (unsigned)9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const long daysSinceEpoch = era * 146097L + (long)doe - 719468L;

	time_t epoch = (time_t)daysSinceEpoch * 86400 + (time_t)hour * 3600 + (time_t)minute * 60 + (time_t)second;
	if (epoch <= 0) {
		return 0;
	}

	if (tzSign == '+' || tzSign == '-') {
		long tzOffsetSec = (long)tzHour * 3600L + (long)tzMinute * 60L;
		epoch -= (tzSign == '+') ? tzOffsetSec : -tzOffsetSec;
	}

	return epoch;
}

void connectWiFi() {
	if (WiFi.status() == WL_CONNECTED) {
		return;
	}

	Serial.print("Verbinde mit WLAN: ");
	Serial.println(WIFI_SSID);

	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASS);

	uint8_t tries = 0;
	while (WiFi.status() != WL_CONNECTED && tries < 40) {
		delay(250);
		Serial.print('.');
		tries++;
	}
	Serial.println();

	if (WiFi.status() == WL_CONNECTED) {
		Serial.print("WLAN verbunden. IP: ");
		Serial.println(WiFi.localIP());
	} else {
		Serial.println("WLAN-Verbindung fehlgeschlagen.");
	}
}

String safeString(const char* value) {
	if (value && strlen(value) > 0) {
		return String(value);
	}
	return String("-");
}

struct SortedDeparture {
	DepartureDisplayRow row;
	int sortEtaMin;
};

void fetchAndPrintDepartures() {
	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("Kein WLAN, Abfrage uebersprungen.");
		return;
	}

	ensureClockSync();

	WiFiClientSecure client;
	client.setInsecure();

	HTTPClient http;
	String url = buildStationboardUrl();

	Serial.println();
	Serial.println("----------------------------------------");
	Serial.print("GET ");
	Serial.println(url);

	if (!http.begin(client, url)) {
		Serial.println("HTTP begin() fehlgeschlagen.");
		return;
	}
	http.useHTTP10(true);
	http.setTimeout(15000);

	int httpCode = http.GET();
	if (httpCode <= 0) {
		Serial.print("HTTP Fehler: ");
		Serial.println(http.errorToString(httpCode));
		http.end();
		return;
	}

	Serial.print("HTTP Status: ");
	Serial.println(httpCode);

	if (httpCode != HTTP_CODE_OK) {
		String payload = http.getString();
		Serial.println("Unerwartete API-Antwort:");
		Serial.println(payload);
		http.end();
		return;
	}

	int payloadSize = http.getSize();
	if (payloadSize > 0) {
		Serial.print("Payload Bytes: ");
		Serial.println(payloadSize);
	}

	DynamicJsonDocument doc(20 * 1024);
	WiFiClient* stream = http.getStreamPtr();
	DeserializationError err = deserializeJson(doc, *stream);
	http.end();
	if (err) {
		Serial.print("JSON Fehler: ");
		Serial.println(err.c_str());
		return;
	}

	JsonArray stationboard = doc["stationboard"].as<JsonArray>();
	if (stationboard.isNull() || stationboard.size() == 0) {
		Serial.println("Keine Abfahrten gefunden.");
		return;
	}

	SortedDeparture sortedRows[RESULT_LIMIT];
	size_t sortedCount = 0;

	for (JsonObject connection : stationboard) {
		if (sortedCount >= RESULT_LIMIT) {
			break;
		}

		const char* number = connection["number"];
		const char* name = connection["name"];
		const char* destination = connection["to"];

		JsonObject stopObj = connection["stop"].as<JsonObject>();
		long plannedTs = stopObj["departureTimestamp"] | 0;
		const char* liveIso = stopObj["prognosis"]["departure"];
		bool hasLiveDelay = !stopObj["delay"].isNull();
		int delayMin = stopObj["delay"] | 0;

		time_t liveTs = parseIsoDateTimeToEpoch(liveIso);
		bool hasLiveData = liveTs > 0;
		time_t effectiveTs = liveTs > 0 ? liveTs : (time_t)plannedTs;

		String delayText = String("-");
		if (hasLiveDelay) {
			delayText = String(delayMin) + " min";
		}

		int etaMin = minutesUntilDeparture(effectiveTs);
		String etaText = String("-");
		if (etaMin == 0) {
			etaText = "\x1E";  // VBZ "sofort" icon from vbzfont
		} else if (etaMin > 0) {
			String liveMarker = hasLiveData ? "`" : "'";
			etaText = String(etaMin) + liveMarker;
		}

		String line = safeString(number);
		if (line == "-") {
			line = safeString(name);
		}

		DepartureDisplayRow row;
		row.line = line;
		row.direction = safeString(destination);
		row.delay = delayText;
		row.liveIn = etaText;

		SortedDeparture item;
		item.row = row;
		item.sortEtaMin = (etaMin >= 0) ? etaMin : 32767;
		sortedRows[sortedCount++] = item;
	}

	// Sort by "Live in" ascending so the next real departure is shown first.
	for (size_t i = 0; i < sortedCount; i++) {
		size_t minIndex = i;
		for (size_t j = i + 1; j < sortedCount; j++) {
			if (sortedRows[j].sortEtaMin < sortedRows[minIndex].sortEtaMin) {
				minIndex = j;
			}
		}
		if (minIndex != i) {
			SortedDeparture tmp = sortedRows[i];
			sortedRows[i] = sortedRows[minIndex];
			sortedRows[minIndex] = tmp;
		}
	}

	displayView.showDeparturesHeader(STATION_KEY, sortedCount);

	for (size_t i = 0; i < sortedCount; i++) {
		displayView.showDepartureRow(sortedRows[i].row);
	}
}

void setup() {
	Serial.begin(115200);
	delay(500);
	displayView.begin();

	connectWiFi();
	fetchAndPrintDepartures();
	lastPollMs = millis();
}

void loop() {
	if (WiFi.status() != WL_CONNECTED) {
		connectWiFi();
	}

	unsigned long now = millis();
	if (now - lastPollMs >= POLL_INTERVAL_MS) {
		fetchAndPrintDepartures();
		lastPollMs = now;
	}

	delay(50);
}
