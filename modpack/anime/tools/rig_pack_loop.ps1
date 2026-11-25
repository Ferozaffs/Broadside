# Run rig_pack.py until every character of pack_list.tsv has an idle (it follows the labels and the cut-outs).
Set-Location "$HOME\Documents\PirateGame-ArtPipeline\rig"
$n = (Get-Content pack_list.tsv | Where-Object { $_ }).Count
while (((Get-ChildItem pack\idles -Filter *.png -ErrorAction SilentlyContinue).Count) -lt $n) {
    & K:\BroadsideRig\venv\Scripts\python.exe -I rig_pack.py pack_list.tsv pack >> rig_pack.log 2>&1
    Start-Sleep 30
}
Add-Content rig_pack.log "all done"
