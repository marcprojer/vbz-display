#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "homeassistant.h"

unsigned long lastPollMs = 0;
bool clockSynced = false;
DisplayView displayView;
HomeAssistantControl haControl;
bool panelAwake = false;
bool otaStarted = false;

String buildStationboardUrl() {
	String url = "https://transport.opendata.ch/v1/stationboard";
	url += "?id=" + String(STATION_ID);
	url += "&station=" + String(STATION_KEY);
	url += "&limit=" + String(API_FETCH_LIMIT);
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
	if (USE_STATIC_IP) {
		if (!WiFi.config(STATIC_IP, STATIC_GATEWAY, STATIC_SUBNET, STATIC_DNS1, STATIC_DNS2)) {
			Serial.println("Statische IP-Konfiguration fehlgeschlagen, nutze DHCP.");
		}
	}
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

void setupOtaIfNeeded() {
	if (!OTA_ENABLED) {
		return;
	}

	if (WiFi.status() != WL_CONNECTED) {
		otaStarted = false;
		return;
	}

	if (otaStarted) {
		return;
	}

	ArduinoOTA.setHostname(OTA_HOSTNAME);
	if (strlen(OTA_PASSWORD) > 0) {
		ArduinoOTA.setPassword(OTA_PASSWORD);
	}

	ArduinoOTA.onStart([]() {
		Serial.println("OTA update gestartet...");
	});

	ArduinoOTA.onEnd([]() {
		Serial.println("OTA update abgeschlossen.");
	});

	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		static unsigned int lastPercent = 0;
		unsigned int percent = total > 0 ? (progress * 100U) / total : 0;
		if (percent >= lastPercent + 10U || percent == 100U) {
			lastPercent = percent;
			Serial.printf("OTA Fortschritt: %u%%\n", percent);
		}
	});

	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("OTA Fehler [%u]\n", (unsigned int)error);
	});

	ArduinoOTA.begin();
	otaStarted = true;
	Serial.print("OTA bereit. Hostname: ");
	Serial.println(OTA_HOSTNAME);
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

