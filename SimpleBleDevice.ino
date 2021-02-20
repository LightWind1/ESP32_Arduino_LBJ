// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Sketch shows how to use SimpleBLE to advertise the name of the device and change it on the press of a button
// Useful if you want to advertise some sort of message
// Button is attached between GPIO 0 and GND, and the device name changes each time the button is pressed

#include "SimpleBLE.h"
#include "HW_RADIO_CC1101.h"
#include "POCSAG_ParseLBJ.h"
#include "POCSAG_GenerateLBJ.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// a flag that a wireless packet has been received
volatile boolean packetAvailable = false;

float Rf_Freq = 821.2375f;		//接收频率821.2375MHz

SimpleBLE ble;

struct LBJInfo_t{
	uint32_t addr;
	int8_t Train_dir;
	char LBJ_InfoStr[16];	//储存列车报警信息
} LBJ_Info;

void CC1101_Interrupt(void) {
	Serial.println("Interrupt OK");
    // disable sleep otherwise mcu could crash
    //sleep_disable();
    // Disable wireless reception interrupt
    //detachInterrupt(0);
    // set the flag that a package is available
    packetAvailable = true;
}

void Button0_Interrupt(void){
	Serial.println("Button0");
	/*
    String out = "BLE32 name: ";
    out += String(millis() / 1000);
    Serial.println(out);
    ble.begin(out);
	*/
	//Transmit_POCSAG_LBJ();
}

void decode(){
		uint8_t* batch_buff = NULL;	//存放码字原始数据的缓冲区
		uint32_t batch_len = CC1101_GetPacketLength(false);
		//获取已设置的包长度,在本例中已在初始化中设置为4个码字的长度16字节
		uint32_t actual_len;//实际读到的原始数据长度，定长模式时和batch_len相同
		POCSAG_RESULT PocsagMsg;//保存POCSAG解码结果的结构体

		if((batch_buff=(uint8_t*)malloc(batch_len*sizeof(uint8_t))) != NULL){

			memset(batch_buff,0,batch_len);	//清空batch缓存

			CC1101_ReadDataFIFO(batch_buff,&actual_len);//从FIFO读入原始数据
			float rssi = CC1101_GetRSSI();//由于接收完成后处于IDLE态
			uint8_t lqi = CC1101_GetLQI();//这里的RSSI和LQI冻结不变与本次数据包相对应

			printf("!!Received %u bytes of raw data.\r\n",actual_len);
			printf("RSSI:%.1f LQI:%hhu\r\n",rssi,lqi);
			printf("Raw data:\r\n");
			for(uint32_t i=0;i < actual_len;i++)
			{
				printf("%02Xh ",batch_buff[i]);//打印原始数据
				if((i+1)%16 == 0)
					printf("\r\n");	//每行16个
			}
			/*
			//解析LBJ信息
			POCSAG_RESULT PocsagMsg;//保存POCSAG解码结果的结构体
      		int8_t state = POCSAG_ParseCodeWordsLBJ(&PocsagMsg,batch_buff,
												 actual_len,true);

      		if(state == POCSAG_ERR_NONE){				
      			//显示地址码，功能码
      			Serial.print("Address:");		  Serial.println(PocsagMsg.Address);		
      			Serial.print("Function:");	  Serial.println(PocsagMsg.FuncCode);		
				//显示文本消息										
      			Serial.print("LBJ Message:"); Serial.println(PocsagMsg.txtMsg);
      
				if(PocsagMsg.Address == LBJ_MESSAGE_ADDR)
				{
				//ShowMessageLBJ(&PocsagMsg,rssi,lqi);	//在OLED屏幕上显示LBJ解码信息
				}
      
			}else{
				Serial.print("POCSAG parse failed! Errorcode:");
      			Serial.println(state);
			}
			*/
			free(batch_buff);
		}
}

