#!/bin/sh
set -eu

qt_prefix=${QT_PREFIX:-$(brew --prefix qt)}
output_directory=${OUTPUT_DIRECTORY:-dist}
packaging_directory=$(dirname "$0")
app_bundle="$output_directory/MacShell.app"
bundle_contents="$app_bundle/Contents"
version=$(build/src/quickshell --version | awk '{print $2}')

rm -rf "$app_bundle"
mkdir -p "$bundle_contents/MacOS"
cp build/src/quickshell "$bundle_contents/MacOS/quickshell"
ln -sf quickshell "$bundle_contents/MacOS/notify-send"
mkdir -p "$bundle_contents/Resources"
cp -R "$packaging_directory/default-config" "$bundle_contents/Resources/default-config"
sed "s/@VERSION@/$version/g" "$packaging_directory/Info.plist" >"$bundle_contents/Info.plist"

library_search_arguments=$(for library_directory in "$(brew --prefix)"/opt/*/lib; do
	printf ' -libpath=%s' "$library_directory"
done)
"$qt_prefix/bin/macdeployqt" "$app_bundle" -qmldir="$packaging_directory" -always-overwrite \
	$library_search_arguments

unneeded_qml_modules="QtQuick/Pdf QtQuick/Scene2D QtQuick/Scene3D QtQuick/Timeline QtQuick/VirtualKeyboard QtQml/StateMachine"
for qml_module in $unneeded_qml_modules; do
	for module_plugin in $(find "$bundle_contents/Resources/qml/$qml_module" -name '*.dylib' 2>/dev/null); do
		rm -f "$bundle_contents/PlugIns/quick/$(basename "$module_plugin")"
	done
	rm -rf "$bundle_contents/Resources/qml/$qml_module"
done
rm -f "$bundle_contents/PlugIns/quick/libvirtualkeyboardplugin.dylib" \
	"$bundle_contents/PlugIns/imageformats/libqpdf.dylib" \
	"$bundle_contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"

default_signing_identity=$(security find-identity -v -p codesigning 2>/dev/null \
	| awk -F'"' '/^ *[0-9]+\)/ {print $2; exit}')
signing_identity=${SIGNING_IDENTITY:-${default_signing_identity:--}}
echo "signing with: $signing_identity"
codesign --force --deep --sign "$signing_identity" "$app_bundle"

linked_libraries=$(find "$bundle_contents" -type f \( -perm -u+x -o -name '*.dylib' \) \
	-exec otool -L {} + 2>/dev/null | awk '/^\t/ {print $1}' | sort -u)
missing_libraries=$(printf '%s\n' "$linked_libraries" | grep -E '^/(opt|usr/local)/' || true)
for rpath_library in $(printf '%s\n' "$linked_libraries" | grep '^@rpath/' | sed 's|^@rpath/||'); do
	if [ ! -e "$bundle_contents/Frameworks/$rpath_library" ]; then
		missing_libraries="$missing_libraries
@rpath/$rpath_library"
	fi
done
if [ -n "$missing_libraries" ]; then
	echo "error: bundle is not self-contained, missing:$missing_libraries" >&2
	exit 1
fi

disk_image="$output_directory/MacShell-$version.dmg"
hdiutil create -quiet -ov -volname MacShell -srcfolder "$app_bundle" -format UDZO "$disk_image"
echo "built $app_bundle and $disk_image"
