/*
 *   Copyright 2015, Timo Wischer <twischer@freenet.de>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "mem.h"
#include "c_types.h"
#include "user_interface.h"
#include "cgiflash.h"
#include "auth.h"
#include "espmissingincludes.h"
#include "osapi.h"
#include "espconn.h"
#include "ets_sys.h"
#include "httpd.h"
#include "httpdespfs.h"
#include "cgi.h"
#include "cgiwifi.h"
#include "cgiartnet.h"
#include "stdout.h"
#include "gpio.h"
#include "artnet.h"

#define at_dmxTaskPrio        USER_TASK_PRIO_0
#define at_dmxTaskQueueLen    1

static ETSTimer wifiModeTimer;


#ifdef USE_DMX_OUTPUT
os_event_t    at_dmxTaskQueue[at_dmxTaskQueueLen];
static void at_dmxTask(os_event_t *events);
#endif

//Function that tells the authentication system what users/passwords live on the system.
//This is disabled in the default build; if you want to try it, enable the authBasic line in
//the builtInUrls below.
int myPassFn(HttpdConnData *connData, int no, char *user, int userLen, char *pass, int passLen) {
	if (no==0) {
		os_strcpy(user, "admin");
		os_strcpy(pass, "s3cr3t");
		return 1;
//Add more users this way. Check against incrementing no for each user added.
//	} else if (no==1) {
//		os_strcpy(user, "user1");
//		os_strcpy(pass, "something");
//		return 1;
	}
	return 0;
}


/*
This is the main url->function dispatching data struct.
In short, it's a struct with various URLs plus their handlers. The handlers can
be 'standard' CGI functions you wrote, or 'special' CGIs requiring an argument.
They can also be auth-functions. An asterisk will match any url starting with
everything before the asterisks; "*" matches everything. The list will be
handled top-down, so make sure to put more specific rules above the more
general ones. Authorization things (like authBasic) act as a 'barrier' and
should be placed above the URLs they protect.
*/
HttpdBuiltInUrl builtInUrls[]={
	{"/", cgiRedirect, "/index.tpl"},
	{"/index.tpl", cgiEspFsTemplate, tplCounter},
	{"/updateweb.cgi", cgiUploadEspfs, NULL},

	//Routines to make the /wifi URL and everything beneath it work.

//Enable the line below to protect the WiFi configuration with an username/password combo.
//	{"/wifi/*", authBasic, myPassFn},

	{"/wifi", cgiRedirect, "/wifi/wifi.tpl"},
	{"/wifi/", cgiRedirect, "/wifi/wifi.tpl"},
	{"/wifi/wifiscan.cgi", cgiWiFiScan, NULL},
	{"/wifi/wifi.tpl", cgiEspFsTemplate, tplWlan},
	{"/wifi/connect.cgi", cgiWiFiConnect, NULL},
	{"/wifi/setmode.cgi", cgiWifiSetMode, NULL},

	/* inferface for configurig art net and dmx addresses */
	{"/artnet", cgiRedirect, "/artnet/index.tpl"},
	{"/artnet/", cgiRedirect, "/artnet/index.tpl"},
	{"/artnet/index.tpl", cgiEspFsTemplate, cgiArtNet_Template},
	{"/artnet/save.cgi", cgiArtNetSave, NULL},

	{"*", cgiEspFsHook, NULL}, //Catch-all cgi function for the filesystem
	{NULL, NULL, NULL}
};


static void ICACHE_FLASH_ATTR wifiModeTimerCb(void *arg) {
	const uint8 status = wifi_station_get_connect_status();
	const uint8 opmode = wifi_get_opmode();

	if (status == STATION_GOT_IP) {
		if (opmode == STATIONAP_MODE) {
			PDBG("Successful connected to station. Disable AP mode.\n");
			wifi_set_opmode(STATION_MODE);
			wifi_softap_dhcps_stop();
		}

	} else {
		if (opmode != STATIONAP_MODE) {
			PDBG("Station lost. Activating AP+STA mode.\n");
			wifi_set_opmode(STATIONAP_MODE);
			wifi_softap_dhcps_start();
		}
	}
}


#ifdef USE_DMX_OUTPUT
static void initDmxOut()
{
	//Enable TxD pin
	PIN_FUNC_SELECT(DMX_IO_MUX, DMX_IO_TXD);

	//Set baud rate and other serial parameters
	uart_div_modify(DMX_UART, UART_CLK_FREQ / 250000);
	WRITE_PERI_REG(UART_CONF0(DMX_UART), (STICK_PARITY_DIS)|(TWO_STOP_BIT << UART_STOP_BIT_NUM_S)| \
			(EIGHT_BITS << UART_BIT_NUM_S));

	system_os_task(at_dmxTask, at_dmxTaskPrio, at_dmxTaskQueue, at_dmxTaskQueueLen);
	system_os_post(at_dmxTaskPrio, 0, 0);
}


static void ICACHE_FLASH_ATTR at_dmxTask(os_event_t *event)
{
	//BREAK
	gpio_output_set(0, DMX_IO_BIT, DMX_IO_BIT, 0);
	PIN_FUNC_SELECT(DMX_IO_MUX, DMX_IO_GPIO);
	os_delay_us(150);

	//MARK
	gpio_output_set(DMX_IO_BIT, 0, DMX_IO_BIT, 0);
	os_delay_us(54);
	
	// NULL START CODE
	uart_tx_one_char(DMX_UART, 0x00);
	PIN_FUNC_SELECT(DMX_IO_MUX, DMX_IO_TXD);
	os_delay_us(54);
	
	//DMX data
	for (uint16 i = 0; i < sizeof(dmx_data); i++)
	{
		uart_tx_one_char(DMX_UART, dmx_data[i]);
		os_delay_us(54);
	}

	system_os_post(at_dmxTaskPrio, 0, 0);
}
#endif


//Main routine. Initialize stdout, the I/O and the webserver and we're done.
void user_init(void)
{
	system_update_cpu_freq(SYS_CLK_MHZ);

	stdoutInit();

	wifi_set_opmode(STATIONAP_MODE);
	struct softap_config apConfig;
	wifi_softap_get_config(&apConfig);
	apConfig.authmode = AUTH_OPEN;
	apConfig.max_connection = 4;
	apConfig.channel = 2;
	wifi_softap_set_config(&apConfig);

#ifdef USE_DMX_OUTPUT
	initDmxOut();
#endif

	os_timer_disarm(&wifiModeTimer);
	os_timer_setfn(&wifiModeTimer, wifiModeTimerCb, NULL);
	os_timer_arm(&wifiModeTimer, 5000, 1);

	httpdInit(builtInUrls, 80);
	artnet_init();
	PDBG("\nReady\n");

//	system_deep_sleep_set_option(2);
//	system_deep_sleep(2000*1000);
}
