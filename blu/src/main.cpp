/***
 * main.cpp - HTTP Get over socket
 * Jon Durrant
 * 4-Oct-2022
 *
 *
 */

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"

#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#include "WifiHelper.h"
#include "Request.h"
#include <md5.h>

extern "C"{
#include <pico_fota_bootloader/core.h>
}


//Check these definitions where added from the makefile
#ifndef WIFI_SSID
#error "WIFI_SSID not defined"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined"
#endif

// LED PAD defintions
#define PULSE_LED       0


#define TASK_PRIORITY      ( tskIDLE_PRIORITY + 1UL )


void runTimeStats(){
  TaskStatus_t         * pxTaskStatusArray;
  volatile UBaseType_t uxArraySize, x;
  unsigned long        ulTotalRunTime;


  /* Take a snapshot of the number of tasks in case it changes while this
  function is executing. */
  uxArraySize = uxTaskGetNumberOfTasks();
  printf("Number of tasks %d\n", uxArraySize);

  /* Allocate a TaskStatus_t structure for each task.  An array could be
  allocated statically at compile time. */
  pxTaskStatusArray = (TaskStatus_t*) pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

  if (pxTaskStatusArray != NULL){
    /* Generate raw status information about each task. */
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray,
                                       uxArraySize,
                                       &ulTotalRunTime);



    /* For each populated position in the pxTaskStatusArray array,
    format the raw data as human readable ASCII data. */
    for (x = 0; x < uxArraySize; x++){
      printf("Task: %d \t cPri:%d \t bPri:%d \t hw:%d \t%s\n",
             pxTaskStatusArray[x].xTaskNumber,
             pxTaskStatusArray[x].uxCurrentPriority,
             pxTaskStatusArray[x].uxBasePriority,
             pxTaskStatusArray[x].usStackHighWaterMark,
             pxTaskStatusArray[x].pcTaskName
      );
    }


    /* The array is no longer needed, free the memory it consumes. */
    vPortFree(pxTaskStatusArray);
  } else{
    printf("Failed to allocate space for stats\n");
  }

  HeapStats_t heapStats;
  vPortGetHeapStats(&heapStats);
  printf("HEAP avl: %d, blocks %d, alloc: %d, free: %d\n",
         heapStats.xAvailableHeapSpaceInBytes,
         heapStats.xNumberOfFreeBlocks,
         heapStats.xNumberOfSuccessfulAllocations,
         heapStats.xNumberOfSuccessfulFrees
  );

}


char *buf = NULL;
const size_t SEGSIZE = 10240;
// Room for a whole segment, the HTTP response headers and the up to 255 bytes
// of zero padding the last segment of the image is written to flash with.
const size_t BUFLEN = SEGSIZE + 1024;


/***
 * Download the FOTA image of the release named by \p url in SEGSIZE byte
 * segments and write each segment into the bootloader's download slot.
 */
void otaUpdate(const char * url){
	MD5 fullMD;
	unsigned char md[16];
	char segNum[10];
	char segSize[10];
	snprintf(segSize, sizeof(segSize), "%lu", (unsigned long) SEGSIZE);

	buf = (char *)malloc(BUFLEN);
	// Offset of the next byte to be written into the download slot. Always a
	// multiple of PFB_ALIGN_SIZE.
	size_t slotOffset = 0;
	// Size of the image itself, without the zero padding added to the end of
	// the download slot. This is the size pfb_firmware_sha256_check() expects.
	size_t firmwareSize = 0;
	int seg = 0;
	int rec = 1;

	int err = pfb_initialize_download_slot();
	if (err){
		printf("Failed to initialize download slot: %d\n", err);
		free(buf);
		return;
	}

	while (rec > 0){

	  Request req(buf, BUFLEN);
	  std::map<std::string, std::string> query;
	  query["segSize"] = segSize;
	  snprintf(segNum, sizeof(segNum), "%d", seg);
	  query["segNum"] = segNum;
	  if (!req.get(url, &query)){
		  break;
	  }
	  printf("Seg %d Req Resp: %d : Len %u \t", seg, req.getStatusCode(), req.getPayloadLen());
	  rec = req.getPayloadLen();
	  if (rec == 0){
		  printf("\n");
		  break;
	  }

	  fullMD.update((const void *) req.getPayload(),  req.getPayloadLen());

	  MD5::hash( (const void *) req.getPayload(),  req.getPayloadLen(),  md );
	  for (int i=0; i < 16; i++){
		  if (md[i] < 0x10){
			printf("0%X", md[i]);
		  } else {
			printf("%X", md[i]);
		  }
	  }
	  printf("\n");

	  // The bootloader can only write whole flash pages, so zero pad the last
	  // segment of the image. The padding lands in the download slot but is not
	  // part of the firmware image, so it is not counted in firmwareSize.
	  uint8_t * payload = (uint8_t *) req.getPayload();
	  size_t payloadLen = (size_t) req.getPayloadLen();
	  size_t paddedLen = (payloadLen + PFB_ALIGN_SIZE - 1) & ~(size_t)(PFB_ALIGN_SIZE - 1);
	  if (paddedLen > payloadLen){
		  memset(&payload[payloadLen], 0, paddedLen - payloadLen);
	  }

	  if (pfb_write_to_flash_aligned_256_bytes(payload, slotOffset, paddedLen)){
		  printf("Failed to write segment %d to flash\n", seg);
		  free(buf);
		  pfb_mark_download_slot_as_invalid();
		  return;
	  }

	  slotOffset += paddedLen;
	  firmwareSize += payloadLen;
	  seg++;
	}

	free(buf);


	printf("Firmware size %lu\n", (unsigned long) firmwareSize);
	printf("Full MD5: ");
	fullMD.finalize( md );
	for (int i=0; i < 16; i++){
	  if (md[i] < 0x10){
		printf("0%X", md[i]);
	  } else {
		printf("%X", md[i]);
	  }
	}
	printf("\n");

	// The SHA256 is appended to every FOTA image by the bootloader build and
	// has to match before the download slot is made valid.
	if (pfb_firmware_sha256_check(firmwareSize)){
		printf("SHA256 check FAILED\n");
		pfb_mark_download_slot_as_invalid();
		return;
	}
	printf("SHA256 check OK\n");

	// Keep the new firmware over the rollback, then hand it over to the
	// bootloader which swaps the partitions over on the next reset.
	pfb_firmware_commit();
	pfb_mark_download_slot_as_valid();
	pfb_perform_update();
}


