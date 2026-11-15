# UWAGA: projekt budujemy CMake'iem (build/). Ten Makefile NIE buduje firmware'u.
#
# Istnieje tylko dla buildera biblioteki micro-ROS (docker microros/micro_ros_static_library_builder),
# ktory wola `make print_cflags` w katalogu /project, zeby poznac flagi kompilatora.
# libmicroros.a MUSI byc skompilowana z tymi samymi flagami ABI/FPU co reszta projektu
# (patrz MCU_FLAGS w arm-gcc-toolchain.cmake), inaczej linker odrzuci obiekty
# ("uses VFP register arguments").
#
# -ffunction-sections/-fdata-sections: zeby --gc-sections wyrzucil nieuzywany kod micro-ROS.

print_cflags:
	@echo "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -ffunction-sections -fdata-sections"

.PHONY: print_cflags
