/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2019 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#include "ESP32CommandStation.h"
#include <esp_event.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <IPAddress.h>
#include "WebServer.h"
#include <esp_log.h>
#include <esp_task_wdt.h>

#include <freertos_drivers/arduino/WifiDefs.hxx>

#include <utils/socket_listener.hxx>

#if HC12_RADIO_ENABLED
#include "HC12Interface.h"
#endif

#if LCC_ENABLED
#include "LCCInterface.h"
#endif

static constexpr char const * WIFI_ENC_TYPES[] = {
  "OPEN",
  "WEP",
  "WPA (PSK)",
  "WPA2 (PSK)",
  "WPA/WPA2 (PSK)",
  "WPA2 Enterprise"
};

char WIFI_SSID[] = SSID_NAME;
char WIFI_PASS[] = SSID_PASSWORD;

void *jmriClientHandler(void *arg);

ESP32CSWebServer esp32csWebServer;
std::vector<int> jmriClients;
std::unique_ptr<SocketListener> JMRIListener;
bool wifiConnected = false;
WiFiInterface wifiInterface;

constexpr int JMRI_CLIENT_PRIORITY = 0;
constexpr size_t JMRI_CLIENT_STACK_SIZE = 4096;
constexpr uint16_t JMRI_LISTENER_PORT = 2560;

static constexpr const char *WIFI_STATUS_STRINGS[] =
{
    "WiFi Idle",            // WL_IDLE_STATUS
    "SSID not found",       // WL_NO_SSID_AVAIL
    "SSID scan completed",  // WL_SCAN_COMPLETED
    "WiFi connected",       // WL_CONNECTED
    "SSID connect failed",  // WL_CONNECT_FAILED
    "WiFi connection lost", // WL_CONNECTION_LOST
    "WiFi disconnected"     // WL_DISCONNECTED
};

WiFiInterface::WiFiInterface() {
}

void WiFiInterface::begin() {
#if NEXTION_ENABLED
  auto nextionTitlePage = static_cast<NextionTitlePage *>(nextionPages[TITLE_PAGE]);
  nextionTitlePage->setStatusText(0, "Initializing WiFi");
#endif
  InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("Init WiFI"));
  InfoScreen::replaceLine(INFO_SCREEN_IP_ADDR_LINE, F("IP:Pending"));
#if defined(WIFI_STATIC_IP_ADDRESS) && defined(WIFI_STATIC_IP_GATEWAY) && defined(WIFI_STATIC_IP_SUBNET)
  IPAddress staticIP, gatewayIP, subnetMask, dnsServer;
  staticIP.fromString(WIFI_STATIC_IP_ADDRESS);
  gatewayIP.fromString(WIFI_STATIC_IP_GATEWAY);
  subnetMask.fromString(WIFI_STATIC_IP_SUBNET);
#if defined(WIFI_STATIC_IP_DNS)
  dnsServer.fromString(WIFI_STATIC_IP_DNS);
#else
  dnsServer.fromString("8.8.8.8");
