#!/usr/bin/env bash
set -euo pipefail

# Run from the repository root after building the Mac application.
: "${APP_VERSION:?Set APP_VERSION to the handwritten app version}"

cd build

DMG="MediaMuster-${APP_VERSION}-Mac.dmg"

macdeployqt MediaMuster.app

APP=MediaMuster.app
PLUG="$APP/Contents/PlugIns"
FW="$APP/Contents/Frameworks"
### Leftovers from custom icons ###
for gone in "$PLUG/imageformats" "$PLUG/iconengines" "$PLUG/generic" \
            "$PLUG/tls" "$PLUG/networkinformation" \
            "$FW/QtSvg.framework" "$FW/QtNetwork.framework"; do
  rm -rf "$gone"
  [ ! -e "$gone" ] || { echo "$gone survived the strip"; exit 1; }
done

missing=0
while IFS= read -r bin; do
  for dep in $(otool -L "$bin" 2>/dev/null | awk '/@rpath\/Qt/ {print $1}' \
               | sed 's#@rpath/##; s#\.framework.*##' | sort -u); do
    if [ ! -d "$FW/$dep.framework" ]; then
      echo "MISSING framework: $dep (linked by $bin)"
      missing=1
    fi
  done
done < <(find "$APP" -type f \( -perm -111 -o -name "*.dylib" \))
if [ "$missing" -ne 0 ]; then
  echo "Qt framework trim broke a dependency"
  exit 1
fi

identity=$(security find-identity -v -p codesigning | grep "Developer ID Application" | head -1 | sed -E 's/.*"(.*)"/\1/' || true)
notarize=false
sign_args=(--force --sign -)
if [ -n "$identity" ] && [ -n "${APPLE_TEAM_ID:-}" ]; then
  echo "Developer ID signing and notarisation"
  sign_args=(--force --options runtime --timestamp --sign "$identity")
  notarize=true
else
  echo "Ad-hoc signing"
fi

# Sign nested components before the app. A failed signature stops packaging.
while IFS= read -r -d '' component; do
  codesign "${sign_args[@]}" "$component"
done < <(find "$FW" -maxdepth 1 -name '*.framework' -print0)
while IFS= read -r -d '' component; do
  codesign "${sign_args[@]}" "$component"
done < <(find "$PLUG" -name '*.dylib' -print0)
codesign "${sign_args[@]}" "$APP"
if "$notarize"; then
  codesign --verify --deep --strict --verbose=2 "$APP"
  sync
  sleep 2
fi

staging=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mediamuster-dmg.XXXXXX")
trap 'rm -rf "$staging"' EXIT
cp -R "$APP" "$staging/"
ln -s /Applications "$staging/Applications"
sync
sleep 4
rm -f "$DMG"
hdiutil create -volname "MediaMuster" -srcfolder "$staging" -ov -format ULMO "$DMG"

if "$notarize"; then
  xcrun notarytool submit "$DMG" --apple-id "$APPLE_ID_USERNAME" --password "$APPLE_ID_PASSWORD" --team-id "$APPLE_TEAM_ID" --wait
  xcrun stapler staple "$DMG"
  xcrun stapler validate "$DMG"
fi
