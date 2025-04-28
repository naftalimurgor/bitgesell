package=qrencode

qrencode_version=4.1.1
qrencode_download_path=https://github.com/fukuchi/libqrencode/archive/refs/tags/
qrencode_file_name=v$(qrencode_version).tar.gz
qrencode_sha256_hash=5385bc1b8c2f20f3b91d258bf8ccc8cf62023935df2d2676b5b67049f31a049c

define qrencode_set_vars
	qrencode_config_opts=--disable-shared --without-tools --without-tests
	qrencode_config_opts += --disable-gprof --disable-gcov --disable-mudflap
	qrencode_config_opts += --disable-dependency-tracking --enable-option-checking
	qrencode_config_opts += --host=$(host)
	qrencode_config_opts_linux=--with-pic
	qrencode_config_opts_android=--with-pic

	qrencode_src_dir=$(BASEDIR)/qrencode-$(qrencode_version)
	qrencode_build_dir=$(BASEDIR)/work/build/$(host)/qrencode/$(qrencode_version)-$(package_id)
endef

define qrencode_preprocess_cmds
	echo "Fetching $(qrencode_file_name) from $(qrencode_download_path)"
	curl -L -o $(qrencode_file_name) $(qrencode_download_path)$(qrencode_file_name)

	tar -xzf $(qrencode_file_name) -C $(BASEDIR)/

	if [ -d "$(qrencode_src_dir)" ]; then rm -rf $(qrencode_src_dir); fi
	mv $(BASEDIR)/libqrencode-$(qrencode_version) $(qrencode_src_dir)
endef

define qrencode_config_cmds
	cd $(qrencode_src_dir) && autoreconf -i
	mkdir -p $(qrencode_build_dir)
	cd $(qrencode_build_dir) && \
		CC="$(host)-clang" \
		CXX="$(host)-clang++" \
		AR="$(host)-ar" \
		RANLIB="$(host)-ranlib" \
		ac_cv_func_malloc_0_nonnull=yes \
		ac_cv_func_realloc_0_nonnull=yes \
		$(qrencode_src_dir)/configure $(qrencode_config_opts)
endef

define qrencode_build_cmds
	cd $(qrencode_build_dir) && $(MAKE)
endef

define qrencode_stage_cmds
	cd $(qrencode_build_dir) && $(MAKE) DESTDIR=$(qrencode_staging_dir) install
endef

define qrencode_postprocess_cmds
	rm -f $(qrencode_staging_dir)/usr/local/lib/*.la
endef
