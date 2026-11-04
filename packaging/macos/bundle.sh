#!/bin/sh
set -eu

qt_prefix=${QT_PREFIX:-$(brew --prefix qt)}
homebrew_prefix=$(brew --prefix)
output_directory=${OUTPUT_DIRECTORY:-dist}
packaging_directory=$(dirname "$0")
app_bundle="$output_directory/MacShell.app"
bundle_contents="$app_bundle/Contents"
frameworks_directory="$bundle_contents/Frameworks"
bundled_executable="$bundle_contents/MacOS/quickshell"
version=$(build/src/quickshell --version | awk '{print $2}')

rm -rf "$app_bundle"
mkdir -p "$bundle_contents/MacOS"
cp build/src/quickshell "$bundled_executable"
ln -sf quickshell "$bundle_contents/MacOS/qs"
ln -sf quickshell "$bundle_contents/MacOS/notify-send"
ln -sf quickshell "$bundle_contents/MacOS/hyprctl"
for compat_command in loginctl systemctl brightnessctl wpctl pactl nmcli wl-copy wl-paste xdg-open cliphist swww hyprpicker grim nproc; do
	ln -sf quickshell "$bundle_contents/MacOS/$compat_command"
done
sed "s/@VERSION@/$version/g" "$packaging_directory/Info.plist" >"$bundle_contents/Info.plist"

library_search_arguments=$(for library_directory in "$homebrew_prefix"/opt/*/lib; do
	printf ' -libpath=%s' "$library_directory"
done)
"$qt_prefix/bin/macdeployqt" "$app_bundle" -qmldir="$packaging_directory" -always-overwrite \
	$library_search_arguments

mkdir -p "$bundle_contents/Resources"
cp -R "$packaging_directory/default-config" "$bundle_contents/Resources/default-config"
printf '[Paths]\nPlugins = PlugIns\nImports = Resources/qml\nQmlImports = Resources/qml\n' \
	>"$bundle_contents/Resources/qt.conf"

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

mach_o_files() {
	find "$bundle_contents" -type f | while read -r candidate; do
		case "$(file -b "$candidate")" in
		*Mach-O*) echo "$candidate" ;;
		esac
	done
}

frameworks_relative_path() {
	case "$1" in
	*.framework/*) echo "$1" | sed -E 's|^.*/([^/]+\.framework/.*)$|\1|' ;;
	*) basename "$1" ;;
	esac
}