void blinkDelay(uint8_t led){
	gpio_init(led);
	gpio_set_dir(led, GPIO_OUT);

	for (int i=0; i < 20; i++){
		gpio_put(led, 1);
		vTaskDelay(500);
		gpio_put(led, 0);
		vTaskDelay(500);
	}

	for (int i=0; i < 3; i++){
		gpio_put(led, 1);
		vTaskDelay(150);
		gpio_put(led, 0);
		vTaskDelay(150);
	}

}




void main_task(void* params){

  printf("RELEASE: %s\n", RELEASE);
  printf("Main task started\n");

  if (pfb_is_after_firmware_update()){
	  printf("RUNNING ON NEW FIRMWARE\n");
  } else {
	  printf("Old firmware\n");
  }

  if (pfb_is_after_rollback()){
	  printf("FIRMWARE ROLLED BACK, THE DOWNLOADED IMAGE DID NOT COMMIT\n");
  }

  // The firmware is up and talking, so tell the bootloader not to roll back
  // the update it may have just performed.
  pfb_firmware_commit();

  if (WifiHelper::init()){
    printf("Wifi Controller Initialised\n");
  } else {
    printf("Failed to initialise controller\n");
    return;
  }


  printf("Connecting to WiFi... %s \n", WIFI_SSID);

  if (WifiHelper::join(WIFI_SSID, WIFI_PASSWORD)){
    printf("Connect to Wifi\n");
  }
  else {
    printf("Failed to connect to Wifi \n");
  }


  //Print MAC Address
  char macStr[20];
  WifiHelper::getMACAddressStr(macStr);
  printf("MAC ADDRESS: %s\n", macStr);

  //Print IP Address
  char ipStr[20];
  WifiHelper::getIPAddressStr(ipStr);
  printf("IP ADDRESS: %s\n", ipStr);



  blinkDelay(LED_GP);

  //otaUpdate("http://vmu22a.local.jondurrant.com:5000/blu");
  otaUpdate(OTA_URL);


  while (true){

    //runTimeStats();

    vTaskDelay(3000);

    /*
    if (!WifiHelper::isJoined()){
      printf("AP Link is down\n");

      if (WifiHelper::join(WIFI_SSID, WIFI_PASSWORD)){
        printf("Connect to Wifi\n");
      } else {
        printf("Failed to connect to Wifi \n");
      }
    }
    */


  }

}


void vLaunch(void) {
  TaskHandle_t task;

  xTaskCreate(main_task, "MainThread", 9000, NULL, TASK_PRIORITY, &task);

  /* Start the tasks and timer running. */
  vTaskStartScheduler();
}


int main(void) {
  stdio_init_all();
  sleep_ms(2000);
  printf("GO\n");

  /* Configure the hardware ready to run the demo. */
  const char* rtos_name;
  rtos_name = "FreeRTOS";
  printf("Starting %s on core 0:\n", rtos_name);
  vLaunch();

  return 0;
}