bool categoryMatchesMode(const char* category, VehicleFilterMode mode) {
	if (mode == VehicleFilterMode::All) {
		return true;
	}

	String cat = safeString(category);
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

String readHttpBodyFast(HTTPClient& http, uint32_t idleTimeoutMs) {
	WiFiClient* stream = http.getStreamPtr();
	if (!stream) {
		return String();
	}

	String payload;
	int contentLength = http.getSize();
	if (contentLength > 0) {
		payload.reserve((size_t)contentLength + 8);
	}

	unsigned long lastDataMs = millis();
	size_t bytesRead = 0;
	
	while (http.connected() || stream->available() > 0) {
		while (stream->available() > 0) {
			char c = (char)stream->read();
			payload += c;
			bytesRead++;
			lastDataMs = millis();
			
			// If we know content length and have read all bytes, return immediately
			if (contentLength > 0 && bytesRead >= (size_t)contentLength) {
				return payload;
			}
		}

		// If no data available and idle timeout passed, stop
		if (millis() - lastDataMs >= idleTimeoutMs) {
			break;
		}
		delay(1);
	}

	return payload;
}

void fetchAndPrintDepartures() {
	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("Kein WLAN, Abfrage uebersprungen.");
		return;
	}

	ensureClockSync();

	IPAddress apiIp;
	if (!WiFi.hostByName("transport.opendata.ch", apiIp)) {
		Serial.println("DNS Fehler: transport.opendata.ch nicht aufloesbar.");
		return;
	}
	Serial.print("API Host IP: ");
	Serial.println(apiIp);

	WiFiClientSecure client;
	client.setInsecure();
 	client.setTimeout(10000);

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
	http.setReuse(false);
	http.addHeader("Connection", "close");
	http.setConnectTimeout(4000);
	http.setTimeout(5000);

	unsigned long getStartMs = millis();
	int httpCode = http.GET();
	unsigned long getDurationMs = millis() - getStartMs;
	if (httpCode <= 0) {
		Serial.print("HTTP Fehler: ");
		Serial.println(http.errorToString(httpCode));
		Serial.print("GET Dauer: ");
		Serial.print(getDurationMs);
		Serial.println(" ms");
		http.end();
		return;
	}

	Serial.print("HTTP Status: ");
	Serial.println(httpCode);
	Serial.print("GET Dauer: ");
	Serial.print(getDurationMs);
	Serial.println(" ms");

	int payloadSize = http.getSize();
	if (payloadSize > 0) {
		Serial.print("Payload Bytes: ");
		Serial.println(payloadSize);
	}

	unsigned long bodyStartMs = millis();
	String payload = readHttpBodyFast(http, 2500);
	unsigned long bodyDurationMs = millis() - bodyStartMs;
	http.end();
	Serial.print("Body Read Dauer: ");
	Serial.print(bodyDurationMs);
	Serial.println(" ms");

	if (httpCode != HTTP_CODE_OK) {
		Serial.println("Unerwartete API-Antwort:");
		Serial.println(payload);
		return;
	}

	if (payload.length() == 0) {
		Serial.println("JSON Fehler: EmptyInput (leerer HTTP-Body)");
		return;
	}

	Serial.print("Body Bytes: ");
	Serial.println(payload.length());

	DynamicJsonDocument doc(20 * 1024);
	DeserializationError err = deserializeJson(doc, payload);
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

		const char* category = connection["category"];
		if (!categoryMatchesMode(category, haControl.getMode())) {
			continue;
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
			etaText = "0";  // Use '0' for serial; display.h converts to glyph for panel
		} else if (etaMin > 0) {
			String liveMarker = hasLiveData ? "`" : "'";
			etaText = String(etaMin) + liveMarker;
		}

		String line = safeString(number);
		if (line == "-") {
			line = safeString(name);
		}

		DepartureDisplayRow row;
		// Generate unique ID: number:destination:plannedTs
		row.id = String(number) + ":" + String(destination) + ":" + String(plannedTs);
		row.line = line;
		row.direction = safeString(destination);
		row.delay = delayText;
		row.liveIn = etaText;
		row.category = safeString(category);

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

	// Extract just the row data (without sortEtaMin) for cache update
	DepartureDisplayRow rowsForCache[RESULT_LIMIT];
	for (size_t i = 0; i < sortedCount; i++) {
		rowsForCache[i] = sortedRows[i].row;
	}

	displayView.updateCachedRows(rowsForCache, sortedCount);
}

void setup() {
	Serial.begin(115200);
	delay(500);
	displayView.begin();

	connectWiFi();
	setupOtaIfNeeded();
	haControl.begin(HA_WEBHOOK_TOKEN, WAKE_DURATION_MINUTES);
	haControl.wakeForMinutes(WAKE_DURATION_MINUTES);
	panelAwake = haControl.isAwake();
	displayView.setPanelAwake(panelAwake);
	fetchAndPrintDepartures();
	lastPollMs = millis();
}

void loop() {
	haControl.handle();

	if (WiFi.status() != WL_CONNECTED) {
		connectWiFi();
	}
	setupOtaIfNeeded();
	if (OTA_ENABLED && WiFi.status() == WL_CONNECTED) {
		ArduinoOTA.handle();
	}

	bool awakeNow = haControl.isAwake();
	if (awakeNow != panelAwake) {
		panelAwake = awakeNow;
		displayView.setPanelAwake(panelAwake);
	}

	unsigned long now = millis();
	if (panelAwake && haControl.consumeRefreshRequest()) {
		displayView.scrollReset();
		fetchAndPrintDepartures();
		lastPollMs = now;
	}

	if (panelAwake && haControl.consumeScrollUpRequest()) {
		displayView.scrollUp();
		displayView.renderCachedRows();
	}

	if (panelAwake && haControl.consumeScrollDownRequest()) {
		displayView.scrollDown();
		displayView.renderCachedRows();
	}

	if (panelAwake && now - lastPollMs >= POLL_INTERVAL_MS) {
		displayView.scrollReset();
		fetchAndPrintDepartures();
		lastPollMs = now;
	}

	delay(50);
}
