// Minimal ES8311 playback, using the board-proven Orb OS codec/I2S setup.
// No SD reads or per-second waveform synthesis. The playback task sleeps
// between ticks and streams complete DMA cycles without blocking the UI.
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "driver/i2s.h"
#include "freertos/semphr.h"
#include "tick_audio.h"

namespace {
constexpr uint8_t CODEC=0x18,PMIC=0x34;
// Stream two DMA ring lengths of silence before the click, so the driver is
// caught up after idle and the amplifier is settled before the transient.
constexpr int RATE=16000,LEAD_FRAMES=2048,CLICK_FRAMES=2560,TAIL_FRAMES=1024;
int16_t packet[(LEAD_FRAMES+CLICK_FRAMES+TAIL_FRAMES)*2]={};
int16_t chunk[512];
bool ready=false;
volatile uint32_t played=0,dropped=0,generation=0;
volatile uint32_t chimed=0;
bool chimeRequested=false;
volatile bool allowed=false;
volatile int currentVolume=35;
TaskHandle_t playbackTask=nullptr;
SemaphoreHandle_t audioLock=nullptr;

bool writeReg(uint8_t address,uint8_t reg,uint8_t value) {
    Wire.beginTransmission(address);Wire.write(reg);Wire.write(value);
    return Wire.endTransmission()==0;
}
bool readReg(uint8_t address,uint8_t reg,uint8_t &value) {
    Wire.beginTransmission(address);Wire.write(reg);
    if (Wire.endTransmission(false)!=0 || Wire.requestFrom(int(address),1)!=1) return false;
    value=Wire.read();return true;
}
void codec(uint8_t reg,uint8_t value){writeReg(CODEC,reg,value);}

void playback(void *) {
    for (;;) {
        const uint32_t pending=ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        const bool bell=__atomic_exchange_n(&chimeRequested,false,__ATOMIC_ACQ_REL);
        if (pending>1&&!bell) dropped+=pending-1;
        if (!allowed) continue;
        xSemaphoreTake(audioLock,portMAX_DELAY);
        const uint32_t request=generation;
        if (!allowed) {xSemaphoreGive(audioLock);continue;}
        digitalWrite(46,HIGH);
        bool ok=true;
        constexpr size_t BELL_FRAMES=11200;
        const size_t total=bell ? (LEAD_FRAMES+BELL_FRAMES+TAIL_FRAMES)*4:sizeof(packet);
        for (size_t offset=0;offset<total;offset+=1024) {
            if (!allowed || generation!=request) {ok=false;break;}
            size_t written=0;
            const size_t count=min(size_t(1024),total-offset);
            const int gain=currentVolume;
            if(bell) {
                for(size_t frame=0;frame<count/4;++frame) {
                    const int n=int(offset/4+frame)-LEAD_FRAMES;float sample=0;
                    if(n>=0&&n<int(BELL_FRAMES)) {
                        const float t=float(n)/RATE;
                        const float env=fminf(1.0f,n/64.0f)*expf(-5.5f*t)*fminf(1.0f,(BELL_FRAMES-n)/320.0f);
                        sample=env*(.65f*sinf(2*M_PI*880*t)+.23f*sinf(2*M_PI*1762*t)+.12f*sinf(2*M_PI*2420*t));
                    }
                    const int16_t value=int16_t(sample*12000*gain/100);
                    chunk[frame*2]=chunk[frame*2+1]=value;
                }
            } else for (size_t sample=0;sample<count/2;++sample) chunk[sample]=int32_t(packet[offset/2+sample])*gain/100;
            if (i2s_write(I2S_NUM_0,chunk,count,&written,pdMS_TO_TICKS(100))!=ESP_OK || written!=count) {ok=false;break;}
        }
        if (ok) {if(bell)++chimed;else ++played;vTaskDelay(pdMS_TO_TICKS(80));}
        else if (allowed && generation==request) ++dropped;
        // Keep the amplifier awake between ticks: short enable windows were
        // inaudible on the actual Orb. Muting powers it down immediately.
        if (!allowed) digitalWrite(46,LOW);
        i2s_zero_dma_buffer(I2S_NUM_0);
        xSemaphoreGive(audioLock);
    }
}
}

void tickAudioVolume(int volume) {
    currentVolume=constrain(volume,0,100);
}

static void buildWaveform() {
    uint32_t noise=0x6415;
    for (int i=0;i<CLICK_FRAMES;++i) {
        const float t=float(i)/RATE;
        noise=noise*1664525u+1013904223u;
        const float n=(int(noise>>16)-32768)/32768.0f;
        const float attack=fminf(1.0f,i/5.0f);
        const float sample=attack*(.60f*sinf(2*M_PI*2300*t)*expf(-180*t)
            +.30f*sinf(2*M_PI*900*t)*expf(-100*t)+.1f*n*expf(-350*t));
        const int16_t value=int16_t(sample*14000);
        packet[(LEAD_FRAMES+i)*2]=packet[(LEAD_FRAMES+i)*2+1]=value;
    }
}

