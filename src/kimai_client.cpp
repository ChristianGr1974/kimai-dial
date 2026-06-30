#include "kimai_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

namespace {

const unsigned long HTTP_TIMEOUT_MS = 5000;

void applyAuthHeaders(HTTPClient &http, const ICredentialsProvider &credentials) {
    http.addHeader("Authorization", String("Bearer ") + credentials.kimaiToken());
    http.addHeader("Content-Type", "application/json");
}

void applyAuthHeadersWithToken(HTTPClient &http, const String &token) {
    http.addHeader("Authorization", String("Bearer ") + token);
    http.addHeader("Content-Type", "application/json");
}

bool httpGet(const ICredentialsProvider &credentials, const String &path, String &outBody, String &outError) {
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    String url = credentials.kimaiBaseUrl() + path;
    if (!http.begin(url)) {
        outError = "HTTP begin fehlgeschlagen";
        return false;
    }
    applyAuthHeaders(http, credentials);
    int code = http.GET();
    if (code <= 0) {
        outError = "Verbindung fehlgeschlagen (" + String(code) + ")";
        http.end();
        return false;
    }
    if (code < 200 || code >= 300) {
        outError = "HTTP " + String(code);
        http.end();
        return false;
    }
    outBody = http.getString();
    http.end();
    return true;
}

bool httpPost(const ICredentialsProvider &credentials, const String &path, const String &jsonBody,
              String &outBody, String &outError) {
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    String url = credentials.kimaiBaseUrl() + path;
    if (!http.begin(url)) {
        outError = "HTTP begin fehlgeschlagen";
        return false;
    }
    applyAuthHeaders(http, credentials);
    int code = http.POST(jsonBody);
    if (code <= 0) {
        outError = "Verbindung fehlgeschlagen (" + String(code) + ")";
        http.end();
        return false;
    }
    if (code < 200 || code >= 300) {
        outError = "HTTP " + String(code);
        http.end();
        return false;
    }
    outBody = http.getString();
    http.end();
    return true;
}

bool httpPatch(const ICredentialsProvider &credentials, const String &path, String &outBody, String &outError) {
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    String url = credentials.kimaiBaseUrl() + path;
    if (!http.begin(url)) {
        outError = "HTTP begin fehlgeschlagen";
        return false;
    }
    applyAuthHeaders(http, credentials);
    int code = http.PATCH("");
    if (code <= 0) {
        outError = "Verbindung fehlgeschlagen (" + String(code) + ")";
        http.end();
        return false;
    }
    if (code < 200 || code >= 300) {
        outError = "HTTP " + String(code);
        http.end();
        return false;
    }
    outBody = http.getString();
    http.end();
    return true;
}

// JSON->domain mapping kept separate from the HTTP/loop logic (step 3 of
// the architecture spec) - receives only objects already filtered for
// visibility; the visible filter deliberately stays in the callers below.
KimaiProject parseProjectJson(JsonObject obj) {
    KimaiProject p;
    p.id = obj["id"].as<int>();
    p.name = obj["name"].as<String>();
    if (!obj["parentTitle"].isNull()) {
        p.customerTitle = obj["parentTitle"].as<String>();
    }
    if (!obj["color"].isNull()) {
        p.colorHex = obj["color"].as<String>();
    }
    return p;
}

KimaiActivity parseActivityJson(JsonObject obj, int projectId) {
    KimaiActivity a;
    a.id = obj["id"].as<int>();
    a.projectId = projectId;
    a.name = obj["name"].as<String>();
    if (!obj["color"].isNull()) {
        a.colorHex = obj["color"].as<String>();
    }
    return a;
}

} // namespace

namespace KimaiClient {

bool fetchProjects(const ICredentialsProvider &credentials, std::vector<KimaiProject> &outProjects,
                    String &outError) {
    String body;
    if (!httpGet(credentials, "/api/projects", body, outError)) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        outError = "JSON-Fehler: " + String(err.c_str());
        return false;
    }

    outProjects.clear();
    for (JsonObject obj : doc.as<JsonArray>()) {
        if (obj["visible"].is<bool>() && !obj["visible"].as<bool>()) {
            continue; // skip invisible projects
        }
        outProjects.push_back(parseProjectJson(obj));
    }
    return true;
}

