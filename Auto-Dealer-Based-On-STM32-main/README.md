# Auto Dealer Based ON STM32
基于STM32的自动售货机

## 2026-07-07 更新

适配国产STM32F103C8T6芯片，修复烧录、时钟、串口、延时等硬件兼容问题；适配有源蜂鸣器；WiFi/MQTT连接调通，已接入OneNET平台。

## 2026-07-08 更新

新增手机Web下单界面 (`web/mobile.html`)，可通过手机浏览器远程下单。

### 📱 手机下单 — 使用方法

**① 推荐：Python HTTP 服务**
```bash
cd web
python -m http.server 8080
```
手机连接**同一 WiFi**，浏览器打开：`http://<电脑IP>:8080/mobile.html`

**② 直接打开**
将 `web/mobile.html` 发送到手机，用浏览器直接打开即可。

**③ 部署到静态托管**
上传到 GitHub Pages / Netlify 等平台，手机浏览器随时访问。

### ⚙ 配置
页面右下角点 ⚙ 按钮可配置 OneNET 连接参数（已预填项目默认值）。
