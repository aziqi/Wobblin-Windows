# Stops all running Wobblin processes.
$procs = Get-Process -Name "Wobblin" -ErrorAction SilentlyContinue
if ($procs) {
    $procs | Stop-Process -Force -ErrorAction SilentlyContinue
    Write-Host "Wobblin terminated ($(@($procs).Count) process(es))."
} else {
    Write-Host "Wobblin is not running."
}
