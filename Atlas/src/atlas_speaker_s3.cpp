// ES3C40P schematic: https://www.lcdwiki.com/res/ES3C40P/4.0inch_ESP32-S3_Display_Schematic.pdf
// ES8311 clock/power sequence follows Espressif esp-bsp/components/es8311.
#include "atlas_speaker.h"
#include "serial_log.h"
#include "config.h"
#include <LovyanGFX.hpp>
#include <driver/i2s.h>
#include <freertos/queue.h>
#include <math.h>
namespace TurnHubAtlas {
namespace {
constexpr int AMP = AtlasConfig::AUDIO_ENABLE_PIN;
constexpr int MCLK = AtlasConfig::AUDIO_MCLK_PIN, BCLK = AtlasConfig::AUDIO_BCLK_PIN;
constexpr int LRCLK = AtlasConfig::AUDIO_LRCLK_PIN, DATA = AtlasConfig::AUDIO_DATA_OUT_PIN;
constexpr uint32_t RATE = 16000;
struct Note { uint16_t hz, ms; uint8_t volume; };
QueueHandle_t queue = nullptr;
class Speaker final : public TurnHub::ToneOutput {
 public:
 void tone(uint16_t hz, uint16_t ms, uint8_t volume) override {
   if (queue) { Note n{hz,ms,static_cast<uint8_t>(volume > 3 ? 3 : volume)}; xQueueOverwrite(queue,&n); }
 }
} speaker;
bool reg(uint8_t r, uint8_t v) {
 // Same driver/lock as FT6336U touch; no competing Wire instance.
 return lgfx::i2c::writeRegister8(0,AtlasConfig::AUDIO_CODEC_ADDRESS,r,v,0,100000).has_value();
}
void synth(void *) {
 int16_t pcm[256]{};
 Note n{}; uint32_t left=0,total=0,phase=0;
 for (;;) {
  if (xQueueReceive(queue,&n,0)==pdTRUE) { total=left=uint32_t(n.ms)*RATE/1000; phase=0; }
  for (unsigned i=0;i<128;++i) {
   int16_t value=0;
   if (left && n.hz && n.volume) {
    uint32_t ramp=total-left; if(ramp>left) ramp=left; if(ramp>64) ramp=64;
    const float amplitude=n.volume==1?1800:n.volume==2?3600:6000;
    value=int16_t(sinf(phase*(6.283185307f/RATE))*amplitude*ramp/64);
    phase=(phase+n.hz)%RATE;
   }
   if(left) --left;
   pcm[i*2]=pcm[i*2+1]=value;
  }
  size_t written=0;
  if(i2s_write(I2S_NUM_0,pcm,sizeof(pcm),&written,pdMS_TO_TICKS(100))!=ESP_OK || written!=sizeof(pcm)) {
   digitalWrite(AMP,HIGH); TurnHub::serialLog.println("[audio] I2S failed; amplifier off"); vTaskSuspend(nullptr);
  }
 }
}
}
TurnHub::ToneOutput *beginAtlasSpeaker() {
 if(queue) return &speaker;
 pinMode(AMP,OUTPUT); digitalWrite(AMP,HIGH);
 i2s_config_t cfg{};
 cfg.mode=static_cast<i2s_mode_t>(I2S_MODE_MASTER|I2S_MODE_TX);
 cfg.sample_rate=RATE; cfg.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;
 cfg.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT; cfg.communication_format=I2S_COMM_FORMAT_STAND_I2S;
 cfg.intr_alloc_flags=ESP_INTR_FLAG_LEVEL1; cfg.dma_buf_count=4; cfg.dma_buf_len=128;
 cfg.tx_desc_auto_clear=true; cfg.fixed_mclk=RATE*256;
 i2s_pin_config_t pins{};
 pins.mck_io_num=MCLK; pins.bck_io_num=BCLK; pins.ws_io_num=LRCLK;
 pins.data_out_num=DATA; pins.data_in_num=I2S_PIN_NO_CHANGE;
 if(i2s_driver_install(I2S_NUM_0,&cfg,0,nullptr)!=ESP_OK) return nullptr;
 bool ok=i2s_set_pin(I2S_NUM_0,&pins)==ESP_OK;
 i2s_zero_dma_buffer(I2S_NUM_0);
 ok=ok && reg(0x00,0x1f); delay(20);
 // 4.096 MHz MCLK, 16 kHz, slave standard I2S, 16-bit samples.
 const uint8_t settings[][2]={
 {0x00,0x00},{0x00,0x80},{0x01,0x3f},{0x02,0x00},{0x03,0x10},{0x04,0x10},
 {0x05,0x00},{0x06,0x03},{0x07,0x00},{0x08,0xff},{0x09,0x0c},{0x0a,0x0c},
 {0x0d,0x01},{0x0e,0x02},{0x12,0x00},{0x13,0x10},{0x1c,0x6a},{0x37,0x08},
 {0x32,0xbf},{0x31,0x00}};
 for(const auto &s:settings) ok=ok && reg(s[0],s[1]);
 if(ok) queue=xQueueCreate(1,sizeof(Note));
 if(!ok || !queue || xTaskCreate(synth,"atlas-audio",3072,nullptr,2,nullptr)!=pdPASS) {
  if(queue) {vQueueDelete(queue);queue=nullptr;}
  i2s_driver_uninstall(I2S_NUM_0); TurnHub::serialLog.println("[audio] ES8311 init failed; amplifier off"); return nullptr;
 }
 digitalWrite(AMP,LOW); TurnHub::serialLog.println("[audio] ES8311 ready"); return &speaker;
}
void serviceAtlasSpeaker(uint32_t) {}
}
