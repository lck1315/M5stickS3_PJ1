#ifndef WIFI_SETUP_HTML_H
#define WIFI_SETUP_HTML_H

#include <Arduino.h>

const char WIFI_SETUP_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="ko">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>M5Stick 스마트 와이파이 설정</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0f172a;
            --text-color: #f1f5f9;
            --primary-color: #38bdf8;
            --card-bg: rgba(30, 41, 59, 0.7);
            --card-border: rgba(255, 255, 255, 0.1);
        }
        body {
            font-family: 'Inter', sans-serif;
            background: var(--bg-color);
            color: var(--text-color);
            margin: 0;
            padding: 20px;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container {
            width: 100%;
            max-width: 400px;
            margin-top: 20px;
        }
        .glass-card {
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 25px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.5);
            text-align: center;
        }
        h1 {
            font-size: 1.5rem;
            margin-top: 0;
            margin-bottom: 20px;
            color: var(--primary-color);
        }
        button {
            width: 100%;
            padding: 12px;
            background: var(--primary-color);
            color: #000;
            border: none;
            border-radius: 12px;
            font-size: 1rem;
            font-weight: bold;
            cursor: pointer;
            transition: transform 0.2s;
            margin-bottom: 15px;
        }
        button:active { transform: scale(0.95); }
        .network-list {
            list-style: none;
            padding: 0;
            margin: 0;
            max-height: 250px;
            overflow-y: auto;
            border-radius: 10px;
        }
        .network-item {
            background: rgba(255,255,255,0.05);
            margin-bottom: 8px;
            padding: 15px;
            border-radius: 12px;
            display: flex;
            justify-content: space-between;
            cursor: pointer;
            transition: background 0.2s;
        }
        .network-item:hover { background: rgba(56, 189, 248, 0.2); }
        .network-item.selected { background: rgba(56, 189, 248, 0.4); border: 1px solid var(--primary-color); }
        
        .input-group {
            margin-top: 20px;
            display: none;
            flex-direction: column;
            gap: 10px;
            text-align: left;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            border-radius: 12px;
            border: 1px solid var(--card-border);
            background: rgba(0,0,0,0.3);
            color: white;
            box-sizing: border-box;
            font-size: 1rem;
        }
        .loader {
            display: none;
            margin: 20px auto;
            border: 4px solid rgba(255,255,255,0.1);
            border-top: 4px solid var(--primary-color);
            border-radius: 50%;
            width: 30px;
            height: 30px;
            animation: spin 1s linear infinite;
        }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container glass-card">
        <a href="https://lck1315.github.io/work.html" target="_blank" style="text-decoration: none; display: inline-block; margin-bottom: 20px; transition: transform 0.2s;" onmouseover="this.style.transform='scale(1.05)'" onmouseout="this.style.transform='scale(1)'">
            <span style="font-size: 2.5rem; font-weight: 800; color: #f8fafc; letter-spacing: -1px;">DODO</span><span style="font-size: 2.5rem; font-weight: 800; color: #818cf8; letter-spacing: -1px;">.work</span>
        </a>
        <h1>📶 Wi-Fi 설정</h1>
        <p style="font-size: 0.9rem; color: #94a3b8; margin-bottom: 20px;">M5Stick을 연결할 와이파이를 선택하세요.</p>
        
        <button id="btn-scan" onclick="scanNetworks()">와이파이 검색하기</button>
        <div id="loader" class="loader"></div>
        
        <ul id="network-list" class="network-list"></ul>
        
        <h2 style="font-size: 1.1rem; color: var(--primary-color); margin-top: 20px; text-align: left; width: 100%;">저장된 와이파이 목록</h2>
        <ul id="saved-list" class="network-list">
            <li style="text-align:center; padding: 15px; color:#94a3b8;">불러오는 중...</li>
        </ul>

        <div id="setup-form" class="input-group">
            <label style="font-size: 0.85rem; color: #94a3b8;">선택된 와이파이 (SSID)</label>
            <input type="text" id="ssid" readonly>
            <label style="font-size: 0.85rem; color: #94a3b8; margin-top: 5px;">비밀번호</label>
            <input type="password" id="password" placeholder="비밀번호를 입력하세요">
            <button onclick="connectWiFi()" style="margin-top: 10px;">저장 및 연결하기</button>
        </div>
        
        <div class="glass-card" style="margin-top: 20px; width: 100%; box-sizing: border-box;">
            <h2 style="font-size: 1.1rem; color: var(--primary-color); margin-top: 0; text-align: left;">🔥 Firebase 설정</h2>
            <p style="font-size: 0.85rem; color: #94a3b8; text-align: left; margin-bottom: 15px;">단어장 연동을 위한 Firebase URL을 별도로 저장합니다.</p>
            <div style="display: flex; flex-direction: column; gap: 10px; text-align: left;">
                <input type="text" id="firebase_url_only" placeholder="https://your-project.firebaseio.com" style="width: 100%; padding: 12px; border-radius: 12px; border: 1px solid var(--card-border); background: rgba(0,0,0,0.3); color: white; box-sizing: border-box; font-size: 1rem;">
                <button onclick="saveFirebaseUrl()" style="margin-top: 10px; margin-bottom: 0;">URL 저장하기</button>
            </div>
        </div>
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', () => {
            loadSavedNetworks();
            loadFirebaseUrl();
            scanNetworks(); // 페이지 로드 시 즉시 와이파이 검색 시작
        });

        async function loadFirebaseUrl() {
            try {
                const res = await fetch('/api/firebase/get');
                const data = await res.json();
                if(data.url) {
                    document.getElementById('firebase_url_only').value = data.url;
                }
            } catch(e) {}
        }

        async function saveFirebaseUrl() {
            const url = document.getElementById('firebase_url_only').value;
            const btn = document.querySelector('button[onclick="saveFirebaseUrl()"]');
            const originalText = btn.innerText;
            btn.innerText = '저장 중...';
            btn.disabled = true;

            try {
                const res = await fetch('/api/firebase/save', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: `firebase_url=${encodeURIComponent(url)}`
                });
                if(res.ok) {
                    alert('Firebase URL이 저장되었습니다.');
                } else {
                    alert('저장 실패');
                }
            } catch(e) {
                alert('오류 발생');
            }
            btn.innerText = originalText;
            btn.disabled = false;
        }

        async function loadSavedNetworks() {
            try {
                const response = await fetch('/api/wifi/saved');
                const saved = await response.json();
                const list = document.getElementById('saved-list');
                list.innerHTML = '';
                
                if(saved.length === 0) {
                    list.innerHTML = '<li style="text-align:center; padding: 15px; color:#94a3b8;">저장된 와이파이가 없습니다.</li>';
                    return;
                }

                saved.forEach(net => {
                    const li = document.createElement('li');
                    li.className = 'network-item';
                    li.innerHTML = `<span>${net.ssid}</span> <button onclick="deleteSavedNetwork(${net.index})" style="background:var(--danger, #ef4444); padding: 4px 8px; font-size: 0.8rem; margin: 0; width: auto;">삭제</button>`;
                    list.appendChild(li);
                });
            } catch(e) {
                document.getElementById('saved-list').innerHTML = '<li style="text-align:center; padding: 15px; color:#ef4444;">불러오기 실패</li>';
            }
        }

        async function deleteSavedNetwork(index) {
            if(!confirm('이 와이파이를 삭제하시겠습니까?')) return;
            try {
                const res = await fetch(`/api/wifi/saved?index=${index}`, {method: 'DELETE'});
                if(res.ok) {
                    loadSavedNetworks();
                } else {
                    alert('삭제 실패');
                }
            } catch(e) {
                alert('오류 발생');
            }
        }

        async function scanNetworks() {
            document.getElementById('btn-scan').style.display = 'none';
            document.getElementById('loader').style.display = 'block';
            document.getElementById('network-list').innerHTML = '';
            document.getElementById('setup-form').style.display = 'none';

            try {
                const response = await fetch('/api/wifi/scan');
                const networks = await response.json();
                
                document.getElementById('loader').style.display = 'none';
                document.getElementById('btn-scan').style.display = 'block';
                document.getElementById('btn-scan').innerText = '다시 검색하기';
                
                const list = document.getElementById('network-list');
                if(networks.length === 0) {
                    list.innerHTML = '<li style="text-align:center; padding: 15px; color:#94a3b8;">검색된 와이파이가 없습니다.</li>';
                    return;
                }

                networks.forEach(net => {
                    const li = document.createElement('li');
                    li.className = 'network-item';
                    
                    // 신호 강도 이모지
                    let rssiIcon = '📶';
                    if (net.rssi < -80) rssiIcon = '📉';
                    else if (net.rssi < -60) rssiIcon = '📊';

                    li.innerHTML = `<span>${net.ssid}</span> <span>${rssiIcon}</span>`;
                    li.onclick = () => selectNetwork(li, net.ssid);
                    list.appendChild(li);
                });
            } catch(e) {
                alert('와이파이 검색에 실패했습니다.');
                document.getElementById('loader').style.display = 'none';
                document.getElementById('btn-scan').style.display = 'block';
            }
        }

        function selectNetwork(element, ssid) {
            document.querySelectorAll('.network-item').forEach(el => el.classList.remove('selected'));
            element.classList.add('selected');
            document.getElementById('ssid').value = ssid;
            document.getElementById('setup-form').style.display = 'flex';
            document.getElementById('password').focus();
        }

        async function connectWiFi() {
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            
            if(!ssid) return alert("와이파이를 선택해주세요.");

            const btn = document.querySelector('#setup-form button');
            btn.innerText = '저장 및 연결 중...';
            btn.disabled = true;

            try {
                const res = await fetch('/api/wifi/connect', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
                });
                
                if(res.ok) {
                    alert('와이파이 설정이 저장되었습니다! 기기가 재시작(또는 와이파이 재연결) 됩니다.');
                } else {
                    alert('설정 저장 중 오류가 발생했습니다.');
                }
            } catch(e) {
                // 재부팅 되면서 연결이 끊어질 수 있으므로 성공으로 간주할 수도 있음
                alert('명령을 전송했습니다. 기기 화면을 확인해주세요!');
            }
        }
    </script>
</body>
</html>
)=====";

#endif