void Transmit_POCSAG_LBJ(void)
{
	int8_t pocsag_retval;	//POCSAG生成程序的状态码
	static uint8_t tx_cnt = 0;	//发送次数计数
	uint32_t pkt_len = CC1101_GetPacketLength(false);
						//读取在初始化时设置的包长度，本程序设为16字节固定长度
						//为POCSAG编码Batch1前4个码字的长度，截断至4个码字
	switch(tx_cnt)
	{
	case 0:
		LBJ_Info.addr = 1234000;
		LBJ_Info.Train_dir = FUNC_XIAXING;
		strcpy(LBJ_Info.LBJ_InfoStr," 2667  75  1280");
		break;
	case 1:
		LBJ_Info.addr = 1234000;
		LBJ_Info.Train_dir = FUNC_SHANGXING;
		strcpy(LBJ_Info.LBJ_InfoStr," 4230 104  1680");
		break;
	case 2:
		LBJ_Info.addr = 1234000;
		LBJ_Info.Train_dir = FUNC_XIAXING;
		strcpy(LBJ_Info.LBJ_InfoStr," 2219  68  1040");
		break;
	case 3:
		LBJ_Info.addr = 1234000;
		LBJ_Info.Train_dir = FUNC_SHANGXING;
		strcpy(LBJ_Info.LBJ_InfoStr," 8054 284  2240");
		break;
	case 4:
		LBJ_Info.addr = 1234008;
		LBJ_Info.Train_dir = FUNC_TIMESYNC;
		strcpy(LBJ_Info.LBJ_InfoStr,"*1612");
		break;
	}
	
	pocsag_retval = POCSAG_MakeCodeWordsLBJ(LBJ_Info.addr,
											LBJ_Info.Train_dir,
					 						LBJ_Info.LBJ_InfoStr,
					 						BATCH2_TRUNCATE,
											true);
	if(pocsag_retval > POCSAG_ERR_NONE)
	{
		printf("POCSAG LBJ message:%s\r\n",LBJ_Info.LBJ_InfoStr);
		printf("Generated %hhd batch(s) of POCSAG codewords.\r\n",
													pocsag_retval);
		CC1101_Transmit((uint8_t*)POCSAG_Batch1,pkt_len);
									//将Batch1前4个码字发送，共16字节
		printf("Transmitted 4 codewords, 16 bytes Total.\r\n\r\n");
	}
	else
	{
		printf("Generate POCSAG codewords failed! Errcode:%hhd\r\n",
													pocsag_retval);
	}

	if(++tx_cnt == 5)	//5次一循环
		tx_cnt = 0;
}

void CC1101_Initialize(void)
{
	int8_t cc1101_state;	//设置CC1101时返回的状态码
	uint8_t delay_count = 0;	//延时计数

	Serial.println("CC1101 Initializing...");
	//1200bps 2FSK频偏4.5khz 接收机带宽58.0kHz 前导码16字节
	//固定包长度16字节，不允许同步字有位错误，启用载波检测，关闭CRC过滤
	//同步字0xEA27（标准POCSAG的低16位的反码）
	cc1101_state = CC1101_Setup(Rf_Freq,1.2f,4.5f,58.0f,0,16);
	
	if(cc1101_state == RADIO_ERR_NONE)	//若找到器件，设置成功
	{
		//attachInterrupt(CC1101_GDO2_PIN,CC1101_Interrupt,FALLING);//下降沿触发
		CC1101_StartReceive();
		Serial.println("CC1101 initialize ");
	}
	else
	{
		Serial.println("CC1101 ERROR");
	}
	
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    pinMode(0, INPUT_PULLUP);
	attachInterrupt(0,Button0_Interrupt,FALLING);//下降沿触发
    Serial.print("ESP32 SDK: ");
    Serial.println(ESP.getSdkVersion());
    ble.begin("LBJ Reciever");
    Serial.println("Press the button to change the device's name");
    CC1101_Initialize();
}


void loop() {
	
    while(Serial.available()) Serial.write(Serial.read());
	/*
	if(packetAvailable){
		Serial.println("OK");

		//decode();
		packetAvailable = false;
		CC1101_StartReceive();	//继续接收
	}
	*/
	if(CC1101_IRQ()){
		Serial.println("IRQ OK");
		decode();
	}
	
		
	//delay(100);
}
