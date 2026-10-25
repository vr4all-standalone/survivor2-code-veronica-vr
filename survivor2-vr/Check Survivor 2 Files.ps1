# Portable Windows PowerShell 5.1 setup check. Reads ZIP metadata, never extracts game data.
param([string]$Folder = (Join-Path $PSScriptRoot 'roms'), [switch]$Library)

function Read-SurvivorZipDirectory([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 22) { throw 'Incomplete ZIP' }
        $length = [int][Math]::Min(65557, $stream.Length)
        $tail = New-Object byte[] $length
        $start = $stream.Length - $length
        $stream.Position = $start
        if ($stream.Read($tail, 0, $length) -ne $length) { throw 'Incomplete ZIP tail' }
        $end = -1
        for ($i = $length - 22; $i -ge 0; $i--) {
            if ([BitConverter]::ToUInt32($tail, $i) -eq 0x06054b50 -and
                $i + 22 + [BitConverter]::ToUInt16($tail, $i + 20) -eq $length) { $end = $i; break }
        }
        if ($end -lt 0) { throw 'ZIP end record missing' }
        $count = [BitConverter]::ToUInt16($tail, $end + 10)
        $size = [BitConverter]::ToUInt32($tail, $end + 12)
        $offset = [BitConverter]::ToUInt32($tail, $end + 16)
        if ([BitConverter]::ToUInt16($tail, $end + 4) -ne 0 -or
            [BitConverter]::ToUInt16($tail, $end + 6) -ne 0 -or
            [BitConverter]::ToUInt16($tail, $end + 8) -ne $count -or
            $count -gt 5000 -or $size -gt 4MB -or [long]$offset + $size -gt $start + $end) {
            throw 'Unsupported or incomplete ZIP directory'
        }
        $bytes = New-Object byte[] $size
        $stream.Position = $offset
        if ($stream.Read($bytes, 0, $size) -ne $size) { throw 'Incomplete ZIP directory' }
        $at = 0
        for ($i = 0; $i -lt $count; $i++) {
            if ($at + 46 -gt $size -or [BitConverter]::ToUInt32($bytes, $at) -ne 0x02014b50) {
                throw 'Invalid ZIP directory entry'
            }
            $nameLength = [BitConverter]::ToUInt16($bytes, $at + 28)
            $next = $at + 46 + $nameLength + [BitConverter]::ToUInt16($bytes, $at + 30) + [BitConverter]::ToUInt16($bytes, $at + 32)
            $compressed = [BitConverter]::ToUInt32($bytes, $at + 20)
            $local = [BitConverter]::ToUInt32($bytes, $at + 42)
            if ($next -gt $size -or [BitConverter]::ToUInt16($bytes, $at + 34) -ne 0 -or
                [long]$local + 30 + $compressed -gt $offset) { throw 'Invalid ZIP entry bounds' }
            [pscustomobject]@{
                Name = [Text.Encoding]::UTF8.GetString($bytes, $at + 46, $nameLength)
                Crc = [BitConverter]::ToUInt32($bytes, $at + 16)
                Size = [BitConverter]::ToUInt32($bytes, $at + 24)
                Compressed = $compressed
            }
            $at = $next
        }
        if ($at -ne $size) { throw 'Unsupported ZIP directory extension' }
    } finally { $stream.Dispose() }
}

function Get-SurvivorBiosStatus([object[]]$Entries) {
    $crcs = 'd4895685','d2a1c6bf','7b452946','55413214','fdf17452','25f64af7','33513691','cce01f1f'
    $names = 'epr-21576h.ic27','epr-21576g.ic27','epr-21578h.ic27','epr-21578g.ic27','epr-21577h.ic27','epr-21577g.ic27','epr-21579d.ic27','epr-21576h_multi.ic27'
    $ready = $false
    foreach ($entry in $Entries) {
        if ($entry.Name.EndsWith('/')) { continue }
        $crc = $entry.Crc.ToString('x8')
        $validSize = $entry.Size -eq 0x200000 -and $entry.Compressed -gt 0
        for ($i = 0; $i -lt $names.Count; $i++) {
            if ($entry.Name -ceq $names[$i] -and ($crc -ne $crcs[$i] -or -not $validSize)) { return 'Conflict' }
        }
        if ($crcs -contains $crc) {
            if (-not $validSize) { return 'Conflict' }
            $ready = $true
        }
    }
    if ($ready) { 'Ready' } else { 'Missing' }
}

function Test-SurvivorFiles([string]$Folder) {
    try {
        $gamePath = Join-Path $Folder 'gunsur2.zip'
        if (-not [IO.File]::Exists($gamePath)) { throw 'Put your complete World BHF2 Ver. E gunsur2.zip in the roms folder.' }
        $game = @(Read-SurvivorZipDirectory $gamePath)
        $crcs = '756e0de6','8073dec7','43cd16a4','f6aebed8','b5e1b582','345fd824','465ecff1','76c92354','d45a46ee','8c38d1f7','f49153c4','0e2bdd9a','055718ad','d06c9bd7','db3c396b'
        foreach ($crc in $crcs) {
            $matching = @($game | Where-Object { $_.Crc.ToString('x8') -eq $crc -and -not $_.Name.EndsWith('/') })
            $expected = if ($crc -eq $crcs[0]) { 0x800000 } else { 0x1000000 }
            if (-not $matching.Count -or @($matching | Where-Object { $_.Size -ne $expected -or $_.Compressed -le 0 }).Count) {
                throw 'gunsur2.zip needs all 15 World BHF2 Ver. E ROMs with matching length and CRC metadata. Regional sets cannot be renamed.'
            }
        }
        $bios = @()
        $biosPath = Join-Path $Folder 'naomi.zip'
        if ([IO.File]::Exists($biosPath)) { $bios = @(Read-SurvivorZipDirectory $biosPath) }
        $separate = Get-SurvivorBiosStatus $bios
        $embedded = Get-SurvivorBiosStatus $game
        if ($separate -eq 'Conflict' -or $embedded -eq 'Conflict' -or
            ($separate -ne 'Ready' -and $embedded -ne 'Ready')) {
            throw 'Add a supported 2 MiB NAOMI BIOS with matching CRC metadata in naomi.zip beside gunsur2.zip, or inside gunsur2.zip. Replace conflicting BIOS entries.'
        }
        [pscustomobject]@{ Ready = $true; Message = 'World cartridge and NAOMI BIOS metadata match. A successful native boot is still required.' }
    } catch {
        [pscustomobject]@{ Ready = $false; Message = $_.Exception.Message }
    }
}

if (-not $Library) {
    $result = Test-SurvivorFiles $Folder
    Write-Host $result.Message
    if (-not $result.Ready) { exit 1 }
    exit 0
}
