#include <I2S.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// make changes as needed
#define RECORD_TIME 1  // 録音秒数
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN 2
#define WAV_FILE_NAME "arduino_rec"  // ファイルネームは動的にする

const int LED_PIN = 43;
const int BTN_A = 2;  // CLAP
const int BTN_B = 3;  // SNAP
const int BTN_C = 4;  // NOISE

int COUNT_A = 0;  // KNOCK
int COUNT_B = 0;  // SNAP
int COUNT_C = 0;  // NOISE


void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_C, INPUT_PULLUP);
  Serial.begin(115200);
  while (!Serial)
    ;
  I2S.setAllPins(-1, 42, 41, -1, -1);
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, SAMPLE_BITS)) {
    Serial.println("Failed to initialize I2S!");
    while (1)
      ;
  }
  if (!SD.begin(21)) {
    Serial.println("Failed to mount SD Card!");
    while (1)
      ;
  }
}



void loop() {
  int a = digitalRead(BTN_A);
  int b = digitalRead(BTN_B);
  int c = digitalRead(BTN_C);


  if (a == LOW) {
    Serial.println("A pressed");
    delay(1000);
    char filename[16];
    snprintf(filename, sizeof(filename), "A%02d", COUNT_A);
    record_wav(filename);
    COUNT_A++;
  }

  else if (b == LOW) {
    Serial.println("B pressed");
    delay(1000);
    char filename[16];
    snprintf(filename, sizeof(filename), "B%02d", COUNT_B);
    record_wav(filename);
    COUNT_B++;
  }

  else if (c == LOW) {
    Serial.println("C pressed");
    delay(1000);
    char filename[16];
    snprintf(filename, sizeof(filename), "C%02d", COUNT_C);
    record_wav(filename);
    COUNT_C++;
  }
}


void record_wav(const char *fileBaseName) {
  digitalWrite(LED_PIN, HIGH);
  delay(50);  // 安定待ち

  uint32_t sample_size = 0;
  uint32_t record_size = (SAMPLE_RATE * SAMPLE_BITS / 8) * RECORD_TIME;
  uint8_t *rec_buffer = NULL;
  Serial.printf("Ready to start recording ...\n");


  // filename Update
  char path[32];
  snprintf(path, sizeof(path), "/%s.wav", fileBaseName);
  Serial.printf("Open: %s\n", path);

  // File file = SD.open("/" WAV_FILE_NAME ".wav", FILE_WRITE);
  File file = SD.open(path, FILE_WRITE);

  // Write the header to the WAV file
  uint8_t wav_header[WAV_HEADER_SIZE];
  generate_wav_header(wav_header, record_size, SAMPLE_RATE);
  file.write(wav_header, WAV_HEADER_SIZE);



  // PSRAM malloc for recording
  rec_buffer = (uint8_t *)ps_malloc(record_size);
  if (rec_buffer == NULL) {
    Serial.printf("malloc failed!\n");
    while (1)
      ;
  }
  Serial.printf("Buffer: %d bytes\n", ESP.getPsramSize() - ESP.getFreePsram());

  // Start recording
  esp_i2s::i2s_read(esp_i2s::I2S_NUM_0, rec_buffer, record_size, &sample_size, portMAX_DELAY);
  if (sample_size == 0) {
    Serial.printf("Record Failed!\n");
  } else {
    Serial.printf("Record %d bytes\n", sample_size);
  }


  // ① DCオフセット除去
  int64_t sum = 0;
  for (uint32_t i = 0; i + 2 <= sample_size; i += 2) {
    sum += *(int16_t *)(rec_buffer + i);
  }
  int16_t mean = (int16_t)(sum / (sample_size / 2));
  for (uint32_t i = 0; i + 2 <= sample_size; i += 2) {
    int16_t *s = (int16_t *)(rec_buffer + i);
    *s = (int16_t)((int32_t)(*s) - mean);
  }

  // ② フェードイン・フェードアウト
  const int FADE_SAMPLES = 300;  // 約18ms（16kHz）
  int total_samples = sample_size / 2;

  // フェードイン
  for (int i = 0; i < min(FADE_SAMPLES, total_samples); ++i) {
    float g = (float)i / FADE_SAMPLES;
    int16_t *s = (int16_t *)(rec_buffer + i * 2);
    *s = (int16_t)((float)(*s) * g);
  }

  // フェードアウト
  for (int i = 0; i < min(FADE_SAMPLES, total_samples); ++i) {
    float g = (float)(FADE_SAMPLES - i) / FADE_SAMPLES;
    int idx = total_samples - 1 - i;
    if (idx < 0) break;
    int16_t *s = (int16_t *)(rec_buffer + idx * 2);
    *s = (int16_t)((float)(*s) * g);
  }


  // Increase volume
  for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS / 8) {
    (*(uint16_t *)(rec_buffer + i)) <<= VOLUME_GAIN;
  }

  // Write data to the WAV file
  Serial.printf("Writing to the file ...\n");
  if (file.write(rec_buffer, record_size) != record_size)
    Serial.printf("Write file Failed!\n");

  free(rec_buffer);
  file.close();
  Serial.printf("The recording is over.\n");

  delay(50);  // 安定待ち
  digitalWrite(LED_PIN, LOW);
}

void generate_wav_header(uint8_t *wav_header, uint32_t wav_size, uint32_t sample_rate) {
  // See this for reference: http://soundfile.sapp.org/doc/WaveFormat/
  uint32_t file_size = wav_size + WAV_HEADER_SIZE - 8;
  uint32_t byte_rate = SAMPLE_RATE * SAMPLE_BITS / 8;
  const uint8_t set_wav_header[] = {
    'R', 'I', 'F', 'F',                                                   // ChunkID
    file_size, file_size >> 8, file_size >> 16, file_size >> 24,          // ChunkSize
    'W', 'A', 'V', 'E',                                                   // Format
    'f', 'm', 't', ' ',                                                   // Subchunk1ID
    0x10, 0x00, 0x00, 0x00,                                               // Subchunk1Size (16 for PCM)
    0x01, 0x00,                                                           // AudioFormat (1 for PCM)
    0x01, 0x00,                                                           // NumChannels (1 channel)
    sample_rate, sample_rate >> 8, sample_rate >> 16, sample_rate >> 24,  // SampleRate
    byte_rate, byte_rate >> 8, byte_rate >> 16, byte_rate >> 24,          // ByteRate
    0x02, 0x00,                                                           // BlockAlign
    0x10, 0x00,                                                           // BitsPerSample (16 bits)
    'd', 'a', 't', 'a',                                                   // Subchunk2ID
    wav_size, wav_size >> 8, wav_size >> 16, wav_size >> 24,              // Subchunk2Size
  };
  memcpy(wav_header, set_wav_header, sizeof(set_wav_header));
}
