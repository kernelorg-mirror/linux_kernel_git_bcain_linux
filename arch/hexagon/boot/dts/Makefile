
#  Whoah this all needs to get redone.

#  Can't use this if the dts's need to be run through the C preprocessor
#%.S: %.dts
#	./scripts/dtc/dtc $< -O asm > $@

dtb-${CONFIG_HEXAGON_COMET}	+= comet.dtb
obj-${CONFIG_HEXAGON_COMET}	+= comet.dtb.o
ifdef CONFIG_H2
dtb-${CONFIG_HEXAGON_SIMULATOR}	+= simulator_h2.dtb
obj-${CONFIG_HEXAGON_SIMULATOR}	+= simulator_h2.dtb.o
else
dtb-${CONFIG_HEXAGON_SIMULATOR}	+= simulator.dtb
obj-${CONFIG_HEXAGON_SIMULATOR}	+= simulator.dtb.o
endif
dtb-${CONFIG_HEXAGON_AMAZON}	+= amazon.dtb
obj-${CONFIG_HEXAGON_AMAZON}	+= amazon.dtb.o

dtb-${CONFIG_HEXAGON_R3PC}	+= r3pc-v4.dtb
obj-${CONFIG_HEXAGON_R3PC}	+= r3pc-v4.dtb.o

dtb-${CONFIG_HEXAGON_MSM8960_FLUID}	+= msm8960_fluid.dtb
obj-${CONFIG_HEXAGON_MSM8960_FLUID}	+= msm8960_fluid.dtb.o

dtb-${CONFIG_HEXAGON_FPGA}	+= fpga.dtb
obj-${CONFIG_HEXAGON_FPGA}	+= fpga.dtb.o

ifdef CONFIG_HEXAGON_MSM8974_FLUID

ifdef CONFIG_HEXAGON_MSS
dtb-${CONFIG_HEXAGON_MSM8974_FLUID}	+= msm8974_fluid_mss.dtb
obj-${CONFIG_HEXAGON_MSM8974_FLUID}	+= msm8974_fluid_mss.dtb.o
else
dtb-${CONFIG_HEXAGON_MSM8974_FLUID}	+= msm8974_fluid.dtb
obj-${CONFIG_HEXAGON_MSM8974_FLUID}	+= msm8974_fluid.dtb.o
endif

endif

dtb-${CONFIG_HEXAGON_ARCHSIM}	+= archsim.dtb
obj-${CONFIG_HEXAGON_ARCHSIM}	+= archsim.dtb.o

dtb-${CONFIG_HEXAGON_MSM8996}	+= msm8996.dtb
obj-${CONFIG_HEXAGON_MSM8996}	+= msm8996.dtb.o

dtb-${CONFIG_HEXAGON_MSM8998}	+= msm8998.dtb
obj-${CONFIG_HEXAGON_MSM8998}	+= msm8998.dtb.o

dtb-${CONFIG_HEXAGON_SDM845}	+= sdm845.dtb
obj-${CONFIG_HEXAGON_SDM845}	+= sdm845.dtb.o

dtb-${CONFIG_HEXAGON_SM8150}	+= sm8150.dtb
obj-${CONFIG_HEXAGON_SM8150}	+= sm8150.dtb.o

targets += dtbs
targets += $(dtb-y)

dtbs: $(addprefix $(obj)/, $(dtb-y))
