import sys
import re

with open("src/main.cpp", "r") as f:
    text = f.read()

# 1. Add forward declaration for speakWordTTS if missing
if "void speakWordTTS(const String& word);" not in text:
    text = text.replace("void updateWordDisplay(const String &word) {", "void speakWordTTS(const String& word);\n\nvoid updateWordDisplay(const String &word) {")

# 2. Fix extractEnglishText to filter Korean
old_extract = """String extractEnglishText(const String& input) {
  int slashIdx = input.indexOf('/');
  if (slashIdx != -1) {
    return input.substring(0, slashIdx);
  }
  return input; // No slash, return as is
}"""

new_extract = """String extractEnglishText(const String& input) {
  int slashIdx = input.indexOf('/');
  if (slashIdx != -1) {
    String eng = input.substring(0, slashIdx);
    eng.trim();
    return eng;
  }
  
  for (int i = 0; i < input.length(); i++) {
    if ((unsigned char)input[i] > 127) {
      return "";
    }
  }
  return input; 
}"""
text = text.replace(old_extract, new_extract)

# 3. Rewrite ttsPlayTask completely to ensure it's correct
old_task = re.search(r"void ttsPlayTask\(void\* pvParameters\).*?delete file;\n  }\n}", text, flags=re.DOTALL)
if old_task:
    new_task = """void ttsPlayTask(void* pvParameters) {
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
    if (xQueueReceive(ttsQueue, nextBuffer, 200 / portTICK_PERIOD_MS) == pdTRUE) {
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
    
    String encoded = urlEncode(engText);
    String url = "http://dict.youdao.com/dictvoice?type=2&audio=" + encoded;

    AudioFileSourceHTTPStreamUA* file = new AudioFileSourceHTTPStreamUA();
    if (!file) continue;
    file->SetReconnect(1, 200);

    if (file->open(url.c_str())) {
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
}"""
    text = text[:old_task.start()] + new_task + text[old_task.end():]

# 4. Remove SO_LINGER from AudioFileSourceHTTPStreamUA
so_linger_code = re.search(r"  virtual bool close\(\) override \{.*?return AudioFileSourceHTTPStream::close\(\);\n  \}", text, flags=re.DOTALL)
if so_linger_code:
    text = text[:so_linger_code.start()] + text[so_linger_code.end():]

# 5. Fix buffer null termination
text = text.replace("buffer[sizeof(buffer) - 1] = ' ';", "buffer[sizeof(buffer) - 1] = '\\0';")

with open("src/main.cpp", "w") as f:
    f.write(text)

