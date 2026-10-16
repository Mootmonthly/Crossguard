<#
.SYNOPSIS
    Starts the local Crossguard backend (Nakama + PostgreSQL in Docker Desktop) and waits until it
    answers. docs/backend-setup.md.

.DESCRIPTION
    First run: creates Backend\.env (gitignored) from Backend\.env.example with random keys, builds the
    Nakama image with our Go module (Backend\nakama\Dockerfile: no Go install needed), starts both
    containers and waits for http://127.0.0.1:7350/healthcheck. Prints the console URL and the flags a
    dedicated server needs.

    Exit codes: 0 running (or stopped); 1 Nakama did not get healthy; 4 Docker is not running.

.PARAMETER Down
    Stops the containers (the database is kept).

.PARAMETER Reset
    Stops the containers and DELETES the local database (every account and save).

.PARAMETER Logs
    Follows the Nakama log.

.EXAMPLE
    Tools\run_local_backend.ps1
    Tools\run_local_backend.ps1 -Down
#>
param(
    [switch]$Down,
    [switch]$Reset,
    [switch]$Logs
)

$ErrorActionPreference = "Stop"
$backend = (Resolve-Path (Join-Path $PSScriptRoot "..\Backend")).Path
Push-Location $backend
try {
    docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Docker is not running. Start Docker Desktop and try again." -ForegroundColor Red
        exit 4
    }

    if (-not (Test-Path ".env")) {
        $lines = foreach ($line in Get-Content ".env.example") {
            if ($line -match '^([A-Z_]+)=generate$') {
                $bytes = New-Object byte[] 24
                [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
                "$($Matches[1])=" + (($bytes | ForEach-Object { $_.ToString("x2") }) -join "")
            } else {
                $line
            }
        }
        # No BOM: docker compose reads the file.
        [System.IO.File]::WriteAllLines((Join-Path $backend ".env"), [string[]]$lines, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "Created Backend\.env with random keys (gitignored)."
    }

    $compose = @("compose", "-f", "docker-compose.yml", "--env-file", ".env")
    if ($Reset) { docker @compose down -v; Write-Host "Local database deleted."; exit 0 }
    if ($Down) { docker @compose down; exit 0 }
    if ($Logs) { docker @compose logs -f nakama; exit 0 }

    docker @compose up -d --build
    if ($LASTEXITCODE -ne 0) { exit 1 }

    Write-Host -NoNewline "Waiting for Nakama"
    for ($i = 0; $i -lt 90; $i++) {
        try {
            Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 "http://127.0.0.1:7350/healthcheck" | Out-Null
            Write-Host " ready."
            $httpKey = ((Get-Content ".env") | Where-Object { $_ -like "NAKAMA_HTTP_KEY=*" }) -replace '^NAKAMA_HTTP_KEY=', ''
            Write-Host ""
            Write-Host "  Game API:  http://127.0.0.1:7350"
            Write-Host "  Console:   http://127.0.0.1:7351  (NAKAMA_CONSOLE_USER / NAKAMA_CONSOLE_PASSWORD in Backend\.env)"
            Write-Host "  Dedicated server flags:"
            Write-Host "    -SaveBackend=Nakama -NakamaUrl=http://127.0.0.1:7350 -NakamaHttpKey=$httpKey"
            Write-Host "  Check it: python Tools\backend_smoke.py    Logs: -Logs    Stop: -Down    Wipe database: -Reset"
            exit 0
        } catch {
            Write-Host -NoNewline "."
            Start-Sleep -Seconds 2
        }
    }
    Write-Host ""
    Write-Host "Nakama did not become healthy in 3 minutes. Last log lines:" -ForegroundColor Red
    docker @compose logs --tail 40 nakama
    exit 1
} finally {
    Pop-Location
}
