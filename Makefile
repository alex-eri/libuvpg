compile: build
	ninja -C build

build:
	meson build

install: compile
	ninja -C build install
