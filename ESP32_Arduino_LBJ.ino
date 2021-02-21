/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include "POCSAG_GenerateLBJ.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "HW_RADIO_CC1101.h"
#include "POCSAG_ParseLBJ.h"


BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"


class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


float Rf_Freq = 821.2375f;		//接收频率821.2375MHz

void Button0_Interrupt(void){
	Serial.println("Button0");
}

void DecodeTask( void * parameter ){
	int8_t cc1101_state;	//设置CC1101时返回的状态码
	uint8_t delay_count = 0;	//延时计数

	Serial.println("CC1101 Initializing...");
	//1200bps 2FSK频偏4.5khz 接收机带宽58.0kHz 前导码16字节
	//固定包长度16字节，不允许同步字有位错误，启用载波检测，关闭CRC过滤
	//同步字0xEA27（标准POCSAG的低16位的反码）
	cc1101_state = CC1101_Setup(Rf_Freq,1.2f,4.5f,58.0f,0,16);
	
	if(cc1101_state == RADIO_ERR_NONE)	//若找到器件，设置成功
	{
		CC1101_StartReceive();
		Serial.println("CC1101 initialize ");
		while(1){
	  		if(CC1101_IRQ()){
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

					printf("!!Received %u bytes of raw data.\n",actual_len);
					printf("RSSI:%.1f LQI:%hhu\n",rssi,lqi);
					printf("Raw data:\n");
					for(uint32_t i=0;i < actual_len;i++){
						printf("%02Xh ",batch_buff[i]);//打印原始数据
						if((i+1)%16 == 0)
							printf("\n");	//每行16个
					}
			
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
							// notify changed value
    						if (deviceConnected) {
								pCharacteristic->setValue(PocsagMsg.txtMsg);
        						pCharacteristic->notify();
        						delay(3); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
								Serial.println("notify send");
    						}
						}
      
					}else{
						Serial.print("POCSAG parse failed! Errorcode:");
      					Serial.println(state);
					}
			
				free(batch_buff);
			}
		}
			vTaskDelay(3);
		}
	}
	else{
		Serial.println("CC1101 ERROR");
	}
	Serial.println("Ending task Decode");

    vTaskDelete(NULL);
}

void BleTask( void * parameter ){
	// Create the BLE Device
  	BLEDevice::init("ESP32");

  	// Create the BLE Server
  	pServer = BLEDevice::createServer();
  	pServer->setCallbacks(new MyServerCallbacks());

  	// Create the BLE Service
  	BLEService *pService = pServer->createService(SERVICE_UUID);

  	// Create a BLE Characteristic
  	pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  	// Create a BLE Descriptor
  	pCharacteristic->addDescriptor(new BLE2902());

  	// Start the service
  	pService->start();

  	// Start advertising
  	BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  	pAdvertising->addServiceUUID(SERVICE_UUID);
  	pAdvertising->setScanResponse(false);
  	pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  	BLEDevice::startAdvertising();
  	Serial.println("Waiting a client connection to notify...");
	
	while(1){
		 // disconnecting
   	 	if (!deviceConnected && oldDeviceConnected) {
        	delay(500); // give the bluetooth stack the chance to get things ready
        	pServer->startAdvertising(); // restart advertising
        	Serial.println("start advertising");
        	oldDeviceConnected = deviceConnected;
    	}
    	// connecting
    	if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        	oldDeviceConnected = deviceConnected;
    	}
		vTaskDelay(3);
	}

	vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    pinMode(0, INPUT_PULLUP);
	attachInterrupt(0,Button0_Interrupt,FALLING);//下降沿触发
 	
	xTaskCreate(DecodeTask,"DecodeTask", 10000, NULL,2,NULL);// Task handle. 
	xTaskCreate(BleTask,"BleTask", 10000, NULL,1,NULL);// Task handle. 
	 //Stack size 10000,Priority 1, Parameter passed as input of the task
}


void loop() {

}
