param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RawArgs
)

$ErrorActionPreference = "Stop"
$configPath = Join-Path $PSScriptRoot "admin_client_config.txt"

$hostName = "127.0.0.1"
$port = [uint16]17999
$account = ""
$password = ""
$commandToExecute = ""

function Load-Config {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        $line = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) {
            continue
        }

        $parts = $line -split '\|'
        if ($parts.Count -ge 4) {
            $parsedPort = [uint16]17999
            [uint16]::TryParse($parts[1].Trim(), [ref]$parsedPort) | Out-Null

            return @{
                Host     = $parts[0].Trim()
                Port     = $parsedPort
                Account  = $parts[2].Trim()
                Password = $parts[3].Trim()
            }
        }
    }
    return $null
}

$config = Load-Config $configPath

if ($config) {
    $hostName = $config.Host
    $port = $config.Port
    $account = $config.Account
    $password = $config.Password
}

if ($RawArgs -and $RawArgs.Count -gt 0) {
    if (-not $config) {
        if ($RawArgs.Count -ge 3) {
            $account = $RawArgs[0]
            $password = $RawArgs[1]
            $commandToExecute = ($RawArgs[2..($RawArgs.Count - 1)]) -join " "
        } else {
            throw "admin_client_config.txt missing. Usage: ./AdminClient <loginName> <loginPassword> <command>"
        }
    } else {
        $commandToExecute = $RawArgs -join " "
    }
} else {
    if (-not $config -or [string]::IsNullOrWhiteSpace($account) -or [string]::IsNullOrWhiteSpace($password)) {
        Write-Host "admin_client_config.txt not found or invalid format." -ForegroundColor Yellow
        $account = Read-Host "Enter Account Name"
        $password = Read-Host "Enter Password"
        Write-Host ""
    }
}

if ([string]::IsNullOrWhiteSpace($account) -or [string]::IsNullOrWhiteSpace($password)) {
    throw "Account or Password credentials are empty."
}

function Send-Packet {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [string]$Payload
    )

    $payloadBytes = [System.Text.Encoding]::UTF8.GetBytes($Payload)
    $totalSize = [uint32](4 + $payloadBytes.Length)
    $sizeBytes = [BitConverter]::GetBytes($totalSize)

    $Stream.Write($sizeBytes, 0, 4)
    if ($payloadBytes.Length -gt 0) {
        $Stream.Write($payloadBytes, 0, $payloadBytes.Length)
    }
    $Stream.Flush()
}

function Read-Exact {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [byte[]]$Buffer,
        [int]$Offset,
        [int]$Count
    )

    $totalRead = 0
    while ($totalRead -lt $Count) {
        $read = $Stream.Read($Buffer, $Offset + $totalRead, $Count - $totalRead)
        if ($read -le 0) {
            throw "Connection closed by Master."
        }
        $totalRead += $read
    }
}

function Receive-Packet {
    param(
        [System.Net.Sockets.NetworkStream]$Stream
    )

    $sizeBytes = New-Object byte[] 4
    Read-Exact $Stream $sizeBytes 0 4
    $totalSize = [BitConverter]::ToUInt32($sizeBytes, 0)

    if ($totalSize -lt 4) {
        throw "Invalid packet size: $totalSize"
    }

    $maxPacketSize = 4MB
    if ($totalSize -gt $maxPacketSize) {
        throw "Packet is too large: $totalSize bytes."
    }

    $payloadSize = [int]$totalSize - 4
    if ($payloadSize -eq 0) { return "" }

    $payloadBytes = New-Object byte[] $payloadSize
    Read-Exact $Stream $payloadBytes 0 $payloadSize

    return [System.Text.Encoding]::UTF8.GetString($payloadBytes)
}

function Send-PacketAndReceive {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [string]$Payload
    )

    Send-Packet -Stream $Stream -Payload $Payload
    return Receive-Packet $Stream
}

$client = $null
$stream = $null

try {
    Write-Host "Connecting to $hostName`:$port..."

    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($hostName, $port)
    $stream = $client.GetStream()

    Send-Packet $stream "auth $account $password"
    $authResponse = Receive-Packet $stream

    if ($authResponse -ne "OK") {
        throw "Authentication failed: $authResponse"
    }

    Write-Host "Connected."
    Write-Host ""

    Start-Sleep -Milliseconds 500

    if (-not [string]::IsNullOrWhiteSpace($commandToExecute)) {
        $response = Send-PacketAndReceive -Stream $stream -Payload $commandToExecute

        if (-not [string]::IsNullOrEmpty($response)) {
            Write-Host $response
        }
        exit 0
    }

    while ($true) {
        $commandText = Read-Host ">"
        if ($null -eq $commandText) { break }

        $commandText = $commandText.Trim()
        if ([string]::IsNullOrWhiteSpace($commandText)) { continue }
        if ($commandText -eq "exit" -or $commandText -eq "quit") { break }

        try {
            $response = Send-PacketAndReceive -Stream $stream -Payload $commandText
            if (-not [string]::IsNullOrEmpty($response)) {
                Write-Host $response
            }
            Start-Sleep -Milliseconds 250
        }
        catch {
            Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
            break
        }
    }
}
catch {
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    if ($stream) { $stream.Dispose() }
    if ($client) { $client.Close() }
}