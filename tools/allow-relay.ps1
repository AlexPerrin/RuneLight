# A mobile's DNS server on Windows (tools/track.sh up starts one per mobile at its own udp $env:ALLOW_DNS:53, down stops it):
# a bind more specific than the 0.0.0.0:53 of Internet Connection Sharing, which Windows prefers; each query goes to
# mobile-allow as DNS over TCP on 127.0.0.1:8053 (the mirrored loopback reaches WSL over TCP alone, and not on port 53) and its answer back.
$ErrorActionPreference = "Stop"   # a failed bind ends the relay, its reason in the log
$u = New-Object System.Net.Sockets.UdpClient([System.Net.IPEndPoint]::new([System.Net.IPAddress]::Parse($env:ALLOW_DNS), 53))
function ReadN($s, $n) { $b = New-Object byte[] $n; $o = 0; while ($o -lt $n) { $r = $s.Read($b, $o, $n - $o); if ($r -le 0) { throw "closed" }; $o += $r }; , $b }
"listening"
while ($true) {
  $peer = $null; $q = $u.Receive([ref]$peer); $t = $null
  try {
    $t = New-Object System.Net.Sockets.TcpClient("127.0.0.1", 8053); $s = $t.GetStream()
    $s.Write([byte[]](@([byte]($q.Length -shr 8), [byte]($q.Length -band 255)) + $q), 0, $q.Length + 2)
    $h = ReadN $s 2; $a = ReadN $s ($h[0] * 256 + $h[1]); [void]$u.Send($a, $a.Length, $peer)
  } catch { } finally { if ($t) { $t.Close() } }   # no answer: the mobile's resolver asks again
}
