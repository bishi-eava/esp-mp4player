#pragma once

namespace mp4 {

// Player page: standalone player with status, controls, and playlist
const char HTML_PLAYER_TEMPLATE[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Movie Player</title>
  <style>
    * {
      box-sizing: border-box;
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      margin: 0;
      padding: 16px;
      background: #f5f5f5;
      color: #333;
      font-size: 18px;
      line-height: 1.5;
    }
    .header-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin: 0 0 16px 0;
      padding-bottom: 12px;
      border-bottom: 3px solid #ff6b6b;
    }
    h1 {
      color: #ff6b6b;
      font-size: 24px;
      margin: 0;
    }
    .header-buttons {
      display: flex;
      gap: 8px;
    }
    .nav-btn {
      background: #ffa94d;
      color: #fff;
      border: none;
      padding: 10px 14px;
      border-radius: 10px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
      text-decoration: none;
      display: inline-flex;
      align-items: center;
    }
    .nav-btn:active {
      transform: scale(0.98);
    }
    .settings-btn {
      background: #868e96;
      color: #fff;
      border: none;
      padding: 10px 14px;
      border-radius: 10px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
    }
    .settings-btn:active {
      transform: scale(0.98);
    }
    .player-section {
      background: #fff;
      border-radius: 16px;
      padding: 24px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.1);
      margin-bottom: 16px;
    }
    .player-status {
      text-align: center;
      padding: 8px 0 16px;
    }
    .player-status-text {
      font-size: 16px;
      color: #868e96;
    }
    .player-file {
      font-size: 18px;
      font-weight: bold;
      color: #ff6b6b;
      margin-top: 4px;
      word-break: break-all;
    }
    .player-controls {
      display: flex;
      justify-content: center;
      align-items: center;
      gap: 12px;
      margin-bottom: 16px;
    }
    .player-ctrl-btn {
      background: #e9ecef;
      border: none;
      width: 48px;
      height: 48px;
      border-radius: 50%;
      cursor: pointer;
      font-size: 20px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .player-ctrl-btn:active {
      background: #ff6b6b;
      color: #fff;
    }
    .player-ctrl-btn.play-btn {
      background: #ff6b6b;
      color: #fff;
      width: 56px;
      height: 56px;
      font-size: 24px;
    }
    .player-ctrl-btn.play-btn:active {
      background: #e85050;
    }
    .volume-row {
      display: flex;
      align-items: center;
      gap: 10px;
      margin-bottom: 16px;
      padding: 0 8px;
    }
    .volume-icon {
      font-size: 20px;
      color: #868e96;
      flex-shrink: 0;
    }
    .volume-slider {
      flex: 1;
      -webkit-appearance: none;
      appearance: none;
      height: 6px;
      background: #e9ecef;
      border-radius: 3px;
      outline: none;
    }
    .volume-slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 22px;
      height: 22px;
      background: #ff6b6b;
      border-radius: 50%;
      cursor: pointer;
    }
    .volume-value {
      font-size: 14px;
      color: #495057;
      font-weight: bold;
      min-width: 36px;
      text-align: right;
    }
    .playlist-section {
      max-height: 400px;
      overflow-y: auto;
    }
    .playlist-item {
      padding: 12px 16px;
      margin: 4px 0;
      background: #f8f9fa;
      border-radius: 8px;
      cursor: pointer;
      font-size: 14px;
      display: flex;
      align-items: center;
    }
    .playlist-item:active {
      background: #e9ecef;
    }
    .playlist-item.current {
      border-left: 3px solid #ff6b6b;
      font-weight: bold;
    }
    .playlist-item .pl-name {
      flex: 1;
      word-break: break-all;
    }
    .playlist-item .pl-icon {
      color: #ff6b6b;
      margin-left: 8px;
    }
    .section-title {
      font-size: 14px;
      color: #868e96;
      margin: 0 0 8px 0;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .no-files {
      text-align: center;
      color: #868e96;
      padding: 24px;
      font-size: 14px;
    }
    .folder-selector {
      margin-bottom: 16px;
    }
    .folder-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
      gap: 12px;
    }
    .folder-card {
      cursor: pointer;
      text-align: center;
    }
    .folder-card:active {
      transform: scale(0.96);
    }
    .folder-thumb {
      width: 100%;
      aspect-ratio: 1;
      border-radius: 12px;
      overflow: hidden;
      background: #e9ecef;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .folder-thumb img {
      max-width: 100%;
      max-height: 100%;
      object-fit: contain;
    }
    .folder-thumb .thumb-blank {
      font-size: 40px;
      color: #ced4da;
    }
    .folder-card.current .folder-thumb {
      box-shadow: 0 0 0 3px #e8590c;
    }
    .folder-card-name {
      margin-top: 6px;
      font-size: 13px;
      font-weight: bold;
      color: #495057;
      word-break: break-all;
      line-height: 1.3;
    }
    .folder-card.current .folder-card-name {
      color: #e8590c;
    }
    .folder-current-row {
      position: relative;
      text-align: center;
      margin-bottom: 8px;
    }
    .folder-back {
      position: absolute;
      left: 0;
      top: 0;
      padding: 8px 16px;
      background: #e9ecef;
      border-radius: 8px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
      color: #495057;
    }
    .folder-back:active {
      background: #dee2e6;
    }
    .folder-default-btn {
      position: absolute;
      right: 0;
      top: 0;
      padding: 6px 10px;
      background: #e9ecef;
      border-radius: 8px;
      cursor: pointer;
      font-size: 20px;
      color: #868e96;
      border: none;
      line-height: 1;
    }
    .folder-default-btn:active {
      background: #dee2e6;
    }
    .folder-default-btn.saved {
      color: #e8590c;
    }
    .folder-current-info {
      display: inline-block;
    }
    .folder-current-info .folder-thumb {
      width: 160px;
      margin: 0 auto;
    }
    .dialog-overlay {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0, 0, 0, 0.5);
      z-index: 100;
    }
    .dialog-overlay.show {
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 16px;
    }
    .dialog {
      background: #fff;
      padding: 24px;
      border-radius: 16px;
      width: 100%;
      max-width: 320px;
      text-align: center;
      box-shadow: 0 8px 32px rgba(0,0,0,0.2);
    }
    .dialog h3 {
      margin: 0 0 24px 0;
      color: #333;
      font-size: 18px;
    }
    .dialog-buttons {
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .dialog-btn {
      padding: 16px 24px;
      border: none;
      border-radius: 12px;
      cursor: pointer;
      font-size: 18px;
      font-weight: bold;
    }
    .dialog-btn:active {
      transform: scale(0.98);
    }
    .btn-save {
      background: #51cf66;
      color: #fff;
    }
    .btn-cancel {
      background: #e9ecef;
      color: #495057;
    }
    .settings-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 12px 0;
      border-bottom: 1px solid #e9ecef;
      text-align: left;
    }
    .settings-row:last-child {
      border-bottom: none;
    }
    .settings-label {
      font-size: 14px;
      color: #495057;
      min-width: 90px;
    }
    .settings-value {
      font-size: 14px;
      color: #333;
      font-weight: bold;
    }
    .settings-input {
      width: 80px;
      padding: 8px 12px;
      font-size: 16px;
      border: 2px solid #e9ecef;
      border-radius: 8px;
      text-align: right;
    }
    .settings-input:focus {
      outline: none;
      border-color: #4dabf7;
    }
    .toggle-switch {
      position: relative;
      width: 50px;
      height: 28px;
    }
    .toggle-switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }
    .toggle-slider {
      position: absolute;
      cursor: pointer;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: #e9ecef;
      border-radius: 28px;
      transition: 0.2s;
    }
    .toggle-slider::before {
      position: absolute;
      content: "";
      height: 22px;
      width: 22px;
      left: 3px;
      bottom: 3px;
      background: #fff;
      border-radius: 50%;
      transition: 0.2s;
      box-shadow: 0 2px 4px rgba(0,0,0,0.2);
    }
    .toggle-switch input:checked + .toggle-slider {
      background: #ff6b6b;
    }
    .toggle-switch input:checked + .toggle-slider::before {
      transform: translateX(22px);
    }
    .start-page-select {
      padding: 8px 12px;
      font-size: 14px;
      border: 2px solid #e9ecef;
      border-radius: 8px;
      background: #fff;
    }
    .start-page-select:focus {
      outline: none;
      border-color: #4dabf7;
    }
  </style>
