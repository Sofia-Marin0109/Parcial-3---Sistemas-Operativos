CC ?= cc
CLANG ?= clang
BPFTOOL ?= bpftool
PKG_CONFIG ?= pkg-config
BTF ?= /sys/kernel/btf/vmlinux
BUILD ?= build
BPF_ARCH ?= x86

WARNINGS := -Wall -Wextra -Werror
LIBBPF_CFLAGS = $(shell $(PKG_CONFIG) --cflags libbpf)
LIBBPF_LIBS = $(shell $(PKG_CONFIG) --libs libbpf)

.PHONY: all clean smoke benchmark
.DELETE_ON_ERROR:

all: $(BUILD)/mmap_guard $(BUILD)/mmap_probe $(BUILD)/mmap_bench

$(BUILD):
	mkdir -p "$@"

$(BUILD)/vmlinux.h: $(BTF) | $(BUILD)
	$(BPFTOOL) btf dump file "$<" format c > "$@"

$(BUILD)/mmap_guard.bpf.o: src/mmap_guard.bpf.c src/mmap_guard.h src/mmap_guard_limits.h $(BUILD)/vmlinux.h
	$(CLANG) -target bpf -D__TARGET_ARCH_$(BPF_ARCH) $(WARNINGS) -O2 -g \
		$(BPF_CPPFLAGS) $(LIBBPF_CFLAGS) -I"$(BUILD)" -c "$<" -o "$@"

$(BUILD)/mmap_guard.skel.h: $(BUILD)/mmap_guard.bpf.o
	$(BPFTOOL) gen skeleton "$<" > "$@"

$(BUILD)/mmap_guard: src/mmap_guard.c src/mmap_guard.h src/mmap_guard_limits.h $(BUILD)/mmap_guard.skel.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -O2 -g \
		$(LIBBPF_CFLAGS) -I"$(BUILD)" "$<" -o "$@" $(LDFLAGS) $(LIBBPF_LIBS)

$(BUILD)/mmap_probe: tests/mmap_probe.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -O2 -g "$<" -o "$@" $(LDFLAGS)

$(BUILD)/mmap_bench: benchmarks/mmap_bench.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -O2 -g "$<" -o "$@" $(LDFLAGS)

smoke: all
	bash tests/smoke.sh "$(BUILD)/mmap_guard" "$(BUILD)/mmap_probe"

benchmark: all
	bash benchmarks/run.sh "$(BUILD)/mmap_guard" "$(BUILD)/mmap_bench"

clean:
	rm -f -- "$(BUILD)/vmlinux.h" "$(BUILD)/mmap_guard.bpf.o" "$(BUILD)/mmap_guard.skel.h" \
		"$(BUILD)/mmap_guard" "$(BUILD)/mmap_probe" "$(BUILD)/mmap_bench"
