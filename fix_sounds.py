import re

with open("src/main.cpp", "r") as f:
    content = f.read()

# Add sound helper functions after playTone
sound_helpers = """
// 동적 노이즈 제어: 소리가 출력될 때만 앰프를 활성화합니다.
void playTone(uint32_t freq, uint32_t dur) {
  if (freq == 0 || dur == 0) return;
  pm1.gpioSetOutput(M5PM1_GPIO_NUM_3, true);
  delay(10);
  M5.Speaker.tone(freq, dur);
  unsigned long start = millis();
  while(millis() - start < dur) {
    M5.update();
    delay(1);
  }
  M5.Speaker.stop();
  pm1.gpioSetOutput(M5PM1_GPIO_NUM_3, false);
}

void soundSuccess() { playTone(880, 100); delay(30); playTone(1318, 150); }
void soundError() { playTone(330, 150); delay(30); playTone(220, 200); }
void soundNext() { playTone(1046, 50); delay(20); playTone(1318, 100); }
void soundPrev() { playTone(1318, 50); delay(20); playTone(1046, 100); }
void soundBTOn() { playTone(523, 100); delay(20); playTone(659, 100); delay(20); playTone(784, 100); delay(20); playTone(1046, 200); }
void soundBTOff() { playTone(1046, 100); delay(20); playTone(784, 100); delay(20); playTone(659, 100); delay(20); playTone(523, 200); }
void soundAutoStart() { playTone(784, 100); delay(30); playTone(1046, 100); delay(30); playTone(1568, 150); }
void soundAutoStop() { playTone(1568, 100); delay(30); playTone(1046, 100); delay(30); playTone(784, 150); }
void soundDelete() { playTone(622, 100); delay(30); playTone(466, 150); }
void soundWake() { playTone(523, 100); delay(30); playTone(1046, 150); }
void soundBeep() { playTone(1046, 50); }
"""

# Replace playTone definition
content = re.sub(r'// 동적 노이즈 제어:.*?pm1\.gpioSetOutput\(M5PM1_GPIO_NUM_3, false\);\n}', sound_helpers, content, flags=re.DOTALL)

# Replace BT Off tones
content = re.sub(r'playTone\(1500, 150\);\s*delay\(200\);\s*playTone\(1000, 150\);\s*delay\(200\);\s*playTone\(500, 150\);', 'soundBTOff();', content)
# Replace BT On tones
content = re.sub(r'playTone\(500, 150\);\s*delay\(200\);\s*playTone\(1000, 150\);\s*delay\(200\);\s*playTone\(1500, 150\);', 'soundBTOn();', content)

# Replace Auto Stop tones
content = re.sub(r'playTone\(1000, 100\);\s*delay\(150\);\s*playTone\(1000, 100\);', 'soundAutoStop();', content)
# Replace Auto Start tones
content = re.sub(r'playTone\(1200, 300\);', 'soundAutoStart();', content)

# Replace other specific playTones
content = content.replace('playTone(1200, 100);', 'soundNext();')
content = content.replace('playTone(800, 100);', 'soundPrev();')
content = content.replace('playTone(200, 200);', 'soundError();')
content = content.replace('playTone(1000, 50);', 'soundSuccess();')
content = content.replace('playTone(1000, 100);', 'soundSuccess();')
content = content.replace('playTone(1500, 100);', 'soundSuccess();')
content = content.replace('playTone(600, 200);', 'soundDelete();')
content = content.replace('playTone(800, 50);', 'soundBeep();')

with open("src/main.cpp", "w") as f:
    f.write(content)
