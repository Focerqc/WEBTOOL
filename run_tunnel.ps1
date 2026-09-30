# Cloudflare Tunnel Auto-Restart Wrapper Script
# Ensures cloudflared runs reliably and reconnects after sleep/network drops.

$cloudflaredPath = "C:\Program Files (x86)\cloudflared\cloudflared.exe"
$targetUrl = "http://127.0.0.1:8080"

if (-not (Test-Path -Path $cloudflaredPath)) {
    # Fallback search in PATH if non-standard install
    $cmd = Get-Command "cloudflared.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        $cloudflaredPath = $cmd.Source
    } else {
        Write-Error "cloudflared.exe not found at '$cloudflaredPath' or in PATH."
        exit 1
    }
}

Write-Host "Starting Cloudflare Tunnel loop targeting $targetUrl..." -ForegroundColor Cyan

while ($true) {
    Write-Host "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] Launching cloudflared tunnel..." -ForegroundColor Green
    
    # Properly quote path when calling with call operator '&'
    & "$cloudflaredPath" tunnel --url $targetUrl
    
    $exitCode = $LASTEXITCODE
    Write-Host "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] cloudflared process stopped (Exit code: $exitCode). Reconnecting in 3 seconds..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3
}
