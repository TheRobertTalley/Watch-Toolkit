#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "graphics/MsdCalculatorPage.h"

#if HAS_WIFI
#include <Arduino.h>
#include <WiFi.h>
#include <cstdlib>
#include <cstring>
#endif

namespace graphics {

#if HAS_WIFI
class MsdCalculatorServer
{
  public:
    struct Results
    {
        float tnt = 0.0f;
        float msd18 = 0.0f;
        float msd24 = 0.0f;
        bool valid = false;
    };

    MsdCalculatorServer() : server(80) {}

    void start()
    {
        if (running)
            return;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        if (!WiFi.softAP(msdCalculatorSsid)) {
            LOG_WARN("MSD Calculator AP failed to start");
            WiFi.mode(WIFI_OFF);
            return;
        }
        server.begin();
        server.setNoDelay(true);
        results = {};
        lastClientMs = 0;
        running = true;
        LOG_INFO("MSD Calculator AP \"%s\" available at %s", msdCalculatorSsid, WiFi.softAPIP().toString().c_str());
    }

    void stop()
    {
        if (!running)
            return;
        server.end();
        running = false;
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        LOG_INFO("MSD Calculator AP stopped");
    }

    void loop()
    {
        if (!running)
            return;
        WiFiClient client = server.available();
        if (!client)
            return;
        client.setTimeout(200);
        String requestLine = client.readStringUntil('\r');
        if (client.available())
            client.read();
        while (client.connected()) {
            String header = client.readStringUntil('\r');
            if (client.available())
                client.read();
            if (header.length() == 0)
                break;
        }
        if (requestLine.length() == 0) {
            client.stop();
            return;
        }
        std::string method;
        std::string path;
        std::string query;
        if (!parseRequestLine(requestLine, method, path, query)) {
            sendNotFound(client);
            client.stop();
            return;
        }
        if (method == "GET" && isMainPagePath(path)) {
            servePage(client);
        } else if (method == "GET" && path == "/msd") {
            serveMsdUpdate(client, query);
        } else if (method == "GET") {
            serveRedirect(client);
        } else {
            sendNotFound(client);
        }
        client.stop();
        lastClientMs = millis();
    }

    bool isActive() const { return running; }

    const Results &getResults() const { return results; }

    const char *statusText() const
    {
        if (!running)
            return "WiFi disabled";
        if (results.valid)
            return "Data received";
        if (millis() - lastClientMs < 3000)
            return "Client connected";
        return "Ready for data";
    }

  private:
    void servePage(WiFiClient &client)
    {
        client.print("HTTP/1.1 200 OK\r\n");
        client.print("Content-Type: text/html\r\n");
        client.print("Connection: close\r\n");
        client.print("Content-Length: ");
        client.print(msdCalculatorPageSize);
        client.print("\r\n\r\n");
        client.write(msdCalculatorPage, msdCalculatorPageSize);
    }

    void serveMsdUpdate(WiFiClient &client, const std::string &query)
    {
        Results parsed;
        bool gotValid = false;
        if (parseQueryParams(query, parsed) && parsed.valid) {
            results = parsed;
            gotValid = true;
        }
        sendTextResponse(client, "ok");
        if (gotValid) {
            stop();
        }
    }

    void serveRedirect(WiFiClient &client)
    {
        client.print("HTTP/1.1 302 Found\r\n");
        client.print("Location: /index.html\r\n");
        client.print("Connection: close\r\n");
        client.print("Content-Length: 0\r\n\r\n");
    }

    bool isMainPagePath(const std::string &path) const
    {
        return path == "/" || path == "/index.html" || path == "/generate_204" || path == "/hotspot-detect.html";
    }

    bool parseRequestLine(const String &line, std::string &method, std::string &path, std::string &query)
    {
        std::string request(line.c_str());
        size_t firstSpace = request.find(' ');
        if (firstSpace == std::string::npos)
            return false;
        size_t secondSpace = request.find(' ', firstSpace + 1);
        if (secondSpace == std::string::npos)
            return false;
        method = request.substr(0, firstSpace);
        std::string pathPart = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        size_t qpos = pathPart.find('?');
        if (qpos != std::string::npos) {
            path = pathPart.substr(0, qpos);
            query = pathPart.substr(qpos + 1);
        } else {
            path = pathPart;
            query.clear();
        }
        return true;
    }

    bool parseQueryParams(const std::string &query, Results &out)
    {
        size_t pos = 0;
        bool seen = false;
        while (pos < query.length()) {
            size_t eq = query.find('=', pos);
            if (eq == std::string::npos)
                break;
            size_t amp = query.find('&', eq + 1);
            std::string key = query.substr(pos, eq - pos);
            std::string value = query.substr(eq + 1, (amp == std::string::npos) ? std::string::npos : amp - eq - 1);
            float numeric = strtof(value.c_str(), nullptr);
            if (key == "msd18") {
                out.msd18 = numeric;
            } else if (key == "msd24") {
                out.msd24 = numeric;
            } else if (key == "tnt") {
                out.tnt = numeric;
                out.valid = numeric > 0;
            }
            seen = true;
            if (amp == std::string::npos)
                break;
            pos = amp + 1;
        }
        return seen;
    }

    void sendTextResponse(WiFiClient &client, const char *body)
    {
        client.print("HTTP/1.1 200 OK\r\n");
        client.print("Content-Type: text/plain\r\n");
        client.print("Connection: close\r\n");
        client.print("Content-Length: ");
        client.print(strlen(body));
        client.print("\r\n\r\n");
        client.print(body);
    }

    void sendNotFound(WiFiClient &client)
    {
        sendTextResponse(client, "404");
    }

    WiFiServer server;
    bool running = false;
    Results results;
    uint32_t lastClientMs = 0;
};
#else
class MsdCalculatorServer
{
  public:
    struct Results
    {
        float tnt = 0.0f;
        float msd18 = 0.0f;
        float msd24 = 0.0f;
        bool valid = false;
    };

    void start() {}
    void stop() {}
    void loop() {}
    bool isActive() const { return false; }
    const Results &getResults() const { return results; }
    const char *statusText() const { return "WiFi unavailable"; }

  private:
    Results results;
};
#endif

} // namespace graphics
