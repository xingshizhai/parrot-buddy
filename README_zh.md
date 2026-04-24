# Parrot Buddy

Parrot Buddy 是运行在 ESP32-S3-BOX-3 上的固件，目标是监听鹦鹉叫声并用不同的模拟鹦鹉声音进行回应。

本项目由通用 Agent 伴侣代码演进而来，目前正在转型为“鹦鹉交互设备”，采用分阶段推进：

1. 第一阶段（当前）：设备本地监听、检测、回叫。
2. 第二阶段：结构化事件记录与数据采集。
3. 第三阶段：接入后端，支持鹦鹉“说话”研究。

[English](README.md)

---

## 硬件要求

硬件参考仓库：[espressif/esp-box](https://github.com/espressif/esp-box)

硬件实现说明：

1. 当前阶段仅面向 ESP32-S3-BOX-3（ESP-BOX-3）。
2. 板级适配代码位于 `components/boards/esp32s3_box3`。
3. 音频、显示、触摸、按键初始化遵循 ESP-BOX 软件栈与托管组件。
4. 升级 ESP-IDF 或托管组件后，建议对照 esp-box 参考仓库重新验证 BSP 与音频行为。

| 组件 | 规格 |
|------|------|
| 开发板 | ESP32-S3-BOX-3 |
| 显示屏 | 2.4" 320x240 ILI9341，GT911 电容触摸 |
| 音频 | ES7210 麦克风阵列 + ES8311 扬声器编解码 |
| IMU | ICM-42670-P |
| 按键 | 3 个实体按键 |
| 存储 | 16 MB Flash + 8 MB PSRAM |

---

## 当前范围（第一阶段）

当前固件重点实现：

1. 持续监听麦克风音频流。
2. 基于轻量特征检测疑似鹦鹉叫声。
3. 触发不同模拟鹦鹉声进行回复。
4. 通过播放门控与冷却时间避免自激。

仓库内保留的 transport/protocol 等历史模块，后续可用于遥测与后端研究接入。

---

## 前置条件

- ESP-IDF v5.4 或 v6.0
- Python 3.8+、CMake 3.24+、Ninja
- USB-C 数据线连接 BOX-3 的 JTAG/UART 口

### 安装 ESP-IDF（仅首次）

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
git checkout v6.0
./install.sh esp32s3
```

---

## 快速上手

### 1. 克隆仓库

```bash
git clone https://github.com/yourorg/parrot-buddy.git
cd parrot-buddy
```

### 2. 激活 ESP-IDF 环境

```bash
source ~/esp-idf/export.sh
```

### 3. 配置

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

如果你的工程目录之前编译过其他 target，请在编译前再次执行 `idf.py set-target esp32s3`。

在 Smart-Buddy Configuration 菜单中，重点关注：

| 配置项 | 路径 | 默认值 |
|--------|------|--------|
| 检测灵敏度 | Parrot Buddy -> Detection sensitivity | 3 |
| 连续触发帧数 | Parrot Buddy -> Trigger hold frames | 3 |
| 回叫冷却时间 | Parrot Buddy -> Reply cooldown (ms) | 1200 |
| 回叫音量 | Parrot Buddy -> Reply volume (%) | 75 |

### 4. 编译

```bash
idf.py build
```

### 5. 烧录并查看日志

```bash
idf.py -p COM3 flash monitor
```

把 COM3 替换为实际串口号。

---

## 运行行为（第一阶段）

1. 设备启动后初始化 16 kHz 单声道音频链路。
2. 监听器对音频 chunk 进行短窗特征计算。
3. 满足触发条件后，选择一种模拟鹦鹉声。
4. 播放结束后进入冷却期，避免连续误触发。

说明：

- 当前是规则检测（轻量 DSP），还不是分类模型。
- 当前回复声以程序生成波形为主，后续可替换为真实样本。

---

## 项目结构

```text
parrot-buddy/
├── main/
├── components/
│   ├── audio_manager/       # 录音/播放运行时
│   ├── boards/esp32s3_box3/ # ESP-BOX-3 板级实现
│   ├── parrot_core/         # 叫声检测与自动回叫引擎
│   ├── ui/                  # LVGL 界面
│   ├── agent_core/          # 历史模块（保留，后续可接后端）
│   ├── protocol/            # 历史模块
│   └── transport/           # 历史模块
├── docs/
└── tools/
```

---

## 开发路线图

1. 第一阶段：本地检测与自动回叫，打通设备闭环。
2. 第二阶段：事件日志（时间戳、特征统计、触发原因、回复 ID）。
3. 第三阶段：可选后端上传与离线分析。
4. 第四阶段：叫声类型分类与行为模式建模。

---

## 常见问题

如果 `idf.py` 不存在，先激活 ESP-IDF 环境。

如果声音过小或过大，可调三处：

1. 板级音频实现中的麦克风增益，
2. Parrot Buddy 灵敏度，
3. 回叫音量百分比。

如果误触发偏多，请提高连续触发帧数并增大冷却时间。

---

## 开源许可

GPL-3.0
