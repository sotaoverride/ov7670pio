add_executable(ov7670_pio)

target_include_directories(ov7670_pio PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/includes
)

pico_generate_pio_header(ov7670_pio ${CMAKE_CURRENT_LIST_DIR}/ov7670.pio)

target_sources(ov7670_pio PRIVATE ov7670pio.c ov7670.c)

target_link_libraries(ov7670_pio PRIVATE
        pico_stdlib
        hardware_pio
	hardware_i2c
	hardware_dma
        )

pico_add_extra_outputs(ov7670_pio)

# add url via pico_set_program_url
example_auto_set_url(ov7670_pio)
