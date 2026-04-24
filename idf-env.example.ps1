# 复制为 idf-env.local.ps1 或 %USERPROFILE%\.esp-idf-build.ps1 后修改路径。
# CMD 入口 build_esp32.bat 会调用本仓库的 build_esp32.ps1。

# 必填：ESP-IDF 根目录（与 EIM 里显示的 IDF_PATH 一致）
$IDF_PATH = "D:\Tools\esp\.espressif\v6.0\esp-idf"

# 建议填写（与 EIM 一致，用于定位 eim_idf.json：%IDF_TOOLS_PATH%\eim_idf.json）
$IDF_TOOLS_PATH = "C:\Espressif\tools"
$IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v6.0\venv"

# 若存在上述 eim_idf.json，脚本会优先点源其中的 activationScript（与「ESP-IDF PowerShell」一致），
# 不再依赖 export.ps1，可避免本机 PATH 里其它 CMake 等与 idf_tools export 冲突。

# 可选：不设置则默认编译本脚本所在目录
# $ProjectDir = "D:\Work\esp32\projects\my-app"

# --- 仅设置环境、自己执行 idf.py ---
# 交互 PowerShell（环境留在当前窗口）：
#   . .\build_esp32.ps1 -ActivateOnly
#   idf.py build
# 自动化一行（opencode / CI，同一进程里先点源脚本再跑命令）：
#   powershell -NoProfile -Command ". 'D:\Work\esp32\projects\parrot-buddy\build_esp32.ps1' -ActivateOnly; idf.py build"
# 错误示例：powershell -Command "idf.py build"  （新进程未激活，找不到 idf.py）
