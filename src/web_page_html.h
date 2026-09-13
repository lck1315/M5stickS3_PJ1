#ifndef WEB_PAGE_HTML_H
#define WEB_PAGE_HTML_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ko">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>M5Stick Wordbook Manager</title>
    <style>
        :root {
            --bg-color: #0f172a;
            --card-bg: rgba(30, 41, 59, 0.7);
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --accent: #3b82f6;
            --accent-hover: #2563eb;
            --danger: #ef4444;
            --danger-hover: #dc2626;
            --border: rgba(255, 255, 255, 0.1);
        }
        body {
            font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            margin: 0;
            padding: 20px;
            display: flex;
            justify-content: center;
            min-height: 100vh;
        }
        .container {
            max-width: 600px;
            width: 100%;
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        h1 {
            font-weight: 600;
            margin: 0 0 10px 0;
            background: linear-gradient(to right, #60a5fa, #a78bfa);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .glass-panel {
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border: 1px solid var(--border);
            border-radius: 16px;
            padding: 24px;
            margin-bottom: 24px;
            box-shadow: 0 4px 30px rgba(0, 0, 0, 0.1);
        }
        .input-group {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
        }
        input[type="text"] {
            flex: 1;
            padding: 12px 16px;
            border-radius: 8px;
            border: 1px solid var(--border);
            background: rgba(15, 23, 42, 0.6);
            color: var(--text-main);
            font-size: 16px;
            outline: none;
            transition: border-color 0.3s;
        }
        input[type="text"]::placeholder {
            color: var(--text-muted);
        }
        input[type="text"]:focus {
            border-color: var(--accent);
        }
        button {
            padding: 12px 20px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            color: white;
        }
        .btn-primary {
            background-color: var(--accent);
        }
        .btn-primary:hover {
            background-color: var(--accent-hover);
        }
        .btn-danger {
            background-color: var(--danger);
            padding: 8px 12px;
            font-size: 14px;
        }
        .btn-danger:hover {
            background-color: var(--danger-hover);
        }
        .word-list {
            list-style: none;
            padding: 0;
            margin: 0;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }
        .word-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 16px;
            background: rgba(255, 255, 255, 0.05);
            border-radius: 8px;
            border: 1px solid rgba(255,255,255,0.05);
            transition: transform 0.2s, background 0.2s;
        }
        .word-item:hover {
            background: rgba(255, 255, 255, 0.08);
            transform: translateY(-2px);
        }
        .word-text {
            font-size: 18px;
            word-break: break-word;
        }
        .empty-state {
            text-align: center;
            color: var(--text-muted);
            padding: 20px 0;
        }
        .stats {
            display: flex;
            justify-content: space-between;
            color: var(--text-muted);
            font-size: 14px;
            margin-bottom: 15px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <a href="https://lck1315.github.io/work.html" target="_blank" style="text-decoration: none; display: inline-block; margin-bottom: 15px; transition: transform 0.2s;" onmouseover="this.style.transform='scale(1.05)'" onmouseout="this.style.transform='scale(1)'">
                <span style="font-size: 2.5rem; font-weight: 800; color: #f8fafc; letter-spacing: -1px;">DODO</span><span style="font-size: 2.5rem; font-weight: 800; color: #818cf8; letter-spacing: -1px;">.work</span>
            </a>
            <h1>단어장 관리</h1>
            <p style="color: var(--text-muted); margin: 0;">M5StickS3 단어 추가 및 삭제</p>
        </div>

        <div class="glass-panel">
            <form id="addForm" onsubmit="addWord(event)">
                <div class="input-group" style="flex-direction: column;">
                    <textarea id="wordInput" placeholder="사과/apple 처럼 여러 줄을 입력하세요" rows="4" style="padding: 12px; border-radius: 8px; border: 1px solid var(--border); background: rgba(15, 23, 42, 0.6); color: var(--text-main); font-size: 16px; resize: vertical;" required></textarea>
                    <button type="submit" class="btn-primary" style="width: 100%;">단어 추가하기</button>
                </div>
            </form>
        </div>

        <div class="glass-panel">
            <div class="stats">
                <span>저장된 단어: <strong id="wordCount">0</strong>개</span>
                <span>(최대 100개)</span>
            </div>
            <ul class="word-list" id="wordList">
                <div class="empty-state">로딩 중...</div>
            </ul>
        </div>
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', loadWords);

        async function loadWords() {
            try {
                const response = await fetch('/api/words');
                if (!response.ok) throw new Error('Network response was not ok');
                const data = await response.json();
                
                document.getElementById('wordCount').textContent = data.count;
                const list = document.getElementById('wordList');
                list.innerHTML = '';

                if (data.count === 0) {
                    list.innerHTML = '<div class="empty-state">저장된 단어가 없습니다.</div>';
                    return;
                }

                data.words.forEach((item) => {
                    const li = document.createElement('li');
                    li.className = 'word-item';
                    
                    const span = document.createElement('span');
                    span.className = 'word-text';
                    span.textContent = item.word;
                    
                    const btnGroup = document.createElement('div');
                    btnGroup.style.display = 'flex';
                    btnGroup.style.gap = '8px';
                    
                    const speakBtn = document.createElement('button');
                    speakBtn.className = 'btn-primary';
                    speakBtn.style.padding = '8px 12px';
                    speakBtn.style.fontSize = '14px';
                    speakBtn.innerHTML = '🔊 듣기';
                    speakBtn.title = '발음 듣기';
                    speakBtn.onclick = () => speakWord(item.word);
                    
                    const delBtn = document.createElement('button');
                    delBtn.className = 'btn-danger';
                    delBtn.textContent = '삭제';
                    delBtn.onclick = () => deleteWord(item.index);

                    btnGroup.appendChild(speakBtn);
                    btnGroup.appendChild(delBtn);

                    li.appendChild(span);
                    li.appendChild(btnGroup);
                    list.appendChild(li);
                });
            } catch (error) {
                console.error('Error loading words:', error);
                document.getElementById('wordList').innerHTML = '<div class="empty-state" style="color:var(--danger)">데이터를 불러오는데 실패했습니다.</div>';
            }
        }

        let speakTimeout = null;

        function speakWord(text) {
            if (!window.speechSynthesis) {
                alert('이 브라우저는 음성 합성(TTS)을 지원하지 않습니다.');
                return;
            }
            
            // 사용자가 버튼을 다다닥 연속으로 누를 때 브라우저 엔진이 뻗는 현상 방지
            if (speakTimeout) clearTimeout(speakTimeout);
            
            // 이전 재생 즉시 중단
            window.speechSynthesis.cancel();
            
            // 0.3초(300ms) 대기 후 재생 (너무 빠르게 요청이 쌓여 먹통이 되는 버그 완벽 해결)
            speakTimeout = setTimeout(() => {
                // 단어가 '영어/한글' 형식인 경우 분리하여 스마트하게 재생
                if (text.includes('/')) {
                    const parts = text.split('/');
                    const englishPart = parts[0].trim();
                    const koreanPart = parts[1].trim();
                    
                    const utteranceEng = new SpeechSynthesisUtterance(englishPart);
                    utteranceEng.lang = 'en-US';
                    utteranceEng.rate = 0.85; // 발음을 또박또박 듣기 편하게 살짝 느리게
                    
                    const utteranceKor = new SpeechSynthesisUtterance(koreanPart);
                    utteranceKor.lang = 'ko-KR';
                    utteranceKor.rate = 1.0;
                    
                    window.speechSynthesis.speak(utteranceEng);
                    window.speechSynthesis.speak(utteranceKor);
                } else {
                    // 한글 포함 여부에 따라 언어 동적 결정
                    const utterance = new SpeechSynthesisUtterance(text);
                    const hasKorean = /[ㄱ-ㅎ|ㅏ-ㅣ|가-힣]/.test(text);
                    utterance.lang = hasKorean ? 'ko-KR' : 'en-US';
                    utterance.rate = hasKorean ? 1.0 : 0.85;
                    window.speechSynthesis.speak(utterance);
                }
            }, 300);
        }

        async function addWord(event) {
            event.preventDefault();
            const input = document.getElementById('wordInput');
            const text = input.value.trim();
            if (!text) return;

            const wordsToAdd = text.split(/\r?\n/).map(w => w.trim()).filter(w => w.length > 0);
            if (wordsToAdd.length === 0) return;

            try {
                let successCount = 0;
                for (const word of wordsToAdd) {
                    const response = await fetch('/api/words', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/x-www-form-urlencoded',
                        },
                        body: `word=${encodeURIComponent(word)}`
                    });
                    if (response.ok) successCount++;
                }
                
                if (successCount > 0) {
                    input.value = '';
                    loadWords();
                    if (wordsToAdd.length > 1) {
                        alert(`총 ${wordsToAdd.length}개 중 ${successCount}개의 단어가 추가되었습니다.`);
                    }
                } else {
                    alert('단어 추가에 실패했습니다. (단어가 꽉 찼을 수 있습니다)');
                }
            } catch (error) {
                console.error('Error adding word:', error);
                alert('오류가 발생했습니다.');
            }
        }

        async function deleteWord(index) {
            if (!confirm('정말 삭제하시겠습니까?')) return;
            
            try {
                const response = await fetch(`/api/words?id=${index}`, {
                    method: 'DELETE'
                });
                
                if (response.ok) {
                    loadWords();
                } else {
                    alert('삭제에 실패했습니다.');
                }
            } catch (error) {
                console.error('Error deleting word:', error);
                alert('오류가 발생했습니다.');
            }
        }
    </script>
</body>
</html>
)rawliteral";

#endif
