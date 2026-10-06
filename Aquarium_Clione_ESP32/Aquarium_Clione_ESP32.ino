// Aquarium Clione ESP32: XIAO ESP32C3 or ESP32-S3 / ST7789 clione tank (1-5 animals, drawn at boot).
// Body sprite plus six separately animated buccal cones and Limacina prey.
#include <esp_heap_caps.h>
#include <sdkconfig.h>
#include <esp_random.h>
#include <esp_timer.h>
#if !CONFIG_IDF_TARGET_ESP32C3
#include <img_converters.h>
#endif
#include "lgfx_setup.h"
#include "gfx.h"
#include "renderer.h"
#include "clione_model.h"
#include "clione_render.h"

static LGFX tft;
static clione::World world;
#if !CONFIG_FREERTOS_UNICORE
static TaskHandle_t worker=nullptr,mainTask=nullptr;
static volatile int workerY0=0,workerY1=0;
#endif
#if !CONFIG_IDF_TARGET_ESP32C3
static constexpr int BANDS=5;
static constexpr uint32_t REC_BAUD=2000000;
static int64_t recordRemaining=0;
#endif
static bool recording=false;
static uint32_t recordPeriod=0;
static int64_t previousFrameUs=0;
static float frameMilliseconds=0;
#if CONFIG_IDF_TARGET_ESP32C3
static uint16_t* FBB=nullptr;  // second band buffer: render one band while the other DMAs
#endif