// Parses an activity list from a JSON body into outActivities (appending).
static bool parseActivityList(const String &body, int projectId,
                               std::vector<KimaiActivity> &outActivities, String &outError) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        outError = "JSON-Fehler: " + String(err.c_str());
        return false;
    }
    for (JsonObject obj : doc.as<JsonArray>()) {
        if (obj["visible"].is<bool>() && !obj["visible"].as<bool>()) {
            continue;
        }
        outActivities.push_back(parseActivityJson(obj, projectId));
    }
    return true;
}

bool fetchActivities(const ICredentialsProvider &credentials, int projectId,
                      std::vector<KimaiActivity> &outActivities, String &outError) {
    outActivities.clear();

    // Project-specific activities.
    String body;
    if (!httpGet(credentials, "/api/activities?project=" + String(projectId), body, outError)) {
        return false;
    }
    if (!parseActivityList(body, projectId, outActivities, outError)) {
        return false;
    }

    // Global activities (not bound to any project, projectId = -1).
    // Kimai does not combine both in a single call with project+globals params,
    // so we do a second request and append the results.
    String globalBody;
    if (!httpGet(credentials, "/api/activities?globals=true", globalBody, outError)) {
        return false;
    }
    if (!parseActivityList(globalBody, -1, outActivities, outError)) {
        return false;
    }

    return true;
}

bool fetchActiveTimesheet(const ICredentialsProvider &credentials, int &outId, int &outProjectId,
                           int &outActivityId, String &outBegin, String &outError) {
    String body;
    if (!httpGet(credentials, "/api/timesheets/active", body, outError)) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        outError = "JSON-Fehler: " + String(err.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) {
        outId = -1;
        return true;
    }

    JsonObject first = arr[0];
    outId = first["id"].as<int>();
    // project/activity can come back either as a plain id or as an object,
    // depending on the Kimai version. We handle both cases defensively.
    if (first["project"].is<JsonObject>()) {
        outProjectId = first["project"]["id"].as<int>();
    } else {
        outProjectId = first["project"].as<int>();
    }
    if (first["activity"].is<JsonObject>()) {
        outActivityId = first["activity"]["id"].as<int>();
    } else {
        outActivityId = first["activity"].as<int>();
    }
    outBegin = first["begin"].as<String>();
    return true;
}

bool startTimesheet(const ICredentialsProvider &credentials, int projectId, int activityId, int &outId,
                     String &outError) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    // begin WITHOUT a timezone suffix, since Kimai interprets the time locally (Europe/Berlin).
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);

    JsonDocument doc;
    doc["begin"] = buf;
    doc["project"] = projectId;
    doc["activity"] = activityId;
    String payload;
    serializeJson(doc, payload);

    String body;
    if (!httpPost(credentials, "/api/timesheets", payload, body, outError)) {
        return false;
    }

    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, body);
    if (err) {
        outError = "JSON-Fehler: " + String(err.c_str());
        return false;
    }
    outId = respDoc["id"].as<int>();
    return true;
}

bool stopTimesheet(const ICredentialsProvider &credentials, int timesheetId, String &outError) {
    String body;
    String path = "/api/timesheets/" + String(timesheetId) + "/stop";
    return httpPatch(credentials, path, body, outError);
}

bool validateToken(const ICredentialsProvider &credentials, const String &token, String &outError) {
    // Deliberately NOT reusing httpGet(), since this call needs to test the
    // NEW (not yet saved) token, not the currently stored one
    // (credentials.kimaiToken()).
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    String url = credentials.kimaiBaseUrl() + "/api/users/me";
    if (!http.begin(url)) {
        outError = "HTTP begin fehlgeschlagen";
        return false;
    }
    applyAuthHeadersWithToken(http, token);
    int code = http.GET();
    if (code <= 0) {
        outError = "Verbindung fehlgeschlagen (" + String(code) + ")";
        http.end();
        return false;
    }
    if (code < 200 || code >= 300) {
        outError = "HTTP " + String(code);
        http.end();
        return false;
    }
    http.end();
    return true;
}

} // namespace KimaiClient
