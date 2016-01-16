// Copyright 2015 by Thorsten von Eicken, see LICENSE.txt
#ifdef MQTT
#include <esp8266.h>
#include "cgi.h"
#include "config.h"
#include "mqtt_client.h"
#include "cgimqtt.h"

static const char* const mqtt_states[] = {
  "disconnected", "reconnecting", "connecting", "connected",
};

// Cgi to return MQTT settings
int ICACHE_FLASH_ATTR cgiMqttGet(HttpdConnData *connData) {
  char buff[1024];
  int len;

  if (connData->conn==NULL) return HTTPD_CGI_DONE;

  len = os_sprintf(buff, "{ "
      "\"slip-enable\":%d, "
      "\"mqtt-enable\":%d, "
      "\"mqtt-state\":\"%s\", "
      "\"mqtt-status-enable\":%d, "
      "\"mqtt-clean-session\":%d, "
      "\"mqtt-port\":%d, "
      "\"mqtt-timeout\":%d, "
      "\"mqtt-keepalive\":%d, "
      "\"mqtt-host\":\"%s\", "
      "\"mqtt-client-id\":\"%s\", "
      "\"mqtt-username\":\"%s\", "
      "\"mqtt-password\":\"%s\", "
      "\"mqtt-status-topic\":\"%s\", "
      "\"mqtt-status-value\":\"%s\", "
      "\"mqtt-heater\":\"%s\", "
      "\"mqtt-temp\":\"%s\", "
      "\"mqtt-humi\":\"%s\"",
      flashConfig.slip_enable, flashConfig.mqtt_enable,
      mqtt_states[mqttClient.connState], flashConfig.mqtt_status_enable,
      flashConfig.mqtt_clean_session, flashConfig.mqtt_port,
      flashConfig.mqtt_timeout, flashConfig.mqtt_keepalive,
      flashConfig.mqtt_host, flashConfig.mqtt_clientid,
      flashConfig.mqtt_username, flashConfig.mqtt_password,
      flashConfig.mqtt_status_topic, "unkown",
      flashConfig.mqtt_heater, flashConfig.mqtt_temperature,
      flashConfig.mqtt_humidity);

  for (uint8_t i=0; i<PWM_CHANNEL; i++) {
    len += os_sprintf(&buff[len], ", \"mqtt-pwm%u\":\"%s\"", i, flashConfig.mqtt_pwms[i]);
  }
  /* append trailing breaked */
  len += os_sprintf(&buff[len], " }");

  jsonHeader(connData, 200);
  httpdSend(connData, buff, len);
  return HTTPD_CGI_DONE;
}

