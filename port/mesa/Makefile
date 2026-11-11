# Mesa for the KSTB6077's V3D 3.3: Mesa 23.3.6 (the last release with V3D
# 3.3) plus our series patches/*.patch, cross-built on the PC (clang, lld,
# ROOT's Alpine as the sysroot).
#   make          -> ../build/out/mesa-<id>.tar: the install tree for
#                    /opt/mesa/<id> (kstb-mesa on the box installs it);
#                    <id> = Mesa version + hash of the series and Meson options
#   make tftp     -> also copies it to the TFTP root as linux-mesa.tar
#   make sysroot  -> ../build/sysroot: /usr/include, /usr/lib, /lib from ROOT
#                    (ssh $(BOARD)); needed once, and after new -dev packages
#   make branch   -> ../build/mesa: git repo, branch kstb = tag mesa-23.3.6 +
#                    the series (git am). Work on fixes there, commit, then
#   make export   -> writes the branch's commits back to patches/
# patches/ is the source of truth: `make` builds the branch, and refuses to
# when the branch and patches/ differ (run `make export` or `make branch`).

V       = 23.3.6
TARBALL = mesa-$(V).tar.xz
SHA256  = cd3d6c60121dea73abbae99d399dc2facaecde1a8c6bd647e6d85410ff4b577b
BOARD   = root@192.168.1.33
B       = $(CURDIR)/../build
REPO    = $(B)/mesa
BUILD   = $(B)/mesa-build
SYSROOT = $(B)/sysroot
OUT     = $(B)/out
TFTP    = $(CURDIR)/../../extras/doom/build
PATCHES = $(sort $(wildcard patches/*.patch))
ID      = $(V)-$(shell { cat $(PATCHES); printf '%s\n' '$(MESON_OPTS)'; } | sha256sum | cut -c1-8)

MESON_OPTS = -Dbuildtype=release -Dprefix=/opt/mesa/current \
	-Dgallium-drivers=v3d,kmsro -Dvulkan-drivers= -Dplatforms=wayland \
	-Dglx=disabled -Degl=enabled -Dgbm=enabled -Dgles1=enabled -Dgles2=enabled \
	-Dllvm=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
	-Dshared-glapi=enabled -Dvideo-codecs= -Dgallium-va=disabled \
	-Dgallium-vdpau=disabled -Dgallium-xa=disabled -Dlmsensors=disabled

all: $(OUT)/mesa-$(ID).tar

# Normal package-managed /usr installation; old targets retain debug builds.
system:
	./system/build.sh

$(B)/dl/$(TARBALL):
	mkdir -p $(B)/dl
	curl -L -o $@.part https://archive.mesa3d.org/$(TARBALL)
	echo "$(SHA256)  $@.part" | sha256sum -c
	mv $@.part $@

sysroot:
	rm -rf $(SYSROOT) && mkdir -p $(SYSROOT)
	ssh $(BOARD) 'tar cf - /usr/include /usr/lib /lib 2>/dev/null' | tar xf - -C $(SYSROOT)
	@echo "sysroot: $$(du -sh $(SYSROOT) | cut -f1)"

$(SYSROOT)/usr/include/xf86drm.h:
	@echo "no sysroot (or no libdrm-dev on ROOT): make sysroot" && false

branch: $(B)/dl/$(TARBALL)
	@if [ -d $(REPO)/.git ] && [ -n "$$(git -C $(REPO) status --porcelain)" ]; then \
		echo "$(REPO) has uncommitted changes"; false; fi
	rm -rf $(REPO) && mkdir -p $(REPO)
	tar xf $< -C $(REPO) --strip-components=1
	cd $(REPO) && git init -q -b kstb && git add -A && \
		git -c user.name=kstb -c user.email=kstb@local commit -q -m "Mesa $(V)" && \
		git tag mesa-$(V) && git am -q $(addprefix $(CURDIR)/,$(PATCHES))
	@git -C $(REPO) log --oneline mesa-$(V)..kstb

export:
	rm -f patches/*.patch
	git -C $(REPO) format-patch -q --no-signature --zero-commit mesa-$(V)..kstb -o $(CURDIR)/patches
	@ls patches

# the branch must hold exactly patches/
check:
	@[ -d $(REPO)/.git ] || { echo "no branch: make branch"; false; }
	@[ -z "$$(git -C $(REPO) status --porcelain)" ] || { echo "$(REPO) has uncommitted changes"; false; }
	@rm -rf $(B)/mesa-export-check && \
	git -C $(REPO) format-patch -q --no-signature --zero-commit mesa-$(V)..kstb -o $(B)/mesa-export-check && \
	diff -r -q $(B)/mesa-export-check patches > /dev/null || \
		{ echo "branch and patches/ differ: make export (keep the branch) or make branch (keep patches/)"; false; }

$(BUILD)/cross.ini: $(SYSROOT)/usr/include/xf86drm.h
	mkdir -p $(BUILD)
	sed 's|@SYSROOT@|$(SYSROOT)|g' cross-aarch64-alpine.ini.in > $@

$(BUILD)/build.ninja: $(BUILD)/cross.ini
	meson setup $(BUILD) $(REPO) $(MESON_OPTS) --cross-file=$(BUILD)/cross.ini

$(OUT)/mesa-$(ID).tar: check $(BUILD)/build.ninja FORCE
	meson configure $(BUILD) $(MESON_OPTS)
	ninja -C $(BUILD)
	rm -rf $(BUILD)/inst
	DESTDIR=$(BUILD)/inst meson install -C $(BUILD) --quiet
	cd $(BUILD)/inst/opt/mesa/current && \
		{ echo "id $(ID)"; echo "built $$(date -u +%FT%TZ)"; \
		  git -C $(REPO) log --reverse --format='patch %s' mesa-$(V)..kstb; \
		  sha256sum lib/dri/v3d_dri.so; } > BUILD-ID
	mkdir -p $(OUT)
	tar -C $(BUILD)/inst/opt/mesa/current -cf $@ .
	@echo "$@"; grep v3d_dri $(BUILD)/inst/opt/mesa/current/BUILD-ID

tftp: all
	cp $(OUT)/mesa-$(ID).tar $(TFTP)/linux-mesa.tar

.PHONY: all system sysroot branch export check tftp FORCE
