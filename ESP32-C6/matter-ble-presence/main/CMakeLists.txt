idf_component_register(SRC_DIRS          "."
                       PRIV_INCLUDE_DIRS "."
                       # Configuration page (HTML, CSS and JavaScript in one file, no CDN): served from flash
                       EMBED_FILES       "web/index.html")

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
