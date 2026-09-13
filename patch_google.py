import sys

with open("src/main.cpp", "r") as f:
    text = f.read()

text = text.replace(
    'String url = "http://dict.youdao.com/dictvoice?type=2&audio=" + encoded;',
    'String url = "http://translate.google.com/translate_tts?ie=UTF-8&client=gtx&tl=en&q=" + encoded;'
)

text = text.replace(
    'if (file->open(url.c_str())) {',
    'if (file->openWithHeaders(url.c_str())) {'
)

with open("src/main.cpp", "w") as f:
    f.write(text)

