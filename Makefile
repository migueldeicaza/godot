build-all: build build-glue

build:
	scons tools=yes swift_glue=no --jobs=32

build-glue:
	bin/godot.osx.tools.64.swift --generate-swift-glue modules/swift/glue
	echo "// This file is generated from swift_glue_gen.cpp" >> modules/swift/glue/swift_glue.gen.inc
	grep ^[A-Za-z] modules/swift/glue/swift_glue.gen.cpp | grep -v " get_" | grep '{' | sed -e 's/{/;/'  > modules/swift/glue/swift_glue.gen.inc

clean-gen:
	rm modules/swift/glue/GodotSwift/Sources/GodotSwift/Generated/*swift
	rm modules/swift/glue/GodotSwift/Sources/GodotSwift/Generated/*/*swift
