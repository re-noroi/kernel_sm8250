# SPDX-License-Identifier: GPL-2.0
snd-soc-core-objs := soc-core.o soc-dapm.o soc-jack.o soc-utils.o
snd-soc-core-objs += soc-pcm.o soc-io.o soc-devres.o soc-ops.o
snd-soc-core-$(CONFIG_SND_SOC_COMPRESS) += soc-compress.o

ifneq ($(CONFIG_SND_SOC_TOPOLOGY),)
snd-soc-core-objs += soc-topology.o
endif

ifneq ($(CONFIG_SND_SOC_GENERIC_DMAENGINE_PCM),)
snd-soc-core-objs += soc-generic-dmaengine-pcm.o
endif

ifneq ($(CONFIG_SND_SOC_AC97_BUS),)
snd-soc-core-objs += soc-ac97.o
endif

ifneq ($(CONFIG_SND_SOC_ACPI),)
snd-soc-acpi-objs := soc-acpi.o
endif

obj-$(CONFIG_SND_SOC_ACPI) += snd-soc-acpi.o

obj-$(CONFIG_SND_SOC)	+= snd-soc-core.o
obj-$(CONFIG_SND_SOC)	+= codecs/
obj-$(CONFIG_SND_SOC)	+= generic/
obj-$(CONFIG_SND_SOC)	+= qcom/