bool tickAudioBegin(int volume) {
    pinMode(46,OUTPUT);digitalWrite(46,LOW);
    uint8_t value=0;
    // AXP2101 ALDO1: 3.3 V codec rail. Preserve all unrelated power rails.
    // Register/voltage formula matches the installed XPowersAXP2101 driver.
    if (!readReg(PMIC,0x92,value) || !writeReg(PMIC,0x92,(value&0xE0)|28)) return false;
    if (!readReg(PMIC,0x90,value) || !writeReg(PMIC,0x90,value|1)) return false;
    delay(5);
    if (!readReg(CODEC,0xFD,value) || value!=0x83) return false;
    const uint8_t init[][2]={
        {0x0D,0xFA},{0x44,0x08},{0x44,0x08},{0x01,0x30},{0x02,0x00},
        {0x03,0x10},{0x16,0x24},{0x04,0x10},{0x05,0x00},{0x0B,0x00},
        {0x0C,0x00},{0x10,0x1F},{0x11,0x7F},{0x00,0x80},{0x00,0x80},
        {0x01,0xBF},{0x13,0x10},{0x1B,0x0A},{0x1C,0x6A},{0x44,0x58},
        {0x02,0x18},{0x05,0x00},{0x03,0x10},{0x04,0x20},{0x07,0x00},
        {0x08,0xFF},{0x06,0x03},{0x09,0x0C},{0x0A,0x0C},{0x00,0x80},
        {0x01,0xBF},{0x09,0x0C},{0x0A,0x0C},{0x17,0xBF},{0x0E,0x02},
        {0x12,0x00},{0x14,0x1A},{0x0D,0x01},{0x15,0x40},{0x37,0x08},
        {0x45,0x00},{0x32,0xBF},{0x31,0x00}
    };
    for (const auto &pair:init) codec(pair[0],pair[1]);
    i2s_config_t cfg={};
    cfg.mode=i2s_mode_t(I2S_MODE_MASTER|I2S_MODE_TX);cfg.sample_rate=RATE;
    cfg.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;cfg.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format=I2S_COMM_FORMAT_STAND_I2S;cfg.intr_alloc_flags=ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count=4;cfg.dma_buf_len=256;cfg.tx_desc_auto_clear=true;
    cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;cfg.bits_per_chan=I2S_BITS_PER_CHAN_16BIT;
    if (i2s_driver_install(I2S_NUM_0,&cfg,0,nullptr)!=ESP_OK) return false;
    i2s_pin_config_t pins={};
    pins.mck_io_num=42;pins.bck_io_num=9;pins.ws_io_num=45;
    pins.data_out_num=8;pins.data_in_num=I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(I2S_NUM_0,&pins)!=ESP_OK) {i2s_driver_uninstall(I2S_NUM_0);return false;}
    i2s_zero_dma_buffer(I2S_NUM_0);buildWaveform();tickAudioVolume(volume);
    audioLock=xSemaphoreCreateMutex();
    if (!audioLock || xTaskCreatePinnedToCore(playback,"clock-tick",2304,nullptr,1,&playbackTask,0)!=pdPASS) return false;
    ready=true;return true;
}

void tickAudioPlay() {
    if (!ready) return;
    tickAudioEnable();xTaskNotifyGive(playbackTask);
}
bool tickAudioChime() {
    if(!ready||!allowed||currentVolume<=0)return false;
    __atomic_store_n(&chimeRequested,true,__ATOMIC_RELEASE);xTaskNotifyGive(playbackTask);return true;
}
uint32_t tickAudioChimeCount(){return chimed;}
void tickAudioEnable() {if (ready) {allowed=true;digitalWrite(46,HIGH);}}
void tickAudioSilence() {
    allowed=false;++generation;digitalWrite(46,LOW);
}
bool tickAudioReady(){return ready;}
uint32_t tickAudioCount(){return played;}
uint32_t tickAudioDropped(){return dropped;}

void tickAudioDiagnostics() {
    Serial.print("!clock {\"ok\":true,\"codec\":{");
    const uint8_t regs[]={0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0D,0x0E,0x12,0x14,0x31,0x32,0x37,0x44,0xFD,0xFE};
    bool first=true;
    for (uint8_t reg:regs) {
        uint8_t value=0;const bool ok=readReg(CODEC,reg,value);
        Serial.printf("%s\"%02X\":%d",first ? "":",",reg,ok ? int(value):-1);first=false;
    }
    uint8_t rail=0,voltage=0;readReg(PMIC,0x90,rail);readReg(PMIC,0x92,voltage);
    int peak=0;for (int16_t sample:packet) peak=max(peak,abs(int(sample)*currentVolume/100));
    Serial.printf("},\"pmic_90\":%u,\"pmic_92\":%u,\"pa\":%d,\"tick_peak\":%d,\"audio_stack_free\":%u}\n",rail,voltage,digitalRead(46),peak,playbackTask ? unsigned(uxTaskGetStackHighWaterMark(playbackTask)):0);
}

// Explicit USB diagnostic only; normal ticking remains nonblocking.
bool tickAudioTest() {
    if (!ready) return false;
    const bool resume=allowed;
    tickAudioSilence();xSemaphoreTake(audioLock,portMAX_DELAY);
    digitalWrite(46,HIGH);delay(25);
    int16_t tone[256*2];bool ok=true;
    for (int block=0;block<94;++block) {
        for (int i=0;i<256;++i) {
            const int frame=block*256+i;
            const int16_t sample=int16_t(8000*sinf(2*M_PI*880*frame/RATE));
            tone[2*i]=tone[2*i+1]=sample;
        }
        size_t written=0;
        if (i2s_write(I2S_NUM_0,tone,sizeof(tone),&written,pdMS_TO_TICKS(100))!=ESP_OK || written!=sizeof(tone)) ok=false;
    }
    delay(80);tickAudioSilence();i2s_zero_dma_buffer(I2S_NUM_0);
    xSemaphoreGive(audioLock);if (resume) tickAudioEnable();return ok;
}
