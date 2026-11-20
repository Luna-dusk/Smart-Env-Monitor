# VSCode 终端 rcfile：进入本项目时自动加载 ESP-IDF v5.4 环境
# 由 .vscode/settings.json 的 terminal.integrated.profiles.linux "ESP-IDF 5.4" 调用：
#   bash --rcfile ${workspaceFolder}/esp-idf-env.sh -i
# --rcfile 会替代 ~/.bashrc，所以要手动 source 一次。

# 1) 先加载你平时的 shell 配置（别名、提示符等）
[ -f "$HOME/.bashrc" ] && . "$HOME/.bashrc"

# 2) 加载 ESP-IDF 环境
if [ -z "${SMART_ENV_IDF_LOADED:-}" ]; then
    export IDF_PATH="$HOME/esp/esp-idf"
    export IDF_TOOLS_PATH="$HOME/.espressif"

    # IDF 的 python venv 是按 python3.10 建的（idf5.4_py3.10_env），
    # 而 export.sh 内部按 `python3` 版本推算 venv 目录，PATH 里若是 3.13 会直接报
    # "ESP-IDF Python virtual environment not found"。让系统 python3 优先即可。
    case "$(command -v python3)" in
        /usr/bin/python3) ;;
        *) export PATH="/usr/bin:/bin:$PATH" ;;
    esac

    if . "$IDF_PATH/export.sh" > /dev/null 2>&1; then
        export SMART_ENV_IDF_LOADED=1
        echo "ESP-IDF $ESP_IDF_VERSION 环境已就绪 (target: ${IDF_TARGET:-esp32s3})"
    else
        echo "警告: ESP-IDF 环境加载失败，请检查 $IDF_PATH/export.sh"
    fi
fi
