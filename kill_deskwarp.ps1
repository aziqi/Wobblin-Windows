# Stops all running Wobblin / Deskwarp processes.
$procs = Get-Process -Name "Wobblin", "Deskwarp" -ErrorAction SilentlyContinue
if ($procs) {
    $procs | Stop-Process -Force -ErrorAction SilentlyContinue
    Write-Host "Wobblin / Deskwarp terminated ($(@($procs).Count) process(es))."
} else {
    Write-Host "Wobblin is not running."
}