</head>
<body>
  <div class="header-row">
    <h1>Movie Player</h1>
    <div class="header-buttons">
      <a class="nav-btn" id="browseBtn" href="/browse">&#x1F4C2;</a>
      <button class="settings-btn" id="settingsBtn">&#x2699;&#xFE0F;</button>
    </div>
  </div>
  <div class="player-section">
    <div class="player-status">
      <div class="player-status-text" id="playerStatusText">Loading...</div>
      <div class="player-file" id="playerFile">-</div>
    </div>
    <div class="player-controls">
      <button class="player-ctrl-btn" onclick="playerApi('/api/prev')">&#x23EE;&#xFE0E;</button>
      <button class="player-ctrl-btn" onclick="playerApi('/api/stop')">&#x23F9;&#xFE0E;</button>
      <button class="player-ctrl-btn play-btn" onclick="playerApi('/api/play')">&#x25B6;&#xFE0E;</button>
      <button class="player-ctrl-btn" onclick="playerApi('/api/next')">&#x23ED;&#xFE0E;</button>
    </div>
    <div class="volume-row">
      <span class="volume-icon">&#x1F509;</span>
      <input type="range" class="volume-slider" id="volumeSlider" min="0" max="100" value="100">
      <span class="volume-value" id="volumeValue">100</span>
    </div>
    <div class="folder-selector" id="folderSelector" style="display:none">
      <p class="section-title">Folders</p>
      <div id="folderList"></div>
    </div>
    <p class="section-title">Playlist</p>
    <div class="playlist-section" id="playerPlaylist">
      <div class="no-files">Loading...</div>
    </div>
  </div>
  <div class="dialog-overlay" id="settingsDialog">
    <div class="dialog">
      <h3>Settings</h3>
      <div class="settings-row">
        <span class="settings-label">SD Total</span>
        <span class="settings-value" id="sdTotal">-</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Used</span>
        <span class="settings-value" id="sdUsed">-</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Free</span>
        <span class="settings-value" id="sdFree">-</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Sync Mode</span>
        <span style="display:flex;align-items:center;margin-left:auto">
          <label class="toggle-switch">
            <input type="checkbox" id="syncModeToggle" checked>
            <span class="toggle-slider"></span>
          </label>
          <span class="settings-value" id="syncModeLabel" style="margin-left:8px;font-size:12px;min-width:90px">Audio Priority</span>
        </span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Repeat</span>
        <span style="display:flex;align-items:center;margin-left:auto">
          <label class="toggle-switch">
            <input type="checkbox" id="repeatToggle">
            <span class="toggle-slider"></span>
          </label>
          <span class="settings-value" id="repeatLabel" style="margin-left:8px;font-size:12px;min-width:90px">Off</span>
        </span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Start Page</span>
        <select class="start-page-select" id="startPageSelect">
          <option value="player">Movie Player</option>
          <option value="browse">File Browser</option>
        </select>
      </div>
      <div class="dialog-buttons" style="margin-top:16px;">
        <button class="dialog-btn btn-save" id="btnSaveSettings">Save</button>
        <button class="dialog-btn btn-cancel" id="btnCloseSettings">Close</button>
      </div>
    </div>
  </div>
  <div class="dialog-overlay" id="stopConfirmDialog">
    <div class="dialog">
      <h3>Stop playback and open File Browser?</h3>
      <div class="dialog-buttons">
        <button class="dialog-btn btn-save" id="btnStopConfirm">Stop &amp; Browse</button>
        <button class="dialog-btn btn-cancel" id="btnStopCancel">Cancel</button>
      </div>
    </div>
  </div>
  <script>
    var settingsDialog = document.getElementById('settingsDialog');
    var settingsBtn = document.getElementById('settingsBtn');
    var startPageSelect = document.getElementById('startPageSelect');
    var syncModeToggle = document.getElementById('syncModeToggle');
    var syncModeLabel = document.getElementById('syncModeLabel');
    var pollTimer = null;
    var isPlaying = false;
    var stopConfirmDialog = document.getElementById('stopConfirmDialog');
    var browseBtn = document.getElementById('browseBtn');

    function formatBytes(bytes) {
      if (bytes < 1024) return bytes + ' B';
      if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
      if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
      return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    function escHtml(s) {
      var d = document.createElement('div');
      d.textContent = s;
      return d.innerHTML;
    }

    // Settings dialog
    settingsBtn.addEventListener('click', function() {
      settingsDialog.classList.add('show');
      fetch('/api/status').then(function(r) { return r.json(); }).then(function(data) {
        startPageSelect.value = data.start_page || 'player';
      }).catch(function() {});
      fetch('/storage-info').then(function(r) { return r.json(); }).then(function(data) {
        document.getElementById('sdTotal').textContent = formatBytes(data.total);
        document.getElementById('sdUsed').textContent = formatBytes(data.used);
        document.getElementById('sdFree').textContent = formatBytes(data.free);
      }).catch(function() {});
    });

    document.getElementById('btnSaveSettings').addEventListener('click', function() {
      fetch('/api/start-page?page=' + startPageSelect.value, {method:'POST'});
      settingsDialog.classList.remove('show');
    });

    document.getElementById('btnCloseSettings').addEventListener('click', function() {
      settingsDialog.classList.remove('show');
    });

    settingsDialog.addEventListener('click', function(e) {
      if (e.target === settingsDialog) settingsDialog.classList.remove('show');
    });

    browseBtn.addEventListener('click', function(e) {
      if (isPlaying) {
        e.preventDefault();
        stopConfirmDialog.classList.add('show');
      }
    });

    document.getElementById('btnStopConfirm').addEventListener('click', function() {
      stopConfirmDialog.classList.remove('show');
      fetch('/api/stop', {method:'POST'}).then(function() {
        window.location.href = '/browse';
      }).catch(function() {
        window.location.href = '/browse';
      });
    });

    document.getElementById('btnStopCancel').addEventListener('click', function() {
      stopConfirmDialog.classList.remove('show');
    });

    stopConfirmDialog.addEventListener('click', function(e) {
      if (e.target === stopConfirmDialog) stopConfirmDialog.classList.remove('show');
    });

    var volumeSlider = document.getElementById('volumeSlider');
    var volumeValue = document.getElementById('volumeValue');
    var volumeFromServer = true;

    volumeSlider.addEventListener('input', function() {
      volumeValue.textContent = volumeSlider.value;
      volumeFromServer = false;
      fetch('/api/volume?vol=' + volumeSlider.value, {method:'POST'}).catch(function(){});
    });

    volumeSlider.addEventListener('change', function() {
      volumeFromServer = true;
      fetch('/api/save-player-config', {method:'POST'}).catch(function(){});
    });

    syncModeToggle.addEventListener('change', function() {
      var mode = syncModeToggle.checked ? 'audio' : 'video';
      syncModeLabel.textContent = syncModeToggle.checked ? 'Audio Priority' : 'Full Video';
      fetch('/api/sync-mode?mode=' + mode, {method:'POST'}).then(function() {
        return fetch('/api/save-player-config', {method:'POST'});
      }).catch(function(){});
    });

    function updateSyncModeUI(mode) {
      var isAudio = (mode === 'audio');
      syncModeToggle.checked = isAudio;
      syncModeLabel.textContent = isAudio ? 'Audio Priority' : 'Full Video';
    }

    var repeatToggle = document.getElementById('repeatToggle');
    var repeatLabel = document.getElementById('repeatLabel');

    repeatToggle.addEventListener('change', function() {
      var mode = repeatToggle.checked ? 'on' : 'off';
      repeatLabel.textContent = repeatToggle.checked ? 'On' : 'Off';
      fetch('/api/repeat?mode=' + mode, {method:'POST'}).then(function() {
        return fetch('/api/save-player-config', {method:'POST'});
      }).catch(function(){});
    });

    function updateRepeatUI(on) {
      repeatToggle.checked = on;
      repeatLabel.textContent = on ? 'On' : 'Off';
    }

    // Player
    var currentFolder = '';
    var defaultFolder = '';
    var hasFolders = false;

    function loadPlayerStatus() {
      fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
        isPlaying = d.playing;
        document.getElementById('playerStatusText').textContent = d.playing ? 'Playing...' : 'Stopped';
        document.getElementById('playerStatusText').style.color = d.playing ? '#51cf66' : '#868e96';
        document.getElementById('playerFile').textContent = d.file ? (d.playing_folder ? d.playing_folder + '/' + d.file : d.file) : '-';
        if (d.sync_mode) updateSyncModeUI(d.sync_mode);
        if (d.repeat !== undefined) updateRepeatUI(d.repeat);
        if (d.volume !== undefined && volumeFromServer) {
          volumeSlider.value = d.volume;
          volumeValue.textContent = d.volume;
        }
        var items = document.querySelectorAll('.playlist-item');
        for (var i = 0; i < items.length; i++) {
          items[i].classList.toggle('current', items[i].dataset.idx == d.index && d.playing && currentFolder === (d.playing_folder || ''));
        }
      }).catch(function() {
        document.getElementById('playerStatusText').textContent = 'Connection error';
        document.getElementById('playerStatusText').style.color = '#fa5252';
      });
    }

    function renderFolders(folders) {
      var sel = document.getElementById('folderSelector');
      var fl = document.getElementById('folderList');
      if (folders.length === 0 && !currentFolder) {
        sel.style.display = 'none';
        hasFolders = false;
        return;
      }
      hasFolders = true;
      sel.style.display = 'block';
      var h = '';
      if (currentFolder) {
        var curThumb = '';
        for (var j = 0; j < folders.length; j++) {
          if ((folders[j].name || folders[j]) === currentFolder) { curThumb = folders[j].thumb || ''; break; }
        }
        var isDefault = (currentFolder === defaultFolder);
        h += '<div class="folder-current-row">';
        h += '<span class="folder-back" onclick="selectFolder(\'\')">&#x2190;</span>';
        h += '<button class="folder-default-btn' + (isDefault ? ' saved' : '') + '" onclick="saveDefaultFolder(this)">' + (isDefault ? '&#x2605;' : '&#x2606;') + '</button>';
        h += '<div class="folder-current-info">';
        h += '<div class="folder-thumb">';
        if (curThumb) {
          h += '<img src="/preview?file=' + encodeURIComponent(curThumb) + '" onerror="this.parentNode.innerHTML=\'<span class=thumb-blank>&#x25A0;</span>\'">';
        } else {
          h += '<span class="thumb-blank">&#x25A0;</span>';
        }
        h += '</div>';
        h += '<div class="folder-card-name" style="color:#e8590c">' + escHtml(currentFolder) + '</div>';
        h += '</div></div>';
      } else {
        h += '<div class="folder-grid">';
        for (var i = 0; i < folders.length; i++) {
          var f = folders[i];
          var name = f.name || f;
          var thumb = f.thumb || '';
          h += '<div class="folder-card" onclick="selectFolder(\'' + escHtml(name) + '\')">';
          h += '<div class="folder-thumb">';
          if (thumb) {
            h += '<img src="/preview?file=' + encodeURIComponent(thumb) + '" onerror="this.parentNode.innerHTML=\'<span class=thumb-blank>&#x25A0;</span>\'">';
          } else {
            h += '<span class="thumb-blank">&#x25A0;</span>';
          }
          h += '</div>';
          h += '<div class="folder-card-name">' + escHtml(name) + '</div>';
          h += '</div>';
        }
        h += '</div>';
      }
      fl.innerHTML = h;
    }

    function loadPlayerPlaylist() {
      fetch('/api/playlist').then(function(r) { return r.json(); }).then(function(data) {
        currentFolder = data.folder || '';
        defaultFolder = data.default_folder || '';
        var list = data.files || [];
        var folders = data.folders || [];
        renderFolders(folders);
        var h = '';
        if (list.length === 0) {
          if (folders.length > 0 && !currentFolder) {
            h = '<div class="no-files">Select a folder above</div>';
          } else {
            h = '<div class="no-files">No MP4 files found</div>';
          }
        }
        for (var i = 0; i < list.length; i++) {
          h += '<div class="playlist-item" data-idx="' + i + '" onclick="playIdx(' + i + ')">';
          h += '<span class="pl-name">' + escHtml(list[i]) + '</span>';
          h += '<span class="pl-icon">&#x25B6;&#xFE0E;</span></div>';
        }
        document.getElementById('playerPlaylist').innerHTML = h;
        loadPlayerStatus();
      }).catch(function() {
        document.getElementById('playerPlaylist').innerHTML = '<div class="no-files" style="color:#fa5252">Failed to load</div>';
      });
    }

    function selectFolder(name) {
      fetch('/api/folder?name=' + encodeURIComponent(name), { method: 'POST' })
        .then(function(r) { return r.json(); })
        .then(function() { loadPlayerPlaylist(); })
        .catch(function() {});
    }

    function saveDefaultFolder(btn) {
      fetch('/api/save-player-config', { method: 'POST' })
        .then(function(r) { return r.json(); })
        .then(function() {
          defaultFolder = currentFolder;
          btn.innerHTML = '&#x2605;';
          btn.classList.add('saved');
        })
        .catch(function() {});
    }

    function playerApi(url) {
      fetch(url, { method: 'POST' }).then(function(r) { return r.json(); }).then(function() {
        setTimeout(loadPlayerStatus, 300);
      }).catch(function() {});
    }

    function playIdx(i) {
      fetch('/api/play?index=' + i, { method: 'POST' }).then(function(r) { return r.json(); }).then(function() {
        setTimeout(loadPlayerStatus, 500);
      });
    }

    // Start polling
    loadPlayerPlaylist();
    pollTimer = setInterval(loadPlayerStatus, 3000);

    // Pause polling when page is hidden
    document.addEventListener('visibilitychange', function() {
      if (document.hidden) {
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
      } else {
        loadPlayerStatus();
        if (!pollTimer) pollTimer = setInterval(loadPlayerStatus, 3000);
      }
    });
  </script>
