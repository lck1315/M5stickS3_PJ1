#ifndef WORDBOOK_H
#define WORDBOOK_H

#include <Arduino.h>
#include <Preferences.h>

#define MAX_WORDS 100 // Maximum number of words that can be stored
#define WORD_LENGTH                                                            \
  192 // Korean UTF-8: 1 char = 3 bytes, so 192 bytes ≈ 64 Korean chars

class Wordbook {
private:
  Preferences preferences;
  String *words;
  int wordCount;
  int currentIndex;
  int *wordGroupIds; // Group ID for each word
  int groupCount;    // Total number of groups

public:
  Wordbook();
  ~Wordbook();

  bool begin();
  bool addWord(String word, int groupId = -1);
  bool addWordFast(String word,
                   int groupId = -1); // Add word without saving (faster)
  bool removeWord(int index);
  bool clearAllWords();
  bool saveWords();
  bool loadWords();

  String getCurrentWord();
  String getNextWord();
  String getPrevWord();
  String getWord(int index);
  int getWordCount();
  int getGroupCount();        // Get number of word groups
  int getCurrentGroupIndex(); // Get current group index (1-based)
  int getMaxWordCount();
  int getStoragePercentage();
  int getCurrentIndex();
  void setCurrentIndex(int index);
  bool saveCurrentIndex();
  bool loadCurrentIndex();
};

extern Wordbook wordbook;

#endif
