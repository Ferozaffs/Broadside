@echo off
rem Broadside TTS: torch with CUDA (the first install got the CPU build back from PyPI with qwen-tts)
set UV_CACHE_DIR=K:\BroadsideTTS\uvcache
set UV_PYTHON_INSTALL_DIR=K:\BroadsideTTS\python
echo == torch cu130 %date% %time%
uv pip install --python K:\BroadsideTTS\venv\Scripts\python.exe --reinstall torch torchaudio --index-url https://download.pytorch.org/whl/cu130
echo == check %date% %time%
K:\BroadsideTTS\venv\Scripts\python.exe "%~dp0tts_check.py"
echo torch done %date% %time%
