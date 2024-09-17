/*
  xdrv_100_AirGap_Deye.ino - WiFi Range Extender for Tasmota

  Copyright (C) 2024  pharaocurt and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_WIFI_AIRGAP_DEYE
/*********************************************************************************************
To use this, add the following to your user_config_override.h
#define USE_WIFI_AIRGAP_DEYE

List AP clients (MAC, IP and RSSI) with command AgdClients on ESP32


An example full static configuration:
#define USE_WIFI_AIRGAP_DEYE
#define USE_WIFI_RANGE_EXTENDER_CLIENTS
#define WIFI_AGD_STATE 1
#define WIFI_AGD_SSID "airgapdeye"
#define WIFI_AGD_PASSWORD "securepassword"
#define WIFI_AGD_IP_ADDRESS "192.168.123.1"
#define WIFI_AGD_SUBNETMASK "255.255.255.0"


A full command to enable the AirGap Deye could be:
Backlog AGDSSID airgapdeye ; AGDPassword securepassword ; AGDAddress 192.168.123.1 ; AGDSubnet 255.255.255.0; AGDState 1 

\*********************************************************************************************/

#define XDRV_100 100

#warning **** USE_WIFI_AIRGAP_DEYE is enabled ****

#ifdef ESP8266
#if LWIP_FEATURES
// All good
#else
#error LWIP_FEATURES required, add "-D PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH" to build_flags
#endif // LWIP_FEATURES
#endif // ESP8266

#ifdef ESP32
#ifdef CONFIG_LWIP_IP_FORWARD
// All good
#else
#error CONFIG_LWIP_IP_FORWARD not set, arduino-esp32 v2 or later required with CONFIG_LWIP_IP_FORWARD support
#endif // CONFIG_LWIP_IP_FORWARD
#ifdef USE_WIFI_RANGE_EXTENDER_NAPT
#ifdef CONFIG_LWIP_IPV4_NAPT
// All good
#else
#error CONFIG_LWIP_IPV4_NAPT not set, arduino-esp32 v2 or later required with CONFIG_LWIP_IPV4_NAPT support
#endif // IP_NAPT
#endif // CONFIG_LWIP_IPV4_NAPT
#endif // ESP32

const char kDrvAgdCommands[] PROGMEM = "Agd|" // Prefix
                                       "State"
                                       "|" D_CMND_SSID
                                       "|" D_CMND_PASSWORD
                                       "|"
                                       "Clients"
                                       "|"
                                       "Address"
                                       "|"
                                       "Subnet";

void (*const DrvAgdCommand[])(void) PROGMEM = {
    &CmndAgdState,
    &CmndAgdSSID,
    &CmndAgdPassword,
    &CmndAgdClients,
    &CmndAgdAddresses,
    &CmndAgdAddresses,
};

#include <lwip/dns.h>
#ifdef ESP8266
#include <dhcpserver.h>
#endif // ESP8266
#ifdef ESP32
#include <dhcpserver/dhcpserver.h>
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#endif // ESP32

#define AGD_NOT_CONFIGURED 0
#define AGD_FORCE_CONFIGURE 1
#define AGD_CONFIGURED 2
#define AGD_CONFIG_INCOMPLETE 3

typedef struct
{
  uint8_t status = AGD_NOT_CONFIGURED;
} TAgdSettings;

TAgdSettings AgdSettings;

// externalize to be able to protect Agd AP from teardown
bool AgdApUp()
{
  return AgdSettings.status == AGD_CONFIGURED;
}

// Check the current configuration is complete, updating AgdSettings.status
void AgdCheckConfig(void)
{
  if (
      strlen(SettingsText(SET_RGX_SSID)) > 0 &&
      strlen(SettingsText(SET_RGX_PASSWORD)) >= 8 &&
      Settings->ipv4_rgx_address &&
      Settings->ipv4_rgx_subnetmask)
  {
    if (AgdSettings.status != AGD_FORCE_CONFIGURE)
    {
      AgdSettings.status = AGD_NOT_CONFIGURED;
    }
  }
  else
  {
    AgdSettings.status = AGD_CONFIG_INCOMPLETE;
  }
}

void CmndAgdClients(void)
{
  Response_P(PSTR("{\"AgdClients\":{"));
  const char *sep = "";

#if defined(ESP32)
  wifi_sta_list_t wifi_sta_list = {0};
  wifi_sta_mac_ip_list_t wifi_sta_ip_mac_list = {0};

  esp_wifi_ap_get_sta_list(&wifi_sta_list);
  esp_wifi_ap_get_sta_list_with_ip(&wifi_sta_list, &wifi_sta_ip_mac_list);

  for (int i=0; i<wifi_sta_ip_mac_list.num; i++)
  {
    const uint8_t *m = wifi_sta_ip_mac_list.sta[i].mac;
    ResponseAppend_P(PSTR("%s\"%02X%02X%02X%02X%02X%02X\":{\"" D_CMND_IPADDRESS "\":\"%_I\",\"" D_JSON_RSSI "\":%d}"),
      sep, m[0], m[1], m[2], m[3], m[4], m[5], wifi_sta_ip_mac_list.sta[i].ip, wifi_sta_list.sta[i].rssi);
    sep = ",";
  }
#elif defined(ESP8266)
  struct station_info *station = wifi_softap_get_station_info();
  while (station)
  {
    const uint8_t *m = station->bssid;
    ResponseAppend_P(PSTR("%s\"%02X%02X%02X%02X%02X%02X\":{\"" D_CMND_IPADDRESS "\":\"%_I\"}"),
      sep, m[0], m[1], m[2], m[3], m[4], m[5], station->ip.addr);
    sep = ",";
    station = STAILQ_NEXT(station, next);
  }
  wifi_softap_free_station_info();
#endif

  ResponseAppend_P(PSTR("}}"));
}

