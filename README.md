# Power4Init
一个简简单单的 C++ 项目，只是为了~~给 init 加点小佐料~~代替 systemd 或其他系统管理器的 init

来实现在管理器之前做一些小准备再正式启动系统管理器。

> [!NOTE]
> 这群人是不是疯了，给init做启动器？该不会未来会有人搞出kernel甚至bootloader的启动器吧。
> 开发者的~~自嘲~~自言自语

> [!WARNING]
> 该项目部分代码使用了 GPT 5.3 Codex 生成。
> 不过人工审阅过了，放心食用（

## 编译
就俩命令，没了。

当然后续可能会有Makefile

```bash
mkdir build 
g++ -std=c++17 -O2 main.cpp acpi_power.cpp -o build/p4init
```

运行示例：

```bash
./p4init --delay 2 --verbose
```
