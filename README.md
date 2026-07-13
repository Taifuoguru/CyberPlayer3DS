
<img width="4000" height="3000" alt="20260713_034441" src="https://github.com/user-attachments/assets/9eba1daa-a2a3-41ae-b986-dc296c8391c0" />











<img width="1107" height="610" alt="Ekran görüntüsü 2026-07-02 015105" src="https://github.com/user-attachments/assets/a8569b00-0ba7-4216-a615-42c3d08870e9" />















<img width="999" height="674" alt="Ekran görüntüsü 2026-07-02 015116" src="https://github.com/user-attachments/assets/dcad15ff-11ef-4afe-b951-89b2a2c70d1c" />










<img width="270" height="279" alt="Ekran görüntüsü 2026-07-02 230219" src="https://github.com/user-attachments/assets/f28d17e5-000d-4859-9bcf-f1bdefd32cfa" />







# CyberPlayer3DS

CyberPlayer3DS is a Y2K / cyber-industrial music player for Nintendo 3DS. It turns the 3DS into a retro Kenwood DSP-inspired audio deck with SD card browsing, bass-reactive visualizers, touchscreen controls, lid-closed playback handling, and experimental cyber effects.

## Features

- Full SD card file browser starting from `sdmc:/`
- Folder navigation with parent directory support
- Audio file detection and listing
- Playable format support for MP3, WAV, FLAC and OGG
- Unsupported format detection for AAC, M4A and OPUS
- Playlist creation from the current folder
- Return from player to file browser without losing folder position
- Kenwood DSP / VFD inspired interface
- Bass-reactive spectrum visualizers
- Peak hold effect on EQ bars
- Random station static transition effect
- Startup and shutdown terminal animations
- SELECT hold switch mode
- Screen-off audio mode
- Lid-closed audio guard mode
- HOME button safe suspend / restore handling
- C-stick mouse cursor mode, enabled from settings
- Circle Pad seek control
- Touchscreen player controls
- Button beep feedback
- Matrix log and Geiger effects, disabled by default
- Format-based default speed:
  - MP3 starts at 105%
  - FLAC, OGG and WAV start at 100%

## Supported Audio Formats

Playable:
- MP3
- WAV PCM16
- FLAC
- OGG Vorbis

Detected but not playable in this build:
- AAC
- M4A
- OPUS

## Controls

### File Browser

- D-Pad Up / Down: Move selection
- D-Pad Left / Right: Page up / page down
- A: Open folder or play selected audio file
- B: Go to parent folder
- X: Search audio files on the SD card
- Y: Open settings
- START: Play shutdown animation and exit
- SELECT: Toggle hold mode

### Player

- A: Play / pause
- B: Return to file browser
- D-Pad Left / Right: Previous / next track
- Circle Pad Left / Right: Seek backward / forward
- X: Toggle screen-off audio mode
- Y: Toggle automatic visualizer switching
- L: Toggle shuffle
- R: Change repeat mode
- ZL / ZR: Change EQ preset
- Touchscreen: Play, previous, next, rewind, fast-forward and seek bar

### Settings

- D-Pad Up / Down: Move through settings
- D-Pad Left / Right: Decrease / increase selected option
- A: Toggle or increase selected option
- B: Return to previous screen
- L / R: Adjust selected option
- Mouse Mode: Enables C-stick cursor control

### C-Stick Mouse Mode

Mouse Mode is disabled by default.

- Enable `MOUSE MODE` from settings
- Move the C-stick to control the cursor
- Press A to click at the cursor position
- Works on the bottom touchscreen UI

### Lid-Closed Mode

When the 3DS lid is closed, the app attempts to keep audio running.

- A / ZL / ZR: Play / pause
- L / D-Pad Left: Previous track
- R / D-Pad Right: Next track

Headphones are recommended for lid-closed playback.

## Notes

CyberPlayer3DS is designed for real Nintendo 3DS hardware and homebrew usage. Some features may behave differently depending on the 3DS model, audio format, SD card speed and headphone state.











