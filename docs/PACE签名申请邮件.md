# PACE Eden 签名工具申请邮件

> 收件人：`audiosdk@avid.com`
> 主题必须是：**PACE Eden Signing Tools Request**
> 方括号 `[...]` 里的内容替换成你自己的信息，其余照抄即可。

---

## 英文版（推荐直接发这个）

**Subject:** PACE Eden Signing Tools Request

Dear Avid Audio SDK Team,

I would like to request the PACE Eden signing tools for my AAX Native plug-in.

- Company name: `[Your Company Name or Your Own Name]`
- Admin full name: `[Your Full Name]`
- Telephone number: `[Your Phone Number, incl. country code, e.g. +86-xxx-xxxx-xxxx]`
- Email: `[Your Email]`
- Country/Region: `China`

**Plug-in overview**

| Item | Value |
| --- | --- |
| Plug-in name | Fusion EQ |
| Manufacturer | Fusion Audio |
| Format | AAX Native, 64-bit (Windows) — also ships as VST3 |
| Category | EQ (`ePlugInCategory_EQ`) |
| Manufacturer code | `0x4675736e` |
| Product code | `0x46754551` |
| Version | 1.0.2 |
| Channels | Stereo / Mono-Mono, with per-band stereo & mid-side modes |

Fusion EQ is a six-band EQ offering six analog-inspired processing modes: Clean (digital), Manley-style tube, Pultec EQP-1A style passive, SSL, Neve and API. It features a real-time spectrum analyser with draggable frequency nodes, per-band dynamics, a fixed 80 Hz high-pass filter, and a dedicated AIR band operating above 11 kHz.

**What has been completed**

1. Registered as an AAX Developer and downloaded the AAX SDK (version 2.8.0).
2. Built the plug-in successfully as AAX Native for Windows x64.
3. Tested the unsigned plug-in in Pro Tools Developer.

**Attachment / link**

A screen recording of the plug-in running inside Pro Tools Developer with audio playback is included below:

`[Insert download link — e.g. WeTransfer / Google Drive / YouTube unlisted]`

Please let me know the next steps for obtaining the PACE Eden signing tools.

Thank you very much for your help.

Best regards,
`[Your Full Name]`
`[Your Company]`

---

## 中文版（方便你自己核对内容）

主题：PACE Eden Signing Tools Request

尊敬的 Avid Audio SDK 团队：

我希望能为我的 AAX Native 插件申请 PACE Eden 签名工具。

- 公司名：`[你的公司名，个人开发者可填本人姓名]`
- 管理员姓名：`[你的姓名]`
- 联系电话：`[含国际区号，例如 +86-138-xxxx-xxxx]`
- 邮箱：`[你的邮箱]`
- 国家/地区：中国

**插件概述**

- 插件名：Fusion EQ
- 厂商：Fusion Audio
- 格式：AAX Native 64 位（Windows），同时发行 VST3
- 分类：EQ（ePlugInCategory_EQ）
- 厂商码：0x4675736e；产品码：0x46754551
- 版本：1.0.2

Fusion EQ 是一款六段均衡器，提供六种音色模式：干净（数字）、Manley 风格电子管、Pultec EQP-1A 风格无源、SSL、Neve 与 API。功能包括带可拖拽频点的实时频谱显示、每段独立动态处理、固定 80 Hz 低切，以及作用于 11 kHz 以上的独立 AIR 高频旋钮。

**已完成**

1. 已注册 AAX 开发者并下载 AAX SDK 2.8.0
2. 已成功编译出 Windows x64 的 AAX Native 插件
3. 已在 Pro Tools Developer 中测试未签名插件

**录屏**

插件在 Pro Tools Developer 中加载并播放音频的录屏链接：
`[贴链接]`

请告知获取 PACE Eden 签名工具的后续步骤，非常感谢。

此致
`[姓名]`
`[公司]`

---

## 发之前 checklist

- [ ] 录屏必须能看到：**插件在 Pro Tools Developer 里加载出来 + 播放音频 + 拖动频点**（Avid 要靠它确认插件真能跑）
- [ ] 附件别直接塞大文件到邮件里，用 **WeTransfer / Google Drive / YouTube 未列出视频**链接更稳妥（国内网盘 Avid 可能打不开）
- [ ] 没有公司就填自己姓名，个人开发者很常见
- [ ] 发出后是 **PACE 官方直接联系你**（不是 Avid），留意邮箱，包括垃圾邮件箱
- [ ] 签名工具本身**免费**，Avid 不收钱；后续若想把插件上架 Avid Marketplace 才是另外的商业流程
- [ ] 拿到工具的授权后，用 `wraptool` 签名；需要 **iLok USB 加密狗**（正式签名流程要用）

## 你现在还缺的

如果你电脑上还没装 **Pro Tools Developer**（免费开发者版，需 iLok 账号），要先给 `devauth@avid.com` 发邮件申请 “Developer Bundle NFR” 激活码——因为签名的前提是先录一段插件在 Developer 版里运行的视频。