#endif
  WiFi.config(staticIP, gatewayIP, subnetMask, dnsServer);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.onEvent([](system_event_id_t event) {
#if LCC_ENABLED
    lccInterface.processWiFiEvent(event);
#endif
#if STATUS_LED_ENABLED
    setStatusLED(STATUS_LED::WIFI_LED, STATUS_LED_COLOR::LED_GREEN);
#endif
    if (wifiConnected) {
      return;
    }
    wifiConnected = true;
#if INFO_SCREEN_ENABLED
  #if INFO_SCREEN_LCD && INFO_SCREEN_LCD_COLUMNS < 20
    InfoScreen::replaceLine(INFO_SCREEN_IP_ADDR_LINE, WiFi.localIP().toString().c_str());
  #else
    InfoScreen::print(3, INFO_SCREEN_IP_ADDR_LINE, WiFi.localIP().toString().c_str());
  #endif
#endif
    LOG(INFO, "[WiFi] IP: %s", WiFi.localIP().toString().c_str());
    if (!MDNS.begin(HOSTNAME)) {
      LOG_ERROR("[WiFi] Failed to start mDNS");
    } else {
      LOG(INFO, "[WiFi] Adding esp32cs.tcp service to mDNS advertiser");
      MDNS.addService("esp32cs", "tcp", JMRI_LISTENER_PORT);
    }

    JMRIListener.reset(new SocketListener(JMRI_LISTENER_PORT, [](int fd) {
      jmriClients.push_back(fd);
      os_thread_create(nullptr, StringPrintf("jmri-%d", fd).c_str(),
                       JMRI_CLIENT_PRIORITY, JMRI_CLIENT_STACK_SIZE,
                       jmriClientHandler, (void *)fd);
    }));
    esp32csWebServer.begin();
#if NEXTION_ENABLED
    static_cast<NextionTitlePage *>(nextionPages[TITLE_PAGE])->clearStatusText();
    // transition to next screen since WiFi connection is complete
    nextionPages[THROTTLE_PAGE]->display();
#endif

  }, SYSTEM_EVENT_STA_GOT_IP);
  WiFi.onEvent([](system_event_id_t event) {
#if LCC_ENABLED
    lccInterface.processWiFiEvent(event);
#endif
#if STATUS_LED_ENABLED
    setStatusLED(STATUS_LED::WIFI_LED, STATUS_LED_COLOR::LED_RED);
#endif
    wifiConnected = false;
#if INFO_SCREEN_ENABLED
  #if INFO_SCREEN_LCD && INFO_SCREEN_LCD_COLUMNS < 20
    InfoScreen::replaceLine(INFO_SCREEN_IP_ADDR_LINE, "Disconnected");
  #else
    InfoScreen::print(3, INFO_SCREEN_IP_ADDR_LINE, "Disconnected");
  #endif
#endif
  }, SYSTEM_EVENT_STA_LOST_IP);
  WiFi.onEvent([](system_event_id_t event, system_event_info_t info) {
#if LCC_ENABLED
    lccInterface.processWiFiEvent(event);
#endif
#if STATUS_LED_ENABLED
    setStatusLED(STATUS_LED::WIFI_LED, STATUS_LED_COLOR::LED_RED);
#endif
    if (wifiConnected) {
      LOG(WARNING, "[WiFi] Connection to WiFi lost, reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    } else if (info.disconnected.reason == WIFI_REASON_AUTH_FAIL) {
      // AUTH_FAIL usually works after a reboot, log the failure and with
      // level FATAL it will reboot.
      LOG(FATAL, "[WiFi] WiFI connect failed (AUTH_FAIL), restarting");
    }
  }, SYSTEM_EVENT_STA_DISCONNECTED);

  WiFi.mode(WIFI_STA);
  InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("WiFi Connecting"));
#if NEXTION_ENABLED
  nextionTitlePage->setStatusText(0, "Connecting to WiFi");
#endif
  LOG(INFO, "[WiFi] WiFi details:\nHostname:%s\nMAC:%s\nSSID: %s", HOSTNAME, WiFi.macAddress().c_str(), WIFI_SSID);
  WiFi.setHostname(HOSTNAME);
#if STATUS_LED_ENABLED
  setStatusLED(STATUS_LED::WIFI_LED, STATUS_LED_COLOR::LED_GREEN_BLINK);
#endif
  if (WiFi.begin(WIFI_SSID, WIFI_PASS) != WL_CONNECT_FAILED) {
    LOG(INFO, "[WiFi] Waiting for WiFi to connect");
#if NEXTION_ENABLED
    nextionTitlePage->setStatusText(1, "Pending...");
#endif
    // this call waits up to 10sec for a result before timing out so it needs to be called a few times
    // until we get a real final result
    uint8_t attemptsRemaining = 10;
    uint8_t wifiStatus = WiFi.waitForConnectResult();
    while (wifiStatus != WL_CONNECTED && wifiStatus != WL_NO_SSID_AVAIL && wifiStatus != WL_CONNECT_FAILED && attemptsRemaining--) {
      esp_task_wdt_reset();
      LOG(INFO, "[WiFi] WiFi not connected yet, status: %d (%s), attempts remaining: %d", wifiStatus, WIFI_STATUS_STRINGS[wifiStatus], attemptsRemaining);
#if NEXTION_ENABLED
      nextionTitlePage->setStatusText(1, StringPrintf("WiFi status: %d (%s)", wifiStatus, WIFI_STATUS_STRINGS[wifiStatus]).c_str());
      nextionTitlePage->setStatusText(2, StringPrintf("remaining attempts: %d", attemptsRemaining).c_str());
#endif
      wifiStatus = WiFi.waitForConnectResult();
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
#if INFO_SCREEN_ENABLED
  #if INFO_SCREEN_LCD && INFO_SCREEN_LCD_COLUMNS < 20
    InfoScreen::replaceLine(INFO_SCREEN_IP_ADDR_LINE, F("WiFi Connection"));
    InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("Failed"));
  #else
    InfoScreen::print(3, INFO_SCREEN_IP_ADDR_LINE, F("Failed"));
    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("SSID not found"));