#if !CONFIG_FREERTOS_UNICORE
static void renderWorker(void*) {
  for(;;) {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    renderBand(world,workerY0,workerY1);
    xTaskNotifyGive(mainTask);
  }
}
static void renderMT(int y0,int y1) {
  int middle=(y0+y1)/2;
  workerY0=y0;workerY1=middle;
  xTaskNotifyGive(worker);
  renderBand(world,middle,y1);
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
}
#elif !CONFIG_IDF_TARGET_ESP32C3
static void renderMT(int y0,int y1) { renderBand(world,y0,y1); }
#endif
#if !CONFIG_IDF_TARGET_ESP32C3
static void sendRecording(uint32_t period) {
  uint8_t* jpg=nullptr;size_t length=0;
  if(!fmt2jpg((uint8_t*)FB,(size_t)FB_W*FB_H*2,FB_W,FB_H,PIXFORMAT_RGB565,85,&jpg,&length))length=0;
  uint32_t checksum=0;
  for(size_t i=0;i<length;++i)checksum=checksum*31u+jpg[i];
  uint32_t size=(uint32_t)length;
  Serial.write((const uint8_t*)"FRM1",4);
  Serial.write((const uint8_t*)&size,4);
  Serial.write((const uint8_t*)&period,4);
  Serial.write((const uint8_t*)&checksum,4);
  if(length)Serial.write(jpg,length);
  if(jpg)free(jpg);
  recordRemaining-=period;
  if(recordRemaining<=0){
    Serial.write((const uint8_t*)"END0",4);Serial.flush();
    Serial.updateBaudRate(115200);recording=false;
  }
}
#endif
static void help() {
  Serial.println("Aquarium Clione ESP32\nFEED | AUTO ON/OFF | SHOW | RESET | REBOOT | HELP");
#if !CONFIG_IDF_TARGET_ESP32C3
  Serial.println("REC <seconds> (compatible with tools/record.py)");
#endif
}
static void command(char* line) {
  char* name=strtok(line," \t");if(!name)return;
  char* arg=strtok(nullptr," \t");
  if(!strcasecmp(name,"FEED"))Serial.println(clione::feed(world)?"FEED OK":"All animals are feeding; try later");
  else if(!strcasecmp(name,"AUTO")) {
    if(arg&&!strcasecmp(arg,"ON"))world.automatic=true;
    else if(arg&&!strcasecmp(arg,"OFF"))world.automatic=false;
    else {Serial.println("AUTO ON or AUTO OFF");return;}
    Serial.println(world.automatic?"automatic feeding ON":"automatic feeding OFF");
  } else if(!strcasecmp(name,"SHOW")){
    Serial.printf("%d Clione, captures=%lu releases=%lu, auto=%s\n",world.count,
      (unsigned long)world.captures,(unsigned long)world.releases,world.automatic?"ON":"OFF");
    Serial.printf("Next automatic feed in %.0f s (12-24 h interval, one animal)\n",world.nextFeedIn);
    Serial.printf("Frame %.1f ms (%.1f fps), continuous wing motion\n",frameMilliseconds,frameMilliseconds>0?1000/frameMilliseconds:0);
    for(int i=0;i<world.count;++i)Serial.printf(" %d: %s\n",i+1,clione::stageName(world.animals[i].stage));
  } else if(!strcasecmp(name,"RESET")) {
    clione::init(world,esp_random(),(float)FB_W/FB_H);Serial.println("RESET OK");
  } else if(!strcasecmp(name,"REBOOT")) {Serial.flush();ESP.restart();}
  else if(!strcasecmp(name,"REC")){
#if CONFIG_IDF_TARGET_ESP32C3
    Serial.println("REC unavailable on ESP32C3 (band framebuffer)");
#else
    int seconds=arg?atoi(arg):30;seconds=seconds<1?30:(seconds>600?600:seconds);
    Serial.println("REC OK");Serial.flush();delay(50);Serial.updateBaudRate(REC_BAUD);
    recordRemaining=(int64_t)seconds*1000000;recordPeriod=0;recording=true;
#endif
  } else if(!strcasecmp(name,"HELP"))help();
  else Serial.println("Unknown command (HELP)");
}
static void console() {
  static char line[96];static size_t used=0;static bool overflow=false;
  while(Serial.available()){
    char ch=(char)Serial.read();
    if(ch=='\n'||ch=='\r'){
      if(overflow)Serial.println("Command too long");
      else if(used){line[used]=0;command(line);}
      used=0;overflow=false;
    } else if(!overflow){
      if(used<sizeof(line)-1)line[used++]=ch;
      else overflow=true;
    }
  }
}
void setup() {
  Serial.begin(115200);delay(200);help();
  tft.init();tft.setRotation(1);tft.setColorDepth(16);tft.fillScreen(0);
#if PIN_TFT_BLK >= 0
  pinMode(PIN_TFT_BLK,OUTPUT);digitalWrite(PIN_TFT_BLK,HIGH);
#endif
  FB=(uint16_t*)heap_caps_malloc((size_t)FB_W*FB_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
#if CONFIG_IDF_TARGET_ESP32C3
  FBB=(uint16_t*)heap_caps_malloc((size_t)FB_W*FB_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  if(!FBB){Serial.println("WARN: second band buffer unavailable; synchronous fallback");FBB=FB;}
#endif
#if !CONFIG_IDF_TARGET_ESP32C3
  if(!FB)FB=(uint16_t*)heap_caps_malloc((size_t)FB_W*FB_H*2,MALLOC_CAP_SPIRAM);
#endif
  if(!FB){
    Serial.println("FATAL: framebuffer allocation failed");
    while(true)delay(1000);
  }
  Serial.println("Background: solid black");
  clione::init(world,esp_random(),(float)FB_W/FB_H);
#if !CONFIG_FREERTOS_UNICORE
  mainTask=xTaskGetCurrentTaskHandle();
  if(xTaskCreatePinnedToCore(renderWorker,"clione0",8192,nullptr,2,&worker,0)!=pdPASS){
    Serial.println("FATAL: render task allocation failed");while(true)delay(1000);
  }
#endif
  Serial.printf("Framebuffer: %u bytes, free heap: %u bytes\n",
    (unsigned)(FB_W*FB_ROWS*2),(unsigned)ESP.getFreeHeap());
  previousFrameUs=esp_timer_get_time();
}
void loop() {
  int64_t start=esp_timer_get_time();
  float dt=(float)(start-previousFrameUs)/1000000.0f;previousFrameUs=start;
  if(!recording)frameMilliseconds+=(dt*1000-frameMilliseconds)*.05f;
  if(recording&&recordPeriod)dt=recordPeriod/1000000.0f;
  // Advance in small steps so slower C3 frames do not slow the feeding clock.
  // Ignore long debugger pauses; normal frames account for up to two seconds.
  dt=fminf(dt,2.0f);
  console();
  while(dt>0){
    float step=fminf(dt,.1f);
    clione::step(world,step);
    dt-=step;
  }
#if CONFIG_IDF_TARGET_ESP32C3
  clionePrepareFrame();
  // Push a band only while it holds an animal (or a free prey shell), plus
  // one black push after it empties so no trails remain; untouched black
  // bands are skipped entirely.
  constexpr int BAND_COUNT=FB_H/FB_ROWS;
  static bool bandDirty[BAND_COUNT]={};
  bool bandActive[BAND_COUNT]={};
  auto markBand=[&](float yc,float half){
    int t=(int)floorf(yc-half),bo=(int)ceilf(yc+half);
    for(int b=0;b<BAND_COUNT;b++)if(bo>b*FB_ROWS&&t<min((b+1)*FB_ROWS,FB_H))bandActive[b]=true;
  };
  for(int i=0;i<world.count;i++)
    markBand(world.animals[i].y*FB_H,world.animals[i].size*FB_H*.8f+1.f);
  for(int i=0;i<clione::PREY_COUNT;i++){
    const clione::Prey& p=world.prey[i];
    if(p.active&&(p.owner<0||world.animals[p.owner].stage<=clione::STRIKE))
      markBand(p.y*FB_H,p.size*FB_H+1.f);
  }
  if(FBB&&FBB!=FB){
    // Two band buffers: render band k+1 while band k is still transferring.
    uint16_t* buf[2]={FB,FBB};
    int ping=0;bool pushed=false,writing=false;
    for(int y0=0;y0<FB_H;y0+=FB_ROWS){
      int y1=min(y0+FB_ROWS,FB_H);
      const int band=y0/FB_ROWS;
      const bool active=bandActive[band];
      if(!active&&!bandDirty[band])continue;
      if(pushed)tft.waitDMA();  // previous transfer done -> its buffer is free
      FB=buf[ping];
      fbOriginY=y0;
      if(active)renderBand(world,y0,y1);
      else memset(FB,0,(size_t)(y1-y0)*FB_W*2);
      fbSwapBand(y0,y1);
      if(!writing){tft.startWrite();writing=true;}
      tft.pushImageDMA(0,y0,FB_W,y1-y0,(lgfx::swap565_t*)FB);
      bandDirty[band]=active;
      ping^=1;pushed=true;
    }
    if(pushed){tft.waitDMA();tft.endWrite();}
    FB=buf[0];
  }else{
    // Synchronous fallback when the second DMA buffer could not be allocated.
    for(int y0=0;y0<FB_H;y0+=FB_ROWS){
      int y1=min(y0+FB_ROWS,FB_H);
      const int band=y0/FB_ROWS;
      const bool active=bandActive[band];
      if(!active&&!bandDirty[band])continue;
      fbOriginY=y0;
      if(active)renderBand(world,y0,y1);
      else memset(FB,0,(size_t)(y1-y0)*FB_W*2);
      fbSwapBand(y0,y1);
      tft.startWrite();
      tft.pushImage(0,y0,FB_W,y1-y0,(lgfx::swap565_t*)FB);
      tft.endWrite();
      bandDirty[band]=active;
    }
  }
#else
  renderMT(0,FB_H/BANDS);
  static bool writing=false;
  if(writing){tft.endWrite();writing=false;}
  tft.startWrite();writing=true;
  for(int band=0;band<BANDS;++band){
    int y0=band*FB_H/BANDS,y1=(band+1)*FB_H/BANDS;
    fbSwapBand(y0,y1);
    tft.pushImageDMA(0,y0,FB_W,y1-y0,(lgfx::swap565_t*)(FB+(size_t)y0*FB_W));
    if(band+1<BANDS)renderMT(y1,(band+2)*FB_H/BANDS);
  }
  if(recording){
    // No JPEG read while the final band is being transferred.
    tft.endWrite();writing=false;
    recordPeriod=(uint32_t)(esp_timer_get_time()-start);
    sendRecording(recordPeriod);
  }
#endif
}
