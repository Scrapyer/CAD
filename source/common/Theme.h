/**
 * @file Theme.h
 * @brief 多主题颜色定义
 *
 * 内置 6 种主题：
 *   - Dark（Catppuccin Mocha）   — 经典深色
 *   - Light（Catppuccin Latte）  — 经典浅色
 *   - Ocean（深海蓝）             — 深蓝色调，科技感
 *   - Forest（森林）              — 深绿色调，自然感
 *   - Sunset（暮光）              — 暖色调，沉稳
 *   - Nord（北欧极光）            — 冷灰蓝色调，柔和
 *
 * 所有面板通过 applyTheme() 方法接收主题并更新样式。
 */

#pragma once

#include <QString>

struct Theme {
    // ── UI 颜色（Qt 样式表用） ──
    QString base;         // 面板主背景
    QString mantle;       // 分组/深层背景
    QString crust;        // 最深层背景
    QString surface0;     // 分隔线/次要背景
    QString surface1;     // hover/选中背景
    QString surface2;     // 边框/indicator
    QString overlay0;     // 弱文字（提示）
    QString overlay2;     // 次要文字
    QString subtext0;     // 子标题
    QString subtext1;     // 标签文字
    QString text;         // 主文字
    QString blue;         // 强调色
    QString blueHover;    // 蓝色 hover
    QString bluePressed;  // 蓝色 pressed
    QString green;        // 成功/绿色标题
    QString red;          // 错误/红色标题
    QString teal;         // 渐变辅助色
    QString btnText;      // 蓝色按钮上的文字
    QString gradTop;      // 按钮渐变顶色
    QString gradBot;      // 按钮渐变底色
    QString gradTopHov;   // 按钮渐变顶色 hover
    QString gradBotHov;   // 按钮渐变底色 hover

    // ── GL 视口背景渐变 ──
    float bgTopR, bgTopG, bgTopB;   // 顶部颜色
    float bgBotR, bgBotG, bgBotB;   // 底部颜色

    // ── 色标文字颜色 ──
    int barTextR, barTextG, barTextB;

    bool isDark;
    QString name;         // 主题名称（用于 UI 显示）

    // ── 主题数量 ──
    static constexpr int count() { return 6; }

    // ── 按索引获取主题 ──
    static Theme byIndex(int index) {
        switch (index) {
        case 0: return dark();
        case 1: return light();
        case 2: return ocean();
        case 3: return forest();
        case 4: return sunset();
        case 5: return nord();
        default: return dark();
        }
    }

    // ── 工厂方法 ──

    /** 经典深色 — Catppuccin Mocha */
    static Theme dark() {
        Theme t;
        t.name        = "深色";
        t.isDark      = true;
        t.base        = "#1e1e2e";
        t.mantle      = "#181825";
        t.crust       = "#11111b";
        t.surface0    = "#313244";
        t.surface1    = "#45475a";
        t.surface2    = "#585b70";
        t.overlay0    = "#6c7086";
        t.overlay2    = "#9399b2";
        t.subtext0    = "#a6adc8";
        t.subtext1    = "#bac2de";
        t.text        = "#cdd6f4";
        t.blue        = "#89b4fa";
        t.blueHover   = "#b4d0fb";
        t.bluePressed = "#74a8f7";
        t.green       = "#a6e3a1";
        t.red         = "#f38ba8";
        t.teal        = "#94e2d5";
        t.btnText     = "#1e1e2e";
        t.gradTop     = "#45475a";
        t.gradBot     = "#313244";
        t.gradTopHov  = "#585b70";
        t.gradBotHov  = "#45475a";
        // GL 视口渐变
        t.bgTopR = 0.38f; t.bgTopG = 0.45f; t.bgTopB = 0.58f;
        t.bgBotR = 0.68f; t.bgBotG = 0.74f; t.bgBotB = 0.82f;
        // 色标文字
        t.barTextR = 255; t.barTextG = 255; t.barTextB = 255;
        return t;
    }

    /** 经典浅色 — Catppuccin Latte */
    static Theme light() {
        Theme t;
        t.name        = "浅色";
        t.isDark      = false;
        t.base        = "#eff1f5";
        t.mantle      = "#e6e9ef";
        t.crust       = "#e6e9ef";
        t.surface0    = "#ccd0da";
        t.surface1    = "#bcc0cc";
        t.surface2    = "#acb0be";
        t.overlay0    = "#9ca0b0";
        t.overlay2    = "#7c7f93";
        t.subtext0    = "#6c6f85";
        t.subtext1    = "#5c5f77";
        t.text        = "#4c4f69";
        t.blue        = "#1e66f5";
        t.blueHover   = "#4d8af7";
        t.bluePressed = "#1559d6";
        t.green       = "#40a02b";
        t.red         = "#d20f39";
        t.teal        = "#179299";
        t.btnText     = "#ffffff";
        t.gradTop     = "#dce0e8";
        t.gradBot     = "#ccd0da";
        t.gradTopHov  = "#ccd0da";
        t.gradBotHov  = "#bcc0cc";
        // GL 视口渐变
        t.bgTopR = 0.93f; t.bgTopG = 0.95f; t.bgTopB = 0.97f;
        t.bgBotR = 0.82f; t.bgBotG = 0.85f; t.bgBotB = 0.90f;
        // 色标文字
        t.barTextR = 30; t.barTextG = 30; t.barTextB = 30;
        return t;
    }