ensure_bundled() {
	relative_path=$1
	[ -e "$frameworks_directory/$relative_path" ] && return 0

	case "$relative_path" in
	*.framework/*)
		framework_name=${relative_path%%/*}
		source_path=$(ls -d "$homebrew_prefix"/opt/*/lib/"$framework_name" 2>/dev/null | head -1)
		[ -n "$source_path" ] || return 1
		mkdir -p "$frameworks_directory/$framework_name"
		cp -R "$source_path/" "$frameworks_directory/$framework_name/"
		find "$frameworks_directory/$framework_name" -name Headers -prune -exec rm -rf {} +
		chmod -R u+w "$frameworks_directory/$framework_name"
		;;
	*)
		source_path=$(ls "$homebrew_prefix"/opt/*/lib/"$relative_path" 2>/dev/null | head -1)
		[ -n "$source_path" ] || return 1
		cp -L "$source_path" "$frameworks_directory/$relative_path"
		chmod u+w "$frameworks_directory/$relative_path"
		;;
	esac

	echo "bundling $relative_path"
}

relocate_binary() {
	binary=$1
	chmod u+w "$binary"
	relocated=0

	install_name=$(otool -D "$binary" | tail -n +2 | head -1)
	case "$install_name" in
	"$homebrew_prefix"/* | /usr/local/*)
		install_name_tool -id "@executable_path/../Frameworks/$(frameworks_relative_path "$install_name")" "$binary"
		relocated=1
		;;
	esac

	for dependency in $(otool -L "$binary" | tail -n +2 | awk '{print $1}'); do
		[ "$dependency" = "$install_name" ] && continue
		case "$dependency" in
		"$homebrew_prefix"/* | /usr/local/*)
			relative_path=$(frameworks_relative_path "$dependency")
			if ! ensure_bundled "$relative_path"; then
				echo "error: cannot bundle $dependency needed by $binary" >&2
				return 1
			fi
			install_name_tool -change "$dependency" "@executable_path/../Frameworks/$relative_path" "$binary"
			relocated=1
			;;
		@rpath/*)
			relative_path=${dependency#@rpath/}
			if [ ! -e "$frameworks_directory/$relative_path" ]; then
				if ! ensure_bundled "$relative_path"; then
					echo "error: cannot bundle $dependency needed by $binary" >&2
					return 1
				fi
				relocated=1
			fi
			;;
		esac
	done

	for search_path in $(otool -l "$binary" | awk '/LC_RPATH/ {getline; getline; print $2}'); do
		case "$search_path" in
		@*) ;;
		*)
			install_name_tool -delete_rpath "$search_path" "$binary"
			relocated=1
			;;
		esac
	done

	return $((relocated == 0 ? 0 : 2))
}

while true; do
	bundle_changed=0
	for binary in $(mach_o_files); do
		status=0
		relocate_binary "$binary" || status=$?
		[ "$status" -eq 1 ] && exit 1
		[ "$status" -eq 2 ] && bundle_changed=1
	done
	[ "$bundle_changed" -eq 0 ] && break
done

if ! otool -l "$bundled_executable" | grep -q '@executable_path/../Frameworks'; then
	install_name_tool -add_rpath @executable_path/../Frameworks "$bundled_executable"
fi

default_signing_identity=$(security find-identity -v -p codesigning 2>/dev/null \
	| awk -F'"' '/^ *[0-9]+\)/ {print $2; exit}')
signing_identity=${SIGNING_IDENTITY:-${default_signing_identity:--}}
echo "signing with: $signing_identity"
codesign --force --deep --sign "$signing_identity" "$app_bundle"

missing_libraries=""
for binary in $(mach_o_files); do
	for dependency in $(otool -L "$binary" | tail -n +2 | awk '{print $1}'); do
		case "$dependency" in
		"$homebrew_prefix"/* | /usr/local/*) missing_libraries="$missing_libraries
$dependency (from $binary)" ;;
		@rpath/*)
			[ -e "$frameworks_directory/${dependency#@rpath/}" ] || missing_libraries="$missing_libraries
$dependency (from $binary)"
			;;
		esac
	done
done
if [ -n "$missing_libraries" ]; then
	echo "error: bundle is not self-contained, missing:$missing_libraries" >&2
	exit 1
fi

smoke_test_directory=$(mktemp -d)
printf 'import Quickshell\nimport QtQuick\nimport QtQuick.Controls\nimport QtMultimedia\nimport Qt5Compat.GraphicalEffects\nScope { Component.onCompleted: Qt.quit() }\n' \
	>"$smoke_test_directory/shell.qml"
DYLD_PRINT_LIBRARIES=1 "$bundled_executable" -p "$smoke_test_directory" </dev/null >"$smoke_test_directory/run.log" 2>&1 &
smoke_test_process=$!
sleep 6
kill "$smoke_test_process" 2>/dev/null || true
if grep -qE "$homebrew_prefix|/usr/local/" "$smoke_test_directory/run.log"; then
	echo "error: bundled app loaded libraries from outside the bundle:" >&2
	grep -oE "($homebrew_prefix|/usr/local)/[^ ]+" "$smoke_test_directory/run.log" | sort -u | head -10 >&2
	exit 1
fi
if grep -qE 'is not installed|Failed to load configuration' "$smoke_test_directory/run.log"; then
	echo "error: bundled app could not load its QML modules:" >&2
	grep -E 'is not installed|caused by' "$smoke_test_directory/run.log" >&2
	exit 1
fi
rm -rf "$smoke_test_directory"

disk_image="$output_directory/MacShell-$version.dmg"
hdiutil create -quiet -ov -volname MacShell -srcfolder "$app_bundle" -format UDZO "$disk_image"
echo "built $app_bundle and $disk_image"
