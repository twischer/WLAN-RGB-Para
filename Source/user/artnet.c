#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"
#include "espconn.h"

#include "espmissingincludes.h"
#include "c_types.h"
#include "io.h"
#include "pwm.h"

// ----------------------------------------------------------------------------
// op-codes
#define OP_POLL					0x2000
#define OP_POLLREPLY			0x2100
#define OP_OUTPUT				0x5000
#define OP_ADDRESS				0x6000
#define OP_IPPROG				0xf800
#define OP_IPPROGREPLY			0xf900

// ----------------------------------------------------------------------------
// status
#define RC_POWER_OK				0x01
#define RC_PARSE_FAIL			0x04
#define RC_SH_NAME_OK			0x06
#define RC_LO_NAME_OK			0x07

// ----------------------------------------------------------------------------
// default values
#define SUBNET_DEFAULT			0
#define INUNIVERSE_DEFAULT		1
#define OUTUNIVERSE_DEFAULT		0
#define PORT_DEFAULT			0x1936
#define NETCONFIG_DEFAULT		1

// ----------------------------------------------------------------------------
// other defines
#define MAX_NUM_PORTS			4
#define SHORT_NAME_LENGTH		18
#define LONG_NAME_LENGTH		64
#define PORT_NAME_LENGTH		32

#define PROTOCOL_VERSION 		14 		// DMX-Hub protocol version.
#define FIRMWARE_VERSION 		0x0310	// DMX-Hub firmware version.
#define OEM_ID 					0xB108  // OEM Code (Registered to DMXControl, must be changed in own implementation!)
#define STYLE_NODE 				0    	// Responder is a Node (DMX <-> Ethernet Device)

#define PORT_TYPE_DMX_OUTPUT	0x80
#define PORT_TYPE_DMX_INPUT 	0x40

#define MAX_CHANNELS 			512
#define IBG   					10		// interbyte gap [us]

#define REFRESH_INTERVAL		4		// [s]

#define ARTNET_PORT 0x1936

#define SIZEOF_ARTNET_POLL_REPLY	207
#define SIZEOF_ARTNET_IP_PROG_REPLY 13
#define SIZEOF_ARTNET_DMX			18

#define HTONS(n) (uint16_t)((((uint16_t) (n)) << 8) | (((uint16_t) (n)) >> 8))

uint8_t shortname[18];
uint8_t longname[64];
uint8_t net;
uint8_t artnet_subNet;
uint8_t artnet_outputUniverse1;
uint8_t dmx_data[513];

// ----------------------------------------------------------------------------
// packet formats
struct artnet_packet_addr {
	uint8_t ip[4];
	uint16_t port;
};

struct artnet_header {
	uint8_t id[8];
	uint16_t opcode;
};

struct artnet_poll {
	uint8_t id[8];
	uint16_t opcode;
	uint8_t versionH;
	uint8_t version;
	uint8_t talkToMe;
	uint8_t pad;
};

struct artnet_pollreply {
	uint8_t ID[8];
	uint16_t OpCode;
	uint8_t IP[4];
	uint16_t Port; 
	uint16_t VersInfo;
	uint8_t NetSwitch;
	uint8_t SubSwitch;
	uint16_t Oem;
	uint8_t Ubea_Version;
	uint8_t Status1;
	uint16_t EstaMan;
	uint8_t ShortName[18];
	uint8_t LongName[64];
	uint8_t NodeReport[64];
	uint16_t NumPorts;
	uint8_t PortTypes[4];
	uint8_t GoodInput[4];
	uint8_t GoodOutput[4];
	uint8_t SwIn[4];
	uint8_t SwOut[4];
	uint8_t SwVideo;
	uint8_t SwMacro;
	uint8_t SwRemote;
	uint8_t Spare[3];
	uint8_t Style;
	uint8_t MAC[6];
	uint8_t BindIp[4];
	uint8_t BindIndex;
	uint8_t Status2;
	uint8_t Filler[26];
};

struct artnet_ipprog {
	uint8_t id[8];
	uint16_t opcode;
	uint8_t versionH;
	uint8_t version;
	uint8_t filler1;
	uint8_t filler2;
	uint8_t command;
	uint8_t filler3;
	uint8_t progIp[4];
	uint8_t progSm[4];
	uint8_t progPort[2];
	uint8_t spare[8];
};

struct artnet_ipprogreply {
	uint8_t id[8];
	uint16_t opcode;
	uint8_t versionH;
	uint8_t version;
	uint8_t filler1;
	uint8_t filler2;
	uint8_t filler3;
	uint8_t filler4;
	uint8_t progIp[4];
	uint8_t progSm[4];
	uint8_t progPort[2];
	uint8_t spare[8];
};

struct artnet_address {
	uint8_t id[8];
	uint16_t opcode;
	uint8_t versionH;
	uint8_t version;
	uint8_t filler1;
	uint8_t filler2;
	uint8_t shortName[SHORT_NAME_LENGTH];
	uint8_t longName[LONG_NAME_LENGTH];
	uint8_t swin[MAX_NUM_PORTS];
	uint8_t swout[MAX_NUM_PORTS];
	uint8_t subSwitch;
	uint8_t swVideo;
	uint8_t command;
};

struct artnet_dmx {
	uint8_t id[8];
	uint16_t opcode;
	uint8_t versionH;
	uint8_t version;
	uint8_t sequence;
	uint8_t physical;
	uint8_t universe;
	uint8_t lengthHi;
	uint8_t length;
	uint8_t dataStart;
};


