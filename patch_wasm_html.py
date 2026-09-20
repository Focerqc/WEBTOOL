#!/usr/bin/env python3
"""
Patches the Qt WebAssembly HTML output with:
- Clean smartphone mobile viewport (480px centered, borderless, unrounded)
- Instant toggle between Mobile (480px) and Desktop (100vw)
- Bottom diagnostics HUD bar with WebSerial USB Connect button
- Real-time telemetry, baud rate, and I/O byte counters
- Side drawer diagnostics & system log viewer
"""

import os
import sys
import re

def patch_html(build_dir="build-wasm"):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    target_dir = os.path.join(script_dir, build_dir) if not os.path.isabs(build_dir) else build_dir
    
    html_src = os.path.join(target_dir, "vesc_tool_7.00.html")
    if not os.path.exists(html_src):
        index_candidate = os.path.join(target_dir, "index.html")
        if os.path.exists(index_candidate):
            html_src = index_candidate
        else:
            print(f"[PATCH ERROR] Cannot find vesc_tool_7.00.html or index.html in {target_dir}")
            return False

    with open(html_src, "r", encoding="utf-8") as f:
        content = f.read()

    # Clean any prior injected HUD and styles
    content = re.sub(r'<!-- VESC Tool WASM Diagnostics Bottom Bar -->.*?window\.__logToScreen\(\'Diagnostics & Web Serial overlay initialized\.\'\);\s*\}\)\(\);\s*</script>', '', content, flags=re.DOTALL)
    content = re.sub(r'<div id="app-wrapper">\s*<div id="qt-container"[^>]*></div>\s*</div>', '<div id="screen"></div>', content)
    content = re.sub(r'<div id="qt-container"[^>]*></div>', '<div id="screen"></div>', content)

    # 1. Clean Viewport Styling for Mobile (480px) and Desktop (100vw) - NO BORDERS, NO ROUNDED CORNERS
    viewport_css = """<style>
    html, body {
      margin: 0;
      padding: 0;
      width: 100%;
      height: 100%;
      height: 100vh;
      height: 100dvh;
      overflow: hidden;
      background-color: #121212 !important;
      display: flex;
      flex-direction: column;
      justify-content: flex-start;
      align-items: center;
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
    }
    #qt-container.viewport-mobile {
      max-width: 480px;
      width: 100%;
      flex: 1 1 auto;
      height: auto;
      min-height: 0;
      margin: 0 auto;
      position: relative;
      overflow: hidden;
      border: none !important;
      border-radius: 0 !important;
      box-shadow: 0 0 35px rgba(0,0,0,0.8);
      background-color: #202020;
    }
    #qt-container.viewport-desktop {
      max-width: 100vw !important;
      width: 100vw !important;
      flex: 1 1 auto;
      height: auto;
      min-height: 0;
      margin: 0 !important;
      border: none !important;
      border-radius: 0 !important;
      position: relative;
      box-shadow: none !important;
      background-color: #202020;
    }
    #qtcanvas {
      width: 100% !important;
      height: 100% !important;
      display: block;
    }
    #qt-shadow-container, .qt-screen, #qt-container canvas {
      width: 100% !important;
      height: 100% !important;
      display: block;
    }
    #qtspinner {
      position: absolute !important;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      margin: 0 !important;
      pointer-events: none !important;
      z-index: 50 !important;
    }
    @media (max-width: 1100px) {
      .desktop-only-stats {
        display: none !important;
      }
    }
    @media (max-width: 600px) {
      .hide-on-compact {
        display: none !important;
      }
      #vesc-diagnostics-bar {
        padding-left: max(4px, env(safe-area-inset-left, 4px)) !important;
        padding-right: max(4px, env(safe-area-inset-right, 4px)) !important;
        gap: 3px !important;
      }
      #vesc-diagnostics-bar button {
        padding: 2px 6px !important;
        font-size: 10px !important;
      }
    }
    @media (max-width: 960px) {
      #vesc-diagnostics-logs-panel {
        position: fixed !important;
        top: 10px !important;
        left: 10px !important;
        right: 10px !important;
        bottom: 46px !important;
        width: auto !important;
        max-width: none !important;
        min-width: 0 !important;
        z-index: 100010 !important;
      }
    }
    </style>"""
    content = re.sub(r'<style>.*?</style>', viewport_css, content, flags=re.DOTALL)

    # Replace #screen container with #qt-container (defaulting to viewport-mobile)
    content = content.replace(
        '<div id="screen"></div>',
        '<div id="qt-container" class="viewport-mobile"></div>'
    )
    content = content.replace(
        "const screen = document.querySelector('#screen');",
        "const screen = document.querySelector('#qt-container');"
    )
    content = content.replace(
        'onLoaded: () => showUi(screen),',
        'onLoaded: () => { showUi(screen); const c = document.querySelector("#qt-container canvas") || document.querySelector("canvas"); if (c) c.id = "qtcanvas"; [10, 100, 300].forEach(delay => setTimeout(() => window.dispatchEvent(new Event("resize")), delay)); },'
    )

    # 2. Pass stdout and stderr hooks into qtLoad
    def patch_qtload(m):
        return m.group(0) + """
                stdout: function(val) {
                    if (typeof val === 'number') {
                        if (val === 10) {
                            if (window.__logToScreen) window.__logToScreen('[STDOUT] ' + (window.__stdoutBuf || ''));
                            window.__stdoutBuf = '';
                        } else {
                            window.__stdoutBuf = (window.__stdoutBuf || '') + String.fromCharCode(val);
                        }
                    } else if (typeof val === 'string') {
                        if (window.__logToScreen) window.__logToScreen('[STDOUT] ' + val);
                    }
                },
                stderr: function(val) {
                    if (typeof val === 'number') {
                        if (val === 10) {
                            if (window.__logToScreen) window.__logToScreen('[STDERR] ' + (window.__stderrBuf || ''), 'red');
                            window.__stderrBuf = '';
                        } else {
                            window.__stderrBuf = (window.__stderrBuf || '') + String.fromCharCode(val);
                        }
                    } else if (typeof val === 'string') {
                        if (window.__logToScreen) window.__logToScreen('[STDERR] ' + val, 'red');
                    }
                },"""

    if "stdout: function(val)" not in content:
        content, _ = re.subn(r'qtLoad\s*\(\s*\{', patch_qtload, content, count=1)

    # Capture the Emscripten module instance from qtLoad
    content = content.replace(
        'const instance = await qtLoad(',
        'const instance = window.Module = await qtLoad('
    )

    # Replace viewport meta tag with mobile safe-area cover
    content = re.sub(
        r'<meta name="viewport"[^>]*>',
        '<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover"/>',
        content
    )

    # 3. Inject HUD, Web Serial controls, and bridge scripts
    hud_html = """
    <!-- Floating Status / Telemetry Popover Tray -->
    <div id="vesc-diagnostics-stats-panel" style="display:none;position:fixed;bottom:46px;left:8px;width:340px;max-width:calc(100vw - 16px);background:rgba(15,23,42,0.96);backdrop-filter:blur(10px);border:1px solid #0284c7;border-radius:8px;box-shadow:0 8px 30px rgba(0,0,0,0.7);z-index:100005;font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:11px;color:#e2e8f0;padding:10px;box-sizing:border-box;">
      <div style="display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid rgba(255,255,255,0.1);padding-bottom:6px;margin-bottom:8px;">
        <span style="color:#38bdf8;font-weight:700;">📊 VESC System & Link Status</span>
        <button type="button" onclick="window.toggleStatsPanel()" style="background:#dc2626;border:none;color:#fff;padding:2px 6px;font-size:10px;border-radius:3px;cursor:pointer;">✖</button>
      </div>
      <div style="display:grid;grid-template-columns:auto 1fr;gap:6px 12px;line-height:1.4;">
        <span style="color:#94a3b8;">🔌 Web Serial:</span>
        <span id="diag-popover-serial-status" style="color:#94a3b8;">Disconnected</span>
        <span style="color:#94a3b8;">⚡ Serial Baud:</span>
        <span id="diag-popover-serial-baud" style="color:#a5f3fc;">--</span>
        <span style="color:#94a3b8;">📈 Serial I/O:</span>
        <span id="diag-popover-serial-io" style="color:#cbd5e1;">RX: 0 B / TX: 0 B</span>
        <span style="color:#94a3b8;">📡 Bluetooth:</span>
        <span id="diag-popover-ble-status" style="color:#94a3b8;">Disconnected</span>
        <span style="color:#94a3b8;">🔒 Isolation (COI):</span>
        <span id="diag-popover-coi">checking...</span>
        <span style="color:#94a3b8;">⚙️ Qt WASM:</span>
        <span id="diag-popover-qt" style="color:#fde047;">Initializing...</span>
      </div>
    </div>

    <!-- VESC Tool WASM Diagnostics Bottom Bar -->
    <div id="vesc-diagnostics-bar" style="z-index:100000;position:relative;pointer-events:auto;width:100%;height:38px;padding-bottom:env(safe-area-inset-bottom, 0px);flex-shrink:0;box-sizing:border-box;background:rgba(15,23,42,0.98);backdrop-filter:blur(6px);color:#e2e8f0;font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:11px;border-top:1px solid #0284c7;display:flex;align-items:center;justify-content:space-between;padding-left:max(8px, env(safe-area-inset-left, 8px));padding-right:max(8px, env(safe-area-inset-right, 8px));box-shadow:0 -2px 10px rgba(0,0,0,0.5);user-select:none;gap:6px;">
      <div style="display:flex;align-items:center;gap:6px;flex-wrap:nowrap;overflow:hidden;height:100%;min-width:0;">
        <span style="color:#38bdf8;font-weight:bold;white-space:nowrap;flex-shrink:0;">⚡ <span class="hide-on-compact">VESC </span>WASM</span>
        <button id="btn-webserial-connect" type="button" onclick="window.toggleWebSerial()" style="position:relative;z-index:100002;pointer-events:auto;background:#0284c7;border:1px solid #0369a1;color:#ffffff;padding:3px 8px;font-size:11px;font-weight:600;border-radius:3px;cursor:pointer;display:inline-flex;align-items:center;gap:4px;white-space:nowrap;flex-shrink:0;">
          🔌 <span class="btn-text">Connect USB</span>
        </button>
        <button id="btn-viewport-toggle" type="button" onclick="window.toggleViewportMode()" style="position:relative;z-index:100002;pointer-events:auto;background:#334155;border:1px solid #475569;color:#ffffff;padding:3px 8px;font-size:11px;font-weight:600;border-radius:3px;cursor:pointer;display:inline-flex;align-items:center;gap:4px;white-space:nowrap;flex-shrink:0;">📱 View: Mobile</button>
        <button id="btn-toggle-stats" type="button" onclick="window.toggleStatsPanel()" style="position:relative;z-index:100002;pointer-events:auto;background:#1e293b;border:1px solid #38bdf8;color:#38bdf8;padding:3px 8px;font-size:11px;font-weight:600;border-radius:3px;cursor:pointer;display:inline-flex;align-items:center;gap:4px;white-space:nowrap;flex-shrink:0;">📊 Status</button>
        <div id="diag-inline-stats" class="desktop-only-stats" style="display:flex;align-items:center;gap:8px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">
          <span>Serial: <span id="diag-val-serial-status" style="color:#94a3b8;">Disconnected</span></span>
          <span>BLE: <span id="diag-val-ble-status" style="color:#94a3b8;">Disconnected</span></span>
          <span>Baud: <span id="diag-val-serial-baud" style="color:#a5f3fc;">--</span></span>
          <span>I/O: <span id="diag-val-serial-io" style="color:#cbd5e1;">RX: 0 B / TX: 0 B</span></span>
          <span>COI: <span id="diag-val-coi">checking...</span></span>
          <span>Qt: <span id="diag-val-qt" style="color:#fde047;">Initializing...</span></span>
        </div>
      </div>
      <div style="display:flex;gap:4px;align-items:center;position:relative;z-index:100000;pointer-events:auto;flex-shrink:0;margin-left:auto;">
        <button id="btn-copy-logs-mini" class="hide-on-compact" type="button" onclick="window.copyLogsToClipboard()" style="background:#059669;border:1px solid #047857;color:#ffffff;padding:3px 8px;font-size:10px;border-radius:3px;cursor:pointer;white-space:nowrap;">📋 Copy</button>
        <button class="hide-on-compact" type="button" onclick="const l=document.getElementById('vesc-diagnostics-logs');if(l)l.innerHTML='';" style="background:#334155;border:1px solid #475569;color:#f1f5f9;padding:3px 8px;font-size:10px;border-radius:3px;cursor:pointer;white-space:nowrap;">Clear</button>
        <button id="btn-toggle-logs" type="button" onclick="window.toggleLogsPanel()" style="background:#0284c7;border:1px solid #0369a1;color:#ffffff;padding:3px 10px;font-size:11px;font-weight:600;border-radius:3px;cursor:pointer;white-space:nowrap;">Toggle Logs 📑</button>
      </div>
    </div>

    <!-- Side Log Panel -->
    <div id="vesc-diagnostics-logs-panel" style="display:none;position:fixed;top:10px;right:10px;bottom:50px;width:540px;max-width:calc(50vw - 245px);min-width:320px;background:rgba(15,23,42,0.96);backdrop-filter:blur(10px);border:1px solid rgba(2,132,199,0.4);border-radius:8px;box-shadow:0 8px 32px rgba(0,0,0,0.8);z-index:100000;flex-direction:column;font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:11px;color:#e2e8f0;">
      <div style="display:flex;align-items:center;justify-content:space-between;padding:8px 12px;background:rgba(30,41,59,0.98);border-bottom:1px solid rgba(255,255,255,0.1);border-radius:8px 8px 0 0;font-weight:600;flex-wrap:wrap;gap:6px;">
        <span style="color:#38bdf8;font-size:11px;">📋 VESC Diagnostics & Logs</span>
        <div style="display:flex;gap:6px;align-items:center;">
          <button id="btn-copy-logs" type="button" onclick="window.copyLogsToClipboard()" style="background:#059669;border:1px solid #047857;color:#ffffff;padding:2px 8px;font-size:10px;border-radius:3px;cursor:pointer;font-weight:600;">📋 Copy Logs</button>
          <button id="btn-toggle-filter-serial" type="button" onclick="window.toggleSerialFilter()" style="background:#0284c7;border:1px solid #0369a1;color:#f1f5f9;padding:2px 8px;font-size:10px;border-radius:3px;cursor:pointer;">🔇 Serial Hex Hidden</button>
          <button type="button" onclick="const l=document.getElementById('vesc-diagnostics-logs');if(l)l.innerHTML='';" style="background:#334155;border:1px solid #475569;color:#f1f5f9;padding:2px 8px;font-size:10px;border-radius:3px;cursor:pointer;">Clear</button>
          <button type="button" onclick="window.toggleLogsPanel()" style="background:#dc2626;border:1px solid #b91c1c;color:#ffffff;padding:2px 8px;font-size:10px;border-radius:3px;cursor:pointer;">✖ Close</button>
        </div>
      </div>
      <div style="display:flex;padding:4px 8px;background:rgba(15,23,42,0.8);border-bottom:1px solid rgba(255,255,255,0.05);gap:6px;">
        <input id="log-filter-input" type="text" placeholder="Search / filter logs (e.g. error, warn, mcconf)..." oninput="window.applyLogFilters()" style="flex:1;background:#1e293b;border:1px solid #334155;color:#f1f5f9;padding:3px 8px;font-size:10px;border-radius:3px;outline:none;" />
      </div>
      <div id="vesc-diagnostics-logs" style="flex:1;overflow-y:auto;padding:8px 12px;line-height:1.45;word-break:break-all;white-space:pre-wrap;"></div>
    </div>
    <script>
    (function() {
      const logContainer = document.getElementById('vesc-diagnostics-logs');
      let hideSerialTraffic = true;

      window.toggleStatsPanel = function() {
        const p = document.getElementById('vesc-diagnostics-stats-panel');
        const btn = document.getElementById('btn-toggle-stats');
        if (!p) return;
        const isHidden = (p.style.display === 'none' || !p.style.display);
        p.style.display = isHidden ? 'block' : 'none';
        if (btn) {
          btn.style.background = isHidden ? '#0284c7' : '#1e293b';
          btn.style.color = isHidden ? '#ffffff' : '#38bdf8';
        }
      };

      window.toggleSerialFilter = function() {
        hideSerialTraffic = !hideSerialTraffic;
        const btn = document.getElementById('btn-toggle-filter-serial');
        if (btn) {
          btn.style.background = hideSerialTraffic ? '#0284c7' : '#475569';
          btn.style.borderColor = hideSerialTraffic ? '#0369a1' : '#64748b';
          btn.textContent = hideSerialTraffic ? '🔇 Serial Hex Hidden' : '🔊 Show Serial Hex';
        }
        window.applyLogFilters();
      };

      window.applyLogFilters = function() {
        const query = (document.getElementById('log-filter-input')?.value || '').toLowerCase().trim();
        if (!logContainer) return;
        const items = logContainer.children;
        for (let i = 0; i < items.length; i++) {
          const it = items[i];
          const isSerial = it.dataset.isSerial === 'true';
          if (hideSerialTraffic && isSerial) {
            it.style.display = 'none';
            continue;
          }
          if (query && !it.textContent.toLowerCase().includes(query)) {
            it.style.display = 'none';
            continue;
          }
          it.style.display = '';
        }
      };

      window.toggleLogsPanel = function() {
        const p = document.getElementById('vesc-diagnostics-logs-panel');
        if (!p) return;
        const isHidden = (p.style.display === 'none' || !p.style.display);
        p.style.display = isHidden ? 'flex' : 'none';
      };

      window.copyLogsToClipboard = function() {
        if (!logContainer) return;
        const btn = document.getElementById('btn-copy-logs');
        const miniBtn = document.getElementById('btn-copy-logs-mini');
        const items = logContainer.children;
        const visibleLines = [];
        for (let i = 0; i < items.length; i++) {
          if (items[i].style.display !== 'none') {
            visibleLines.push(items[i].textContent);
          }
        }
        const linesToCopy = visibleLines.length > 1000 ? visibleLines.slice(-1000) : visibleLines;
        const fullText = linesToCopy.join(String.fromCharCode(10));

        const onSuccess = function() {
          if (btn) {
            const orig = btn.textContent;
            btn.textContent = '✓ Copied!';
            btn.style.background = '#10b981';
            setTimeout(function() {
              btn.textContent = orig;
              btn.style.background = '#059669';
            }, 1500);
          }
          if (miniBtn) {
            miniBtn.textContent = '✓ Copied';
            miniBtn.style.background = '#10b981';
            setTimeout(function() {
              miniBtn.textContent = '📋 Copy';
              miniBtn.style.background = '#059669';
            }, 1500);
          }
        };

        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(fullText).then(onSuccess).catch(function() {
            fallbackCopy(fullText, onSuccess);
          });
        } else {
          fallbackCopy(fullText, onSuccess);
        }
      };

      function fallbackCopy(text, cb) {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.top = '0';
        ta.style.left = '0';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.focus();
        ta.select();
        try {
          document.execCommand('copy');
          if (cb) cb();
        } catch(e) {
          console.error('Copy fallback failed: ', e);
        }
        document.body.removeChild(ta);
      }

      function isFilteredMessage(text) {
        if (typeof text !== 'string') return false;
        return text.includes('Failed to link shader program') ||
               text.includes('Failed to build graphics pipeline state') ||
               text.includes('res//primitives/Cube.mesh') ||
               text.includes('Failed to load mesh');
      }

      function bytesToHex(arr) {
        if (!arr || !arr.length) return '';
        const hex = [];
        for (let i = 0; i < arr.length; i++) {
          hex.push(arr[i].toString(16).padStart(2, '0'));
        }
        return hex.join(' ');
      }

      window.__logToScreen = function(text, color) {
        if (!logContainer || isFilteredMessage(text)) return;
        const item = document.createElement('div');
        item.style.padding = '1px 0';
        item.style.borderBottom = '1px solid rgba(255,255,255,0.04)';
        if (color) {
          item.style.color = color;
        } else if (typeof text === 'string' && (text.includes('[STDERR]') || text.includes('[ERROR]'))) {
          item.style.color = '#f87171';
        } else if (typeof text === 'string' && text.includes('[WARN]')) {
          item.style.color = '#facc15';
        } else {
          item.style.color = '#cbd5e1';
        }
        const ts = new Date().toTimeString().split(' ')[0] + '.' + String(new Date().getMilliseconds()).padStart(3, '0');
        item.textContent = '[' + ts + '] ' + text;

        const isSerial = typeof text === 'string' && (
          text.startsWith('[SERIAL TX]') ||
          text.startsWith('[SERIAL RX]') ||
          text.includes('writeData called with')
        );
        if (isSerial) {
          item.dataset.isSerial = 'true';
          if (hideSerialTraffic) {
            item.style.display = 'none';
          }
        } else {
          const query = (document.getElementById('log-filter-input')?.value || '').toLowerCase().trim();
          if (query && !item.textContent.toLowerCase().includes(query)) {
            item.style.display = 'none';
          }
        }

        logContainer.appendChild(item);
        if (item.style.display !== 'none') {
          logContainer.scrollTop = logContainer.scrollHeight;
        }
      };

      // 1. Live status monitors
      function getQtStatus() {
        const el = document.getElementById('qtstatus') || document.querySelector('.qtstatus');
        if (el && el.innerText && el.innerText.trim().length > 0) {
          return el.innerText.trim();
        }
        const spinner = document.getElementById('qtspinner');
        if (spinner && spinner.style.display !== 'none') {
          return 'Loading / Compiling...';
        }
        return 'Running / Ready';
      }

      function updateHud() {
        const isCoi = window.crossOriginIsolated;
        const coiHtml = isCoi
          ? '<span style="color:#4ade80;font-weight:bold;">true (Isolated)</span>'
          : '<span style="color:#f87171;font-weight:bold;">false (NOT Isolated - SAB Disabled!)</span>';
        const coiEl = document.getElementById('diag-val-coi');
        if (coiEl) coiEl.innerHTML = coiHtml;
        const coiPop = document.getElementById('diag-popover-coi');
        if (coiPop) coiPop.innerHTML = coiHtml;

        const qtStatus = getQtStatus();
        const qtEl = document.getElementById('diag-val-qt');
        if (qtEl) qtEl.textContent = qtStatus;
        const qtPop = document.getElementById('diag-popover-qt');
        if (qtPop) qtPop.textContent = qtStatus;
      }
      updateHud();
      setInterval(updateHud, 500);

      // 2. Global log redirection
      const origLog = console.log;
      const origWarn = console.warn;
      const origError = console.error;

      console.log = function(...args) {
        const joined = args.map(a => (typeof a === 'object' ? JSON.stringify(a) : String(a))).join(' ');
        if (!isFilteredMessage(joined)) {
          try {
            window.__logToScreen(joined);
          } catch (_) {}
        }
        origLog.apply(console, args);
      };

      console.warn = function(...args) {
        const joined = args.map(a => (typeof a === 'object' ? JSON.stringify(a) : String(a))).join(' ');
        if (!isFilteredMessage(joined)) {
          try {
            window.__logToScreen('[WARN] ' + joined, '#facc15');
          } catch (_) {}
        }
        origWarn.apply(console, args);
      };

      console.error = function(...args) {
        const joined = args.map(a => (a instanceof Error ? (a.stack || a.message) : (typeof a === 'object' ? JSON.stringify(a) : String(a)))).join(' ');
        if (!isFilteredMessage(joined)) {
          try {
            window.__logToScreen('[ERROR] ' + joined, '#f87171');
          } catch (_) {}
        }
        origError.apply(console, args);
      };

      window.onerror = function(msg, url, line, col, error) {
        const errText = error ? (error.stack || error.message) : (msg + ' (' + url + ':' + line + ':' + col + ')');
        if (!isFilteredMessage(errText)) {
          window.__logToScreen('[UNCAUGHT ERROR] ' + errText, '#ef4444');
        }
        return false;
      };

      window.onunhandledrejection = function(event) {
        const reason = event.reason ? (event.reason.stack || event.reason.message || event.reason) : 'Unknown Promise Rejection';
        if (!isFilteredMessage(String(reason))) {
          window.__logToScreen('[UNHANDLED REJECTION] ' + reason, '#ef4444');
        }
      };

      // Module hooks
      window.Module = window.Module || {};
      var stdoutBuffer = '';
      var stderrBuffer = '';

      window.Module.stdout = function(charCode) {
        if (typeof charCode === 'number') {
          if (charCode === 10) {
            if (!isFilteredMessage(stdoutBuffer)) {
              window.__logToScreen('[STDOUT] ' + stdoutBuffer);
            }
            stdoutBuffer = '';
          } else {
            stdoutBuffer += String.fromCharCode(charCode);
          }
        } else if (typeof charCode === 'string') {
          if (!isFilteredMessage(charCode)) {
            window.__logToScreen('[STDOUT] ' + charCode);
          }
        }
      };

      window.Module.stderr = function(charCode) {
        if (typeof charCode === 'number') {
          if (charCode === 10) {
            if (!isFilteredMessage(stderrBuffer)) {
              window.__logToScreen('[STDERR] ' + stderrBuffer, 'red');
            }
            stderrBuffer = '';
          } else {
            stderrBuffer += String.fromCharCode(charCode);
          }
        } else if (typeof charCode === 'string') {
          if (!isFilteredMessage(charCode)) {
            window.__logToScreen('[STDERR] ' + charCode, 'red');
          }
        }
      };

      const origPrint = window.Module.print || console.log;
      const origPrintErr = window.Module.printErr || console.error;
      window.Module.print = function(text) {
        if (typeof text === 'string') {
          if (!isFilteredMessage(text)) {
            window.__logToScreen('[STDOUT] ' + text);
          }
        }
        origPrint(text);
      };
      window.Module.printErr = function(text) {
        if (typeof text === 'string') {
          if (!isFilteredMessage(text)) {
            window.__logToScreen('[STDERR] ' + text, 'red');
          }
        }
        origPrintErr(text);
      };

      // 3. Web Serial API Integration
      let serialPort = null;
      let serialReader = null;
      let serialWriter = null;
      let isSerialConnected = false;
      let bytesRx = 0;
      let bytesTx = 0;
      let txBytes = 0;

      function formatBytes(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
      }

      function updateSerialHud() {
        const btn = document.getElementById('btn-webserial-connect');
        const statusEl = document.getElementById('diag-val-serial-status');
        const baudEl = document.getElementById('diag-val-serial-baud');
        const ioEl = document.getElementById('diag-val-serial-io');

        const popStatus = document.getElementById('diag-popover-serial-status');
        const popBaud = document.getElementById('diag-popover-serial-baud');
        const popIo = document.getElementById('diag-popover-serial-io');

        const statusHtml = isSerialConnected
          ? '<span style="color:#4ade80;font-weight:bold;">Connected</span>'
          : '<span style="color:#94a3b8;">Disconnected</span>';
        const baudText = isSerialConnected ? '115200' : '--';
        const ioText = 'RX: ' + formatBytes(bytesRx) + ' / TX: ' + formatBytes(bytesTx || txBytes);

        if (statusEl) statusEl.innerHTML = statusHtml;
        if (popStatus) popStatus.innerHTML = statusHtml;
        if (baudEl) baudEl.textContent = baudText;
        if (popBaud) popBaud.textContent = baudText;
        if (ioEl) ioEl.textContent = ioText;
        if (popIo) popIo.textContent = ioText;

        if (btn) {
          if (isSerialConnected) {
            btn.innerHTML = '🔌 <span class="btn-text">Disconnect USB</span>';
            btn.style.background = '#dc2626';
            btn.style.borderColor = '#b91c1c';
          } else {
            btn.innerHTML = '🔌 <span class="btn-text">Connect USB</span>';
            btn.style.background = '#0284c7';
            btn.style.borderColor = '#0369a1';
          }
        }
      }

      function updateHudCounters() {
        updateSerialHud();
      }
      window.updateHudCounters = updateHudCounters;

      window.toggleWebSerial = async function() {
        window.__logToScreen('[SERIAL] Connect USB button triggered (isSerialConnected=' + isSerialConnected + ')');
        if (isSerialConnected) {
          await disconnectWebSerial();
        } else {
          await connectWebSerial();
        }
      };

      async function connectWebSerial() {
        if (!('serial' in navigator)) {
          window.__logToScreen('[SERIAL ERROR] Web Serial API is not supported in this browser. Please use Chrome, Edge, or Opera.', '#f87171');
          alert('Web Serial API is not supported in this browser.\\nPlease use Google Chrome, Microsoft Edge, or a Chromium-based browser.');
          return;
        }

        try {
          window.__logToScreen('[SERIAL] Requesting USB serial device via browser dialog...');
          serialPort = await navigator.serial.requestPort();
          if (serialPort) {
            if (window._webSerialBridge) {
              var portId = window._webSerialBridge.findOrAddPort(serialPort);
              window.__logToScreen('[SERIAL] Port granted (ID: ' + portId + '). Connecting VESC Interface...', '#4ade80');
            }
            isSerialConnected = true;
            updateSerialHud();
            // Notify WASM module (VescInterface) to connect through QSerialPort / webserialbridge
            notifyWasmConnection(1);
          }
        } catch (err) {
          window.__logToScreen('[SERIAL ERROR] Connection failed: ' + (err.message || err), '#f87171');
          await disconnectWebSerial();
        }
      }

      async function disconnectWebSerial() {
        isSerialConnected = false;
        window.__logToScreen('[SERIAL] Disconnecting serial port...');
        notifyWasmConnection(0);
        updateSerialHud();
        window.__logToScreen('[SERIAL] Disconnected.');
      }

      // Web Bluetooth Controls
      window.updateBleHud = function() {
        const btn = document.getElementById('btn-webble-connect');
        const statusEl = document.getElementById('diag-val-ble-status');
        const popStatus = document.getElementById('diag-popover-ble-status');
        const bridge = window._webBleBridge;
        const isConn = bridge && bridge.isConnected;
        let bleHtml = '<span style="color:#94a3b8;">Disconnected</span>';
        if (isConn) {
          bleHtml = '<span style="color:#4ade80;font-weight:bold;">' + (bridge.activeDevice?.name || 'Connected') + '</span>';
        } else if (bridge && bridge.isConnecting) {
          bleHtml = '<span style="color:#fde047;">Connecting...</span>';
        }

        if (statusEl) statusEl.innerHTML = bleHtml;
        if (popStatus) popStatus.innerHTML = bleHtml;

        if (btn) {
          if (isConn) {
            btn.innerHTML = '📡 <span class="btn-text">Disconnect BLE</span>';
            btn.style.background = '#dc2626';
            btn.style.borderColor = '#b91c1c';
          } else {
            btn.innerHTML = '📡 <span class="btn-text">Connect BLE</span>';
            btn.style.background = '#059669';
            btn.style.borderColor = '#047857';
          }
        }
      };

      window.toggleWebBle = async function() {
        const bridge = window._webBleBridge;
        if (bridge && bridge.isConnected) {
          if (typeof Module !== 'undefined' && Module._webble_disconnect) {
            Module._webble_disconnect();
          } else if (bridge.gattServer) {
            bridge.gattServer.disconnect();
          }
        } else {
          if (!('bluetooth' in navigator)) {
            window.__logToScreen('[BLE ERROR] Web Bluetooth API is not supported in this browser. Please use Chrome or Edge.', '#f87171');
            alert('Web Bluetooth API is not supported in this browser.\\nPlease use Google Chrome, Microsoft Edge, or a Chromium-based browser.');
            return;
          }
          if (typeof Module !== 'undefined' && Module._webble_request_device) {
            Module._webble_request_device();
          } else if (window._webBleBridge) {
            try {
              window.__logToScreen('[BLE] Requesting Bluetooth device via browser dialog...');
              const dev = await navigator.bluetooth.requestDevice({
                acceptAllDevices: true,
                optionalServices: ['6e400001-b5a3-f393-e0a9-e50e24dcca9e']
              });
              window._webBleBridge.devices[dev.id] = dev;
              window._webBleBridge.activeDevice = dev;
              window._webBleBridge.dispatchScan(dev.name || 'VESC BLE', dev.id);
              if (typeof Module !== 'undefined' && Module._webble_connect) {
                Module._webble_connect(0);
              }
            } catch(e) {
              console.warn("BLE requestDevice error:", e);
              window.__logToScreen('[BLE] ' + (e.message || e), '#facc15');
            }
          }
        }
      };

      function notifyWasmConnection(connected) {
        if (typeof window.__wasm_serial_set_connected_js === 'function') {
          window.__wasm_serial_set_connected_js(connected ? true : false);
        } else if (window.Module && typeof window.Module._wasm_serial_set_connected === 'function') {
          window.Module._wasm_serial_set_connected(connected ? 1 : 0);
        }
      }

      // Expose outgoing serial TX hook for C++ wasm_serial_tx
      window.wasm_serial_tx = async function(uint8Array) {
        if (serialWriter) {
          try {
            const data = (uint8Array && uint8Array.buffer && uint8Array.buffer instanceof SharedArrayBuffer)
              ? new Uint8Array(uint8Array)
              : (uint8Array instanceof Uint8Array ? uint8Array : new Uint8Array(uint8Array));

            const hexStr = bytesToHex(data);
            window.__logToScreen(`[SERIAL TX] (${data.length} bytes): ${hexStr}`, '#86efac');

            await serialWriter.write(data);
            bytesTx += data.length;
            txBytes = bytesTx;
            updateHudCounters();
          } catch (err) {
            console.error('[SERIAL TX ERROR]', err);
            window.__logToScreen('[SERIAL TX ERROR] ' + (err.message || err), '#f87171');
          }
        }
      };

      // Listen for device plug/unplug events
      if ('serial' in navigator) {
        navigator.serial.addEventListener('disconnect', (event) => {
          if (serialPort && event.target === serialPort) {
            window.__logToScreen('[SERIAL] Device unplugged.', '#facc15');
            disconnectWebSerial();
          }
        });
      }

      // Viewport mode toggle function and initial mode detection
      window.toggleViewportMode = function() {
        const container = document.getElementById('qt-container');
        const btn = document.getElementById('btn-viewport-toggle');
        if (!container) return;

        const isMobile = container.classList.contains('viewport-mobile');
        if (isMobile) {
          container.classList.remove('viewport-mobile');
          container.classList.add('viewport-desktop');
          if (btn) btn.innerHTML = '🖥️ View: Desktop';
          try {
            const url = new URL(window.location);
            url.searchParams.set('desktop', '1');
            window.history.replaceState({}, '', url);
          } catch (_) {}
          if (window.__logToScreen) window.__logToScreen('[VIEWPORT] Switched to Desktop Viewport (100vw).');
        } else {
          container.classList.remove('viewport-desktop');
          container.classList.add('viewport-mobile');
          if (btn) btn.innerHTML = '📱 View: Mobile';
          try {
            const url = new URL(window.location);
            url.searchParams.delete('desktop');
            window.history.replaceState({}, '', url);
          } catch (_) {}
          if (window.__logToScreen) window.__logToScreen('[VIEWPORT] Switched to Mobile Viewport (480px).');
        }

        const canvas = container.querySelector('canvas') || document.querySelector('canvas');
        if (canvas && canvas.id !== 'qtcanvas') {
          canvas.id = 'qtcanvas';
        }

        [10, 50, 150, 300].forEach(delay => {
          setTimeout(() => window.dispatchEvent(new Event('resize')), delay);
        });
      };

      // Check initial viewport mode based on URL (?desktop=1)
      (function initViewport() {
        try {
          const params = new URLSearchParams(window.location.search);
          const isDesktop = params.get('desktop') === '1';
          const container = document.getElementById('qt-container');
          const btn = document.getElementById('btn-viewport-toggle');
          if (isDesktop) {
            if (container) {
              container.classList.remove('viewport-mobile');
              container.classList.add('viewport-desktop');
            }
            if (btn) btn.innerHTML = '🖥️ View: Desktop';
            [10, 50, 150, 300].forEach(delay => {
              setTimeout(() => window.dispatchEvent(new Event('resize')), delay);
            });
          }
        } catch (_) {}
      })();

      // Attach explicit event listeners to Connect USB button
      const connectBtn = document.getElementById('btn-webserial-connect');
      if (connectBtn) {
        connectBtn.addEventListener('click', function(e) {
          e.preventDefault();
          e.stopPropagation();
          window.toggleWebSerial();
        });
      }

      // Attach explicit event listeners to Connect BLE button
      const bleBtn = document.getElementById('btn-webble-connect');
      if (bleBtn) {
        bleBtn.addEventListener('click', function(e) {
          e.preventDefault();
          e.stopPropagation();
          window.toggleWebBle();
        });
      }

      // Attach explicit event listener to Viewport toggle button
      const vToggleBtn = document.getElementById('btn-viewport-toggle');
      if (vToggleBtn) {
        vToggleBtn.addEventListener('click', function(e) {
          e.preventDefault();
          e.stopPropagation();
          window.toggleViewportMode();
        });
      }

      // Observer to keep canvas id set to qtcanvas
      const cObserver = new MutationObserver(function() {
        const c = document.querySelector('#qt-container canvas') || document.querySelector('canvas');
        if (c && c.id !== 'qtcanvas') {
          c.id = 'qtcanvas';
        }
      });
      const cTarget = document.getElementById('qt-container');
      if (cTarget) {
        cObserver.observe(cTarget, { childList: true, subtree: true });
      }

      window.__logToScreen('Diagnostics & Web Serial overlay initialized.');
    })();
    </script>
    """

    if '</body>' in content:
        content = content.replace('</body>', hud_html + '\n</body>')
    elif '</BODY>' in content:
        content = content.replace('</BODY>', hud_html + '\n</BODY>')
    else:
        content += hud_html

    # Write to both vesc_tool_7.00.html and index.html
    out_html = os.path.join(target_dir, "vesc_tool_7.00.html")
    out_index = os.path.join(target_dir, "index.html")
    with open(out_html, "w", encoding="utf-8") as f:
        f.write(content)
    with open(out_index, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[PATCH SUCCESS] Patched {out_html} and {out_index}")
    return True

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "build-wasm"
    patch_html(target)