</body>
</html>
)rawliteral";

// SSR HTML template for file browser page.
// Markers (replaced by browse_handler via chunked response):
//   <!--BACK_BTN-->       back button HTML (empty string or <a> tag)
//   <!--PATH_DISPLAY-->   current path text (e.g. "/ folder / sub")
//   <!--FILE_LIST-->      file listing HTML (<ul>...</ul> or <p>)
//   __JSPATH__            raw path for JavaScript (e.g. /folder)

const char HTML_TEMPLATE[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>File Browser</title>
  <style>
    * {
      box-sizing: border-box;
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      margin: 0;
      padding: 16px;
      background: #f5f5f5;
      color: #333;
      font-size: 18px;
      line-height: 1.5;
    }
    .header-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin: 0 0 16px 0;
      padding-bottom: 12px;
      border-bottom: 3px solid #ff6b6b;
    }
    h1 {
      color: #ff6b6b;
      font-size: 24px;
      margin: 0;
    }
    ul {
      list-style: none;
      padding: 0;
      margin: 0;
    }
    li {
      padding: 16px;
      margin: 12px 0;
      background: #fff;
      border-radius: 12px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.1);
    }
    a {
      color: #4dabf7;
      text-decoration: none;
      font-size: 18px;
      display: block;
    }
    li.dir {
      position: relative;
    }
    .dir a {
      color: #ffa94d;
      font-weight: bold;
      padding-right: 32px;
    }
    .folder-edit-btn {
      position: absolute;
      right: 12px;
      top: 12px;
      background: none;
      border: none;
      font-size: 22px;
      color: #868e96;
      cursor: pointer;
      padding: 4px 8px;
      display: none;
    }
    .new-folder-btn {
      background: #ffa94d;
      color: #fff;
      border: none;
      padding: 10px 16px;
      border-radius: 10px;
      cursor: pointer;
      font-size: 14px;
      font-weight: bold;
      white-space: nowrap;
      display: none;
      margin-left: auto;
    }
    .new-folder-btn:active {
      transform: scale(0.98);
    }
    .nav-row {
      display: flex;
      align-items: center;
      gap: 12px;
      margin: 12px 0 16px 0;
    }
    .back-btn {
      display: inline-block;
      background: #e9ecef;
      color: #495057;
      padding: 10px 16px;
      border-radius: 10px;
      font-size: 16px;
      font-weight: bold;
      text-decoration: none;
      white-space: nowrap;
    }
    .back-btn:active {
      transform: scale(0.98);
      background: #dee2e6;
    }
    .path {
      color: #495057;
      font-size: 16px;
      padding: 0;
      margin: 0;
      background: none;
    }
    .error {
      color: #fa5252;
      font-weight: bold;
    }
    .upload-toggle {
      background: #51cf66;
      color: #fff;
      border: none;
      padding: 10px 20px;
      border-radius: 10px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
      box-shadow: 0 4px 12px rgba(81,207,102,0.3);
      white-space: nowrap;
    }
    .upload-toggle:active {
      transform: scale(0.98);
    }
    .upload-area {
      border: 3px dashed #adb5bd;
      border-radius: 16px;
      padding: 32px 16px;
      text-align: center;
      margin: 16px 0;
      background: #fff;
      display: none;
    }
    .upload-area.show {
      display: block;
    }
    .upload-area.dragover {
      background: #e8f5e9;
      border-color: #51cf66;
    }
    .upload-area p {
      font-size: 16px;
      color: #868e96;
      margin: 0 0 16px 0;
    }
    .upload-btn {
      background: #4dabf7;
      color: #fff;
      border: none;
      padding: 16px 32px;
      border-radius: 12px;
      cursor: pointer;
      font-size: 18px;
      font-weight: bold;
      box-shadow: 0 4px 12px rgba(77,171,247,0.3);
    }
    .upload-btn:active {
      transform: scale(0.98);
    }
    #fileInput {
      display: none;
    }
    .upload-status {
      margin-top: 16px;
      color: #51cf66;
      font-weight: bold;
    }
    .progress-wrapper {
      display: none;
      margin-top: 16px;
      align-items: center;
      gap: 8px;
    }
    .progress-wrapper.show {
      display: flex;
    }
    .progress-container {
      flex: 1;
      background: #e9ecef;
      border-radius: 8px;
      overflow: hidden;
      height: 24px;
    }
    .progress-bar {
      height: 100%;
      background: linear-gradient(90deg, #51cf66, #40c057);
      border-radius: 8px;
      transition: width 0.1s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      min-width: 40px;
    }
    .progress-text {
      color: #fff;
      font-size: 12px;
      font-weight: bold;
      text-shadow: 0 1px 2px rgba(0,0,0,0.2);
    }
    .cancel-btn {
      background: #ff6b6b;
      color: #fff;
      border: none;
      width: 24px;
      height: 24px;
      border-radius: 50%;
      cursor: pointer;
      font-size: 14px;
      font-weight: bold;
      line-height: 1;
      padding: 0;
    }
    .cancel-btn:active {
      transform: scale(0.95);
    }
    .file-info {
      color: #868e96;
      font-size: 14px;
      margin-top: 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .file-date {
      text-align: right;
      white-space: nowrap;
    }
    .dialog-overlay {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0, 0, 0, 0.5);
      z-index: 100;
    }
    .dialog-overlay.show {
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 16px;
    }
    .dialog {
      background: #fff;
      padding: 24px;
      border-radius: 16px;
      width: 100%;
      max-width: 320px;
      text-align: center;
      box-shadow: 0 8px 32px rgba(0,0,0,0.2);
    }
    .dialog h3 {
      margin: 0 0 24px 0;
      color: #333;
      font-size: 18px;
      word-break: break-all;
    }
    .dialog-buttons {
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .dialog-btn {
      padding: 16px 24px;
      border: none;
      border-radius: 12px;
      cursor: pointer;
      font-size: 18px;
      font-weight: bold;
    }
    .dialog-btn:active {
      transform: scale(0.98);
    }
    .btn-download {
      background: #4dabf7;
      color: #fff;
    }
    .btn-delete {
      background: #ff6b6b;
      color: #fff;
    }
    .btn-preview {
      background: #9775fa;
      color: #fff;
    }
    .btn-rename {
      background: #ffa94d;
      color: #fff;
    }
    .btn-cancel {
      background: #e9ecef;
      color: #495057;
    }
    .btn-save {
      background: #51cf66;
      color: #fff;
    }
    .preview-overlay {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0, 0, 0, 0.85);
      z-index: 300;
      flex-direction: column;
    }
    .preview-overlay.show {
      display: flex;
    }
    .preview-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 12px 16px;
      background: rgba(0,0,0,0.3);
    }
    .preview-title {
      color: #fff;
      font-size: 14px;
      word-break: break-all;
      margin-right: 12px;
    }
    .preview-close {
      background: #ff6b6b;
      color: #fff;
      border: none;
      width: 36px;
      height: 36px;
      border-radius: 50%;
      cursor: pointer;
      font-size: 18px;
      font-weight: bold;
      flex-shrink: 0;
    }
    .preview-body {
      flex: 1;
      overflow: auto;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 16px;
    }
    .preview-body img {
      max-width: 100%;
      max-height: 100%;
      object-fit: contain;
      border-radius: 8px;
    }
    .preview-body pre {
      background: #1e1e1e;
      color: #d4d4d4;
      padding: 16px;
      border-radius: 8px;
      font-size: 13px;
      line-height: 1.5;
      overflow: auto;
      max-width: 100%;
      max-height: 100%;
      width: 100%;
      margin: 0;
      white-space: pre-wrap;
      word-break: break-all;
      align-self: flex-start;
    }
    .rename-input {
      width: 100%;
      padding: 12px;
      font-size: 16px;
      border: 2px solid #e9ecef;
      border-radius: 8px;
      margin-bottom: 16px;
    }
    .rename-input:focus {
      outline: none;
      border-color: #ffa94d;
    }
    .settings-btn {
      background: #868e96;
      color: #fff;
      border: none;
      padding: 10px 14px;
      border-radius: 10px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
    }
    .settings-btn:active {
      transform: scale(0.98);
    }
    .settings-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 12px 0;
      border-bottom: 1px solid #e9ecef;
      text-align: left;
    }
    .settings-row:last-child {
      border-bottom: none;
    }
    .settings-label {
      font-size: 14px;
      color: #495057;
      min-width: 90px;
    }
    .settings-value {
      font-size: 14px;
      color: #333;
      font-weight: bold;
    }
    .settings-input {
      width: 80px;
      padding: 8px 12px;
      font-size: 16px;
      border: 2px solid #e9ecef;
      border-radius: 8px;
      text-align: right;
    }
    .settings-input:focus {
      outline: none;
      border-color: #4dabf7;
    }
    .header-buttons {
      display: flex;
      gap: 8px;
    }
    .nav-btn {
      background: #4dabf7;
      color: #fff;
      border: none;
      padding: 10px 14px;
      border-radius: 10px;
      cursor: pointer;
      font-size: 16px;
      font-weight: bold;
      text-decoration: none;
      display: inline-flex;
      align-items: center;
    }
    .nav-btn:active {
      transform: scale(0.98);
    }
    .toggle-switch {
      position: relative;
      width: 50px;
      height: 28px;
    }
    .toggle-switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }
    .toggle-slider {
      position: absolute;
      cursor: pointer;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: #e9ecef;
      border-radius: 28px;
      transition: 0.2s;
    }
    .toggle-slider::before {
      position: absolute;
      content: "";
      height: 22px;
      width: 22px;
      left: 3px;
      bottom: 3px;
      background: #fff;
      border-radius: 50%;
      transition: 0.2s;
      box-shadow: 0 2px 4px rgba(0,0,0,0.2);
    }
    .toggle-switch input:checked + .toggle-slider {
      background: #ff6b6b;
    }
    .toggle-switch input:checked + .toggle-slider::before {
      transform: translateX(22px);
    }
    .loading-overlay {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(255, 255, 255, 0.8);
      z-index: 200;
      justify-content: center;
      align-items: center;
      flex-direction: column;
    }
    .loading-overlay.show {
      display: flex;
    }
    .spinner {
      width: 48px;
      height: 48px;
      border: 4px solid #e9ecef;
      border-top: 4px solid #ff6b6b;
      border-radius: 50%;
      animation: spin 1s linear infinite;
    }
    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
    .loading-text {
      margin-top: 16px;
      color: #495057;
      font-size: 16px;
    }
    .start-page-select {
      padding: 8px 12px;
      font-size: 14px;
      border: 2px solid #e9ecef;
      border-radius: 8px;
      background: #fff;
    }
    .start-page-select:focus {
      outline: none;
      border-color: #4dabf7;
    }
  </style>
