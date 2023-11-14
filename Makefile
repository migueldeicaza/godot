all: build-swift
	scons target=editor arch=arm64 dev_build=yes vulkan_sdk_path=~/MoltenVK/

build-swift:
	swift build
	cp .build/arm64-apple-macosx/debug/Godot.build/Godot-Swift.h main