    /** Tokyo Night — 深蓝紫色调，科技感 */
    static Theme ocean() {
        Theme t;
        t.name        = "东京之夜";
        t.isDark      = true;
        t.base        = "#24283b";
        t.mantle      = "#1f2335";
        t.crust       = "#1a1e30";
        t.surface0    = "#292e42";
        t.surface1    = "#343a55";
        t.surface2    = "#414868";
        t.overlay0    = "#565f89";
        t.overlay2    = "#737aa2";
        t.subtext0    = "#a9b1d6";
        t.subtext1    = "#b4bce3";
        t.text        = "#c0caf5";
        t.blue        = "#7aa2f7";
        t.blueHover   = "#8db4ff";
        t.bluePressed = "#6990e5";
        t.green       = "#9ece6a";
        t.red         = "#f7768e";
        t.teal        = "#7dcfff";
        t.btnText     = "#1a1e30";
        t.gradTop     = "#343a55";
        t.gradBot     = "#292e42";
        t.gradTopHov  = "#414868";
        t.gradBotHov  = "#343a55";
        // GL 视口渐变
        t.bgTopR = 0.12f; t.bgTopG = 0.14f; t.bgTopB = 0.21f;
        t.bgBotR = 0.26f; t.bgBotG = 0.32f; t.bgBotB = 0.50f;
        // 色标文字
        t.barTextR = 192; t.barTextG = 202; t.barTextB = 245;
        return t;
    }

    /** Gruvbox — 暖色大地色调，自然质朴 */
    static Theme forest() {
        Theme t;
        t.name        = "大地";
        t.isDark      = true;
        t.base        = "#282828";
        t.mantle      = "#1d2021";
        t.crust       = "#181a1b";
        t.surface0    = "#3c3836";
        t.surface1    = "#504945";
        t.surface2    = "#665c54";
        t.overlay0    = "#7c6f64";
        t.overlay2    = "#a89984";
        t.subtext0    = "#bdae93";
        t.subtext1    = "#d5c4a1";
        t.text        = "#ebdbb2";
        t.blue        = "#8ec07c";
        t.blueHover   = "#a4d092";
        t.bluePressed = "#7ab068";
        t.green       = "#b8bb26";
        t.red         = "#fb4934";
        t.teal        = "#83a598";
        t.btnText     = "#1d2021";
        t.gradTop     = "#504945";
        t.gradBot     = "#3c3836";
        t.gradTopHov  = "#665c54";
        t.gradBotHov  = "#504945";
        // GL 视口渐变
        t.bgTopR = 0.15f; t.bgTopG = 0.13f; t.bgTopB = 0.11f;
        t.bgBotR = 0.34f; t.bgBotG = 0.30f; t.bgBotB = 0.25f;
        // 色标文字
        t.barTextR = 235; t.barTextG = 219; t.barTextB = 178;
        return t;
    }

    /** Dracula — 经典暗紫色调，鲜明活泼 */
    static Theme sunset() {
        Theme t;
        t.name        = "暮光";
        t.isDark      = true;
        t.base        = "#282a36";
        t.mantle      = "#22232e";
        t.crust       = "#1d1e28";
        t.surface0    = "#343746";
        t.surface1    = "#44475a";
        t.surface2    = "#545768";
        t.overlay0    = "#6272a4";
        t.overlay2    = "#8791b8";
        t.subtext0    = "#a6b0cc";
        t.subtext1    = "#bcc4da";
        t.text        = "#f8f8f2";
        t.blue        = "#bd93f9";
        t.blueHover   = "#caa8ff";
        t.bluePressed = "#a97ee8";
        t.green       = "#50fa7b";
        t.red         = "#ff5555";
        t.teal        = "#8be9fd";
        t.btnText     = "#282a36";
        t.gradTop     = "#44475a";
        t.gradBot     = "#343746";
        t.gradTopHov  = "#545768";
        t.gradBotHov  = "#44475a";
        // GL 视口渐变
        t.bgTopR = 0.16f; t.bgTopG = 0.16f; t.bgTopB = 0.21f;
        t.bgBotR = 0.32f; t.bgBotG = 0.34f; t.bgBotB = 0.48f;
        // 色标文字
        t.barTextR = 248; t.barTextG = 248; t.barTextB = 242;
        return t;
    }

    /** Solarized — 经典青蓝色调，低对比护眼 */
    static Theme nord() {
        Theme t;
        t.name        = "青玉";
        t.isDark      = true;
        t.base        = "#002b36";
        t.mantle      = "#00252f";
        t.crust       = "#001f28";
        t.surface0    = "#073642";
        t.surface1    = "#0d4150";
        t.surface2    = "#1a5060";
        t.overlay0    = "#586e75";
        t.overlay2    = "#657b83";
        t.subtext0    = "#839496";
        t.subtext1    = "#93a1a1";
        t.text        = "#eee8d5";
        t.blue        = "#268bd2";
        t.blueHover   = "#4aa0e0";
        t.bluePressed = "#1a7abc";
        t.green       = "#859900";
        t.red         = "#dc322f";
        t.teal        = "#2aa198";
        t.btnText     = "#fdf6e3";
        t.gradTop     = "#0d4150";
        t.gradBot     = "#073642";
        t.gradTopHov  = "#1a5060";
        t.gradBotHov  = "#0d4150";
        // GL 视口渐变
        t.bgTopR = 0.00f; t.bgTopG = 0.17f; t.bgTopB = 0.21f;
        t.bgBotR = 0.07f; t.bgBotG = 0.32f; t.bgBotB = 0.38f;
        // 色标文字
        t.barTextR = 238; t.barTextG = 232; t.barTextB = 213;
        return t;
    }
};
