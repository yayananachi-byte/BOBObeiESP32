# 中文界面字库

`ui_font_16.c` 和 `ui_font_22.c` 是项目专用的 LVGL 4 bpp 字库，包含
`main/ui.c` 文案实际使用的全部字符以及 ASCII 32–126。两种字号行高分别为
20 和 26 像素。字模未压缩，正常 ESP-IDF 构建直接编译这些 C 文件。

字体来自 LVGL 依赖附带的完整 `SourceHanSansSC-Normal.otf`，版权属于 Adobe，
使用 SIL Open Font License 1.1，许可全文见 `LICENSE-SourceHanSansSC.txt`。

新增中文文案后，在安装了 Pillow 的 Python 环境下从项目根目录执行：

```powershell
python tools/generate_ui_fonts.py
```

脚本离线读取 LVGL 依赖中的原始字体，验证所需字符均有字形，再更新两份 C 字库
以及 `ui_font_characters.txt`。可加 `--preview build/ui-font-preview.png` 生成字形预览。
