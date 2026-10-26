# wuwapad

让 **鸣潮桌宠 Wuwa Tappo** 响应手柄输入（键鼠姿势 / 敲桌子动画），而**不影响任何其他程序**。

> 背景：Wuwa Tappo 的输入层只有键盘鼠标（`RegisterRawInputDevices` + `WH_KEYBOARD_LL`），
> exe 里连 `xinput`、`gamepad` 字符串都没有，所以手柄按下去它完全没反应。
> 常见做法是 `SendInput` 把按键全局注入 —— 但那样全屏游戏、聊天窗口都会收到。
> 本项目改成**进程内喂事件**：直接调用桌宠自己的键盘钩子过程，输入不进系统队列。

## 演示

![demo](docs/demo.gif)

（[1080p/MP4 版](docs/demo.mp4)，无声，17 秒）

## 原理

```
DLL (wuwapad.dll)  ── LoadLibrary ──▶ WuwaTappo.exe
   │
   ├─ hook user32!CallNextHookEx (IAT patch)
   │      └─ 每次真实按键，桌宠的 WH_KEYBOARD_LL 回调会调用它
   │         用 _ReturnAddress() + RtlLookupFunctionEntry 反解出回调函数入口
   │
   ├─ 轮询 XInput (xinput1_4.dll)
   │      └─ 按键上升沿 / 摇杆越出死区 → 映射成某个 VK
   │
   └─ 直接调用回调函数(HC_ACTION, WM_KEYDOWN, &KBDLLHOOKSTRUCT)
          然后 WM_KEYUP —— 只影响桌宠自己
```

关键点：低级别键盘钩子的回调签名是 `LRESULT (int nCode, WPARAM wParam, LPARAM lParam)`，
`lParam` 指向 `KBDLLHOOKSTRUCT`。进程内直接调用它，等价于“这个进程收到了一个按键”，
但系统输入队列里什么都没有。

## 组成

| 文件 | 作用 |
|---|---|
| `src/wuwapad.c` | 注入的 DLL（XInput 轮询 + 钩子回调调用） |
| `scripts/agent.ps1` | 常驻**提权**代理：轮询 `cmd.txt` → 执行 → 写 `out.txt`（一次 UAC，之后不再弹） |
| `scripts/watch.ps1` | 看护进程：每 5 秒确认桌宠在跑且 DLL 已注入，重启/更新后自动补注入 |
| `scripts/apply.ps1` | 单次“确保运行 + 注入” |
| `scripts/inject.ps1` | 真正的注入器（OpenProcess / VirtualAllocEx / CreateRemoteThread） |
| `wuwa_pad.cmd` | 一键入口 |

> 为什么要提权：Wuwa Tappo 自身以管理员身份运行（exe 内置计划任务，`RunLevel=HighestAvailable`），
> 往高完整性进程注入必须同为管理员。所以有 `agent.ps1` 这个常驻代理，避免每次注入都弹 UAC。

## 构建

需要 VS2022（或 Build Tools）+ Windows SDK：

```bat
build.bat
```

产物：`build\wuwapad.dll`（x64，静态链接 CRT，无运行库依赖）。

## 使用

1. 构建，或使用 Release 里的 `wuwapad.dll`。
2. 双击 `wuwa_pad.cmd`（首次会弹一次 UAC）。
3. 按手柄测试。

日常无需再操作：桌宠重启或版本更新后，看护进程会在 5 秒内重新注入。

停止：

```powershell
Remove-Item scripts\watch.run      # 停止看护
'exit' | Set-Content scripts\cmd.txt   # 关闭提权代理
```

## 开机自启（和桌宠一起启动）

Wuwa Tappo 自己就是靠计划任务在登录时以管理员身份启动的（`\WuwaTappo\ElevatedAutostart`，
`RunLevel=HighestAvailable`），所以它天生是提权进程。要跟着它一起起来，我们也用计划任务 ——
计划任务的 `HighestAvailable` 直接以高完整性运行，**不弹 UAC**。

```powershell
# 一次性注册（会弹一次 UAC）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install_autostart.ps1
```

注册后的任务：`\WuwaTappo\PadBridge`

| 项 | 值 |
|---|---|
| 触发 | 用户登录后延迟 20 秒（比桌宠自己的 5 秒晚，确保它先起来） |
| 身份 | `InteractiveToken` + `HighestAvailable`（提权、无 UAC） |
| 动作 | `powershell -File scripts\start_all.ps1` |
| 隐藏 | 是；`MultipleInstancesPolicy=IgnoreNew` |

`start_all.ps1` 做三件事：拉起常驻提权代理（已起来就跳过）→ 拉起看护进程 → 立刻执行一次注入。
看护进程只负责“注入”，**不会**把用户主动退出的桌宠再拉起来。

卸载：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\uninstall_autostart.ps1
```

> 任务里写死了 `start_all.ps1` 的绝对路径，**移动/重命名目录后要重新执行一次 install_autostart.ps1**。
## 默认映射

| 手柄 | 键 | 手柄 | 键 |
|---|---|---|---|
| A / B / X / Y | J / K / U / I | LB / RB | H / N |
| Back / Start | M / O | LS / RS 按下 | R / F |
| 十字键 上/下/左/右 | W / S / A / D | LT / RT | Q / E |
| 左摇杆 上/下/左/右 | T / G / C / V | 右摇杆 上/下/左/右 | Y / B / L / P |

摇杆是“从原点移出触发一次”：推过 `sdz` 触发，回到 `srz` 以内才重新布防，推住不放不会连发。

修改映射：`src/wuwapad.c` 里的 `g_map` 表 / `StickAxes()` 里的键值。

## 配置

运行时读取 `%TEMP%\wuwapad_cfg.txt`（缺失时看护进程会自动重建）：

```
up=1          # 0 = 只发 keydown（不发 keyup）
updelay=60    # 按下到抬起的间隔 ms
stick=1       # 0 = 关闭摇杆触发
sdz=16000     # 摇杆触发阈值（0-32767）
srz=8000      # 摇杆回中阈值
proc=2        # 用第几个发现到的钩子回调；0 = 全部（会一按两触发！）
debounce=150  # 同一个键的最小间隔 ms
```

`proc` 说明：桌宠进程里存在**两个**都调用 `CallNextHookEx` 的 `WH_KEYBOARD_LL` 回调
（`0x...73FFB0` 和 `0x...DE7CF0` 附近），两个都调会让一次按键触发两次动画。
实测 `proc=2` 正常。

## 已知问题 / 注意

- **独占全屏时看不见桌宠**：全屏独占绕过 DWM，任何 topmost 窗口都画不上去；请用「无边框窗口」。
- **统计会虚高**：桌宠的敲击计数把喂进去的键也算进真实按键，手柄按一次会计一次（或两次，取决于 `proc`）。
- 自动校准会发一次 `Shift`：回调地址只有钩子被真实调用过才能反解，注入后 2 秒内会用
  `SendInput` 发一次 Shift（不产生字符）。
- 注入取决于桌宠内部结构，**游戏更新后回调地址/数量可能变化**；若失效，先看
  `%TEMP%\wuwapad.log` 里 `hook proc #N` 的数量，再调 `proc`。
- 只测过 Wuwa Tappo 1.0.1 (x64) / Windows 11 26200。

## 免责声明

仅用于个人桌宠的输入桥接。请遵守游戏与软件的用户协议；本项目不修改游戏文件、不读写游戏内存。