void CmndAgdState(void)
{
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 1))
  {
    if (Settings->sbflag1.range_extender != XdrvMailbox.payload)
    {
      Settings->sbflag1.range_extender = XdrvMailbox.payload;
      if (0 == XdrvMailbox.payload)
      { // Turn off
        TasmotaGlobal.restart_flag = 2;
      }
    }
  }
  ResponseCmndStateText(Settings->sbflag1.range_extender);
}

void CmndAgdAddresses(void)
{
  char network_address[22];
  ext_snprintf_P(network_address, sizeof(network_address), PSTR(" (%_I)"), (uint32_t)NetworkAddress());
  uint32_t ipv4_address;
  if (ParseIPv4(&ipv4_address, XdrvMailbox.data))
  {
    if (XdrvMailbox.command[3] == 'S') // Subnet
    {
      Settings->ipv4_rgx_subnetmask = ipv4_address;
    }
    else
    {
      Settings->ipv4_rgx_address = ipv4_address;
    }
    AgdSettings.status = AGD_FORCE_CONFIGURE;
  }
  ResponseAgdConfig();
}

void CmndAgdSSID(void)
{
  if (XdrvMailbox.data_len > 0)
  {
    SettingsUpdateText(SET_RGX_SSID, (SC_CLEAR == Shortcut()) ? "" : XdrvMailbox.data);
    AgdSettings.status = AGD_FORCE_CONFIGURE;
  }
  ResponseAgdConfig();
}

void CmndAgdPassword(void)
{
  if (XdrvMailbox.data_len > 0)
  {
    SettingsUpdateText(SET_RGX_PASSWORD, (SC_CLEAR == Shortcut()) ? "" : XdrvMailbox.data);
    AgdSettings.status = AGD_FORCE_CONFIGURE;
  }
  ResponseAgdConfig();
}

void ResponseAgdConfig(void)
{
  AgdCheckConfig();
  Response_P(PSTR("{\"Agd\":{\"Valid\":\"%s\",\"" D_CMND_SSID "\":\"%s\",\"" D_CMND_PASSWORD "\":\"%s\",\"" D_CMND_IPADDRESS "\":\"%_I\",\"" D_JSON_SUBNETMASK "\":\"%_I\"}}"),
             (AgdSettings.status == AGD_CONFIG_INCOMPLETE) ? "false" : "true",
             EscapeJSONString(SettingsText(SET_RGX_SSID)).c_str(),
             EscapeJSONString(SettingsText(SET_RGX_PASSWORD)).c_str(),
             Settings->ipv4_rgx_address,
             Settings->ipv4_rgx_subnetmask);
}

void agdSetup()
{
  // Check we have a complete config first
  AgdCheckConfig();
  if (AgdSettings.status == AGD_CONFIG_INCOMPLETE)
  {
    AddLog(LOG_LEVEL_DEBUG, PSTR("AGD: Range Extender config incomplete"));
    return;
  }

  // WiFi.softAPConfig(EXTENDER_LOCAL_IP, EXTENDER_GATEWAY_IP, EXTENDER_SUBNET);
  WiFi.softAPConfig(Settings->ipv4_rgx_address, Settings->ipv4_rgx_address, Settings->ipv4_rgx_subnetmask);
  WiFi.softAP(SettingsText(SET_RGX_SSID), SettingsText(SET_RGX_PASSWORD), (int)1, (int)0, (int)4);
  AddLog(LOG_LEVEL_INFO, PSTR("AGD: WiFi Extender AP Enabled with SSID: %s"), WiFi.softAPSSID().c_str());
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv100(uint32_t function)
{
  bool result = false;

  if (FUNC_COMMAND == function)
  {
    result = DecodeCommand(kDrvAgdCommands, DrvAgdCommand);
  }
  else if (Settings->sbflag1.range_extender && !TasmotaGlobal.restart_flag)
  {
    switch (function)
    {
    case FUNC_PRE_INIT:
      break;
    case FUNC_EVERY_SECOND:
      // AddLog(LOG_LEVEL_INFO, PSTR("AGD: XXX DEBUG: Wifi.status: %d, WiFi.getMode(): %d, AgdSettings.status: %d, link_count: %d"), Wifi.status, WiFi.getMode(), AgdSettings.status, Wifi.link_count);
      if (AgdSettings.status == AGD_NOT_CONFIGURED && Wifi.status == WL_CONNECTED)
      {
        // Setup only if WiFi in STA only mode
        if (WiFi.getMode() == WIFI_STA)
        {
          // Connecting for the first time, setup WiFi
          agdSetup();
        }
        else
        {
          AgdSettings.status = AGD_CONFIGURED;
        }
      }
      else if (AgdSettings.status == AGD_FORCE_CONFIGURE && Wifi.status == WL_CONNECTED)
      {
        agdSetup();
      }
      else if (AgdSettings.status == AGD_CONFIGURED)
      {
        if (Wifi.status == WL_CONNECTED && WiFi.getMode() != WIFI_AP_STA)
        {
          // Should not happen... our AP is gone and only a restart will get it back properly
          AddLog(LOG_LEVEL_INFO, PSTR("AGD: WiFi mode is %d not %d. Restart..."), WiFi.getMode(), WIFI_AP_STA);
          TasmotaGlobal.restart_flag = 2;
        }
      }
      break;
    case FUNC_ACTIVE:
      result = true;
      break;
    }
  }
  return result;
}

#endif // USE_WIFI_AIRGAP_DEYE