// ----------------------------------------------------------------------------
// send an ArtPollReply packet
static void ICACHE_FLASH_ATTR artnet_sendPollReply (struct espconn *conn, unsigned char *packet, unsigned short packetlen) 
{
		struct ip_info ipconfig;
		char mac_addr[6];

		struct artnet_pollreply msg;
		memset(&msg, 0, sizeof(struct artnet_pollreply));
		
		strcpy((char*)msg.ID,"Art-Net");
		
		msg.OpCode = OP_POLLREPLY;
		
		//read ip and write in pollreply packet
		wifi_get_ip_info(STATION_IF, &ipconfig);
		memcpy(msg.IP,&ipconfig.ip.addr,4);
		
		msg.Port = ARTNET_PORT;
	
		msg.VersInfo = HTONS(0x0100);
		msg.NetSwitch = net;
		msg.SubSwitch = artnet_subNet;
		msg.Oem = HTONS(0x08B1);						
		msg.Ubea_Version = 0;
		msg.Status1 = 0;
		msg.EstaMan = 0;						

		memcpy(msg.ShortName,shortname, sizeof(msg.ShortName));
		memcpy(msg.LongName,longname, sizeof(msg.LongName));
		strcpy((char *)msg.NodeReport,"OK");
	
		msg.NumPorts = HTONS(1);
		msg.PortTypes[0]=0x80;
		
		msg.SwOut[0] = artnet_outputUniverse1;
		
		//read mac and write in pollreply packet
		wifi_get_macaddr((uint8)STATION_IF,(uint8*) mac_addr);
		memcpy(msg.MAC,mac_addr,6);
		
		espconn_sent(conn,(uint8_t*)&msg,sizeof(struct artnet_pollreply));
}


// ----------------------------------------------------------------------------
// Art-Net DMX packet
static void ICACHE_FLASH_ATTR artnet_recv_opoutput(unsigned char *data, unsigned short packetlen)
{
	//os_printf("Received artnet output packet!\r\n");
	struct artnet_dmx *dmx;
	dmx = (struct artnet_dmx *) data;

	//os_printf("Received artnet output packet! Universe %x\r\n", dmx->universe);
	
	uint16_t artnet_dmxChannels;
	
	if (dmx->universe == ((artnet_subNet << 4) | artnet_outputUniverse1)) 
	{
		//Daten vom Ethernetframe in den DMX Buffer kopieren
		artnet_dmxChannels = (dmx->lengthHi << 8) | dmx->length;
		if(artnet_dmxChannels > MAX_CHANNELS) artnet_dmxChannels = MAX_CHANNELS;
		
		for(uint16_t tmp = 0;tmp<513;tmp++)
		{
			dmx_data[tmp+1] = data[tmp+18];
		}


		pwm_set_duty(dmx_data[1], 1);


		static uint8_t old = 0;
		if (old != dmx_data[1]) {
			os_printf("%d\n", dmx_data[1]);
			old = dmx_data[1];

			pwm_start();
		}
	}
}

// ----------------------------------------------------------------------------
// receive Art-Net packet
static void ICACHE_FLASH_ATTR artnet_get(void *arg, char *data, unsigned short length) {

	//os_printf("Get on Art Net Port\n");
	unsigned char *eth_buffer =(unsigned char *)data;
	
	struct artnet_header *header;
	header = (struct artnet_header *) data;
	
	//check the id
	if(os_strcmp((char*)&header->id,"Art-Net\0") != 0){
		os_printf("Wrong ArtNet header, discarded\r\n");
		return;
	}

	switch(header->opcode)
	{
		//OP_POLL
		case (OP_POLL):{
			//os_printf("Received artnet poll packet!\r\n");
			artnet_sendPollReply ((struct espconn *)arg,&eth_buffer[0],length);
			return; 
		}
		//OP_POLLREPLY
		case (OP_POLLREPLY):{
			//os_printf("Received artnet poll reply packet!\r\n");
			return;
		}
		//OP_OUTPUT	
		case (OP_OUTPUT):{
			//os_printf("Received artnet output packet!\r\n");
			artnet_recv_opoutput (&eth_buffer[0],length);
			return; 
		}
		//OP_ADDRESS
		case (OP_ADDRESS):{
			//os_printf("Received artnet address packet!\r\n");
			return;
		}
		//OP_IPPROG
		case (OP_IPPROG):{
			//os_printf("Received artnet ip prog packet!\r\n");
			return;		
		}	
	}
}

// ----------------------------------------------------------------------------
// Art-Net init
void artnet_init() {


	os_printf("Art Net Init\n");
	
	//Init Data
	net = 0;
	artnet_subNet = 0;
	artnet_outputUniverse1 = 1;
	strcpy((char*)shortname,"ESP8266 NODE");
	strcpy((char*)longname,"ESP based Art-Net Node");
	
	static struct espconn artnetconn;
	static esp_udp artnetudp;
	artnetconn.type = ESPCONN_UDP;
	artnetconn.state = ESPCONN_NONE;
	artnetconn.proto.udp = &artnetudp;
	artnetudp.local_port=ARTNET_PORT;
	artnetconn.reverse = NULL;
	espconn_regist_recvcb(&artnetconn, artnet_get);
	espconn_create(&artnetconn);

	uint8_t duty[] = {64, 64, 64};
	pwm_init(100, duty);
}