// Cgi to change choice of pin assignments
int ICACHE_FLASH_ATTR cgiMqttSet(HttpdConnData *connData) {
  if (connData->conn==NULL) return HTTPD_CGI_DONE;

  // handle MQTT server settings
  int8_t mqtt_server = 0; // accumulator for changes/errors
  mqtt_server |= getStringArg(connData, "mqtt-host",
      flashConfig.mqtt_host, sizeof(flashConfig.mqtt_host));
  if (mqtt_server < 0) return HTTPD_CGI_DONE;
  mqtt_server |= getStringArg(connData, "mqtt-client-id",
      flashConfig.mqtt_clientid, sizeof(flashConfig.mqtt_clientid));

  if (mqtt_server < 0) return HTTPD_CGI_DONE;
  mqtt_server |= getStringArg(connData, "mqtt-username",
      flashConfig.mqtt_username, sizeof(flashConfig.mqtt_username));
  if (mqtt_server < 0) return HTTPD_CGI_DONE;
  mqtt_server |= getStringArg(connData, "mqtt-password",
      flashConfig.mqtt_password, sizeof(flashConfig.mqtt_password));

  if (mqtt_server < 0) return HTTPD_CGI_DONE;
  mqtt_server |= getBoolArg(connData, "mqtt-clean-session",
	  (bool*)&flashConfig.mqtt_clean_session);

  if (mqtt_server < 0) return HTTPD_CGI_DONE;
  int8_t mqtt_en_chg = getBoolArg(connData, "mqtt-enable",
	  (bool*)&flashConfig.mqtt_enable);

  char buff[16];

  // handle mqtt port
  if (httpdFindArg(connData->getArgs, "mqtt-port", buff, sizeof(buff)) > 0) {
    int32_t port = atoi(buff);
    if (port > 0 && port < 65536) {
      flashConfig.mqtt_port = port;
      mqtt_server |= 1;
    } else {
      errorResponse(connData, 400, "Invalid MQTT port");
      return HTTPD_CGI_DONE;
    }
  }

  // handle mqtt timeout
  if (httpdFindArg(connData->getArgs, "mqtt-timeout", buff, sizeof(buff)) > 0) {
    int32_t timeout = atoi(buff);
    flashConfig.mqtt_timeout = timeout;
  }

  // handle mqtt keepalive
  if (httpdFindArg(connData->getArgs, "mqtt-keepalive", buff, sizeof(buff)) > 0) {
    int32_t keepalive = atoi(buff);
    flashConfig.mqtt_keepalive = keepalive;
  }

  // if server setting changed, we need to "make it so"
  if (mqtt_server) {
#ifdef CGIMQTT_DBG
    os_printf("MQTT server settings changed, enable=%d\n", flashConfig.mqtt_enable);
#endif
    MQTT_Free(&mqttClient); // safe even if not connected
    mqtt_client_init();

  // if just enable changed we just need to bounce the client
  } else if (mqtt_en_chg > 0) {
#ifdef CGIMQTT_DBG
    os_printf("MQTT server enable=%d changed\n", flashConfig.mqtt_enable);
#endif
    if (flashConfig.mqtt_enable && strlen(flashConfig.mqtt_host) > 0)
      MQTT_Reconnect(&mqttClient);
    else
      MQTT_Disconnect(&mqttClient);
  }


  static const char mqtt_pwm_template[] = "mqtt-pwm%u";
  for (uint8_t i=0; i<PWM_CHANNEL; i++) {
      char name[sizeof(mqtt_pwm_template) + 2];
      os_sprintf(name, mqtt_pwm_template, i);

      if (getStringArg(connData, name, flashConfig.mqtt_pwms[i], sizeof(flashConfig.mqtt_pwms[i])) < 0) {
        return HTTPD_CGI_DONE;
      }
  }

  if (getStringArg(connData, "mqtt-heater", flashConfig.mqtt_heater, sizeof(flashConfig.mqtt_heater)) < 0) {
    return HTTPD_CGI_DONE;
  }

  if (getStringArg(connData, "mqtt-temp", flashConfig.mqtt_temperature, sizeof(flashConfig.mqtt_temperature)) < 0) {
    return HTTPD_CGI_DONE;
  }

  if (getStringArg(connData, "mqtt-humi", flashConfig.mqtt_humidity, sizeof(flashConfig.mqtt_humidity)) < 0) {
    return HTTPD_CGI_DONE;
  }

  // no action required if mqtt status settings change, they just get picked up at the
  // next status tick
  if (getBoolArg(connData, "mqtt-status-enable", (bool*)&flashConfig.mqtt_status_enable) < 0)
    return HTTPD_CGI_DONE;
  if (getStringArg(connData, "mqtt-status-topic",
        flashConfig.mqtt_status_topic, sizeof(flashConfig.mqtt_status_topic)) < 0)
    return HTTPD_CGI_DONE;

  // if SLIP-enable is toggled it gets picked-up immediately by the parser
  int slip_update = getBoolArg(connData, "slip-enable", (bool*)&flashConfig.slip_enable);
  if (slip_update < 0) return HTTPD_CGI_DONE;
#ifdef CGIMQTT_DBG
  if (slip_update > 0) os_printf("SLIP-enable changed: %d\n", flashConfig.slip_enable);

  os_printf("Saving config\n");
#endif
  if (configSave()) {
    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);
  } else {
    httpdStartResponse(connData, 500);
    httpdEndHeaders(connData);
    httpdSend(connData, "Failed to save config", -1);
  }
  return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiMqtt(HttpdConnData *connData) {
  if (connData->requestType == HTTPD_METHOD_GET) {
    return cgiMqttGet(connData);
  } else if (connData->requestType == HTTPD_METHOD_POST) {
    return cgiMqttSet(connData);
  } else {
    jsonHeader(connData, 404);
    return HTTPD_CGI_DONE;
  }
}
#endif // MQTT