</head>
<body>
  <div class="header-row">
    <h1>File Browser</h1>
    <div class="header-buttons">
      <a class="nav-btn" href="/player">&#x1F3AC;</a>
      <button class="settings-btn" id="settingsBtn">&#x2699;&#xFE0F;</button>
      <button class="upload-toggle" id="uploadToggle">Upload</button>
    </div>
  </div>
  <div class="upload-area" id="dropZone">
    <p>Drop files here to upload</p>
    <input type="file" id="fileInput" multiple>
    <button class="upload-btn" onclick="document.getElementById('fileInput').click()">Choose Files</button>
    <div class="progress-wrapper" id="progressWrapper">
      <div class="progress-container">
        <div class="progress-bar" id="progressBar">
          <span class="progress-text" id="progressText">0%</span>
        </div>
      </div>
      <button class="cancel-btn" id="cancelBtn">&#x2715;</button>
    </div>
    <div id="uploadStatus" class="upload-status"></div>
  </div>
  <div class="nav-row"><!--BACK_BTN--><p class="path"><!--PATH_DISPLAY--></p><button class="new-folder-btn" id="newFolderBtn">+ Folder</button></div>
  <!--FILE_LIST-->
  <div class="loading-overlay" id="loadingOverlay">
    <div class="spinner"></div>
    <div class="loading-text">Loading...</div>
  </div>
  <div class="dialog-overlay" id="fileDialog">
    <div class="dialog">
      <h3 id="dialogFileName"></h3>
      <div class="dialog-buttons">
        <button class="dialog-btn btn-preview" id="btnPreview">Preview</button>
        <button class="dialog-btn btn-download" id="btnDownload">Download</button>
        <button class="dialog-btn btn-rename" id="btnRename">Rename</button>
        <button class="dialog-btn btn-delete" id="btnDelete">Delete</button>
        <button class="dialog-btn btn-cancel" id="btnCancel">Cancel</button>
      </div>
    </div>
  </div>
  <div class="dialog-overlay" id="folderDialog">
    <div class="dialog">
      <h3 id="folderDialogName"></h3>
      <div class="dialog-buttons">
        <button class="dialog-btn btn-rename" id="btnFolderRename">Rename</button>
        <button class="dialog-btn btn-delete" id="btnFolderDelete">Delete</button>
        <button class="dialog-btn btn-cancel" id="btnFolderCancel">Cancel</button>
      </div>
    </div>
  </div>
  <div class="dialog-overlay" id="newFolderDialog">
    <div class="dialog">
      <h3>New Folder</h3>
      <input type="text" class="rename-input" id="newFolderInput" placeholder="Folder name">
      <div class="dialog-buttons">
        <button class="dialog-btn btn-save" id="btnNewFolderSave">Create</button>
        <button class="dialog-btn btn-cancel" id="btnNewFolderCancel">Cancel</button>
      </div>
    </div>
  </div>
  <div class="dialog-overlay" id="renameDialog">
    <div class="dialog">
      <h3>Rename</h3>
      <input type="text" class="rename-input" id="renameInput" placeholder="New name">
      <div class="dialog-buttons">
        <button class="dialog-btn btn-save" id="btnRenameSave">Save</button>
        <button class="dialog-btn btn-cancel" id="btnRenameCancel">Cancel</button>
      </div>
    </div>
  </div>
  <div class="preview-overlay" id="previewOverlay">
    <div class="preview-header">
      <span class="preview-title" id="previewTitle"></span>
      <button class="preview-close" id="previewClose">&#x2715;</button>
    </div>
    <div class="preview-body" id="previewBody"></div>
  </div>
  <div class="dialog-overlay" id="settingsDialog">
    <div class="dialog">
      <h3>Settings</h3>
      <div class="settings-row">
        <span class="settings-label">SD Total</span>
        <span class="settings-value" id="sdTotal">-</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Used</span>
        <span class="settings-value" id="sdUsed">-</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Free</span>
        <span class="settings-value" id="sdFree">-</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Upload Limit</span>
        <span><input type="number" class="settings-input" id="maxSizeInput" min="1" max="100"> MB</span>
      </div>
      <div class="settings-row">
        <span class="settings-label">Allow Management</span>
        <label class="toggle-switch">
          <input type="checkbox" id="deleteAllowedToggle">
          <span class="toggle-slider"></span>
        </label>
      </div>
      <div class="settings-row">
        <span class="settings-label">Start Page</span>
        <select class="start-page-select" id="startPageSelect">
          <option value="player">Movie Player</option>
          <option value="browse">File Browser</option>
        </select>
      </div>
      <div class="dialog-buttons" style="margin-top:16px;">
        <button class="dialog-btn btn-save" id="btnSaveSettings">Save</button>
        <button class="dialog-btn btn-cancel" id="btnCloseSettings">Close</button>
      </div>
    </div>
  </div>
  <script>
    var dropZone = document.getElementById('dropZone');
    var fileInput = document.getElementById('fileInput');
    var uploadStatusEl = document.getElementById('uploadStatus');
    var uploadToggle = document.getElementById('uploadToggle');
    var currentPath = '__JSPATH__';
    var fileDialog = document.getElementById('fileDialog');
    var dialogFileName = document.getElementById('dialogFileName');
    var settingsDialog = document.getElementById('settingsDialog');
    var settingsBtn = document.getElementById('settingsBtn');
    var maxSizeInput = document.getElementById('maxSizeInput');
    var deleteAllowedToggle = document.getElementById('deleteAllowedToggle');
    var startPageSelect = document.getElementById('startPageSelect');
    var btnDelete = document.getElementById('btnDelete');
    var btnRename = document.getElementById('btnRename');
    var btnPreview = document.getElementById('btnPreview');
    var renameDialog = document.getElementById('renameDialog');
    var renameInput = document.getElementById('renameInput');
    var previewOverlay = document.getElementById('previewOverlay');
    var previewTitle = document.getElementById('previewTitle');
    var previewBody = document.getElementById('previewBody');
    var folderDialog = document.getElementById('folderDialog');
    var folderDialogName = document.getElementById('folderDialogName');
    var newFolderBtn = document.getElementById('newFolderBtn');
    var newFolderDialog = document.getElementById('newFolderDialog');
    var newFolderInput = document.getElementById('newFolderInput');
    var selectedFilePath = '';
    var selectedFileName = '';
    var maxUploadSize = parseInt(sessionStorage.getItem('maxUploadSize') || '15');
    var sdFreeSpace = 0;
    var deleteAllowed = sessionStorage.getItem('deleteAllowed') === 'true';

    function updateManageUI() {
      newFolderBtn.style.display = deleteAllowed ? 'block' : 'none';
      var btns = document.querySelectorAll('.folder-edit-btn');
      for (var i = 0; i < btns.length; i++) {
        btns[i].style.display = deleteAllowed ? 'block' : 'none';
      }
    }
    updateManageUI();

    var imageExts = ['jpg','jpeg','png','gif','bmp','webp','svg'];
    var textExts = ['txt','csv','json','xml','html','htm','css','js','py','c','cpp','h','hpp','md','log','ini','cfg','config','yaml','yml'];

    function getFileExt(name) {
      var i = name.lastIndexOf('.');
      return i > 0 ? name.substring(i + 1).toLowerCase() : '';
    }
    function isPreviewable(name) {
      var ext = getFileExt(name);
      return imageExts.indexOf(ext) >= 0 || textExts.indexOf(ext) >= 0;
    }
    function isImageFile(name) {
      return imageExts.indexOf(getFileExt(name)) >= 0;
    }

    maxSizeInput.value = maxUploadSize;

    function formatBytes(bytes) {
      if (bytes < 1024) return bytes + ' B';
      if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
      if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
      return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    // Settings dialog
    settingsBtn.addEventListener('click', function() {
      deleteAllowedToggle.checked = deleteAllowed;
      settingsDialog.classList.add('show');
      fetch('/api/status').then(function(r) { return r.json(); }).then(function(data) {
        startPageSelect.value = data.start_page || 'player';
      }).catch(function() {});
      fetch('/storage-info').then(function(r) { return r.json(); }).then(function(data) {
        document.getElementById('sdTotal').textContent = formatBytes(data.total);
        document.getElementById('sdUsed').textContent = formatBytes(data.used);
        document.getElementById('sdFree').textContent = formatBytes(data.free);
        sdFreeSpace = data.free;
      }).catch(function() {});
    });

    document.getElementById('btnSaveSettings').addEventListener('click', function() {
      var val = parseInt(maxSizeInput.value);
      if (val >= 1 && val <= 100) {
        maxUploadSize = val;
        sessionStorage.setItem('maxUploadSize', val.toString());
        deleteAllowed = deleteAllowedToggle.checked;
        sessionStorage.setItem('deleteAllowed', deleteAllowed.toString());
        fetch('/api/start-page?page=' + startPageSelect.value, {method:'POST'});
        updateManageUI();
        settingsDialog.classList.remove('show');
      } else {
        alert('Enter a value between 1 and 100');
      }
    });

    document.getElementById('btnCloseSettings').addEventListener('click', function() {
      maxSizeInput.value = maxUploadSize;
      deleteAllowedToggle.checked = deleteAllowed;
      settingsDialog.classList.remove('show');
    });

    settingsDialog.addEventListener('click', function(e) {
      if (e.target === settingsDialog) {
        maxSizeInput.value = maxUploadSize;
        deleteAllowedToggle.checked = deleteAllowed;
        settingsDialog.classList.remove('show');
      }
    });

    // File dialog
    function showFileDialog(filePath, fileName) {
      selectedFilePath = filePath;
      selectedFileName = fileName;
      dialogFileName.textContent = fileName;
      btnPreview.style.display = isPreviewable(fileName) ? 'block' : 'none';
      btnRename.style.display = deleteAllowed ? 'block' : 'none';
      btnDelete.style.display = deleteAllowed ? 'block' : 'none';
      fileDialog.classList.add('show');
    }

    document.getElementById('btnDownload').addEventListener('click', function() {
      window.location.href = '/download?file=' + encodeURIComponent(selectedFilePath);
      fileDialog.classList.remove('show');
    });

    document.getElementById('btnDelete').addEventListener('click', function() {
      if (confirm('Delete "' + selectedFileName + '"?')) {
        fetch('/delete?file=' + encodeURIComponent(selectedFilePath), { method: 'POST' })
          .then(function(r) {
            if (r.ok) { location.reload(); } else { alert('Delete failed'); }
          }).catch(function(err) { alert('Error: ' + err); });
      }
      fileDialog.classList.remove('show');
    });

    document.getElementById('btnCancel').addEventListener('click', function() {
      fileDialog.classList.remove('show');
    });

    fileDialog.addEventListener('click', function(e) {
      if (e.target === fileDialog) fileDialog.classList.remove('show');
    });

    // Preview
    btnPreview.addEventListener('click', function() {
      fileDialog.classList.remove('show');
      previewTitle.textContent = selectedFileName;
      previewBody.innerHTML = '';
      if (isImageFile(selectedFileName)) {
        var img = document.createElement('img');
        img.src = '/preview?file=' + encodeURIComponent(selectedFilePath);
        img.alt = selectedFileName;
        previewBody.appendChild(img);
      } else {
        var pre = document.createElement('pre');
        pre.textContent = 'Loading...';
        previewBody.appendChild(pre);
        fetch('/preview?file=' + encodeURIComponent(selectedFilePath))
          .then(function(r) { return r.text(); })
          .then(function(text) { pre.textContent = text; })
          .catch(function(err) { pre.textContent = 'Error: ' + err; });
      }
      previewOverlay.classList.add('show');
    });

    function closePreview() {
      previewOverlay.classList.remove('show');
      previewBody.innerHTML = '';
    }

    document.getElementById('previewClose').addEventListener('click', closePreview);
    previewOverlay.addEventListener('click', function(e) {
      if (e.target === previewOverlay) closePreview();
    });

    // Rename
    btnRename.addEventListener('click', function() {
      fileDialog.classList.remove('show');
      renameInput.value = selectedFileName;
      renameDialog.classList.add('show');
      var dotIdx = selectedFileName.lastIndexOf('.');
      renameInput.focus();
      if (dotIdx > 0) {
        renameInput.setSelectionRange(0, dotIdx);
      } else {
        renameInput.select();
      }
    });

    document.getElementById('btnRenameSave').addEventListener('click', function() {
      var newName = renameInput.value.trim();
      if (!newName || newName === selectedFileName) {
        renameDialog.classList.remove('show');
        return;
      }
      fetch('/rename?file=' + encodeURIComponent(selectedFilePath) + '&name=' + encodeURIComponent(newName), { method: 'POST' })
        .then(function(r) {
          if (r.ok) { location.reload(); } else { alert('Rename failed'); }
        }).catch(function(err) { alert('Error: ' + err); });
      renameDialog.classList.remove('show');
    });

    document.getElementById('btnRenameCancel').addEventListener('click', function() {
      renameDialog.classList.remove('show');
    });

    renameDialog.addEventListener('click', function(e) {
      if (e.target === renameDialog) renameDialog.classList.remove('show');
    });

    // Folder dialog
    function showFolderDialog(path, name) {
      selectedFilePath = path;
      selectedFileName = name;
      folderDialogName.textContent = name + '/';
      folderDialog.classList.add('show');
    }

    document.getElementById('btnFolderDelete').addEventListener('click', function() {
      if (confirm('Delete folder "' + selectedFileName + '" and all contents?')) {
        fetch('/delete?file=' + encodeURIComponent(selectedFilePath) + '&dir=1', { method: 'POST' })
          .then(function(r) {
            if (r.ok) { location.reload(); } else { alert('Delete folder failed'); }
          }).catch(function(err) { alert('Error: ' + err); });
      }
      folderDialog.classList.remove('show');
    });

    document.getElementById('btnFolderRename').addEventListener('click', function() {
      folderDialog.classList.remove('show');
      renameInput.value = selectedFileName;
      renameDialog.classList.add('show');
      renameInput.focus();
      renameInput.select();
    });

    document.getElementById('btnFolderCancel').addEventListener('click', function() {
      folderDialog.classList.remove('show');
    });

    folderDialog.addEventListener('click', function(e) {
      if (e.target === folderDialog) folderDialog.classList.remove('show');
    });

    // New folder
    newFolderBtn.addEventListener('click', function() {
      newFolderInput.value = '';
      newFolderDialog.classList.add('show');
      newFolderInput.focus();
    });

    document.getElementById('btnNewFolderSave').addEventListener('click', function() {
      var name = newFolderInput.value.trim();
      if (!name) { newFolderDialog.classList.remove('show'); return; }
      fetch('/mkdir?path=' + encodeURIComponent(currentPath) + '&name=' + encodeURIComponent(name) + '&time=' + Math.floor(Date.now() / 1000) + '&tz=' + new Date().getTimezoneOffset(), { method: 'POST' })
        .then(function(r) {
          if (r.ok) { location.reload(); } else { alert('Create folder failed'); }
        }).catch(function(err) { alert('Error: ' + err); });
      newFolderDialog.classList.remove('show');
    });

    document.getElementById('btnNewFolderCancel').addEventListener('click', function() {
      newFolderDialog.classList.remove('show');
    });

    newFolderDialog.addEventListener('click', function(e) {
      if (e.target === newFolderDialog) newFolderDialog.classList.remove('show');
    });

    // Upload area
    if (sessionStorage.getItem('uploadAreaOpen') === 'true') {
      dropZone.classList.add('show');
    }

    uploadToggle.addEventListener('click', function() {
      dropZone.classList.toggle('show');
      sessionStorage.setItem('uploadAreaOpen', dropZone.classList.contains('show'));
    });

    dropZone.addEventListener('dragover', function(e) {
      e.preventDefault();
      dropZone.classList.add('dragover');
    });

    dropZone.addEventListener('dragleave', function(e) {
      e.preventDefault();
      dropZone.classList.remove('dragover');
    });

    dropZone.addEventListener('drop', function(e) {
      e.preventDefault();
      dropZone.classList.remove('dragover');
      uploadFiles(e.dataTransfer.files);
    });

    fileInput.addEventListener('change', function(e) {
      uploadFiles(e.target.files);
    });

    var progressWrapper = document.getElementById('progressWrapper');
    var progressBar = document.getElementById('progressBar');
    var progressText = document.getElementById('progressText');
    var cancelBtn = document.getElementById('cancelBtn');
    var currentXhr = null;
    var uploadCancelled = false;

    cancelBtn.addEventListener('click', function() {
      if (currentXhr) {
        uploadCancelled = true;
        currentXhr.abort();
      }
    });

    function uploadFile(file, path, time) {
      return new Promise(function(resolve, reject) {
        var xhr = new XMLHttpRequest();
        currentXhr = xhr;

        xhr.upload.addEventListener('progress', function(e) {
          if (e.lengthComputable) {
            var percent = Math.round((e.loaded / e.total) * 100);
            progressBar.style.width = percent + '%';
            progressText.textContent = percent + '%';
          }
        });

        xhr.addEventListener('load', function() {
          currentXhr = null;
          if (xhr.status === 200) {
            resolve();
          } else {
            reject(new Error(xhr.responseText || 'Upload failed'));
          }
        });

        xhr.addEventListener('error', function() {
          currentXhr = null;
          reject(new Error('Network error'));
        });

        xhr.addEventListener('abort', function() {
          currentXhr = null;
          reject(new Error('Cancelled'));
        });

        xhr.open('POST', '/upload?path=' + encodeURIComponent(path) + '&filename=' + encodeURIComponent(file.name) + '&size=' + file.size + '&time=' + time + '&tz=' + new Date().getTimezoneOffset());
        xhr.send(file);
      });
    }

    function uploadFiles(files) {
      uploadStatusEl.textContent = 'Checking...';
      uploadStatusEl.style.color = '#51cf66';
      progressWrapper.classList.remove('show');
      uploadCancelled = false;

      fetch('/storage-info').then(function(r) { return r.json(); }).then(function(data) {
        sdFreeSpace = data.free;
        doUploadFiles(files);
      }).catch(function() {
        uploadStatusEl.textContent = 'Storage check failed';
        uploadStatusEl.style.color = '#fa5252';
      });
    }

    function doUploadFiles(files) {
      var maxBytes = maxUploadSize * 1024 * 1024;
      var now = Math.floor(Date.now() / 1000);
      var uploaded = 0;
      var idx = 0;

      function next() {
        if (idx >= files.length || uploadCancelled) {
          progressWrapper.classList.remove('show');
          if (uploadCancelled) {
            uploadStatusEl.textContent = 'Upload cancelled';
            uploadStatusEl.style.color = '#868e96';
          }
          if (uploaded > 0) {
            sessionStorage.setItem('uploadAreaOpen', 'true');
            setTimeout(function() { location.reload(); }, 500);
          }
          return;
        }

        var f = files[idx];
        idx++;

        if (f.size > maxBytes) {
          uploadStatusEl.textContent = f.name + ' exceeds limit (' + maxUploadSize + 'MB)';
          uploadStatusEl.style.color = '#fa5252';
          next();
          return;
        }
        if (f.size > sdFreeSpace) {
          uploadStatusEl.textContent = 'Not enough space on SD card';
          uploadStatusEl.style.color = '#fa5252';
          next();
          return;
        }

        uploadStatusEl.style.color = '#51cf66';
        uploadStatusEl.textContent = f.name + ' uploading...';
        progressBar.style.width = '0%';
        progressText.textContent = '0%';
        progressWrapper.classList.add('show');

        uploadFile(f, currentPath, now).then(function() {
          progressBar.style.width = '100%';
          progressText.textContent = '100%';
          uploadStatusEl.textContent = f.name + ' uploaded';
          sdFreeSpace -= f.size;
          uploaded++;
          next();
        }).catch(function(err) {
          if (uploadCancelled) {
            uploadStatusEl.textContent = 'Upload cancelled';
            uploadStatusEl.style.color = '#868e96';
            progressWrapper.classList.remove('show');
            if (uploaded > 0) {
              sessionStorage.setItem('uploadAreaOpen', 'true');
              setTimeout(function() { location.reload(); }, 500);
            }
          } else {
            uploadStatusEl.textContent = 'Error: ' + err.message;
            uploadStatusEl.style.color = '#fa5252';
            next();
          }
        });
      }
      next();
    }

    // Loading overlay for folder navigation
    var loadingOverlay = document.getElementById('loadingOverlay');
    var navLinks = document.querySelectorAll('.dir a, .back-btn');
    for (var li = 0; li < navLinks.length; li++) {
      (function(link) {
        link.addEventListener('click', function(e) {
          e.preventDefault();
          loadingOverlay.classList.add('show');
          var href = link.getAttribute('href');
          requestAnimationFrame(function() {
            window.location.href = href;
          });
        });
      })(navLinks[li]);
    }
  </script>
</body>
</html>
)rawliteral";

}  // namespace mp4
