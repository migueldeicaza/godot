all: build-swift build-godot

build-godot:
	scons target=editor arch=arm64 dev_build=yes vulkan_sdk_path=~/MoltenVK/

build-swift:
	sh sync-files
	swift build
	cp .build/arm64-apple-macosx/debug/Godot.build/Godot-Swift.h main
