#include "shell_init.hpp"

// 紧急shell的初始化脚本
char* siEmergencyShell = R"CMD(
bash -c '
bash --init-file <(
  cat << "EOF"
# 防止因为systemd死掉而无法直接重启/关机
alias reboot="/usr/bin/reboot -f"
alias poweroff="/usr/bin/poweroff -f"
echo "=== EMERGENCY SHELL ==="
echo "Now you are in emergency shell."
echo "Type \"exit\" to boot system again."
echo "Type \"reboot\" to reboot your computer."
echo "Type \"poweroff\" to power off your computer."
EOF
) -i
'
)CMD";

// 给/etc/p4initrc搭建环境的脚本
char* siScripts = R"CMD(
bash -c '
bash --init-file <(
cat << EOF
# WIP
) -i
'
)CMD";