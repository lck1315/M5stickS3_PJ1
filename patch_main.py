import os
import sys

with open("src/main.cpp", "r") as f:
    lines = f.readlines()

# 1. Insert headers at line 20
headers = """#include <AudioGeneratorMP3.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioOutput.h>
#include <lwip/sockets.h>
"""
lines.insert(20, headers)

# 2. Find setup() line
setup_idx = -1
for i, line in enumerate(lines):
    if line.startswith("void setup() {"):
        setup_idx = i
        break

if setup_idx == -1:
    print("Cannot find setup()")
    sys.exit(1)

tts_code = """
// --- TTS ENGINE ---
class AudioFileSourceHTTPStreamUA : public AudioFileSourceHTTPStream {
public:
  AudioFileSourceHTTPStreamUA() : AudioFileSourceHTTPStream() {}

  bool openWithHeaders(const char* url) {
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(3000);
    http.setUserAgent("stagefright/1.2 (Linux;Android 5.0)");
    http.addHeader("Accept", "*/*");
    http.addHeader("Accept-Language", "ko-KR,ko;q=0.9,en-US;q=0.8,en;q=0.7");
    http.addHeader("Connection", "close");
    http.addHeader("Referer", "https://translate.google.com/");
    return this->open(url);
  }

  virtual bool close() override {
    WiFiClient* client = http.getStreamPtr();
    if (client) {
      int sock = client->fd();
      if (sock >= 0) {
        struct linger so_linger;
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;
        setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
      }
    }
    return AudioFileSourceHTTPStream::close();
  }
};

class AudioOutputM5Speaker : public AudioOutput
{
  public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0)
    {
      _m5sound = m5sound;
      _virtual_ch = virtual_sound_channel;
      _buf_index = 0;
      _current_buf = 0;
    }
    virtual ~AudioOutputM5Speaker(void)
    {
      stop();
    }
    virtual bool begin(void) override
    {
      _buf_index = 0;
      return true;
    }
    virtual bool ConsumeSample(int16_t sample[2]) override
    {
      _local_buf[_current_buf][_buf_index++] = sample[0];
      if (_buf_index >= buf_size) flush();
      return true; 
    }
    virtual void flush(void) override
    {
      if (_buf_index > 0)
      {
        unsigned long flushStart = millis();
        while (_m5sound->isPlaying(_virtual_ch) == 2)
        {
          if (millis() - flushStart > 100) break;
          delay(1);
        }
        _m5sound->playRaw(_local_buf[_current_buf], _buf_index, hertz, false, 1, _virtual_ch, false);
        _buf_index = 0;
        _current_buf = 1 - _current_buf;
      }
    }
    virtual bool stop(void) override
    {
      flush();
      _m5sound->stop(_virtual_ch);
      return true;
    }

  protected:
    m5::Speaker_Class* _m5sound;
    uint8_t _virtual_ch;
    static constexpr size_t buf_size = 2048; 
    int16_t _local_buf[2][buf_size]; 
    size_t _buf_index = 0;
    uint8_t _current_buf = 0;
};

QueueHandle_t ttsQueue = NULL;
volatile bool ttsCancelFlag = false;

String extractEnglishText(const String& input) {
  int slashIdx = input.indexOf('/');
  if (slashIdx != -1) {
    String eng = input.substring(0, slashIdx);
    eng.trim();
    return eng;
  }
  return input;
}

String urlEncode(const String& str) {
  String encodedString = "";
  char c, code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

void ttsPlayTask(void* pvParameters) {
  AudioOutputM5Speaker* out = new AudioOutputM5Speaker(&M5.Speaker, 2);
  static uint8_t mp3PreallocBuf[AudioGeneratorMP3::preAllocSize()];
  AudioGeneratorMP3* mp3 = new AudioGeneratorMP3(mp3PreallocBuf, sizeof(mp3PreallocBuf));

  String currentWord = "";

  while (true) {
    if (currentWord == "") {
      delay(50);
      setAmplifier(false);
      char buffer[512];
      if (xQueueReceive(ttsQueue, buffer, portMAX_DELAY) == pdTRUE) {
        currentWord = String(buffer);
      }
    }

    char nextBuffer[512];
    // 사용자 요청: "바로 단어를 읽어주게 해줘" -> 딜레이를 50ms로 최소화 (즉시 재생)
    if (xQueueReceive(ttsQueue, nextBuffer, 50 / portTICK_PERIOD_MS) == pdTRUE) {
      currentWord = String(nextBuffer);
      continue;
    }

    if (!soundEnabled) {
      currentWord = "";
      continue;
    }

    String engText = extractEnglishText(currentWord);
    currentWord = "";
    if (engText.length() == 0) continue;

    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 20000) continue;

    ttsCancelFlag = false;
    setAmplifier(true);
    
    // Google TTS
    String encoded = urlEncode(engText);
    String url = "http://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=en&q=" + encoded;

    AudioFileSourceHTTPStreamUA* file = new AudioFileSourceHTTPStreamUA();
    if (!file) continue;
    file->SetReconnect(1, 200);

    if (file->openWithHeaders(url.c_str())) {
      if (!ttsCancelFlag && mp3->begin(file, out)) {
        unsigned long ttsStartTime = millis();
        while (mp3->isRunning() && !ttsCancelFlag) {
          if (millis() - ttsStartTime > 15000) break;
          if (!mp3->loop()) break;
          delay(1);
        }
        mp3->stop();
      }
      file->close();
    }
    delete file;
  }
}

void speakWordTTS(const String& word) {
  if (WiFi.status() != WL_CONNECTED || !soundEnabled) return;
  ttsCancelFlag = true; // 현재 재생 중단
  if (ttsQueue != NULL) {
    xQueueReset(ttsQueue);
    char buffer[512];
    strncpy(buffer, word.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    xQueueSend(ttsQueue, buffer, 0);
  }
}
// --- END TTS ENGINE ---

"""
lines.insert(setup_idx, tts_code)

# 3. Add task initialization in setup()
setup_body_idx = setup_idx + 2
setup_init = """
  // TTS Queue & Task Init
  ttsQueue = xQueueCreate(1, 512);
  xTaskCreatePinnedToCore(
      ttsPlayTask,
      "TTS_Task",
      8192,
      NULL,
      1,
      NULL,
      0
  );
"""
lines.insert(setup_body_idx, setup_init)

# 4. Insert speakWordTTS(word) into updateWordDisplay
# Find "void updateWordDisplay(const String &word) {"
upd_idx = -1
for i, line in enumerate(lines):
    if line.startswith("void updateWordDisplay(const String &word) {"):
        upd_idx = i
        break

if upd_idx != -1:
    lines.insert(upd_idx + 1, "  speakWordTTS(word);\n")

with open("src/main.cpp", "w") as f:
    f.writelines(lines)

print("Patch applied.")
