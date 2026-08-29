# Susuwatari (ススワタリ)

Animated *Spirited Away* Soot Sprites for **Omarchy** & Wayland / Hyprland.

Featuring an animated Waybar toggle widget and playful desktop pets that live directly on your active window frame!

![Susuwatari](preview.png)

## ✨ Features

- 🏮 **Active Window Frame Pets**: Soot sprites balance and scurry (`トコトコ`) along the top bar frame of your active window.
- 🖱️ **Mischievous Solo Mouse Heist**: When your mouse is idle for >2.5s, a solitary soot sprite sneaks down from the window, lifts your cursor with both hands, and drags it across your screen before letting go with a sparkle (`✨`)!
- 💨 **Hover Startle & Scatter**: Hover your mouse over the sprites on the window bar to watch them jump (`ぴょん！`) with wide shock eyes (`O O`) and scatter away.
- 🪨 **Boiler Room Coal**: Carrying tiny lumps of coal (`炭`) like in Kamaji's boiler room.
- ⚡ **Ultra-Lightweight (~500 KB RAM / 46 KB binary)**: Pure native Wayland C client with zero GNOME/GTK bloat, 0% idle CPU.
- 🎛️ **Waybar Widget Controller**: An animated soot sprite on your Waybar that runs faster with CPU load and toggles the desktop pets ON/OFF with a click.

## 📦 Install

```sh
omarchy plugin add https://github.com/giogio/omasusuwatari.git --enable
```

Or clone manually:

```sh
git clone https://github.com/giogio/omasusuwatari.git ~/.config/omarchy/plugins/omasusuwatari
~/.config/omarchy/plugins/omasusuwatari/setup.sh
```

## 🎮 Controls & Usage

- **Click Waybar Icon**: Toggles the window pets ON / OFF.
- **Hover on Window Bar**: Startles the soot sprites.
- **Leave Mouse Still**: Triggers the solo mouse heist!

## ⚙️ Configuration

Move the Waybar widget section in `~/.config/omarchy/shell.json` or run:

```sh
omarchy bar move io.github.antigravity.omasusuwatari --section right
```

## 🗑️ Remove

```sh
omarchy plugin remove io.github.antigravity.omasusuwatari --yes
```

## 📄 License

MIT © [giogio](https://github.com/giogio)
