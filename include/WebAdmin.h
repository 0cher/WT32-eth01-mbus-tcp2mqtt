#ifndef WEB_ADMIN_H
#define WEB_ADMIN_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Update.h>
#include <vector>
#include "AppSettings.h"
#include "Debug.h"
#include "MQTTClient.h"
#include "EthernetManager.h"
#include "MBusGateway.h"
#include "MbusService.h"
#include "TimeSync.h"

namespace WebAdmin {
  static WebServer server(80);
  static bool networkApplyPending = false;
  static unsigned long networkApplyAtMs = 0;
  using CachedMbusSnapshot = MbusService::CachedMbusSnapshot;
  using SnapshotRefreshStatus = MbusService::SnapshotRefreshStatus;

  static String htmlEscape(const String& input) {
    String output;
    output.reserve(input.length() + 16);
    for (size_t index = 0; index < input.length(); ++index) {
      char ch = input[index];
      switch (ch) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output += ch; break;
      }
    }
    return output;
  }

  static bool ensureAuthenticated() {
    AppSettings::Settings& settings = AppSettings::get();
    if (server.authenticate(settings.webUsername.c_str(), settings.webPassword.c_str())) {
      return true;
    }

    server.requestAuthentication();
    return false;
  }

  static void sendHtml(const String& html, int statusCode = 200) {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    if (html.length() <= 4096) {
      server.send(statusCode, "text/html; charset=utf-8", html);
      return;
    }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(statusCode, "text/html; charset=utf-8", "");
    const char* data = html.c_str();
    size_t sent = 0;
    while (sent < html.length()) {
      char chunk[1025];
      size_t chunkSize = html.length() - sent;
      if (chunkSize > 1024) {
        chunkSize = 1024;
      }
      memcpy(chunk, data + sent, chunkSize);
      chunk[chunkSize] = '\0';
      server.sendContent(chunk);
      sent += chunkSize;
    }
  }

  static void sendJson(const String& json, int statusCode = 200) {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(statusCode, "application/json; charset=utf-8", json);
  }

  static String serializeJsonResponse(const JsonDocument& doc) {
    String json;
    json.reserve(measureJson(doc) + 1);
    serializeJson(doc, json);
    return json;
  }

  static String buildMbusCheckBusJson(bool found,
                                      int primaryAddress,
                                      const String& secondaryAddress,
                                      const String& manufacturer,
                                      const String& type,
                                      const String& id,
                                      uint8_t version,
                                      const String& error = String()) {
    JsonDocument doc;
    doc["found"] = found;
    if (error.length() > 0) {
      doc["error"] = error;
      return serializeJsonResponse(doc);
    }

    doc["primaryAddress"] = primaryAddress;
    doc["secondaryAddress"] = secondaryAddress;
    doc["manufacturer"] = manufacturer;
    doc["type"] = type;
    doc["id"] = id;
    doc["version"] = version;
    return serializeJsonResponse(doc);
  }

  static void addDeviceSummaryJson(JsonArray devicesArray,
                                   int primaryAddress,
                                   const String& secondaryAddress,
                                   const String& manufacturer,
                                   const String& type) {
    JsonObject item = devicesArray.add<JsonObject>();
    item["primaryAddress"] = primaryAddress;
    item["secondaryAddress"] = secondaryAddress;
    item["manufacturer"] = manufacturer;
    item["type"] = type;
  }

  static void addSnapshotValueJson(JsonArray valuesArray,
                                   const String& field,
                                   const String& value,
                                   const String& unit) {
    JsonObject item = valuesArray.add<JsonObject>();
    item["field"] = field;
    item["value"] = value;
    item["unit"] = unit;
  }

  static JsonObject addSnapshotSummaryJson(JsonArray snapshotsArray,
                                           int primaryAddress,
                                           const String& secondaryAddress,
                                           unsigned long updatedAtMs,
                                           const String& updatedAtLabel,
                                           const String& id,
                                           const String& manufacturer,
                                           const String& type) {
    JsonObject item = snapshotsArray.add<JsonObject>();
    item["primaryAddress"] = primaryAddress;
    item["secondaryAddress"] = secondaryAddress;
    item["updatedAtMs"] = updatedAtMs;
    item["updatedAtLabel"] = updatedAtLabel;
    item["id"] = id;
    item["manufacturer"] = manufacturer;
    item["type"] = type;
    return item;
  }

  static void sendHtmlWithUrlReplace(const String& html, const char* path, int statusCode = 200) {
    if (path == nullptr || path[0] == '\0') {
      sendHtml(html, statusCode);
      return;
    }

    String patched = html;
    String script = String("<script>if(window.history&&window.history.replaceState){window.history.replaceState({},'',\"") + path + "\");}</script>";
    int bodyIndex = patched.indexOf("</body>");
    if (bodyIndex >= 0) {
      patched = patched.substring(0, bodyIndex) + script + patched.substring(bodyIndex);
    } else {
      patched += script;
    }
    sendHtml(patched, statusCode);
  }

  static String statusLabel(bool ok) {
    return ok ? "Connected" : "Disconnected";
  }

  static String networkFieldValue(const String& storedValue, const IPAddress& runtimeValue) {
    if (storedValue.length() > 0) {
      return storedValue;
    }
    if (runtimeValue == IPAddress(0, 0, 0, 0)) {
      return "";
    }
    return runtimeValue.toString();
  }

  static String displayText(const String& value) {
    String text = value;
    text.trim();
    return text.length() > 0 ? text : String("-");
  }

  static String cacheAgeLabel(unsigned long updatedAtMs) {
    if (updatedAtMs == 0) {
      return "-";
    }

    unsigned long ageSeconds = (millis() - updatedAtMs) / 1000UL;
    if (ageSeconds < 60UL) {
      return String(ageSeconds) + " s ago";
    }

    unsigned long ageMinutes = ageSeconds / 60UL;
    if (ageMinutes < 60UL) {
      return String(ageMinutes) + " min ago";
    }

    unsigned long ageHours = ageMinutes / 60UL;
    return String(ageHours) + " h ago";
  }

  static String durationMinutesLabel(uint16_t minutes) {
    if (minutes == 0) {
      return "Disabled";
    }
    return String(minutes) + " min";
  }

  static String relativeDueLabel(unsigned long referenceMs, uint16_t intervalMinutes) {
    if (intervalMinutes == 0 || referenceMs == 0) {
      return "-";
    }

    unsigned long intervalMs = static_cast<unsigned long>(intervalMinutes) * 60000UL;
    unsigned long now = millis();
    if (now >= referenceMs + intervalMs) {
      return "due now";
    }
    unsigned long remainingSeconds = ((referenceMs + intervalMs) - now) / 1000UL;
    if (remainingSeconds < 60UL) {
      return String(remainingSeconds) + " s";
    }
    return String(remainingSeconds / 60UL) + " min";
  }

  static String scanProgressLabel(const MbusService::ScanStatus& scanStatus) {
    if (scanStatus.totalSteps > 0) {
      return String(scanStatus.completedSteps) + "/" + String(scanStatus.totalSteps);
    }
    return String(scanStatus.completedSteps);
  }

  static bool copyMbusSnapshot(int primaryAddress, const String& secondaryAddress, CachedMbusSnapshot& outSnapshot) {
    return MbusService::copySnapshot(primaryAddress, secondaryAddress, outSnapshot);
  }

  static String buildMbusSnapshotStatusJson() {
    AppSettings::Settings& settings = AppSettings::get();
    SnapshotRefreshStatus refreshStatus = MbusService::snapshotRefreshStatus();
    JsonDocument doc;
    doc["active"] = refreshStatus.active;
    doc["lastSuccess"] = refreshStatus.lastSuccess;
    doc["processedDevices"] = refreshStatus.processedDevices;
    doc["totalDevices"] = refreshStatus.totalDevices;
    doc["updatedDevices"] = refreshStatus.updatedDevices;
    doc["refreshState"] = refreshStatus.active ? "Running" : "Idle";
    doc["intervalLabel"] = durationMinutesLabel(settings.mbusSnapshotRefreshMinutes);
    doc["progressLabel"] = String(refreshStatus.processedDevices) + "/" + String(refreshStatus.totalDevices);
    doc["lastRefreshLabel"] = refreshStatus.finishedAtMs > 0 ? cacheAgeLabel(refreshStatus.finishedAtMs) : String("-");
    doc["nextRefreshLabel"] = refreshStatus.active ? String("running now") : MbusService::nextAutoRefreshLabel();
    doc["note"] = refreshStatus.error;
    return serializeJsonResponse(doc);
  }

  static String buildTimeSyncStatusJson() {
    String readable = TimeSync::currentTimestampReadable();
    String localReadable = TimeSync::currentLocalTimestampReadable();
    String timezone = TimeSync::formatTimezoneOffset(AppSettings::get().timezoneOffsetMinutes);
    JsonDocument doc;
    doc["synchronized"] = TimeSync::isSynchronized();
    doc["timestamp"] = static_cast<unsigned long>(TimeSync::currentTimestamp());
    doc["timestampReadable"] = readable;
    doc["localTimestampReadable"] = localReadable;
    doc["localTimezone"] = timezone;
    doc["label"] = readable.length() > 0 ? readable + String(" UTC") : String("not synchronized");
    doc["localLabel"] = localReadable.length() > 0 ? localReadable + String(" ") + timezone : String("not synchronized");
    return serializeJsonResponse(doc);
  }

  static String buildMbusSnapshotsJson() {
    std::vector<CachedMbusSnapshot> snapshots = MbusService::snapshots();
    JsonDocument doc;
    JsonArray snapshotsArray = doc["snapshots"].to<JsonArray>();
    for (size_t index = 0; index < snapshots.size(); ++index) {
      const CachedMbusSnapshot& snapshot = snapshots[index];
      JsonObject item = addSnapshotSummaryJson(snapshotsArray,
                                              snapshot.primaryAddress,
                                              snapshot.secondaryAddress,
                                              snapshot.updatedAtMs,
                                              cacheAgeLabel(snapshot.updatedAtMs),
                                              snapshot.id,
                                              snapshot.manufacturer,
                                              snapshot.type);

      JsonArray values = item["values"].to<JsonArray>();
      for (size_t valueIndex = 0; valueIndex < snapshot.values.size(); ++valueIndex) {
        addSnapshotValueJson(values,
                             snapshot.values[valueIndex].field,
                             snapshot.values[valueIndex].value,
                             snapshot.values[valueIndex].unit);
      }
    }
    return serializeJsonResponse(doc);
  }

  static String navigationHtml(const char* active) {
    String nav;
    nav.reserve(768);
    nav += "<div class='card'><div class='actions'>";
    nav += String("<a href='/'") + (strcmp(active, "main") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">Main</a>";
    nav += String("<a href='/mqtt'") + (strcmp(active, "mqtt") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">MQTT Settings</a>";
    nav += String("<a href='/network'") + (strcmp(active, "network") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">Network</a>";
    nav += String("<a href='/mbus'") + (strcmp(active, "mbus") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">M-Bus Devices</a>";
    nav += String("<a href='/mbus-gateway'") + (strcmp(active, "mbus-gateway") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">M-Bus Gateway</a>";
    nav += String("<a href='/time'") + (strcmp(active, "time") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">Time</a>";
    nav += String("<a href='/web-access'") + (strcmp(active, "web") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">Web Access</a>";
    nav += String("<a href='/firmware'") + (strcmp(active, "firmware") == 0 ? " style='font-weight:700;color:#0f766e'" : "") + ">Firmware Update</a>";
    nav += "</div></div>";
    return nav;
  }

  static String pageStart(const char* title, const char* active) {
    String page;
    page.reserve(16384);
    String utcTime = TimeSync::currentTimestampReadable();
    String localTime = TimeSync::currentLocalTimestampReadable();
    String localTimezone = TimeSync::formatTimezoneOffset(AppSettings::get().timezoneOffsetMinutes);
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    page += String("<title>") + title + "</title>";
    page += "<style>body{font-family:Verdana,sans-serif;background:#f2f4f7;color:#1f2937;margin:0;padding:24px;}";
    page += ".wrap{max-width:1120px;margin:0 auto;} .card{background:#fff;border-radius:12px;padding:20px;margin-bottom:16px;box-shadow:0 10px 30px rgba(0,0,0,.08);} h1,h2,h3{margin-top:0;} label{display:block;font-weight:600;margin:12px 0 6px;} input,textarea,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid #cbd5e1;border-radius:8px;} textarea{min-height:220px;font-family:Consolas,monospace;} button,.buttonLink{background:#0f766e;color:#fff;border:0;border-radius:8px;padding:10px 16px;cursor:pointer;text-decoration:none;display:inline-block;} .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px;} .note{padding:12px;border-radius:8px;margin-bottom:16px;} .ok{background:#dcfce7;color:#166534;} .err{background:#fee2e2;color:#991b1b;} .muted{color:#64748b;font-size:14px;} .actions{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap;margin-top:14px;} .switchRow{display:flex;align-items:center;gap:12px;font-weight:600;} .switch{position:relative;display:inline-block;width:52px;height:30px;flex:0 0 auto;} .switch input{opacity:0;width:0;height:0;} .slider{position:absolute;cursor:pointer;inset:0;background:#cbd5e1;border-radius:999px;transition:.2s;} .slider:before{content:'';position:absolute;height:22px;width:22px;left:4px;top:4px;background:#fff;border-radius:50%;transition:.2s;box-shadow:0 1px 4px rgba(0,0,0,.25);} .switch input:checked + .slider{background:#0f766e;} .switch input:checked + .slider:before{transform:translateX(22px);} .badge{display:inline-block;padding:2px 8px;border-radius:999px;background:#e2e8f0;color:#0f172a;font-size:12px;font-weight:700;text-transform:uppercase;} .tableWrap{overflow-x:auto;} table{width:100%;border-collapse:collapse;margin-top:12px;} th,td{padding:10px;border-bottom:1px solid #e2e8f0;text-align:left;vertical-align:top;} th{font-size:13px;text-transform:uppercase;color:#475569;} .snapshotTable{table-layout:fixed;} .snapshotTable th:nth-child(1),.snapshotTable td:nth-child(1){width:40%;} .snapshotTable th:nth-child(2),.snapshotTable td:nth-child(2){width:40%;} .snapshotTable th:nth-child(3),.snapshotTable td:nth-child(3){width:20%;} .mono{font-family:Consolas,monospace;font-size:13px;} .inlineForm{display:flex;gap:8px;align-items:center;flex-wrap:wrap;} .inlineForm input{min-width:180px;flex:1 1 220px;} .smallButton{padding:8px 12px;font-size:14px;} .sectionTitle{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap;} a{color:#0f766e;text-decoration:none;}</style></head><body><div class='wrap'>";
    page += "<div class='card'><div class='sectionTitle'>";
    page += "<div><h1>WT32 Admin</h1><p class='muted'>Serial: ";
    page += htmlEscape(AppSettings::deviceSerial());
    page += "</p></div>";
    page += "<div style='text-align:right'><div class='muted' style='font-weight:600'>UTC time</div><div id='wt32-admin-utc-time' class='mono' style='font-size:16px;margin-top:4px'>";
    page += htmlEscape(utcTime.length() > 0 ? utcTime + String(" UTC") : String("not synchronized"));
    page += "</div><div class='muted' style='font-weight:600;margin-top:8px'>Local time</div><div id='wt32-admin-local-time' class='mono' style='font-size:16px;margin-top:4px'>";
    page += htmlEscape(localTime.length() > 0 ? localTime + String(" ") + localTimezone : String("not synchronized"));
    page += "</div></div></div></div>";
    page += navigationHtml(active);
    return page;
  }

  static String renderPage(const String& notice = String(), bool isError = false) {
    String page = pageStart("WT32 Admin", "main");
    std::vector<AppSettings::SavedMbusDevice> savedDevices = AppSettings::mbusDevices();
    AppSettings::Settings& settings = AppSettings::get();
    SnapshotRefreshStatus refreshStatus = MbusService::snapshotRefreshStatus();

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><h2>Status</h2><div class='grid'>";
    page += "<div><strong>Ethernet link</strong><br>" + String(EthernetManager::linkUp() ? "UP" : "DOWN") + "</div>";
    page += "<div><strong>Network mode</strong><br>" + EthernetManager::modeLabel() + "</div>";
    page += "<div><strong>IP</strong><br>" + htmlEscape(EthernetManager::localIP().toString()) + "</div>";
    page += "<div><strong>MQTT</strong><br>" + statusLabel(MQTTClient::isConnected()) + "</div>";
    page += "<div><strong>Telemetry</strong><br>" + htmlEscape(String(MQTTClient::getTelemetryTopic())) + "</div>";
    page += "<div><strong>Config topic</strong><br>" + htmlEscape(String(MQTTClient::getConfigTopic())) + "</div>";
    page += "<div><strong>TLS CA source</strong><br>" + htmlEscape(String(AppSettings::mqttCaSourceLabel())) + "</div>";
    page += "<div><strong>M-Bus snapshot refresh</strong><br><span id='mbus-refresh-state'>" + htmlEscape(refreshStatus.active ? String("Running") : String("Idle")) + "</span></div>";
    page += "<div><strong>Auto refresh interval</strong><br><span id='mbus-refresh-interval'>" + htmlEscape(durationMinutesLabel(settings.mbusSnapshotRefreshMinutes)) + "</span></div>";
    page += "<div><strong>Refresh progress</strong><br><span id='mbus-refresh-progress'>" + htmlEscape(String(refreshStatus.processedDevices) + "/" + String(refreshStatus.totalDevices)) + "</span></div>";
    page += "<div><strong>Last refresh</strong><br><span id='mbus-refresh-last'>" + htmlEscape(refreshStatus.finishedAtMs > 0 ? cacheAgeLabel(refreshStatus.finishedAtMs) : String("-")) + "</span></div>";
    page += "<div><strong>Next auto refresh</strong><br><span id='mbus-refresh-next'>" + htmlEscape(refreshStatus.active ? String("running now") : MbusService::nextAutoRefreshLabel()) + "</span></div>";
    page += "</div></div>";

    page += "<div class='card'><div class='sectionTitle'><h2>M-Bus Snapshot</h2><span class='muted'>Cache refresh (manual or auto)</span></div>";
    page += "<div style='display:flex;gap:12px;align-items:center;flex-wrap:wrap;padding:6px 0'>";
    page += "<form method='post' action='/mbus-snapshot/refresh' style='margin:0;display:inline-block;'>";
    page += "<button id='mbus-refresh-now' class='smallButton' type='submit'";
    if (refreshStatus.active) { page += " disabled"; }
    page += ">Refresh Now</button></form>";
    page += "<form method='post' action='/save-mbus-snapshot-settings' autocomplete='off' style='margin:0;display:inline-block;align-items:center;display:flex;gap:8px;'>";
    page += "<label class='muted' style='margin:0 4px 0 8px;font-weight:600'>Auto (min)</label>";
    page += "<input name='mbus_snapshot_refresh_minutes' type='number' min='0' max='1440' value='" + String(settings.mbusSnapshotRefreshMinutes) + "' style='width:84px;padding:6px;border-radius:8px;border:1px solid #cbd5e1'>";
    page += "<button class='smallButton' type='submit'>Save</button></form>";
    page += "<div style='margin-left:auto' class='muted'>Next: <span id='mbus-refresh-inline-next'>" + htmlEscape(refreshStatus.active ? String("running now") : MbusService::nextAutoRefreshLabel()) + "</span></div>";
    page += "</div>";
    if (refreshStatus.error.length() > 0) {
      page += "<p id='mbus-refresh-note' class='muted' style='margin-top:6px'>Last refresh note: " + htmlEscape(refreshStatus.error) + "</p>";
    } else {
      page += "<p id='mbus-refresh-note' class='muted' style='display:none;margin-top:6px'></p>";
    }
    page += "</div>";

    page += "<div class='card'><div class='sectionTitle'><h2>Saved M-Bus Snapshot</h2><span class='muted'>Informational only, no live bus reads</span></div>";
    if (savedDevices.empty()) {
      page += "<p class='muted'>No saved M-Bus devices yet.</p>";
    } else {
      for (size_t index = 0; index < savedDevices.size(); ++index) {
        const AppSettings::SavedMbusDevice& device = savedDevices[index];
        std::vector<String> selectedFieldKeys = AppSettings::mbusSelectedFieldKeys(device.primaryAddress, device.secondaryAddress);
        CachedMbusSnapshot snapshot;
        bool hasSnapshot = copyMbusSnapshot(device.primaryAddress, device.secondaryAddress, snapshot);
        String idStr = String("mbsnap-") + String(device.primaryAddress) + "-" + device.secondaryAddress;
        page += "<div id='" + idStr + "' class='card' style='padding:16px;margin-top:12px'>";
        page += "<div class='sectionTitle'><div><h3 style='margin:0'>" + htmlEscape(displayText(device.description)) + "</h3>";
        page += "<div class='muted' style='margin-top:4px'>";
        page += "ID: <span id='" + String("mbsnap-id-") + idStr + "'>" + htmlEscape(displayText(hasSnapshot ? snapshot.id : String())) + "</span>";
        page += " | Type: <span id='" + String("mbsnap-type-") + idStr + "'>" + htmlEscape(displayText(hasSnapshot && snapshot.type.length() > 0 ? snapshot.type : device.type)) + "</span>";
        page += " | Manufacturer: <span id='" + String("mbsnap-manufacturer-") + idStr + "'>" + htmlEscape(displayText(hasSnapshot && snapshot.manufacturer.length() > 0 ? snapshot.manufacturer : device.manufacturer)) + "</span>";
        page += "</div></div><span id='" + String("mbsnap-age-") + idStr + "' class='badge'>" + htmlEscape(hasSnapshot ? cacheAgeLabel(snapshot.updatedAtMs) : String("No cache")) + "</span></div>";
        bool showValuesTable = (hasSnapshot && !snapshot.values.empty()) || !selectedFieldKeys.empty();
        if (showValuesTable) {
          page += "<div id='" + String("mbsnap-tablewrap-") + idStr + "' class='tableWrap'><table class='snapshotTable'><thead><tr><th>Field</th><th>Value</th><th>Unit</th></tr></thead><tbody>";
          size_t rowCount = hasSnapshot && !snapshot.values.empty() ? snapshot.values.size() : selectedFieldKeys.size();
          for (size_t valueIndex = 0; valueIndex < rowCount; ++valueIndex) {
            String fieldLabel = hasSnapshot && valueIndex < snapshot.values.size()
              ? displayText(snapshot.values[valueIndex].field)
              : displayText(selectedFieldKeys[valueIndex]);
            String valueText = hasSnapshot && valueIndex < snapshot.values.size() ? displayText(snapshot.values[valueIndex].value) : String("-");
            String unitText = hasSnapshot && valueIndex < snapshot.values.size() ? displayText(snapshot.values[valueIndex].unit) : String("-");
            page += "<tr>";
            page += "<td id='" + String("mbsnap-") + idStr + "-field-" + String(valueIndex) + "'>" + htmlEscape(fieldLabel) + "</td>";
            page += "<td id='" + String("mbsnap-") + idStr + "-value-" + String(valueIndex) + "'>" + htmlEscape(valueText) + "</td>";
            page += "<td id='" + String("mbsnap-") + idStr + "-unit-" + String(valueIndex) + "'>" + htmlEscape(unitText) + "</td>";
            page += "</tr>";
          }
          page += "</tbody></table></div>";
          page += "<p id='" + String("mbsnap-empty-") + idStr + "' class='muted'" + String(hasSnapshot && !snapshot.values.empty() ? " style='display:none'" : "") + ">Waiting for first cached values.</p>";
        } else {
          page += "<p id='" + String("mbsnap-empty-") + idStr + "' class='muted'>No cached selected values yet. Open the device page to read a telegram and cache the selected fields.</p>";
        }
        page += "</div>";
      }
    }
    page += "</div>";

    page += R"JS(<script>(function(){
      var wasActive = )JS" + String(refreshStatus.active ? "true" : "false") + R"JS(;

      function pollUtcTime(){
        fetch('/time-sync/status',{cache:'no-store',credentials:'same-origin'})
          .then(function(response){ if(!response.ok){ throw new Error('time'); } return response.json(); })
          .then(function(data){
            var utcEl = document.getElementById('wt32-admin-utc-time');
            if(utcEl){ utcEl.textContent = data.label || 'not synchronized'; }
            var localEl = document.getElementById('wt32-admin-local-time');
            if(localEl){ localEl.textContent = data.localLabel || 'not synchronized'; }
            window.setTimeout(pollUtcTime, 1000);
          })
          .catch(function(){ window.setTimeout(pollUtcTime, 3000); });
      }

      function updateSnapshotsData(obj){
        if(!obj || !obj.snapshots) return;
        obj.snapshots.forEach(function(s){
          var idStr = 'mbsnap-' + s.primaryAddress + '-' + s.secondaryAddress;
          var ageEl = document.getElementById('mbsnap-age-' + idStr);
          if(ageEl && s.updatedAtLabel) ageEl.textContent = s.updatedAtLabel;
          var idEl = document.getElementById('mbsnap-id-' + idStr);
          var typeEl = document.getElementById('mbsnap-type-' + idStr);
          var manufacturerEl = document.getElementById('mbsnap-manufacturer-' + idStr);
          if(idEl) idEl.textContent = s.id || '-';
          if(typeEl) typeEl.textContent = s.type || '-';
          if(manufacturerEl) manufacturerEl.textContent = s.manufacturer || '-';
          var emptyEl = document.getElementById('mbsnap-empty-' + idStr);
          if(emptyEl && s.values && s.values.length){ emptyEl.style.display = 'none'; }
          if(s.values && s.values.length){
            for(var i=0;i<s.values.length;i++){
              var fieldEl = document.getElementById('mbsnap-' + idStr + '-field-' + i);
              var valEl = document.getElementById('mbsnap-' + idStr + '-value-' + i);
              var unitEl = document.getElementById('mbsnap-' + idStr + '-unit-' + i);
              if(fieldEl) fieldEl.textContent = s.values[i].field || '';
              if(valEl) valEl.textContent = s.values[i].value || '';
              if(unitEl) unitEl.textContent = s.values[i].unit || '';
            }
          }
        });
      }

      function pollSnapshotStatus(){
        fetch('/mbus-snapshot/status',{cache:'no-store',credentials:'same-origin'})
          .then(function(response){ if(!response.ok){ throw new Error('status'); } return response.json(); })
          .then(function(data){
            var refreshState=document.getElementById('mbus-refresh-state'); if(refreshState){ refreshState.textContent=data.refreshState||''; }
            var refreshInterval=document.getElementById('mbus-refresh-interval'); if(refreshInterval){ refreshInterval.textContent=data.intervalLabel||''; }
            var refreshProgress=document.getElementById('mbus-refresh-progress'); if(refreshProgress){ refreshProgress.textContent=data.progressLabel||''; }
            var refreshLast=document.getElementById('mbus-refresh-last'); if(refreshLast){ refreshLast.textContent=data.lastRefreshLabel||''; }
            var refreshNext=document.getElementById('mbus-refresh-next'); if(refreshNext){ refreshNext.textContent=data.nextRefreshLabel||''; }
            var refreshInlineNext=document.getElementById('mbus-refresh-inline-next'); if(refreshInlineNext){ refreshInlineNext.textContent=data.nextRefreshLabel||''; }
            var refreshNow=document.getElementById('mbus-refresh-now');
            if(refreshNow){
              refreshNow.disabled = !!data.active;
              refreshNow.textContent = data.active ? 'Refreshing...' : 'Refresh Now';
            }
            var refreshNote=document.getElementById('mbus-refresh-note');
            if(refreshNote){
              if(data.note){ refreshNote.style.display='block'; refreshNote.textContent='Last refresh note: '+data.note; }
              else { refreshNote.style.display='none'; refreshNote.textContent=''; }
            }

            // fetch snapshot values and update only the values/age badges in-place
            fetch('/mbus-snapshots/data',{cache:'no-store',credentials:'same-origin'})
              .then(function(r){ if(!r.ok){ throw new Error('snap'); } return r.json(); })
              .then(function(snap){ updateSnapshotsData(snap); })
              .catch(function(){ /* ignore snapshot fetch errors */ });

            wasActive = !!data.active;
            window.setTimeout(pollSnapshotStatus, data.active ? 1500 : 5000);
          })
          .catch(function(){ window.setTimeout(pollSnapshotStatus, 5000); });
      }

      pollSnapshotStatus();
      pollUtcTime();
    })();</script>)JS";
    page += "</div></body></html>";
    return page;
  }

  static String renderMqttPage(const String& notice = String(), bool isError = false) {
    AppSettings::Settings& settings = AppSettings::get();
    String page = pageStart("WT32 MQTT Settings", "mqtt");

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><div class='sectionTitle'><h2>MQTT Settings</h2><div style='text-align:right'><div class='muted' style='font-weight:600'>Client ID</div><div class='mono' style='font-size:14px'>" + htmlEscape(AppSettings::deviceSerial()) + "</div><div class='muted' style='font-size:12px;margin-top:4px'>username defaults to serial if empty</div></div></div><form method='post' action='/save-mqtt' enctype='multipart/form-data' autocomplete='off'>";
    page += "<div class='note ok'><strong>Active TLS CA source:</strong> ";
    page += "<span class='badge'>" + htmlEscape(String(AppSettings::mqttCaSourceLabel())) + "</span>";
    page += "</div>";
    page += "<label>Broker host</label><input name='mqtt_host' autocomplete='off' value='" + htmlEscape(settings.mqttHost) + "'>";
    page += "<label>Broker port</label><input name='mqtt_port' type='number' min='1' max='65535' autocomplete='off' value='" + String(settings.mqttPort) + "'>";
    page += "<label>Broker password</label><input name='mqtt_password' type='password' autocomplete='new-password' value='' placeholder='Leave empty to keep the current password'>";
    page += "<label>MQTT username</label><input name='mqtt_username' autocomplete='off' placeholder='Leave empty to use device serial' value='" + htmlEscape(settings.mqttUsername) + "'>";
    page += "<label>Keepalive (seconds)</label><input name='mqtt_keepalive' type='number' min='5' max='3600' autocomplete='off' value='" + String(settings.mqttKeepAliveSeconds) + "'>";
    page += "<label>Reconnect interval (ms)</label><input name='mqtt_reconnect' type='number' min='1000' max='600000' autocomplete='off' value='" + String(settings.mqttReconnectIntervalMs) + "'>";
    page += "<label>CA certificate</label><textarea name='mqtt_ca' autocomplete='off' spellcheck='false' placeholder='Leave empty to keep the current CA certificate'></textarea>";
    page += "<div class='switchRow' style='margin-top:8px'><span>Use custom CA</span><label class='switch'><input name='mqtt_use_custom_ca' type='checkbox' value='1'";
    if (settings.mqttUseCustomCa) {
      page += " checked";
    }
    page += "><span class='slider'></span></label><span class='muted'>built-in / custom</span></div>";
    page += "<div class='card' style='padding:16px;margin-top:16px'><h3>Topic Settings</h3>";
    page += "<p class='muted'>Format strings use %s as placeholder for the device serial number. Example: <code>telemetry/v2/"
 + htmlEscape(AppSettings::deviceSerial()) + "</code> when format is <code>telemetry/v2/%s</code>.</p>";
    page += "<label>Telemetry publish topic</label><input name='mqtt_topic_telemetry' autocomplete='off' placeholder='telemetry/v2/%s' value='" + htmlEscape(settings.mqttTopicTelemetry) + "'>";
    page += "<label>Telemetry log publish topic</label><input name='mqtt_topic_telemetry_log' autocomplete='off' placeholder='telemetry/v2/%s/log' value='" + htmlEscape(settings.mqttTopicTelemetryLog) + "'>";
    page += "<label>Config subscribe topic</label><input name='mqtt_topic_config' autocomplete='off' placeholder='config/%s' value='" + htmlEscape(settings.mqttTopicConfig) + "'>";
    page += "</div>";
    page += "<p class='muted'>Broker password and CA certificate are never rendered back into this page. Leave them empty to keep the stored values unchanged.</p>";
    page += "<p class='muted'>If the switch below is disabled, changes in the certificate field do not affect MQTT.</p>";
    page += "<div class='actions'>";
    page += "<button type='submit'>Save MQTT Settings</button>";
    page += "</div></form></div>";
    page += "</div></body></html>";
    return page;
  }

  static String renderTimePage(const String& notice = String(), bool isError = false) {
    AppSettings::Settings& settings = AppSettings::get();
    String page = pageStart("WT32 Time Settings", "time");
    String utcReadable = TimeSync::currentTimestampReadable();
    String localReadable = TimeSync::currentLocalTimestampReadable();
    String timezoneLabel = TimeSync::formatTimezoneOffset(settings.timezoneOffsetMinutes);

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><h2>Time Settings</h2>";
    page += "<div class='grid'>";
    page += "<div><strong>UTC time</strong><br><span class='mono'>" + htmlEscape(utcReadable.length() > 0 ? utcReadable + String(" UTC") : String("not synchronized")) + "</span></div>";
    page += "<div><strong>Local time</strong><br><span class='mono'>" + htmlEscape(localReadable.length() > 0 ? localReadable + String(" ") + timezoneLabel : String("not synchronized")) + "</span></div>";
    page += "<div><strong>Telemetry readable time</strong><br>" + htmlEscape(settings.telemetryUseLocalTime ? String("Local time") : String("UTC")) + "</div>";
    page += "</div>";
    page += "<form method='post' action='/save-time' autocomplete='off'>";
    page += "<label>Timezone offset</label><select name='timezone_offset_minutes'>";
    const int offsets[] = {-720, -600, -480, -300, 0, 60, 120, 180, 240, 300, 330, 480, 540, 600, 720, 840};
    bool offsetInList = false;
    for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
      page += "<option value='" + String(offsets[index]) + "'";
      if (settings.timezoneOffsetMinutes == offsets[index]) {
        offsetInList = true;
        page += " selected";
      }
      page += ">" + htmlEscape(TimeSync::formatTimezoneOffset(static_cast<int16_t>(offsets[index]))) + "</option>";
    }
    if (!offsetInList) {
      page += "<option value='" + String(settings.timezoneOffsetMinutes) + "' selected>" +
              htmlEscape(TimeSync::formatTimezoneOffset(settings.timezoneOffsetMinutes)) + "</option>";
    }
    page += "</select>";
    page += "<label>Custom offset (minutes)</label><input name='timezone_offset_custom' type='number' min='-720' max='840' step='1' value='' placeholder='Optional, e.g. 180 for UTC+03:00'>";
    page += "<div class='actions'>";
    page += "<div class='switchRow'><span>Telemetry readable time</span><label class='switch'><input name='telemetry_use_local_time' type='checkbox' value='1'";
    if (settings.telemetryUseLocalTime) {
      page += " checked";
    }
    page += "><span class='slider'></span></label><span class='muted'>UTC / local</span></div>";
    page += "<button type='submit'>Save Time Settings</button>";
    page += "</div></form>";
    page += "<p class='muted'>The numeric timestamp is always Unix UTC epoch. This setting changes timestampReadable and adds timestampTimezone.</p>";
    page += "</div></body></html>";
    return page;
  }

  static String renderNetworkPage(const String& notice = String(), bool isError = false) {
    AppSettings::Settings& settings = AppSettings::get();
    String page = pageStart("WT32 Network", "network");
    String displayIp = networkFieldValue(settings.networkIp, EthernetManager::localIP());
    String displayGateway = networkFieldValue(settings.networkGateway, EthernetManager::gatewayIP());
    String displaySubnet = networkFieldValue(settings.networkSubnet, EthernetManager::subnetMask());
    String displayDns1 = networkFieldValue(settings.networkDns1, EthernetManager::dnsIP(0));
    String displayDns2 = networkFieldValue(settings.networkDns2, EthernetManager::dnsIP(1));

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><h2>Network Settings</h2>";
    page += "<p class='muted'>Default and post-reset mode is DHCP. Static mode requires IP address, gateway and subnet mask.</p>";
    page += "<form method='post' action='/save-network' enctype='multipart/form-data' autocomplete='off'>";
    page += "<label>Mode</label><select name='network_mode'>";
    page += String("<option value='dhcp'") + (settings.networkUseDhcp ? " selected" : "") + ">DHCP</option>";
    page += String("<option value='static'") + (!settings.networkUseDhcp ? " selected" : "") + ">Static</option>";
    page += "</select>";
    page += "<label>Static IP address</label><input name='network_ip' autocomplete='off' placeholder='192.168.8.26' value='" + htmlEscape(displayIp) + "'>";
    page += "<label>Gateway</label><input name='network_gateway' autocomplete='off' placeholder='192.168.8.1' value='" + htmlEscape(displayGateway) + "'>";
    page += "<label>Subnet mask</label><input name='network_subnet' autocomplete='off' placeholder='255.255.255.0' value='" + htmlEscape(displaySubnet) + "'>";
    page += "<label>Primary DNS</label><input name='network_dns1' autocomplete='off' placeholder='8.8.8.8' value='" + htmlEscape(displayDns1) + "'>";
    page += "<label>Secondary DNS</label><input name='network_dns2' autocomplete='off' placeholder='1.1.1.1' value='" + htmlEscape(displayDns2) + "'>";
    page += "<div class='actions'><button type='submit'>Save Network Settings</button><a class='buttonLink' href='/'>Back</a></div>";
    page += "</form></div>";

    page += "<div class='card'><h2>Current Runtime</h2><div class='grid'>";
    page += "<div><strong>Mode</strong><br>" + EthernetManager::modeLabel() + "</div>";
    page += "<div><strong>IP</strong><br>" + htmlEscape(EthernetManager::localIP().toString()) + "</div>";
    page += "<div><strong>Gateway</strong><br>" + htmlEscape(EthernetManager::gatewayIP().toString()) + "</div>";
    page += "<div><strong>Subnet</strong><br>" + htmlEscape(EthernetManager::subnetMask().toString()) + "</div>";
    page += "<div><strong>DNS 1</strong><br>" + htmlEscape(EthernetManager::dnsIP(0).toString()) + "</div>";
    page += "<div><strong>DNS 2</strong><br>" + htmlEscape(EthernetManager::dnsIP(1).toString()) + "</div>";
    page += "</div></div>";
    page += "</div></body></html>";
    return page;
  }

  static String renderWebAccessPage(const String& notice = String(), bool isError = false) {
    AppSettings::Settings& settings = AppSettings::get();
    String page = pageStart("WT32 Web Access", "web");

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><h2>Web Access</h2><form method='post' action='/save-auth' enctype='multipart/form-data' autocomplete='off'>";
    page += "<label>Web username</label><input name='web_username' autocomplete='off' value='" + htmlEscape(settings.webUsername) + "'>";
    page += "<label>New password</label><input name='web_password' type='password' value=''>";
    page += "<label>Confirm password</label><input name='web_password_confirm' type='password' value=''>";
    page += "<p class='muted'>Leave password fields empty to keep the current password unchanged.</p>";
    page += "<button type='submit'>Save Web Access</button></form></div>";
    page += "</div></body></html>";
    return page;
  }

  static String renderFirmwarePage(const String& notice = String(), bool isError = false) {
    String page = pageStart("WT32 Firmware Update", "firmware");

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><h2>Firmware Update</h2><form method='post' action='/update' enctype='multipart/form-data'>";
    page += "<label>Select firmware .bin</label><input name='firmware' type='file' accept='.bin'>";
    page += "<button type='submit'>Upload Firmware</button></form>";
    page += "<p class='muted'>NVS settings and the stored device serial number persist across OTA updates and normal reboots.</p></div>";
    page += "</div></body></html>";
    return page;
  }

  static String renderMbusGatewayPage(const String& notice = String(), bool isError = false) {
    AppSettings::Settings& settings = AppSettings::get();
    String page = pageStart("WT32 M-Bus Gateway", "mbus-gateway");

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    }

    page += "<div class='card'><h2>M-Bus TCP/IP Gateway</h2>";
    page += "<p class='muted'>Configure the external TCP/IP to M-Bus gateway used for scanning and reading bus devices.</p>";
    page += "<form method='post' action='/save-mbus-gateway' autocomplete='off'>";
    page += "<label>Gateway IP or host</label><input name='mbus_host' autocomplete='off' placeholder='192.168.8.41' value='" + htmlEscape(settings.mbusGatewayHost) + "'>";
    page += "<label>Gateway TCP port</label><input name='mbus_port' type='number' min='1' max='65535' autocomplete='off' value='" + String(settings.mbusGatewayPort) + "'>";
    page += "<div class='actions'><button type='submit'>Save M-Bus Gateway</button><a class='buttonLink' href='/mbus'>Open M-Bus Devices</a></div>";
    page += "</form></div>";
    page += "</div></body></html>";
    return page;
  }

  static bool findSavedMbusDevice(int primaryAddress,
                                  const String& secondaryAddress,
                                  AppSettings::SavedMbusDevice& foundDevice) {
    std::vector<AppSettings::SavedMbusDevice> devices = AppSettings::mbusDevices();

    for (size_t index = 0; index < devices.size(); ++index) {
      if (AppSettings::mbusIdentityMatches(devices[index].primaryAddress,
                                           devices[index].secondaryAddress,
                                           primaryAddress,
                                           secondaryAddress)) {
        foundDevice = devices[index];
        return true;
      }
    }
    return false;
  }

  static bool containsString(const std::vector<String>& values, const String& needle) {
    for (size_t index = 0; index < values.size(); ++index) {
      if (values[index] == needle) {
        return true;
      }
    }
    return false;
  }

  static String renderMbusDevicePage(const AppSettings::SavedMbusDevice& savedDevice,
                                     const MBusGateway::TelegramResult& telegramResult,
                                     const std::vector<String>& selectedFieldKeys,
                                     const String& notice = String(),
                                     bool isError = false) {
    String page = pageStart("WT32 M-Bus Device", "mbus");
    std::vector<MBusGateway::TelegramRecord> selectableRecords = MbusService::selectableRecords(savedDevice, telegramResult);

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    } else if (!telegramResult.success && telegramResult.error.length() > 0) {
      page += "<div class='note err'>" + htmlEscape(telegramResult.error) + "</div>";
    }

    page += "<div class='card'><div class='sectionTitle'><h2>Device Header</h2><span class='badge'>Stored in NVS</span></div>";
    page += "<div class='grid'>";
    page += "<div><strong>Description</strong><br>" + htmlEscape(savedDevice.description.length() > 0 ? savedDevice.description : String("-")) + "</div>";
    page += "<div><strong>Primary</strong><br><span class='mono'>" + htmlEscape(savedDevice.primaryAddress >= 0 ? String(savedDevice.primaryAddress) : String("-")) + "</span></div>";
    page += "<div><strong>Secondary</strong><br><span class='mono'>" + htmlEscape(savedDevice.secondaryAddress) + "</span></div>";
    page += "<div><strong>Manufacturer</strong><br>" + htmlEscape(displayText(savedDevice.manufacturer)) + "</div>";
    page += "<div><strong>Type</strong><br>" + htmlEscape(displayText(savedDevice.type)) + "</div>";
    page += "</div>";
    page += "<div class='actions'>";
    page += "<a class='buttonLink' href='/mbus'>Back to M-Bus Devices</a>";
    page += "<a class='buttonLink' href='/mbus-device?primary_address=" + String(savedDevice.primaryAddress) + "&secondary_address=" + htmlEscape(savedDevice.secondaryAddress) + "'>Refresh Telegram</a>";
    page += "</div></div>";

    page += "<div class='card'><div class='sectionTitle'><h2>Raw Telegram</h2><span class='muted'>Hex dump</span></div>";
    if (telegramResult.rawFrame.length() > 0) {
      page += "<div class='mono' style='word-break:break-word;line-height:1.7'>" + htmlEscape(telegramResult.rawFrame) + "</div>";
    } else {
      page += "<p class='muted'>No raw telegram available.</p>";
    }
    page += "</div>";

    page += "<div class='card'><div class='sectionTitle'><h2>Parsed Values</h2><span class='muted'>These fields can be used later for JSON mapping</span></div>";
    if (selectableRecords.empty()) {
      page += "<p class='muted'>No selectable telegram values are available yet.</p>";
    } else {
      page += "<form method='post' action='/mbus-device/save-selection' autocomplete='off'>";
      page += "<input type='hidden' name='primary_address' value='" + String(savedDevice.primaryAddress) + "'>";
      page += "<input type='hidden' name='secondary_address' value='" + htmlEscape(savedDevice.secondaryAddress) + "'>";
      page += "<div class='actions'><button type='submit'>Save Selected Fields</button><span class='muted'>Choose which values should later go into MQTT JSON.</span></div>";
      page += "<div class='tableWrap'><table><thead><tr><th>Use</th><th>#</th><th>Field</th><th>Function</th><th>Storage</th><th>Value</th><th>Unit</th><th>VIF</th><th>Raw</th></tr></thead><tbody>";
      for (size_t index = 0; index < selectableRecords.size(); ++index) {
        const MBusGateway::TelegramRecord& record = selectableRecords[index];
        page += "<tr>";
        page += "<td><input type='checkbox' name='field_" + String(index) + "' value='" + htmlEscape(record.key) + "'" + (containsString(selectedFieldKeys, record.key) ? String(" checked") : String("")) + "></td>";
        page += "<td class='mono'>" + String(record.index) + "</td>";
        page += "<td>" + htmlEscape(record.quantity.length() > 0 ? record.quantity : String("Unknown")) + (record.recognized ? "" : " <span class='muted'>(raw)</span>") + "</td>";
        page += "<td>" + htmlEscape(record.function) + "</td>";
        page += "<td class='mono'>" + String(record.storage) + "</td>";
        page += "<td>" + htmlEscape(record.value) + "</td>";
        page += "<td>" + htmlEscape(record.unit) + "</td>";
        page += "<td class='mono'>" + htmlEscape(record.vif) + "</td>";
        page += "<td class='mono'>" + htmlEscape(record.raw) + "</td>";
        page += "</tr>";
      }
      page += "</tbody></table></div>";
      page += "<div class='actions'><button type='submit'>Save Selected Fields</button></div></form>";
    }
    page += "</div>";

    page += "</div></body></html>";
    return page;
  }

  static String renderMbusPage(const String& notice = String(), bool isError = false) {
    AppSettings::Settings& settings = AppSettings::get();
    std::vector<AppSettings::SavedMbusDevice> savedDevices = AppSettings::mbusDevices();
    MbusService::ScanStatus scanStatus = MbusService::scanStatus();
    std::vector<MBusGateway::DeviceInfo> displayResults = MbusService::scanDevices();
    String page = pageStart("WT32 M-Bus Devices", "mbus");

    if (notice.length() > 0) {
      page += String("<div class='note ") + (isError ? "err" : "ok") + "'>" + htmlEscape(notice) + "</div>";
    } else if (scanStatus.finished && scanStatus.error.length() > 0) {
      page += String("<div class='note ") + (scanStatus.success ? "ok" : "err") + "'>" + htmlEscape(scanStatus.error) + "</div>";
    } else if (scanStatus.finished && scanStatus.success) {
      page += "<div class='note ok'>Scan finished successfully.</div>";
    }

    page += "<div class='card'><div class='sectionTitle'><h2>M-Bus Scanner</h2><span class='badge'>Gateway ";
    page += htmlEscape(settings.mbusGatewayHost + ":" + String(settings.mbusGatewayPort));
    page += "</span></div>";
    page += "<p class='muted'>Primary scanning polls each primary address in the selected range. Secondary scanning uses wildcard selection and then filters found devices by the optional hexadecimal range.</p>";
    page += "<div class='grid'>";
    page += "<div><h3>Primary Range</h3><form method='post' action='/scan-mbus' autocomplete='off'>";
    page += "<input type='hidden' name='scan_mode' value='primary'>";
    page += "<label>Start primary address</label><input name='primary_start' type='number' min='1' max='250' value='1'>";
    page += "<label>End primary address</label><input name='primary_end' type='number' min='1' max='250' value='250'>";
    if (scanStatus.active) {
      page += "<p class='muted'>A scan is already running.</p>";
    }
    page += "<button type='submit'>Scan Primary Range</button></form></div>";
    page += "<div><h3>Secondary Range</h3><form method='post' action='/scan-mbus' autocomplete='off'>";
    page += "<input type='hidden' name='scan_mode' value='secondary'>";
    page += "<label>Start secondary address</label><input name='secondary_start' maxlength='16' placeholder='0000000000000000'>";
    page += "<label>End secondary address</label><input name='secondary_end' maxlength='16' placeholder='FFFFFFFFFFFFFFFF'>";
    page += "<button type='submit'>Scan Secondary Range</button></form></div>";
    page += "</div></div>";

    // Manual device add form
    page += "<div class='card'><div class='sectionTitle'><h2>Add Device Manually</h2><span class='muted'>Enter primary or secondary address, optionally check on bus before saving</span></div>";
    page += "<form id='add-device-form' method='post' action='/mbus-devices/add' autocomplete='off'>";
    page += "<div class='grid' style='grid-template-columns:1fr 1fr 1fr 1fr;align-items:end'>";
    page += "<div><label>Primary address</label><input name='primary_address' type='number' min='1' max='250' placeholder='1-250'></div>";
    page += "<div><label>Secondary address</label><input name='secondary_address' maxlength='16' placeholder='0000000000000000'></div>";
    page += "<div><label>Description</label><input name='device_description' placeholder='e.g. Cold water'></div>";
    page += "<div style='display:flex;gap:8px;align-items:end;padding-bottom:6px'>";
    page += "<button class='smallButton' type='button' id='check-on-bus-btn' onclick='checkDeviceOnBus()'>Check on Bus</button>";
    page += "<button class='smallButton' type='submit'>Save Device</button></div>";
    page += "</div>";
    page += "<input type='hidden' name='manufacturer' id='add-manufacturer' value=''>";
    page += "<input type='hidden' name='type' id='add-type' value=''>";
    page += "<div id='check-result' style='margin-top:8px'></div>";
    page += "</form></div>";

    page += "<script>function checkDeviceOnBus(){";
    page += "var btn=document.getElementById('check-on-bus-btn');var result=document.getElementById('check-result');";
    page += "var primary=document.getElementsByName('primary_address')[0].value;";
    page += "var secondary=document.getElementsByName('secondary_address')[0].value;";
    page += "if(!primary&&!secondary){result.innerHTML='<div class=\\'note err\\'>Enter a primary or secondary address.</div>';return;}";
    page += "btn.disabled=true;btn.textContent='Checking...';";
    page += "result.innerHTML='<div class=\\'note\\' style=\\'background:#f0f4ff\\'>Checking bus, please wait...</div>';";
    page += "fetch('/mbus-devices/check-bus?primary_address='+encodeURIComponent(primary)+'&secondary_address='+encodeURIComponent(secondary),{cache:'no-store',credentials:'same-origin'})";
    page += ".then(function(r){if(!r.ok)throw new Error('check failed');return r.json();})";
    page += ".then(function(data){";
    page += "if(data.found){";
    page += "document.getElementsByName('primary_address')[0].value=data.primaryAddress;";
    page += "document.getElementsByName('secondary_address')[0].value=data.secondaryAddress;";
    page += "document.getElementById('add-manufacturer').value=data.manufacturer||'';";
    page += "document.getElementById('add-type').value=data.type||'';";
    page += "result.innerHTML='<div class=\\'note ok\\'>Device found on bus: primary='+data.primaryAddress+', secondary='+data.secondaryAddress+', manufacturer='+(data.manufacturer||'-')+', type='+(data.type||'-')+'. You can now save.</div>';";
    page += "}else{";
    page += "result.innerHTML='<div class=\\'note err\\'>Device not found on bus: '+(data.error||'no response')+'.</div>';";
    page += "}";
    page += "btn.disabled=false;btn.textContent='Check on Bus';";
    page += "}).catch(function(e){result.innerHTML='<div class=\\'note err\\'>Check failed: '+e.message+'</div>';btn.disabled=false;btn.textContent='Check on Bus';});";
    page += "}</script>";

    page += "<div class='card'><div class='sectionTitle'><h2>Scan Status</h2><span id='scan-badge' class='badge'>" + htmlEscape(scanStatus.active ? String("Running") : (scanStatus.finished ? String("Finished") : String("Idle"))) + "</span></div>";
    if (scanStatus.active || scanStatus.finished) {
      page += "<div class='grid'>";
      page += "<div><strong>Mode</strong><br><span id='scan-mode'>" + htmlEscape(scanStatus.mode) + "</span></div>";
      page += "<div><strong>Current</strong><br><span id='scan-current'>" + htmlEscape(scanStatus.progressLabel + String(" ") + String(scanStatus.currentValue)) + "</span></div>";
      page += "<div><strong>Progress</strong><br><span id='scan-progress'>" + htmlEscape(scanProgressLabel(scanStatus)) + "</span></div>";
      page += "<div><strong>Found</strong><br><span id='scan-found'>" + htmlEscape(String(displayResults.size())) + "</span> device(s)</div>";
      page += "</div>";
      if (scanStatus.active) {
        page += "<form method='post' action='/scan-mbus/stop'><div class='actions'><button type='submit'>Stop Scan</button><span id='scan-hint' class='muted'>Status updates automatically without page reload.</span></div></form>";
      } else {
        page += "<p id='scan-hint' class='muted'>Status is updated in place by JavaScript polling.</p>";
      }
    } else {
      page += "<p id='scan-hint' class='muted'>No active scan. Start a primary or secondary scan above.</p>";
    }
    page += "</div>";

    page += "<div class='card'><div class='sectionTitle'><h2>Last Scan Results</h2><span id='results-count' class='muted'>" + String(displayResults.size()) + " device(s)</span></div>";
    if (displayResults.empty()) {
      page += "<p id='results-empty' class='muted'>Run a scan to populate this list, then save the devices you want to keep after reboot.</p>";
      page += "<div id='results-table' class='tableWrap' style='display:none'><table><thead><tr><th>Primary</th><th>Secondary</th><th>Manufacturer</th><th>Type</th><th>Save</th></tr></thead><tbody id='results-body'></tbody></table></div>";
    } else {
      page += "<p id='results-empty' class='muted' style='display:none'>Run a scan to populate this list, then save the devices you want to keep after reboot.</p>";
      page += "<div id='results-table' class='tableWrap'><table><thead><tr><th>Primary</th><th>Secondary</th><th>Manufacturer</th><th>Type</th><th>Save</th></tr></thead><tbody id='results-body'>";
      for (size_t index = 0; index < displayResults.size(); ++index) {
        const MBusGateway::DeviceInfo& device = displayResults[index];
        page += "<tr>";
        page += "<td class='mono'>" + htmlEscape(device.primaryAddress >= 0 ? String(device.primaryAddress) : String("-")) + "</td>";
        page += "<td class='mono'>" + htmlEscape(device.secondaryAddress) + "</td>";
        page += "<td>" + htmlEscape(device.manufacturer) + "</td>";
        page += "<td>" + htmlEscape(device.type) + "</td>";
        page += "<td><form class='inlineForm' method='post' action='/mbus-devices/add' autocomplete='off'>";
        page += "<input type='hidden' name='primary_address' value='" + String(device.primaryAddress) + "'>";
        page += "<input type='hidden' name='secondary_address' value='" + htmlEscape(device.secondaryAddress) + "'>";
        page += "<input type='hidden' name='manufacturer' value='" + htmlEscape(device.manufacturer) + "'>";
        page += "<input type='hidden' name='type' value='" + htmlEscape(device.type) + "'>";
        page += "<input name='device_description' placeholder='Description' style='width:120px'>";
        page += "<button class='smallButton' type='submit'>Save</button></form></td>";
        page += "</tr>";
      }
      page += "</tbody></table></div>";
    }
    page += "</div>";

    page += "<div class='card'><div class='sectionTitle'><h2>Saved M-Bus Devices</h2><span class='muted'>Stored in NVS</span></div>";
    if (savedDevices.empty()) {
      page += "<p class='muted'>No devices saved yet.</p>";
    } else {
      page += "<div class='tableWrap'><table><thead><tr><th>Primary</th><th>Secondary</th><th>Manufacturer</th><th>Type</th><th>Description</th><th>Open</th><th>Delete</th></tr></thead><tbody>";
      for (size_t index = 0; index < savedDevices.size(); ++index) {
        const AppSettings::SavedMbusDevice& device = savedDevices[index];
        page += "<tr>";
        page += "<td class='mono'>" + htmlEscape(device.primaryAddress >= 0 ? String(device.primaryAddress) : String("-")) + "</td>";
        page += "<td class='mono'>" + htmlEscape(device.secondaryAddress) + "</td>";
        page += "<td>" + htmlEscape(displayText(device.manufacturer)) + "</td>";
        page += "<td>" + htmlEscape(displayText(device.type)) + "</td>";
        page += "<td>" + htmlEscape(device.description) + "</td>";
        page += "<td><a class='buttonLink smallButton' href='/mbus-device?primary_address=" + String(device.primaryAddress) + "&secondary_address=" + htmlEscape(device.secondaryAddress) + "'>Open</a></td>";
        page += "<td><form method='post' action='/mbus-devices/delete'>";
        page += "<input type='hidden' name='primary_address' value='" + String(device.primaryAddress) + "'>";
        page += "<input type='hidden' name='secondary_address' value='" + htmlEscape(device.secondaryAddress) + "'>";
        page += "<button class='smallButton' type='submit'>Delete</button></form></td>";
        page += "</tr>";
      }
      page += "</tbody></table></div>";
    }
    page += "</div>";

    page += R"JS(<script>(function(){
      function esc(value){
        var map={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;','\'':'&#39;'};
        return String(value==null?'':value).replace(/[&<>"']/g,function(ch){return map[ch]||ch;});
      }

      function renderRows(devices){
        var rows='';
        for(var index=0;index<devices.length;index+=1){
          var device=devices[index];
          var primary=device.primaryAddress>=0?device.primaryAddress:'-';
          rows += '<tr>'+
                  '<td class="mono">'+esc(primary)+'</td>'+
                  '<td class="mono">'+esc(device.secondaryAddress)+'</td>'+
                  '<td>'+esc(device.manufacturer)+'</td>'+
                  '<td>'+esc(device.type)+'</td>'+
                  '<td><form class="inlineForm" method="post" action="/mbus-devices/add" autocomplete="off">'+
                    '<input type="hidden" name="primary_address" value="'+esc(device.primaryAddress)+'">'+
                    '<input type="hidden" name="secondary_address" value="'+esc(device.secondaryAddress)+'">'+
                    '<input type="hidden" name="manufacturer" value="'+esc(device.manufacturer)+'">'+
                    '<input type="hidden" name="type" value="'+esc(device.type)+'">'+
                    '<input name="device_description" placeholder="Description" style="width:120px">'+
                    '<button class="smallButton" type="submit">Save</button>'+
                  '</form></td>'+
                '</tr>';
        }
        return rows;
      }

      function updateView(data){
        var badge=document.getElementById('scan-badge'); if(badge){ badge.textContent=data.badge; }
        var mode=document.getElementById('scan-mode'); if(mode){ mode.textContent=data.mode||''; }
        var current=document.getElementById('scan-current'); if(current){ current.textContent=data.current||''; }
        var progress=document.getElementById('scan-progress'); if(progress){ progress.textContent=data.progress||''; }
        var found=document.getElementById('scan-found'); if(found){ found.textContent=String(data.devices.length); }
        var count=document.getElementById('results-count'); if(count){ count.textContent=String(data.devices.length)+' device(s)'; }

        var body=document.getElementById('results-body');
        if(body){
          try{
            // Save focused description input and caret position
            var activeEl = document.activeElement;
            var focusedSec = null; var selStart = null; var selEnd = null;
            if (activeEl && activeEl.tagName === 'INPUT' && activeEl.name === 'device_description') {
              var p = activeEl.closest('.inlineForm');
              if (p) {
                var s = p.querySelector('input[name="secondary_address"]');
                if (s) {
                  focusedSec = s.value;
                  try { selStart = activeEl.selectionStart; selEnd = activeEl.selectionEnd; } catch(e) { selStart = null; selEnd = null; }
                }
              }
            }

            var prev = {};
            var existingForms = document.querySelectorAll('#results-body .inlineForm');
            for (var i = 0; i < existingForms.length; ++i) {
              try{
                var form = existingForms[i];
                var sec = form.querySelector('input[name="secondary_address"]');
                var desc = form.querySelector('input[name="device_description"]');
                if (sec && desc) { prev[sec.value] = desc.value; }
              } catch(e) {}
            }

            body.innerHTML = renderRows(data.devices);

            var newForms = document.querySelectorAll('#results-body .inlineForm');
            for (var j = 0; j < newForms.length; ++j) {
              try{
                var nform = newForms[j];
                var nsec = nform.querySelector('input[name="secondary_address"]');
                var ndesc = nform.querySelector('input[name="device_description"]');
                if (nsec && ndesc && prev[nsec.value]) { ndesc.value = prev[nsec.value]; }
                // restore focus and caret
                if (focusedSec && nsec && nsec.value === focusedSec && ndesc) {
                  try { ndesc.focus(); if (selStart !== null && selEnd !== null) { ndesc.setSelectionRange(selStart, selEnd); } } catch(e) {}
                }
              } catch(e) {}
            }
          } catch(e) {
            try { body.innerHTML = renderRows(data.devices); } catch(err) {}
          }
        }

        var table=document.getElementById('results-table');
        var empty=document.getElementById('results-empty');
        if(data.devices.length>0){ if(table){ table.style.display='block'; } if(empty){ empty.style.display='none'; } }
        else { if(table){ table.style.display='none'; } if(empty){ empty.style.display='block'; } }
        var hint=document.getElementById('scan-hint'); if(hint){ hint.textContent=data.hint||''; }
      }

      function poll(){
        fetch('/mbus-scan-status',{cache:'no-store',credentials:'same-origin'})
          .then(function(response){ if(!response.ok){ throw new Error('status'); } return response.json(); })
          .then(function(data){ updateView(data); window.setTimeout(poll, data.active ? 2000 : 4000); })
          .catch(function(){ window.setTimeout(poll, 4000); });
      }

      poll();
    })();</script>)JS";
    page += "</div></body></html>";
    return page;
  }

  static String buildMbusStatusJson() {
    MbusService::ScanStatus scanStatus = MbusService::scanStatus();
    std::vector<MBusGateway::DeviceInfo> devices = MbusService::scanDevices();
    String badge = scanStatus.active ? "Running" : (scanStatus.finished ? "Finished" : "Idle");
    String current = scanStatus.progressLabel.length() > 0 ? scanStatus.progressLabel + String(" ") + String(scanStatus.currentValue) : "";
    String progress = scanProgressLabel(scanStatus);
    String hint = scanStatus.active
      ? "Status updates automatically without page reload."
      : (scanStatus.finished ? (scanStatus.error.length() > 0 ? scanStatus.error : String("Scan finished.")) : String("No active scan. Start a primary or secondary scan above."));

    JsonDocument doc;
    doc["active"] = scanStatus.active;
    doc["finished"] = scanStatus.finished;
    doc["success"] = scanStatus.success;
    doc["badge"] = badge;
    doc["mode"] = scanStatus.mode;
    doc["current"] = current;
    doc["progress"] = progress;
    doc["hint"] = hint;

    JsonArray devicesArray = doc["devices"].to<JsonArray>();
    for (size_t index = 0; index < devices.size(); ++index) {
      const MBusGateway::DeviceInfo& device = devices[index];
      addDeviceSummaryJson(devicesArray,
                           device.primaryAddress,
                           device.secondaryAddress,
                           device.manufacturer,
                           device.type);
    }
    return serializeJsonResponse(doc);
  }

  static void scheduleNetworkApply() {
    networkApplyPending = true;
    networkApplyAtMs = millis() + 300;
  }

  static void applyPendingNetworkSettings() {
    if (!networkApplyPending) {
      return;
    }
    unsigned long now = millis();
    if (static_cast<long>(now - networkApplyAtMs) < 0) {
      return;
    }

    networkApplyPending = false;
    bool ok = EthernetManager::restart();
    MQTTClient::reloadSettings();
    DBG_INFO(String("Web admin: network settings updated, mode=") + EthernetManager::modeLabel());
    if (!ok) {
      DBG_WARN("Web admin: Ethernet restart did not obtain an IP after applying network settings");
    }
  }

  static void handleRoot() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderPage());
  }

  static void handleNetworkPage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderNetworkPage());
  }

  static void handleMqttPage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderMqttPage());
  }

  static void handleTimePage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderTimePage());
  }

  static void handleWebAccessPage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderWebAccessPage());
  }

  static void handleFirmwarePage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderFirmwarePage());
  }

  static void handleMbusPage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderMbusPage());
  }

  static void handleMbusGatewayPage() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendHtml(renderMbusGatewayPage());
  }

  static void handleMbusDevicePage() {
    if (!ensureAuthenticated()) {
      return;
    }

    int primaryAddress = server.arg("primary_address").toInt();
    String secondaryAddress = server.arg("secondary_address");
    secondaryAddress.trim();
    secondaryAddress.toUpperCase();

    AppSettings::SavedMbusDevice savedDevice;
    savedDevice.primaryAddress = primaryAddress;
    savedDevice.secondaryAddress = secondaryAddress;
    savedDevice.description = "";
    savedDevice.manufacturer = "";
    savedDevice.type = "";
    findSavedMbusDevice(primaryAddress, secondaryAddress, savedDevice);

    if (savedDevice.primaryAddress < 0 && savedDevice.secondaryAddress.length() == 0) {
      sendHtml(renderMbusPage("Saved device identifier is missing.", true), 400);
      return;
    }

    MBusGateway::TelegramResult telegramResult = MbusService::readDeviceTelegram(savedDevice.primaryAddress,
                                           savedDevice.secondaryAddress);
    std::vector<String> selectedFieldKeys = AppSettings::mbusSelectedFieldKeys(savedDevice.primaryAddress, savedDevice.secondaryAddress);
    if (telegramResult.success) {
      MbusService::updateSnapshot(savedDevice, telegramResult, selectedFieldKeys);
    }
    sendHtml(renderMbusDevicePage(savedDevice, telegramResult, selectedFieldKeys));
  }

  static void handleSaveMbusDeviceSelection() {
    if (!ensureAuthenticated()) {
      return;
    }

    int primaryAddress = server.arg("primary_address").toInt();
    String secondaryAddress = server.arg("secondary_address");
    secondaryAddress.trim();
    secondaryAddress.toUpperCase();

    std::vector<String> fieldKeys;
    for (int index = 0; index < server.args(); ++index) {
      String argName = server.argName(index);
      if (argName.startsWith("field_")) {
        String fieldKey = server.arg(index);
        fieldKey.trim();
        if (fieldKey.length() > 0) {
          fieldKeys.push_back(fieldKey);
        }
      }
    }

    AppSettings::saveMbusSelectedFieldKeys(primaryAddress, secondaryAddress, fieldKeys);

    AppSettings::SavedMbusDevice savedDevice;
    savedDevice.primaryAddress = primaryAddress;
    savedDevice.secondaryAddress = secondaryAddress;
    savedDevice.description = "";
    savedDevice.manufacturer = "";
    savedDevice.type = "";
    findSavedMbusDevice(primaryAddress, secondaryAddress, savedDevice);

    MBusGateway::TelegramResult telegramResult = MbusService::readDeviceTelegram(savedDevice.primaryAddress,
                                           savedDevice.secondaryAddress);
    if (telegramResult.success) {
      MbusService::updateSnapshot(savedDevice, telegramResult, fieldKeys);
    }
    sendHtml(renderMbusDevicePage(savedDevice,
                                  telegramResult,
                                  AppSettings::mbusSelectedFieldKeys(primaryAddress, secondaryAddress),
                                  "Selected fields saved for this device."));
  }

  static void handleMbusScanStatus() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendJson(buildMbusStatusJson());
  }

  static void handleMbusCheckBus() {
    if (!ensureAuthenticated()) {
      return;
    }

    int primaryAddress = server.arg("primary_address").toInt();
    String secondaryAddress = server.arg("secondary_address");
    secondaryAddress.trim();
    secondaryAddress.toUpperCase();

    if (primaryAddress < 1 && secondaryAddress.length() != 16) {
      sendJson(buildMbusCheckBusJson(false, -1, "", "", "", "", 0,
                                     "Enter a primary (1-250) or 16-char secondary address."));
      return;
    }

    MBusGateway::TelegramResult result = MbusService::readDeviceTelegram(
      primaryAddress >= 1 ? primaryAddress : -1,
      secondaryAddress,
      MbusService::ReadProfile::Interactive);

    if (!result.success) {
      String error = result.error.length() > 0 ? result.error : "Device did not respond.";
      sendJson(buildMbusCheckBusJson(false, -1, "", "", "", "", 0, error));
      return;
    }

    sendJson(buildMbusCheckBusJson(true,
                                   result.device.primaryAddress >= 0 ? result.device.primaryAddress : primaryAddress,
                                   result.device.secondaryAddress,
                                   result.device.manufacturer,
                                   result.device.type,
                                   result.device.id,
                                   result.device.version));
  }

  static void handleMbusSnapshotStatus() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendJson(buildMbusSnapshotStatusJson());
  }

  static void handleTimeSyncStatus() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendJson(buildTimeSyncStatusJson());
  }

  static void handleMbusSnapshotsData() {
    if (!ensureAuthenticated()) {
      return;
    }
    sendJson(buildMbusSnapshotsJson());
  }

  static void handleRefreshAllMbusSnapshots() {
    if (!ensureAuthenticated()) {
      return;
    }

    String error;
    if (!MbusService::startSnapshotRefresh(false, error)) {
      sendHtml(renderPage(error, true), 400);
      return;
    }

    sendHtmlWithUrlReplace(renderPage("Background refresh of saved M-Bus snapshot values started."), "/");
  }

  static void handleSaveMbusSnapshotSettings() {
    if (!ensureAuthenticated()) {
      return;
    }

    int minutes = server.arg("mbus_snapshot_refresh_minutes").toInt();
    if (minutes < 0 || minutes > 1440) {
      sendHtml(renderPage("Snapshot refresh interval must be between 0 and 1440 minutes.", true), 400);
      return;
    }

    AppSettings::saveMbusSnapshotRefreshMinutes(static_cast<uint16_t>(minutes));
    sendHtmlWithUrlReplace(renderPage("M-Bus snapshot refresh interval saved."), "/");
  }

  static void handleSaveMqtt() {
    if (!ensureAuthenticated()) {
      return;
    }

    String host = server.arg("mqtt_host");
    uint16_t port = static_cast<uint16_t>(server.arg("mqtt_port").toInt());
    String password = server.arg("mqtt_password");
    String username = server.arg("mqtt_username");
    bool useCustomCa = server.hasArg("mqtt_use_custom_ca");
    uint16_t keepAlive = static_cast<uint16_t>(server.arg("mqtt_keepalive").toInt());
    uint32_t reconnectMs = static_cast<uint32_t>(server.arg("mqtt_reconnect").toInt());
    String caCert = server.arg("mqtt_ca");

    if (password.length() == 0) {
      password = AppSettings::get().mqttPassword;
    }
    if (caCert.length() == 0) {
      caCert = AppSettings::get().mqttCaCert;
    }

    AppSettings::saveMqtt(host, port, password, username, caCert, useCustomCa, keepAlive, reconnectMs);

    // Save topic settings
    {
      String topicTelemetry = server.arg("mqtt_topic_telemetry");
      String topicTelemetryLog = server.arg("mqtt_topic_telemetry_log");
      String topicConfig = server.arg("mqtt_topic_config");
      AppSettings::saveMqttTopics(topicTelemetry, topicTelemetryLog, topicConfig);
    }

    MQTTClient::reloadSettings();
    DBG_INFO("Web admin: MQTT settings updated");
    sendHtmlWithUrlReplace(renderMqttPage("MQTT settings saved."), "/mqtt");
  }

  static void handleSaveTime() {
    if (!ensureAuthenticated()) {
      return;
    }

    int offsetMinutes = server.arg("timezone_offset_custom").toInt();
    if (server.arg("timezone_offset_custom").length() == 0) {
      offsetMinutes = server.arg("timezone_offset_minutes").toInt();
    }
    if (offsetMinutes < -720 || offsetMinutes > 840) {
      sendHtml(renderTimePage("Timezone offset must be between -720 and 840 minutes.", true), 400);
      return;
    }

    bool telemetryUseLocalTime = server.hasArg("telemetry_use_local_time");
    AppSettings::saveTimeSettings(static_cast<int16_t>(offsetMinutes), telemetryUseLocalTime);
    DBG_INFO(String("Web admin: time settings updated, timezone=") + TimeSync::formatTimezoneOffset(static_cast<int16_t>(offsetMinutes)) +
             String(", telemetry readable=") + (telemetryUseLocalTime ? "local" : "UTC"));
    sendHtmlWithUrlReplace(renderTimePage("Time settings saved."), "/time");
  }

  static void handleSaveAuth() {
    if (!ensureAuthenticated()) {
      return;
    }

    String username = server.arg("web_username");
    String password = server.arg("web_password");
    String confirm = server.arg("web_password_confirm");

    if (password.length() == 0 && confirm.length() == 0) {
      password = AppSettings::get().webPassword;
      confirm = password;
    }

    if (password != confirm) {
      sendHtml(renderPage("Web password confirmation does not match.", true), 400);
      return;
    }

    AppSettings::saveWebAuth(username, password);
    DBG_INFO("Web admin: credentials updated");
    sendHtmlWithUrlReplace(renderWebAccessPage("Web credentials saved. Browser may ask for the new password on the next request."), "/web-access");
  }

  static void handleSaveNetwork() {
    if (!ensureAuthenticated()) {
      return;
    }

    bool useDhcp = server.arg("network_mode") != "static";
    String ip = server.arg("network_ip");
    String gateway = server.arg("network_gateway");
    String subnet = server.arg("network_subnet");
    String dns1 = server.arg("network_dns1");
    String dns2 = server.arg("network_dns2");

    if (!useDhcp) {
      IPAddress parsed;
      if (!parsed.fromString(ip) || !parsed.fromString(gateway) || !parsed.fromString(subnet)) {
        sendHtml(renderNetworkPage("Static mode requires valid IP, gateway and subnet mask.", true), 400);
        return;
      }
      if (dns1.length() > 0 && !parsed.fromString(dns1)) {
        sendHtml(renderNetworkPage("Primary DNS address is invalid.", true), 400);
        return;
      }
      if (dns2.length() > 0 && !parsed.fromString(dns2)) {
        sendHtml(renderNetworkPage("Secondary DNS address is invalid.", true), 400);
        return;
      }
    }

    AppSettings::saveNetwork(useDhcp, ip, gateway, subnet, dns1, dns2);
    scheduleNetworkApply();

    String notice = "Network settings saved. Applying in a moment.";
    if (useDhcp) {
      notice += " The device may receive a different IP from DHCP.";
    } else {
      notice += String(" Reopen the web admin at http://") + ip + "/network if the current page disconnects.";
    }
    sendHtmlWithUrlReplace(renderNetworkPage(notice), "/network");
  }

  static void handleSaveMbusGateway() {
    if (!ensureAuthenticated()) {
      return;
    }

    String host = server.arg("mbus_host");
    host.trim();
    uint16_t port = static_cast<uint16_t>(server.arg("mbus_port").toInt());

    if (host.length() == 0) {
      sendHtml(renderMbusGatewayPage("Gateway host cannot be empty.", true), 400);
      return;
    }
    if (port == 0) {
      sendHtml(renderMbusGatewayPage("Gateway port must be between 1 and 65535.", true), 400);
      return;
    }

    AppSettings::saveMbusGateway(host, port);
    DBG_INFO(String("Web admin: M-Bus gateway updated to ") + host + ":" + String(port));
    sendHtmlWithUrlReplace(renderMbusGatewayPage("M-Bus gateway settings saved."), "/mbus-gateway");
  }

  static void handleScanMbus() {
    if (!ensureAuthenticated()) {
      return;
    }

    AppSettings::Settings& settings = AppSettings::get();
    String mode = server.arg("scan_mode");
    String error;
    bool started = false;

    if (mode == "secondary") {
      String secondaryStart = server.arg("secondary_start");
      String secondaryEnd = server.arg("secondary_end");
      secondaryStart.trim();
      secondaryEnd.trim();
      secondaryStart.toUpperCase();
      secondaryEnd.toUpperCase();

      if (secondaryStart.length() == 0 && secondaryEnd.length() == 0) {
        secondaryStart = "0000000000000000";
        secondaryEnd = "FFFFFFFFFFFFFFFF";
      }

      started = MbusService::startSecondaryScan(secondaryStart, secondaryEnd, error);
    } else {
      int primaryStart = server.arg("primary_start").toInt();
      int primaryEnd = server.arg("primary_end").toInt();
      started = MbusService::startPrimaryScan(primaryStart, primaryEnd, error);
    }

    if (!started) {
      sendHtml(renderMbusPage(error, true), 400);
      return;
    }

    sendHtmlWithUrlReplace(renderMbusPage("Background scan started."), "/mbus");
  }

  static void handleStopMbusScan() {
    if (!ensureAuthenticated()) {
      return;
    }

    MbusService::requestStopScan();
    sendHtmlWithUrlReplace(renderMbusPage(), "/mbus");
  }

  static void handleAddMbusDevice() {
    if (!ensureAuthenticated()) {
      return;
    }

    int primaryAddress = server.arg("primary_address").toInt();
    String secondaryAddress = server.arg("secondary_address");
    String description = server.arg("device_description");
    String manufacturer = server.arg("manufacturer");
    String type = server.arg("type");
    secondaryAddress.trim();
    secondaryAddress.toUpperCase();
    manufacturer.trim();
    type.trim();

    if (primaryAddress < 0 && secondaryAddress.length() == 0) {
      sendHtml(renderMbusPage("At least one address is required to save a device.", true), 400);
      return;
    }

    AppSettings::addMbusDevice(primaryAddress, secondaryAddress, description, manufacturer, type);
    sendHtmlWithUrlReplace(renderMbusPage("M-Bus device saved to persistent list."), "/mbus");
  }

  static void handleDeleteMbusDevice() {
    if (!ensureAuthenticated()) {
      return;
    }

    int primaryAddress = server.arg("primary_address").toInt();
    String secondaryAddress = server.arg("secondary_address");
    secondaryAddress.trim();
    secondaryAddress.toUpperCase();
    AppSettings::removeMbusDevice(primaryAddress, secondaryAddress);
    sendHtmlWithUrlReplace(renderMbusPage("M-Bus device removed."), "/mbus");
  }

  static void handleUpdateFinished() {
    if (!ensureAuthenticated()) {
      return;
    }

    if (Update.hasError()) {
      sendHtmlWithUrlReplace(renderFirmwarePage("Firmware update failed.", true), "/firmware", 500);
      return;
    }

    sendHtmlWithUrlReplace(renderFirmwarePage("Firmware uploaded successfully. Device will reboot now."), "/firmware");
    delay(500);
    ESP.restart();
  }

  static void handleUpdateUpload() {
    if (!ensureAuthenticated()) {
      return;
    }

    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      DBG_INFO(String("Web admin: OTA upload started: ") + upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
      return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      return;
    }

    if (upload.status == UPLOAD_FILE_END) {
      if (!Update.end(true)) {
        Update.printError(Serial);
        return;
      }
      DBG_INFO(String("Web admin: OTA upload finished, size=") + String(upload.totalSize));
    }
  }

  static void begin() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/mqtt", HTTP_GET, handleMqttPage);
    server.on("/time", HTTP_GET, handleTimePage);
    server.on("/network", HTTP_GET, handleNetworkPage);
    server.on("/mbus", HTTP_GET, handleMbusPage);
    server.on("/mbus-device", HTTP_GET, handleMbusDevicePage);
    server.on("/mbus-scan-status", HTTP_GET, handleMbusScanStatus);
    server.on("/mbus-devices/check-bus", HTTP_GET, handleMbusCheckBus);
    server.on("/mbus-snapshot/status", HTTP_GET, handleMbusSnapshotStatus);
    server.on("/time-sync/status", HTTP_GET, handleTimeSyncStatus);
    server.on("/mbus-snapshots/data", HTTP_GET, handleMbusSnapshotsData);
    server.on("/mbus-gateway", HTTP_GET, handleMbusGatewayPage);
    server.on("/web-access", HTTP_GET, handleWebAccessPage);
    server.on("/firmware", HTTP_GET, handleFirmwarePage);
    server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
    server.on("/save-mqtt", HTTP_POST, handleSaveMqtt);
    server.on("/save-time", HTTP_POST, handleSaveTime);
    server.on("/save-network", HTTP_POST, handleSaveNetwork);
    server.on("/save-mbus-gateway", HTTP_POST, handleSaveMbusGateway);
    server.on("/scan-mbus", HTTP_POST, handleScanMbus);
    server.on("/scan-mbus/stop", HTTP_POST, handleStopMbusScan);
    server.on("/mbus-device/save-selection", HTTP_POST, handleSaveMbusDeviceSelection);
    server.on("/mbus-devices/add", HTTP_POST, handleAddMbusDevice);
    server.on("/mbus-devices/delete", HTTP_POST, handleDeleteMbusDevice);
    server.on("/mbus-snapshot/refresh", HTTP_POST, handleRefreshAllMbusSnapshots);
    server.on("/save-mbus-snapshot-settings", HTTP_POST, handleSaveMbusSnapshotSettings);
    server.on("/save-auth", HTTP_POST, handleSaveAuth);
    server.on("/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload);
    server.onNotFound(handleRoot);
    server.begin();
    DBG_INFO("Web admin started on port 80");
  }

  static void loop() {
    server.handleClient();
    applyPendingNetworkSettings();
  }
}

#endif // WEB_ADMIN_H