#if STATUS_LED_ENABLED
      setStatusLED(STATUS_LED::WIFI_LED, STATUS_LED_COLOR::LED_YELLOW_BLINK);
#endif
    } else {
      InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("Generic WiFi fail"));
#if STATUS_LED_ENABLED
      setStatusLED(STATUS_LED::WIFI_LED, STATUS_LED_COLOR::LED_YELLOW);
#endif
    }
  #endif
#endif
#if NEXTION_ENABLED
    nextionTitlePage->setStatusText(2, "");
    nextionTitlePage->setStatusText(0, "WiFi connection Failed");
    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      nextionTitlePage->setStatusText(1, "SSID not found");
    } else {
      nextionTitlePage->setStatusText(1, "Generic WiFi fail");
    }
#endif
    // since we couldn't connect to the configured SSID, perform a scan to help in
    // troubleshooting.
    int networks = WiFi.scanNetworks();
    if (networks) {
      bool ssidMatch = false;
      LOG(INFO, "Available WiFi networks:");
      for (int index = 0; index < networks; index++) {
        LOG(INFO, "SSID: %s (RSSI: %d) Encryption: %s",
          WiFi.SSID(index).c_str(), WiFi.RSSI(index), WIFI_ENC_TYPES[WiFi.encryptionType(index)]);
        if (WiFi.SSID(index).equalsIgnoreCase(WIFI_SSID)) {
          ssidMatch = true;
        }
      }
      if (ssidMatch) {
        LOG(WARNING, "Expected SSID was found, perhaps an incorrect value was provided in Config_WiFi.h WIFI_PASSWORD?");
#if INFO_SCREEN_ENABLED && (INFO_SCREEN_OLED || (INFO_SCREEN_LCD && INFO_SCREEN_LCD_COLUMNS >= 20))
        InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("BAD SSID PASSWORD!"));
#endif
#if NEXTION_ENABLED
        nextionTitlePage->setStatusText(2, "Invalid SSID password");
#endif
      }
    } else {
      LOG(WARNING, "[WiFi]Unable to find any WiFi networks!");
    }
    LOG(FATAL, "[WiFi] WiFI connect failed, restarting");
  } else {
    LOG(INFO, "[WiFi] Connected to %s!", WIFI_SSID);
  }
}

void WiFiInterface::showInitInfo() {
  print(F("<N1: %s>"), WiFi.localIP().toString().c_str());
}

void WiFiInterface::send(const String &buf) {
  for (const int client : jmriClients) {
    ::write(client, buf.c_str(), buf.length());
  }
  esp32csWebServer.broadcastToWS(buf);
#if HC12_RADIO_ENABLED
  HC12Interface::send(buf);
#endif
}

void WiFiInterface::print(const __FlashStringHelper *fmt, ...) {
  char buf[256] = {0};
  va_list args;
  va_start(args, fmt);
  vsnprintf_P(buf, sizeof(buf), (const char *)fmt, args);
  va_end(args);
  send(buf);
}

void *jmriClientHandler(void *arg) {
  int fd = (int)arg;
  DCCPPProtocolConsumer consumer;
  std::unique_ptr<uint8_t> buf(new uint8_t[128]);
  HASSERT(buf.get() != nullptr);

  // tell JMRI about our state
  DCCPPProtocolHandler::process("s");

  while (true) {
    int bytesRead = ::read(fd, buf.get(), 128);
    if (bytesRead < 0 && (errno == EINTR || errno == EAGAIN)) {
      // no data to read yet
    } else if (bytesRead > 0) {
      consumer.feed(buf.get(), bytesRead);
    } else if (bytesRead == 0) {
      // EOF, close client
      LOG(INFO, "[JMRI %d] disconnected", fd);
      break;
    } else {
      // some other error, close client
      LOG(INFO, "[JMRI %d] error:%d, %s. Disconnecting.", fd, errno, strerror(errno));
      break;
    }
  }
  // remove client FD
  std::vector<int>::iterator it = std::find(jmriClients.begin(), jmriClients.end(), fd);
  if (it != jmriClients.end()) {
    jmriClients.erase(it);
  }
  ::close(fd);
  return nullptr;
}