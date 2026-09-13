#include "wordbook.h"
#include "web_server_manager.h"
Wordbook wordbook;

Wordbook::Wordbook() {
  wordCount = 0;
  currentIndex = 0;
  groupCount = 0;

  // 동적 할당
  words = new String[MAX_WORDS];
  wordGroupIds = new int[MAX_WORDS];

  // Initialize all group IDs to -1
  for (int i = 0; i < MAX_WORDS; i++) {
    wordGroupIds[i] = -1;
  }
}

Wordbook::~Wordbook() {
  // Clean up
  delete[] words;
  delete[] wordGroupIds;
}

bool Wordbook::begin() {
  // Add delay for Preferences initialization to complete
  delay(100);
  
  // Use safe prefsBegin to ensure NVS is initialized and recovered if needed
  prefsBegin(preferences, "wordbook", false);
  delay(50);  // Additional safety delay
  
  loadWords();

  // Initialize currentIndex if wordCount is 0
  if (wordCount == 0) {
    currentIndex = 0;
  } else if (currentIndex >= wordCount) {
    currentIndex = wordCount - 1;
  }

  return true;
}

bool Wordbook::addWord(String word, int groupId) {
  if (wordCount < MAX_WORDS) {
    // Limit word length to prevent excessive memory usage
    // Use UTF-8 safe truncation to avoid cutting multi-byte characters (Korean)
    if (word.length() > WORD_LENGTH) {
      int safeLen = WORD_LENGTH;
      const char* s = word.c_str();
      // Walk back to find a valid UTF-8 character boundary
      while (safeLen > 0 && (s[safeLen] & 0xC0) == 0x80) {
        safeLen--;  // Skip continuation bytes (10xxxxxx)
      }
      word = word.substring(0, safeLen);
    }

    words[wordCount] = word;

    // If groupId is -1, create a new group
    if (groupId == -1) {
      wordGroupIds[wordCount] = groupCount;
      groupCount++;
    } else {
      wordGroupIds[wordCount] = groupId;

      // Update groupCount if this groupId is new
      if (groupId >= groupCount) {
        groupCount = groupId + 1;
      }
    }

    wordCount++;
    return saveWords();
  }
  return false; // Maximum word count reached
}

bool Wordbook::addWordFast(String word, int groupId) {
  if (wordCount < MAX_WORDS) {
    // Limit word length to prevent excessive memory usage
    // Use UTF-8 safe truncation to avoid cutting multi-byte characters (Korean)
    if (word.length() > WORD_LENGTH) {
      int safeLen = WORD_LENGTH;
      const char* s = word.c_str();
      // Walk back to find a valid UTF-8 character boundary
      while (safeLen > 0 && (s[safeLen] & 0xC0) == 0x80) {
        safeLen--;  // Skip continuation bytes (10xxxxxx)
      }
      word = word.substring(0, safeLen);
    }

    words[wordCount] = word;

    // If groupId is -1, create a new group
    if (groupId == -1) {
      wordGroupIds[wordCount] = groupCount;
      groupCount++;
    } else {
      wordGroupIds[wordCount] = groupId;

      // Update groupCount if this groupId is new
      if (groupId >= groupCount) {
        groupCount = groupId + 1;
      }
    }

    wordCount++;
    return true; // Don't save yet
  }
  return false; // Maximum word count reached
}

bool Wordbook::saveWords() {
  // Save safely by updating only what changed
  preferences.putInt("count", wordCount);
  preferences.putInt("groupCount", groupCount);

  for (int i = 0; i < wordCount; i++) {
    String key = "word" + String(i);
    preferences.putString(key.c_str(), words[i]);

    String groupKey = "group" + String(i);
    preferences.putInt(groupKey.c_str(), wordGroupIds[i]);
  }

  preferences.putInt("current_index", currentIndex);
  return true;
}

bool Wordbook::loadWords() {
  wordCount = preferences.getInt("count", 0);
  groupCount = preferences.getInt("groupCount", 0);

  for (int i = 0; i < wordCount; i++) {
    String key = "word" + String(i);
    words[i] = preferences.getString(key.c_str(), "");

    String groupKey = "group" + String(i);
    wordGroupIds[i] = preferences.getInt(groupKey.c_str(), -1);
  }

  if (wordCount > 0 && currentIndex >= wordCount) {
    currentIndex = wordCount - 1;
  } else if (wordCount == 0) {
    currentIndex = 0;
  }

  return true;
}

String Wordbook::getCurrentWord() {
  if (wordCount > 0 && currentIndex >= 0 && currentIndex < wordCount) {
    return words[currentIndex];
  }
  return "";
}

String Wordbook::getNextWord() {
  if (wordCount > 0) {
    currentIndex++;
    if (currentIndex >= wordCount) {
      currentIndex = 0; // Loop back to first word
    }
    return words[currentIndex];
  }
  return "";
}

String Wordbook::getPrevWord() {
  if (wordCount > 0) {
    currentIndex--;
    if (currentIndex < 0) {
      currentIndex = wordCount - 1; // Loop to last word
    }
    return words[currentIndex];
  }
  return "";
}

int Wordbook::getWordCount() { return wordCount; }

int Wordbook::getMaxWordCount() { return MAX_WORDS; }

int Wordbook::getCurrentIndex() { return currentIndex; }

int Wordbook::getStoragePercentage() {
  if (MAX_WORDS == 0) {
    return 0;
  }
  return (wordCount * 100) / MAX_WORDS;
}

void Wordbook::setCurrentIndex(int index) {
  if (index >= 0 && index < wordCount) {
    currentIndex = index;
  }
}

bool Wordbook::saveCurrentIndex() {
  preferences.putInt("current_index", currentIndex);
  return true;
}

bool Wordbook::loadCurrentIndex() {
  currentIndex = preferences.getInt("current_index", 0);

  // Ensure currentIndex is within valid range
  if (wordCount > 0) {
    if (currentIndex >= wordCount) {
      currentIndex = wordCount - 1;
    } else if (currentIndex < 0) {
      currentIndex = 0;
    }
  } else {
    currentIndex = 0;
  }

  return true;
}

bool Wordbook::removeWord(int index) {
  // Validate index
  if (index < 0 || index >= wordCount) {
    return false;
  }

  // Shift all words after the removed word one position to the left
  for (int i = index; i < wordCount - 1; i++) {
    words[i] = words[i + 1];
  }

  // Decrease word count
  wordCount--;

  // Adjust current index if needed
  if (currentIndex >= wordCount) {
    currentIndex = wordCount - 1;
  }

  // Save updated words to preferences
  return saveWords();
}

bool Wordbook::clearAllWords() {
  // Clear all words
  wordCount = 0;
  currentIndex = 0;
  groupCount = 0;

  // Clear all group IDs
  for (int i = 0; i < MAX_WORDS; i++) {
    wordGroupIds[i] = -1;
  }

  // Clear all data in preferences
  preferences.clear();

  // Save empty state
  preferences.putInt("count", 0);
  preferences.putInt("current_index", 0);
  preferences.putInt("groupCount", 0);

  return true;
}

int Wordbook::getGroupCount() { return groupCount; }

int Wordbook::getCurrentGroupIndex() {
  if (wordCount == 0 || currentIndex < 0 || currentIndex >= wordCount) {
    return 0;
  }
  // Return the group ID + 1 (1-based index)
  return wordGroupIds[currentIndex] + 1;
}

String Wordbook::getWord(int index) {
  if (index >= 0 && index < wordCount) {
    return words[index];
  }
  return "";
}
