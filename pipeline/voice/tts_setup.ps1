# Qwen3-TTS (Apache 2.0) for Broadside's voices, all of it on K: (C: is nearly full): a Python 3.12 venv made by uv,
# torch with CUDA, qwen-tts, and the two models (VoiceDesign: a voice from a description; Base: cloning it per line).
$ErrorActionPreference = "Continue"
$T = "K:\BroadsideTTS"
New-Item -ItemType Directory -Force $T | Out-Null
$env:UV_PYTHON_INSTALL_DIR = "$T\python"
$env:UV_CACHE_DIR = "$T\uvcache"
$env:HF_HOME = "$T\hf"
$env:PIP_CACHE_DIR = "$T\pipcache"
Set-Location $T
"== venv $(Get-Date)"
uv venv --python 3.12 "$T\venv" 2>&1
"== torch $(Get-Date)"
uv pip install --python "$T\venv\Scripts\python.exe" torch torchaudio --index-url https://download.pytorch.org/whl/cu124 2>&1 | Select-Object -Last 5
"== qwen-tts $(Get-Date)"
uv pip install --python "$T\venv\Scripts\python.exe" -U qwen-tts soundfile huggingface_hub 2>&1 | Select-Object -Last 5
"== check $(Get-Date)"
& "$T\venv\Scripts\python.exe" -c "import torch, qwen_tts; print('torch', torch.__version__, 'cuda', torch.cuda.is_available())" 2>&1
"== models $(Get-Date)"
& "$T\venv\Scripts\python.exe" -c "from huggingface_hub import snapshot_download as d; [print(d(m)) for m in ('Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign','Qwen/Qwen3-TTS-12Hz-1.7B-Base','Qwen/Qwen3-TTS-Tokenizer-12Hz')]" 2>&1 | Select-Object -Last 4
"setup done $(Get-Date)"
