# Running TrueNorth on Windows

Windows-specific companion to [`doc/testnet.md`](testnet.md) — read that
first for the testnet's background, chain parameters, and general operator
expectations (what's running, why testnet4, reporting issues). This doc
covers only what differs on Windows: Tor setup, paths, process management
without a POSIX shell, and an unattended-mining script.

## Requirements

Same as the general runbook: Tor 0.4.x+ with SOCKS on `127.0.0.1:9050`,
~1 GiB RAM, ~4 GiB disk. Windows 10/11 x86_64. The release binaries are
native `.exe` files — no WSL required.

## 1. Install Tor

The testnet is onion-only (see `doc/testnet.md`), so this step isn't
optional. The standalone Tor Expert Bundle is enough — the full Tor Browser
isn't needed:

1. Download the Tor Expert Bundle for Windows from
   [torproject.org](https://www.torproject.org/download/tor/)
2. Extract to `C:\tor\`
3. Run it, leave it running:
   ```powershell
   Start-Process -FilePath "C:\tor\tor.exe" -WindowStyle Hidden
   ```
4. First run takes 30-60s to establish circuits. Confirm it's up:
   ```powershell
   Test-NetConnection -ComputerName 127.0.0.1 -Port 9050
   ```

## 2. Get the binary

Same as `doc/testnet.md`'s "Getting the binary" section, but grab
`truenorth-<tag>-windows-x86_64.zip` and verify its `.sha256`:

```powershell
Get-FileHash truenorth-<tag>-windows-x86_64.zip -Algorithm SHA256
```

Extract to `C:\truenorth\`. Windows will likely show a SmartScreen
"unknown publisher" warning on the `.exe` files — expected for a project
without a code-signing certificate, not itself a sign anything's wrong,
but it's exactly the situation the hash check above is for.

## 3. Configure

Windows' equivalent of `~/.truenorth/truenorth.conf` is
`%APPDATA%\TrueNorth\truenorth.conf`:

```powershell
New-Item -ItemType Directory -Force "$env:APPDATA\TrueNorth" | Out-Null
$bytes = New-Object byte[] 32
[System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
$RpcPass = -join ($bytes | ForEach-Object { $_.ToString('x2') })
@"
testnet4=1
proxy=127.0.0.1:9050

[testnet4]
addnode=3guf2wvltezb6w3tjjveumtt6lsnb7bjpts6ornk4hj4wlod4s7n53id.onion:49555
onlynet=onion
listen=1
listenonion=0

rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=tn_user
rpcpassword=$RpcPass
"@ | Set-Content "$env:APPDATA\TrueNorth\truenorth.conf"
```

Same `listenonion=0` guidance as the general runbook applies.

## 4. Start the node

Windows builds don't support the POSIX `-daemon` flag (that's a fork-based
mechanism); `Start-Process -WindowStyle Hidden` is the equivalent way to
run it detached:

```powershell
cd C:\truenorth\bin
Start-Process -FilePath ".\truenorthd.exe" -ArgumentList "-testnet4" -WindowStyle Hidden
.\truenorth-cli.exe -testnet4 -rpcwait getblockchaininfo
```

Same expectations as the general runbook from here: `-rpcwait` blocks
until the daemon answers, first connection can take 10-30s while Tor
circuits establish, and you want `getconnectioncount` > 0 before
continuing.

## 5. Start mining

```powershell
.\truenorth-cli.exe -testnet4 createwallet mywallet
$ADDR = .\truenorth-cli.exe -testnet4 -rpcwallet=mywallet getnewaddress

.\truenorth-miner.exe `
    -chain=testnet4 `
    -datadir="$env:APPDATA\TrueNorth" `
    -address=$ADDR `
    -threads=$([Environment]::ProcessorCount) `
    -budgetseconds=300
```

The miner authenticates against the daemon automatically from the
credentials in `truenorth.conf` — no separate RPC flags needed. Same
verification steps as the general runbook (`getblockcount` before/after,
`getbalances` for immature vs. spendable) apply unchanged.

> If you're on a release old enough that `truenorth-miner.exe -help`
> still lists a `-cli=` flag, add `-cli="C:\truenorth\bin\truenorth-cli.exe"`
> to the command above — that release predates the direct-RPC miner.

## Running unattended

A foreground PowerShell window dies the moment you close it. This script
starts Tor if it isn't already running, starts the node, waits for RPC,
then runs the miner in a self-restarting loop at **idle CPU priority** —
`IDLE_PRIORITY_CLASS`, Windows' equivalent of Linux's `SCHED_IDLE`, is only
scheduled when nothing else wants the CPU.

Save as `start-truenorth.ps1`, set `$Address`, run it:

```powershell
# TrueNorth testnet4 - full node + idle-priority self-restarting miner
# Edit $Address below before running.

$InstallDir = "C:\truenorth"
$BinDir     = "$InstallDir\bin"
$DataDir    = "$env:APPDATA\TrueNorth"
$ConfPath   = "$DataDir\truenorth.conf"
$DaemonExe  = "$BinDir\truenorthd.exe"
$CliExe     = "$BinDir\truenorth-cli.exe"
$MinerExe   = "$BinDir\truenorth-miner.exe"
$TorExe     = "C:\tor\tor.exe"
$Address    = "REPLACE_WITH_YOUR_ADDRESS"
$Threads    = [Environment]::ProcessorCount
$LogFile    = "$DataDir\miner.log"

# --- 0. Tor must be reachable before anything else is worth trying -------
function Test-Port($ComputerName, $Port, $TimeoutMs = 500) {
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $task = $client.ConnectAsync($ComputerName, $Port)
        $ok = $task.Wait($TimeoutMs) -and $client.Connected
        $client.Close()
        return $ok
    } catch { return $false }
}

if (-not (Test-Port "127.0.0.1" 9050)) {
    $existingTor = Get-Process -Name "tor" -ErrorAction SilentlyContinue
    if (-not $existingTor) {
        if (-not (Test-Path $TorExe)) {
            Write-Error "Tor isn't running and $TorExe doesn't exist. See step 1 above."
            exit 1
        }
        Write-Host "Starting Tor..."
        Start-Process -FilePath $TorExe -WindowStyle Hidden
    }
    Write-Host "Waiting for Tor to establish circuits (first run can take 30-60s)..."
    $torReady = $false
    for ($i = 0; $i -lt 90; $i++) {
        if (Test-Port "127.0.0.1" 9050) { $torReady = $true; break }
        Start-Sleep -Seconds 1
    }
    if (-not $torReady) {
        Write-Error "Tor never came up on 127.0.0.1:9050 after 90s."
        exit 1
    }
}
Write-Host "Tor is up."

# --- 1. Ensure datadir + conf exist ------------------------------------
if (-not (Test-Path $DataDir)) { New-Item -ItemType Directory -Path $DataDir | Out-Null }
if (-not (Test-Path $ConfPath)) {
    $bytes = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $RpcPass = -join ($bytes | ForEach-Object { $_.ToString('x2') })
    @"
testnet4=1
proxy=127.0.0.1:9050

[testnet4]
addnode=3guf2wvltezb6w3tjjveumtt6lsnb7bjpts6ornk4hj4wlod4s7n53id.onion:49555
onlynet=onion
listen=1
listenonion=0

rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=tn_user
rpcpassword=$RpcPass
"@ | Set-Content -Path $ConfPath
    Write-Host "Wrote fresh conf to $ConfPath (generated a new rpcpassword)"
}

# --- 2. Start the daemon if it isn't already running --------------------
$existingDaemon = Get-Process -Name "truenorthd" -ErrorAction SilentlyContinue
if (-not $existingDaemon) {
    Write-Host "Starting truenorthd..."
    $daemonProc = Start-Process -FilePath $DaemonExe -ArgumentList @("-testnet4", "-datadir=$DataDir") `
        -WindowStyle Hidden -PassThru
    Add-Content -Path $LogFile -Value "$(Get-Date -Format o) daemon started, pid=$($daemonProc.Id)"
} else {
    Write-Host "truenorthd already running (pid=$($existingDaemon.Id))"
}

# --- 3. Wait for RPC to come up -----------------------------------------
Write-Host "Waiting for RPC..."
$ready = $false
for ($i = 0; $i -lt 120; $i++) {
    & $CliExe -testnet4 "-datadir=$DataDir" getblockchaininfo *> $null
    if ($LASTEXITCODE -eq 0) { $ready = $true; break }
    Start-Sleep -Seconds 1
}
if (-not $ready) {
    Write-Error "truenorthd RPC did not respond after 120s -- check Task Manager and $DataDir\testnet4\debug.log"
    exit 1
}
Write-Host "RPC is up. Starting miner."

# --- 4. Idle-priority, self-restarting miner loop ------------------------
while ($true) {
    $minerArgs = @(
        "-chain=testnet4", "-datadir=$DataDir", "-address=$Address",
        "-threads=$Threads", "-budgetseconds=300"
    )
    $proc = Start-Process -FilePath $MinerExe -ArgumentList $minerArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "${LogFile}.out" -RedirectStandardError "${LogFile}.err"

    $proc.PriorityClass = 'Idle'
    Add-Content -Path $LogFile -Value "$(Get-Date -Format o) miner started, pid=$($proc.Id), priority=Idle"
    $proc.WaitForExit()
    Add-Content -Path $LogFile -Value "$(Get-Date -Format o) miner exited with code $($proc.ExitCode), restarting in 2s"
    Start-Sleep -Seconds 2
}
```

To start it automatically at login:

```powershell
schtasks /create /tn "TrueNorth Miner" /tr "powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\truenorth\start-truenorth.ps1" /sc onlogon /rl limited
```

`/rl limited` keeps it unelevated — idle-priority scheduling doesn't need
admin rights.

## Windows-specific troubleshooting

Report anything not covered here per `doc/testnet.md`'s "What to look for
and report" section.

| Symptom | Likely cause | Fix |
|---|---|---|
| Stuck waiting for RPC indefinitely | Tor isn't actually reachable | `Test-NetConnection 127.0.0.1 -Port 9050`; if it fails, Tor isn't running or hasn't finished bootstrapping |
| `Could not authenticate RPC, no cookie found and password not set` | A stale `truenorthd.exe` started before the current conf existed | `Get-Process truenorthd \| Stop-Process -Force`, then restart so it picks up the current conf |
| Daemon running, `getconnectioncount` stays 0 for a long time | Tor circuits still forming, or the seed is temporarily unreachable | Give it a few minutes; check `debug.log` for repeated "stale tip" past ~10 minutes as a sign it's genuinely stuck rather than just slow |
| SmartScreen blocks or warns on the `.exe` files | Unsigned binary, expected for this project | Verify the sha256 in step 2 before running anything, then proceed |
| Balance stuck at 0 after mining a block | Coinbase rewards need 100 confirmations | Check `immature` in `getbalances`, not `trusted`, until then |

## Performance tuning

See the README's [Performance tuning](../README.md#performance-tuning-huge-pages-and-numa)
section for `-largepages`/`-numa` details and the expected hashrate gains.
The Windows-specific step is granting `SeLockMemoryPrivilege` via
`gpedit.msc` → Local Computer Policy → Windows Settings → Security
Settings → Local Policies → User Rights Assignment → "Lock pages in
memory", added for the user that runs `truenorth-miner.exe` (re-login
required). NUMA pinning uses `libnuma` and is Linux-only; it's a no-op on
Windows builds.
