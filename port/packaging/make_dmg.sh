#!/bin/sh
# Builds "Dyna Blaster.app" and a distributable DMG (no original game data inside).
set -e
cd "$(dirname "$0")/.."
APP="build/Dyna Blaster.app"
VER="${VER:-1.0}"
make
rm -rf "$APP" build/dmg build/DynaBlaster.iconset
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp dynablaster "$APP/Contents/MacOS/dynablaster"
python3 packaging/make_icon.py build/DynaBlaster.iconset
iconutil -c icns build/DynaBlaster.iconset -o "$APP/Contents/Resources/DynaBlaster.icns"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Dyna Blaster</string>
  <key>CFBundleDisplayName</key><string>Dyna Blaster</string>
  <key>CFBundleIdentifier</key><string>com.example.dynablaster-port</string>
  <key>CFBundleExecutable</key><string>dynablaster</string>
  <key>CFBundleIconFile</key><string>DynaBlaster</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VER</string>
  <key>CFBundleVersion</key><string>$VER</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>
</dict></plist>
EOF
# ad-hoc signature (required for arm64 binaries); use SIGN_ID="Developer ID Application: ..." to sign for real
codesign --force --deep --options runtime -s "${SIGN_ID:--}" "$APP"
mkdir -p build/dmg
cp -R "$APP" build/dmg/
ln -s /Applications build/dmg/Applications
cat > build/dmg/READ\ ME.txt <<'EOF'
Dyna Blaster - native Apple Silicon port

1. Drag "Dyna Blaster" into Applications.
2. The game files are NOT included. You need your own copy of the original DOS
   game (folder with DYNA.EXE, GFX/, DIGI.DAT, MUSIC.DAT, DEMO.DAT, DYNA.CFG).
   On first launch the app asks you to select that folder and remembers it.
3. The app is not notarized: the first time, right-click it -> Open -> Open
   (or run: xattr -dr com.apple.quarantine "/Applications/Dyna Blaster.app").

Controls: arrows + Space (bomb) + Ctrl (remote) for player 1, I J K L + A for
player 2, gamepads for players 3/4. P pause, M music, S sound, Esc menu,
F11 fullscreen.
EOF
rm -f build/DynaBlaster.dmg
hdiutil create -volname "Dyna Blaster" -srcfolder build/dmg -ov -format UDZO build/DynaBlaster.dmg >/dev/null
echo "built: $(pwd)/build/DynaBlaster.dmg"
