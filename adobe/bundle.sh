#!/bin/bash
# Assemble the After Effects / Premiere .plugin bundle from the cargo build.
#
#   ./bundle.sh            debug build, native arch
#   ./bundle.sh release    universal (arm64 + x86_64), what ships
#
# The bundle layout and the eFKT/FXTC markers are what MediaCore requires;
# the .rsrc is the PiPL that build.rs generates during the cargo build.
set -euo pipefail
cd "$(dirname "$0")"

PROFILE="${1:-debug}"
NAME="LumaKey"
BIN_LIB="liblumakey_adobe.dylib"
BIN_RSRC="lumakey-adobe.rsrc"
TARGET="${CARGO_TARGET_DIR:-target}"

if [ "$PROFILE" = "release" ]; then
    rustup target add aarch64-apple-darwin x86_64-apple-darwin >/dev/null 2>&1 || true
    cargo build --release --target aarch64-apple-darwin
    cargo build --release --target x86_64-apple-darwin
else
    cargo build
fi

OUT="$TARGET/$PROFILE/$NAME.plugin"
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

printf 'eFKTFXTC' > "$OUT/Contents/PkgInfo"
/usr/libexec/PlistBuddy -c 'add CFBundlePackageType string eFKT' "$OUT/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'add CFBundleSignature string FXTC' "$OUT/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'add CFBundleIdentifier string com.stoatworks.lumakey.adobe' "$OUT/Contents/Info.plist"

if [ "$PROFILE" = "release" ]; then
    cp "$TARGET/x86_64-apple-darwin/release/$BIN_RSRC" "$OUT/Contents/Resources/$NAME.rsrc"
    lipo "$TARGET/aarch64-apple-darwin/release/$BIN_LIB" \
         "$TARGET/x86_64-apple-darwin/release/$BIN_LIB" \
         -create -output "$OUT/Contents/MacOS/$NAME"
else
    cp "$TARGET/$PROFILE/$BIN_RSRC" "$OUT/Contents/Resources/$NAME.rsrc"
    cp "$TARGET/$PROFILE/$BIN_LIB" "$OUT/Contents/MacOS/$NAME"
fi

# Ad-hoc by default; a real identity can be supplied as SIGN_IDENTITY.
codesign --force --sign "${SIGN_IDENTITY:--}" --timestamp=none "$OUT"

echo "built $OUT"
